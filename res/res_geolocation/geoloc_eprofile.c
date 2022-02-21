/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#include "asterisk.h"
#include "asterisk/xml.h"
#include "geoloc_private.h"

extern const uint8_t _binary_res_geolocation_pidf_to_eprofile_xslt_start[];
extern const uint8_t _binary_res_geolocation_pidf_to_eprofile_xslt_end[];
static size_t pidf_to_eprofile_xslt_size;

extern const uint8_t _binary_res_geolocation_pidf_lo_test_xml_start[];
extern const uint8_t _binary_res_geolocation_pidf_lo_test_xml_end[];
static size_t pidf_lo_test_xml_size;

static struct ast_xslt_doc *pidf_lo_xslt;

static struct ast_sorcery *geoloc_sorcery;

static void geoloc_effective_profile_destructor(void *obj) {
	struct ast_geoloc_effective_profile *eprofile = obj;

	ast_string_field_free_memory(eprofile);
	ast_variables_destroy(eprofile->location_vars);
	ast_variables_destroy(eprofile->location_refinement);
	ast_variables_destroy(eprofile->location_variables);
	ast_variables_destroy(eprofile->effective_location);
	ast_variables_destroy(eprofile->usage_rules_vars);
}

struct ast_geoloc_effective_profile *ast_geoloc_eprofile_alloc(const char *name)
{
	struct ast_geoloc_effective_profile *eprofile = ao2_alloc_options(sizeof(*eprofile),
		geoloc_effective_profile_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK);

	ast_string_field_init(eprofile, 256);
	ast_string_field_set(eprofile, id, name); /* SAFE string fields handle NULL */

	return eprofile;
}

int ast_geoloc_eprofile_refresh_location(struct ast_geoloc_effective_profile *eprofile)
{
	struct ast_geoloc_location *loc = NULL;
	struct ast_variable *var;

	if (!ast_strlen_zero(eprofile->location_reference)) {
		loc = ast_sorcery_retrieve_by_id(geoloc_sorcery, "location", eprofile->location_reference);
		if (!loc) {
			ast_log(LOG_ERROR, "Profile '%s' referenced location '%s' does not exist!", eprofile->id,
				eprofile->location_reference);
			return -1;
		}

		eprofile->format = loc->format;
		ast_variables_destroy(eprofile->location_vars);
		eprofile->location_vars = ast_variables_dup(loc->location_vars);
		ao2_ref(loc, -1);
	}

	ast_variables_destroy(eprofile->effective_location);
	eprofile->effective_location = ast_variables_dup(eprofile->location_vars);
	if (eprofile->location_refinement) {
		for (var = eprofile->location_refinement; var; var = var->next) {
			struct ast_variable *newvar = ast_variable_new(var->name, var->value, "");
			if (ast_variable_list_replace(&eprofile->effective_location, newvar)) {
				ast_variable_list_append(&eprofile->effective_location, newvar);
			}
		}
	}

	return 0;
}

#define DUP_VARS(_dest, _source) \
({ \
	int _rc = 0; \
	if (_source) { \
		struct ast_variable *_vars = ast_variables_dup(_source); \
		if (!_vars) { \
			_rc = -1; \
		} else { \
			_dest = _vars; \
		} \
	} \
	(_rc); \
})

struct ast_geoloc_effective_profile *ast_geoloc_eprofile_create_from_profile(struct ast_geoloc_profile *profile)
{
	struct ast_geoloc_effective_profile *eprofile;
	const char *profile_id;
	int rc = 0;

	if (!profile) {
		return NULL;
	}

	profile_id = ast_sorcery_object_get_id(profile);

	eprofile = ast_geoloc_eprofile_alloc(profile_id);
	if (!eprofile) {
		return NULL;
	}

	ao2_lock(profile);
	ast_string_field_set(eprofile, location_reference, profile->location_reference);
	eprofile->geolocation_routing = profile->geolocation_routing;
	eprofile->pidf_element = profile->pidf_element;

	rc = DUP_VARS(eprofile->location_refinement, profile->location_refinement);
	if (rc == 0) {
		rc = DUP_VARS(eprofile->location_variables, profile->location_variables);
	}
	if (rc == 0) {
		rc = DUP_VARS(eprofile->usage_rules_vars, profile->usage_rules_vars);
	}
	if (rc != 0) {
		ao2_unlock(profile);
		ao2_ref(eprofile, -1);
		return NULL;
	}

	eprofile->location_disposition = profile->location_disposition;
	eprofile->send_location = profile->send_location;
	ao2_unlock(profile);

	if (ast_geoloc_eprofile_refresh_location(eprofile) != 0) {
		ao2_ref(eprofile, -1);
		return NULL;
	}

	return eprofile;
}

struct ast_geoloc_effective_profile *ast_geoloc_eprofile_create_from_uri(const char *uri,
	const char *reference_string)
{
	struct ast_geoloc_effective_profile *eprofile = NULL;
	char *local_uri;

	if (ast_strlen_zero(uri)) {
		return NULL;
	}
	local_uri = ast_strdupa(uri);

	if (local_uri[0] == '<') {
		local_uri++;
	}
	if (ast_ends_with(local_uri, ">")) {
		local_uri[strlen(local_uri)-1] = '\0';
	}
	ast_strip(local_uri);

	eprofile = ast_geoloc_eprofile_alloc(local_uri);
	if (!eprofile) {
		return NULL;
	}

	eprofile->format = AST_GEOLOC_FORMAT_URI;
	eprofile->location_vars = ast_variable_new("URI", local_uri, "");

	return eprofile;
}

static struct ast_geoloc_effective_profile *geoloc_eprofile_create_from_xslt_result(
	struct ast_xml_doc *result_doc,
	const char *reference_string)
{
	struct ast_geoloc_effective_profile *eprofile;
	struct ast_xml_node *presence = NULL;
	struct ast_xml_node *pidf_element = NULL;
	struct ast_xml_node *location_info = NULL;
	struct ast_xml_node *usage_rules = NULL;
	struct ast_xml_node *method = NULL;
	const char *id;
	const char *format_str;
	const char *pidf_element_str;
	const char *location_str;
	const char *usage_str;
	const char *method_str;
	char *duped;

	presence = ast_xml_get_root(result_doc);
	pidf_element = ast_xml_node_get_children(presence);
	location_info = ast_xml_find_child_element(pidf_element, "location-info", NULL, NULL);
	usage_rules = ast_xml_find_child_element(pidf_element, "usage-rules", NULL, NULL);
	method = ast_xml_find_child_element(pidf_element, "method", NULL, NULL);

	id = S_OR(ast_xml_get_attribute(pidf_element, "id"), ast_xml_get_attribute(presence, "entity"));
	eprofile = ast_geoloc_eprofile_alloc(id);
	if (!eprofile) {
		return NULL;
	}

	format_str = ast_xml_get_attribute(location_info, "format");
	if (ast_strings_equal(format_str, "gml")) {
		eprofile->format = AST_GEOLOC_FORMAT_GML;
	} else if (ast_strings_equal(format_str, "civicAddress")) {
		eprofile->format = AST_GEOLOC_FORMAT_CIVIC_ADDRESS;
	} else {
		ao2_ref(eprofile, -1);
		ast_log(LOG_ERROR, "%s: Unknown format '%s'\n", reference_string, format_str);
		return NULL;
	}

	pidf_element_str = ast_xml_get_attribute(pidf_element, "name");
	eprofile->pidf_element = geoloc_pidf_element_str_to_enum(pidf_element_str);

	location_str = ast_xml_get_text(location_info);
	duped = ast_strdupa(location_str);
	eprofile->location_vars = ast_variable_list_from_string(duped, ",", "=", "\"");
	if (!eprofile->location_vars) {
		ao2_ref(eprofile, -1);
		ast_log(LOG_ERROR, "%s: Unable to create location variables from '%s'\n", reference_string, location_str);
		return NULL;
	}

	usage_str = ast_xml_get_text(usage_rules);
	duped = ast_strdupa(usage_str);
	eprofile->usage_rules_vars = ast_variable_list_from_string(duped, ",", "=", "\"");

	method_str = ast_xml_get_text(method);
	ast_string_field_set(eprofile, method, method_str);

	return eprofile;
}

struct ast_geoloc_effective_profile *ast_geoloc_eprofile_create_from_pidf(
	struct ast_xml_doc *pidf_xmldoc, const char *reference_string)
{
	RAII_VAR(struct ast_xml_doc *, result_doc, NULL, ast_xml_close);
	struct ast_geoloc_effective_profile *eprofile;

	/*
	 * The namespace prefixes used here (dm, def, gp, etc) don't have to match
	 * the ones used in the received PIDF-LO document but they MUST match the
	 * ones in the embedded pidf_to_eprofile stylesheet.
	 *
	 * RFC5491 Rule 8 states that...
	 * Where a PIDF document contains more than one <geopriv>
     * element, the priority of interpretation is given to the first
     * <device> element in the document containing a location.  If no
     * <device> element containing a location is present in the document,
     * then priority is given to the first <tuple> element containing a
     * location.  Locations contained in <person> tuples SHOULD only be
     * used as a last resort.
     *
     * Reminder: xpath arrays are 1-based not 0-based.
	 */
	const char *find_device[] = { "path", "/def:presence/dm:device[.//gp:location-info][1]", NULL};
	const char *find_tuple[] = { "path", "/def:presence/def:tuple[.//gp:location-info][1]", NULL};
	const char *find_person[] = { "path", "/def:presence/dm:person[.//gp:location-info][1]", NULL};

	result_doc = ast_xslt_apply(pidf_lo_xslt, pidf_xmldoc, find_device);
	if (!result_doc || !ast_xml_node_get_children((struct ast_xml_node *)result_doc)) {
		ast_xml_close(result_doc);
		result_doc = ast_xslt_apply(pidf_lo_xslt, pidf_xmldoc, find_tuple);
	}
	if (!result_doc || !ast_xml_node_get_children((struct ast_xml_node *)result_doc)) {
		ast_xml_close(result_doc);
		result_doc = ast_xslt_apply(pidf_lo_xslt, pidf_xmldoc, find_person);
	}
	if (!result_doc || !ast_xml_node_get_children((struct ast_xml_node *)result_doc)) {
		return NULL;
	}

	/*
	 * The document returned from the stylesheet application looks like this...
	 * <presence id="presence-entity">
	 *     <pidf-element name="tuple" id="element-id">
	 *         <location-info format="gml">format="gml", type="Ellipsoid", crs="3d", ...</location-info>
	 *         <usage-rules>retransmission-allowed="no", retention-expiry="2010-11-14T20:00:00Z"</usage-rules>
	 *         <method>Hybrid_A-GPS</method>
	 *     </pidf-element>
	 *  </presence>
	 *
	 * Regardless of whether the pidf-element was tuple, device or person and whether
	 * the format is gml or civicAddress, the presence, pidf-element, location-info,
	 * usage-rules and method elements should be there although usage-rules and method
	 * may be empty.
	 *
	 * The contents of the location-info and usage-rules elements can be passed directly to
	 * ast_variable_list_from_string().
	 */

	eprofile = geoloc_eprofile_create_from_xslt_result(result_doc, reference_string);

	return eprofile;
}

#ifdef TEST_FRAMEWORK
static void load_tests(void);
static void unload_tests(void);
#else
static void load_tests(void) {}
static void unload_tests(void) {}
#endif


int geoloc_eprofile_unload(void)
{
	unload_tests();
	if (pidf_lo_xslt) {
		ast_xslt_close(pidf_lo_xslt);
	}

	if (geoloc_sorcery) {
		ast_sorcery_unref(geoloc_sorcery);
	}

	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_eprofile_load(void)
{
	geoloc_sorcery = geoloc_get_sorcery();

	pidf_to_eprofile_xslt_size =
		(_binary_res_geolocation_pidf_to_eprofile_xslt_end - _binary_res_geolocation_pidf_to_eprofile_xslt_start);

	pidf_lo_test_xml_size =
		(_binary_res_geolocation_pidf_lo_test_xml_end - _binary_res_geolocation_pidf_lo_test_xml_start);

	pidf_lo_xslt = ast_xslt_read_memory(
		(char *)_binary_res_geolocation_pidf_to_eprofile_xslt_start, pidf_to_eprofile_xslt_size);
	if (!pidf_lo_xslt) {
		ast_log(LOG_ERROR, "Unable to read pidf_lo_xslt from memory\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	load_tests();

	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_eprofile_reload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}


#ifdef TEST_FRAMEWORK
#include "asterisk/test.h"

AST_TEST_DEFINE(test_create_from_uri)
{

	RAII_VAR(struct ast_geoloc_effective_profile *, eprofile,  NULL, ao2_cleanup);
	const char *uri = NULL;
	int rc = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "create_from_uri";
		info->category = "/geoloc/";
		info->summary = "Test create from uri";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	eprofile = ast_geoloc_eprofile_create_from_uri("http://some_uri&a=b", __func__);
	ast_test_validate(test, eprofile != NULL);
	ast_test_validate(test, eprofile->format == AST_GEOLOC_FORMAT_URI);
	ast_test_validate(test, eprofile->location_vars != NULL);
	uri = ast_variable_find_in_list(eprofile->location_vars, "URI");
	ast_test_validate(test, uri != NULL);
	ast_test_validate(test, strcmp(uri, "http://some_uri&a=b") == 0);

	return rc;
}

static enum ast_test_result_state validate_eprofile(struct ast_test *test,
	struct ast_xml_doc * pidf_xmldoc,
	const char *path,
	const char *id,
	enum ast_geoloc_pidf_element pidf_element,
	enum ast_geoloc_format format,
	const char *method,
	const char *location,
	const char *usage
	)
{
	RAII_VAR(struct ast_str *, str, NULL, ast_free);
	RAII_VAR(struct ast_geoloc_effective_profile *, eprofile,  NULL, ao2_cleanup);
	RAII_VAR(struct ast_xml_doc *, result_doc, NULL, ast_xml_close);
	const char *search[] = { "path", path, NULL };

	if (!ast_strlen_zero(path)) {
		result_doc = ast_xslt_apply(pidf_lo_xslt, pidf_xmldoc, (const char **)search);
		ast_test_validate(test, (result_doc && ast_xml_node_get_children((struct ast_xml_node *)result_doc)));

		eprofile = geoloc_eprofile_create_from_xslt_result(result_doc, "test_create_from_xslt");
	} else {
		eprofile = ast_geoloc_eprofile_create_from_pidf(pidf_xmldoc, "test_create_from_pidf");
	}

	ast_test_validate(test, eprofile != NULL);
	ast_test_status_update(test, "ID: '%s'  pidf_element: '%s'  format: '%s'  method: '%s'\n", eprofile->id,
		geoloc_pidf_element_to_name(eprofile->pidf_element),
		geoloc_format_to_name(eprofile->format),
		eprofile->method);

	ast_test_validate(test, ast_strings_equal(eprofile->id, id));
	ast_test_validate(test, eprofile->pidf_element == pidf_element);
	ast_test_validate(test, eprofile->format == format);
	ast_test_validate(test, ast_strings_equal(eprofile->method, method));

	str = ast_variable_list_join(eprofile->location_vars, ",", "=", NULL, NULL);
	ast_test_validate(test, str != NULL);
	ast_test_status_update(test, "location_vars: %s\n", ast_str_buffer(str));
	ast_test_validate(test, ast_strings_equal(ast_str_buffer(str), location));
	ast_free(str);

	str = ast_variable_list_join(eprofile->usage_rules_vars, ",", "=", "'", NULL);
	ast_test_validate(test, str != NULL);
	ast_test_status_update(test, "usage_rules: %s\n", ast_str_buffer(str));
	ast_test_validate(test, ast_strings_equal(ast_str_buffer(str), usage));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_create_from_pidf)
{

	RAII_VAR(struct ast_xml_doc *, pidf_xmldoc, NULL, ast_xml_close);
	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "create_from_pidf";
		info->category = "/geoloc/";
		info->summary = "Test create from pidf scenarios";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	pidf_xmldoc = ast_xml_read_memory((char *)_binary_res_geolocation_pidf_lo_test_xml_start, pidf_lo_test_xml_size);
	ast_test_validate(test, pidf_xmldoc != NULL);

	res = validate_eprofile(test, pidf_xmldoc,
		NULL,
		"arcband-2d",
		AST_PIDF_ELEMENT_DEVICE,
		AST_GEOLOC_FORMAT_GML,
		"TA-NMR",
		"format=gml,type=ArcBand,crs=2d,pos=-43.5723 153.21760,innerRadius=3594,"
				"outerRadius=4148,startAngle=20,startAngle_uom=radians,openingAngle=20,"
				"openingAngle_uom=radians",
		"retransmission-allowed='yes',ruleset-preference='https:/www/more.com',"
			"retention-expires='2007-06-22T20:57:29Z'"
		);
	ast_test_validate(test, res == AST_TEST_PASS);


	res = validate_eprofile(test, pidf_xmldoc,
		"/def:presence/dm:device[.//ca:civicAddress][1]",
		"pres:alice@asterisk.org",
		AST_PIDF_ELEMENT_DEVICE,
		AST_GEOLOC_FORMAT_CIVIC_ADDRESS,
		"GPS",
		"format=civicAddress,country=AU,A1=NSW,A3=Wollongong,A4=North Wollongong,"
			"RD=Flinders,STS=Street,RDBR=Campbell Street,LMK=Gilligan's Island,"
			"LOC=Corner,NAM=Video Rental Store,PC=2500,ROOM=Westerns and Classics,"
			"PLC=store,POBOX=Private Box 15",
		"retransmission-allowed='yes',ruleset-preference='https:/www/more.com',"
			"retention-expires='2007-06-22T20:57:29Z'"
		);
	ast_test_validate(test, res == AST_TEST_PASS);


	return res;
}

static void load_tests(void) {
	AST_TEST_REGISTER(test_create_from_uri);
	AST_TEST_REGISTER(test_create_from_pidf);
}
static void unload_tests(void) {
	AST_TEST_UNREGISTER(test_create_from_uri);
	AST_TEST_UNREGISTER(test_create_from_pidf);
}

#endif
