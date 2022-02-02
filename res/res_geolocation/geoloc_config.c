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
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "geoloc_private.h"
#include "asterisk/res_geolocation.h"

static struct ast_sorcery *geoloc_sorcery;

static const char *pidf_section_names[] = {
	"<none>",
	"tuple",
	"device",
	"person"
};

static const char *format_names[] = {
	"<none>",
	"civicAddress",
	"GML",
	"URI",
};

static const char * location_disposition_names[] = {
	"discard",
	"append",
	"prepend",
	"replace"
};

CONFIG_ENUM(location, format)
CONFIG_VAR_LIST(location, location_vars)

static void geoloc_location_destructor(void *obj) {
	struct ast_geoloc_location *location = obj;

	ast_variables_destroy(location->location_vars);
}

static void *geoloc_location_alloc(const char *name)
{
	return ast_sorcery_generic_alloc(sizeof(struct ast_geoloc_location), geoloc_location_destructor);
}


CONFIG_ENUM(profile, pidf_section)
CONFIG_ENUM(profile, location_disposition)
CONFIG_VAR_LIST(profile, location_refinement)
CONFIG_VAR_LIST(profile, location_variables)
CONFIG_VAR_LIST(profile, usage_rules_vars)


static void geoloc_profile_destructor(void *obj) {
	struct ast_geoloc_profile *profile = obj;

	ast_string_field_free_memory(profile);
	ast_variables_destroy(profile->location_refinement);
	ast_variables_destroy(profile->location_variables);
	ast_variables_destroy(profile->usage_rules_vars);
}

static void *geoloc_profile_alloc(const char *name)
{
	struct ast_geoloc_profile *profile = ast_sorcery_generic_alloc(sizeof(*profile), geoloc_profile_destructor);
	if (profile) {
		ast_string_field_init(profile, 128);
	}

	return profile;
}

static void geoloc_effective_profile_destructor(void *obj) {
	struct ast_geoloc_effective_profile *eprofile = obj;

	ast_string_field_free_memory(eprofile);
	ast_variables_destroy(eprofile->location_refinement);
	ast_variables_destroy(eprofile->location_variables);
	ast_variables_destroy(eprofile->effective_location);
	ast_variables_destroy(eprofile->usage_rules_vars);
}

static void *geoloc_effective_profile_alloc(const char *name)
{
	struct ast_geoloc_effective_profile *eprofile = ao2_alloc_options(sizeof(*eprofile),
		geoloc_effective_profile_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK);

	ast_string_field_init(eprofile, 256);
	ast_string_field_set(eprofile, id, name);

	return eprofile;
}

struct ast_geoloc_effective_profile *ast_geoloc_effective_profile_create(struct ast_geoloc_profile *profile)
{
	struct ast_geoloc_effective_profile *eprofile;
	const char *profile_id;
	struct ast_geoloc_location *loc = NULL;

	if (!profile) {
		return NULL;
	}
	profile_id = ast_sorcery_object_get_id(profile);
	eprofile = geoloc_effective_profile_alloc(profile_id);
	if (!eprofile) {
		return NULL;
	}

	ao2_lock(profile);
	ast_string_field_set(eprofile, location_reference, profile->location_reference);
	eprofile->geolocation_routing = profile->geolocation_routing;
	eprofile->pidf_section = profile->pidf_section;
	eprofile->location_refinement = profile->location_refinement ? ast_variables_dup(profile->location_refinement) : NULL;
	eprofile->location_variables = profile->location_variables ? ast_variables_dup(profile->location_variables) : NULL;
	eprofile->usage_rules_vars = profile->usage_rules_vars ? ast_variables_dup(profile->usage_rules_vars) : NULL;
	eprofile->location_disposition = profile->location_disposition;
	eprofile->send_location = profile->send_location;
	ao2_unlock(profile);

	if (!ast_strlen_zero(eprofile->location_reference)) {
		loc = ast_sorcery_retrieve_by_id(geoloc_sorcery, "location", eprofile->location_reference);
		if (loc) {
			struct ast_variable *var;
			eprofile->format = loc->format;
			eprofile->location_vars = ast_variables_dup(loc->location_vars);
			ao2_ref(loc, -1);

			eprofile->effective_location = ast_variables_dup(loc->location_vars);
			for (var = profile->location_refinement; var; var = var->next) {
				struct ast_variable *newvar = ast_variable_new(var->name, var->value, "");
				if (ast_variable_list_replace(&eprofile->effective_location, newvar)) {
					ast_variable_list_append(&eprofile->effective_location, newvar);
				}
			}
		} else {
			ast_log(LOG_ERROR, "Profile '%s' referenced location '%s' does not exist!", profile_id,
				eprofile->location_reference);
		}
	}

	return eprofile;
}

static int geoloc_location_apply_handler(const struct ast_sorcery *sorcery, void *obj)
{
	struct ast_geoloc_location *location = obj;
	const char *location_id = ast_sorcery_object_get_id(location);
	const char *failed;
	const char *uri;
	enum ast_geoloc_validate_result result;

	switch (location->format) {
	case AST_GEOLOC_FORMAT_NONE:
		ast_log(LOG_ERROR, "Location '%s' must have a format\n", location_id);
		return -1;
	case AST_GEOLOC_FORMAT_CIVIC_ADDRESS:
		result = ast_geoloc_civicaddr_validate_varlist(location->location_vars, &failed);
		if (result != AST_GEOLOC_VALIDATE_SUCCESS) {
			ast_log(LOG_ERROR, "Location '%s' has invalid item '%s' in the location\n",
				location_id, failed);
			return -1;
		}
		break;
	case AST_GEOLOC_FORMAT_GML:
		result = ast_geoloc_gml_validate_varlist(location->location_vars, &failed);
		if (result != AST_GEOLOC_VALIDATE_SUCCESS) {
			ast_log(LOG_ERROR, "%s for item '%s' in location '%s'\n",
				ast_geoloc_validate_result_to_str(result),	failed, location_id);
			return -1;
		}

		break;
	case AST_GEOLOC_FORMAT_URI:
		uri = ast_variable_find_in_list(location->location_vars, "URI");
		if (!uri) {
			struct ast_str *str = ast_variable_list_join(location->location_vars, ",", "=", "\"", NULL);

			ast_log(LOG_ERROR, "Geolocation location '%s' format is set to '%s' but no 'URI' was found in location parameter '%s'\n",
				location_id, format_names[AST_GEOLOC_FORMAT_URI], ast_str_buffer(str));
			ast_free(str);
			return -1;
		}
		break;
	}

	return 0;
}

static int geoloc_profile_apply_handler(const struct ast_sorcery *sorcery, void *obj)
{
	struct ast_geoloc_profile *profile = obj;
	struct ast_geoloc_location *location;
	const char *profile_id = ast_sorcery_object_get_id(profile);
	const char *failed;
	enum ast_geoloc_validate_result result;

	if (ast_strlen_zero(profile->location_reference)) {
		if (profile->location_refinement ||
			profile->location_variables) {
			ast_log(LOG_ERROR, "Profile '%s' can't have location_refinement or location_variables without a location_reference",
				profile_id);
			return -1;
		}
		return 0;
	}

	location = ast_sorcery_retrieve_by_id(geoloc_sorcery, "location", profile->location_reference);
	if (!location) {
		ast_log(LOG_ERROR, "Profile '%s' has a location_reference '%s' that doesn't exist",
			profile_id, profile->location_reference);
		return -1;
	}

	if (profile->location_refinement) {
		switch (location->format) {
		case AST_GEOLOC_FORMAT_NONE:
			break;
		case AST_GEOLOC_FORMAT_CIVIC_ADDRESS:
			result = ast_geoloc_civicaddr_validate_varlist(profile->location_refinement, &failed);
			if (result != AST_GEOLOC_VALIDATE_SUCCESS) {
				ast_log(LOG_ERROR, "Profile '%s' error: %s: for item '%s' in the location_refinement\n",
					profile_id,	ast_geoloc_validate_result_to_str(result), failed);
				ao2_ref(location, -1);
				return -1;
			}
			break;
		case AST_GEOLOC_FORMAT_GML:
			break;
		case AST_GEOLOC_FORMAT_URI:
			break;
		}
	}
	ao2_ref(location, -1);

	return 0;
}

struct ast_sorcery *geoloc_get_sorcery(void)
{
	ast_sorcery_ref(geoloc_sorcery);
	return geoloc_sorcery;
}

static char *geoloc_config_list_locations(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator iter;
	struct ao2_container *sorted_container;
	struct ao2_container *unsorted_container;
	struct ast_geoloc_location *loc;
	int using_regex = 0;
	char *result = CLI_SUCCESS;
	int ret = 0;
	char *format_name;
	int count = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "geoloc list locations";
		e->usage = "Usage: geoloc list locations [ like <pattern> ]\n"
		            "      List Geolocation Location Objects\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3 && a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 5) {
		if (strcasecmp(a->argv[3], "like")) {
			return CLI_SHOWUSAGE;
		}
		using_regex = 1;
	}

	sorted_container = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		ast_sorcery_object_id_sort, NULL);
	if (!sorted_container) {
		ast_cli(a->fd, "Geolocation Location Objects: Unable to allocate temporary container\n");
		return CLI_FAILURE;
	}

	/* Get a sorted snapshot of the scheduled tasks */
	if (using_regex) {
		unsorted_container = ast_sorcery_retrieve_by_regex(geoloc_sorcery, "location", a->argv[4]);
	} else {
		unsorted_container = ast_sorcery_retrieve_by_fields(geoloc_sorcery, "location",
			AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	}

	ret = ao2_container_dup(sorted_container, unsorted_container, 0);
	ao2_ref(unsorted_container, -1);
	if (ret != 0) {
		ao2_ref(sorted_container, -1);
		ast_cli(a->fd, "Geolocation Location Objects: Unable to sort temporary container\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Geolocation Location Objects:\n\n");

	ast_cli(a->fd,
		"<Object ID...................................> <Format.....> <Details.............>\n"
		"===================================================================================\n");

	iter = ao2_iterator_init(sorted_container, AO2_ITERATOR_UNLINK);
	for (; (loc = ao2_iterator_next(&iter)); ao2_ref(loc, -1)) {
		struct ast_str *str;

		ao2_lock(loc);
		str = ast_variable_list_join(loc->location_vars, ",", "=", "\"", NULL);
		if (!str) {
			ao2_unlock(loc);
			ao2_ref(loc, -1);
			ast_cli(a->fd, "Geolocation Location Objects: Unable to allocate temp string for '%s'\n",
				ast_sorcery_object_get_id(loc));
			result = CLI_FAILURE;
			break;
		}

		format_to_str(loc, NULL, &format_name);
		ast_cli(a->fd, "%-46.46s %-13s %-s\n",
			ast_sorcery_object_get_id(loc),
			format_name,
			ast_str_buffer(str));
		ao2_unlock(loc);
		ast_free(str);
		ast_free(format_name);
		count++;
	}
	ao2_iterator_destroy(&iter);
	ao2_ref(sorted_container, -1);
	ast_cli(a->fd, "\nTotal Location Objects: %d\n\n", count);

	return result;
}

static char *geoloc_config_list_profiles(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator iter;
	struct ao2_container *sorted_container;
	struct ao2_container *unsorted_container;
	struct ast_geoloc_profile *profile;
	int using_regex = 0;
	char *result = CLI_SUCCESS;
	int ret = 0;
	char *disposition;
	int count = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "geoloc list profiles";
		e->usage = "Usage: geoloc list profiles [ like <pattern> ]\n"
		            "      List Geolocation Profile Objects\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3 && a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 5) {
		if (strcasecmp(a->argv[3], "like")) {
			return CLI_SHOWUSAGE;
		}
		using_regex = 1;
	}

	sorted_container = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		ast_sorcery_object_id_sort, NULL);
	if (!sorted_container) {
		ast_cli(a->fd, "Geolocation Profile Objects: Unable to allocate temporary container\n");
		return CLI_FAILURE;
	}

	/* Get a sorted snapshot of the scheduled tasks */
	if (using_regex) {
		unsorted_container = ast_sorcery_retrieve_by_regex(geoloc_sorcery, "profile", a->argv[4]);
	} else {
		unsorted_container = ast_sorcery_retrieve_by_fields(geoloc_sorcery, "profile",
			AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	}

	ret = ao2_container_dup(sorted_container, unsorted_container, 0);
	ao2_ref(unsorted_container, -1);
	if (ret != 0) {
		ao2_ref(sorted_container, -1);
		ast_cli(a->fd, "Geolocation Profile Objects: Unable to sort temporary container\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Geolocation Profile Objects:\n\n");

	ast_cli(a->fd,
		"<Object ID...................................> <Disposition> <Send> <Location Reference> \n"
		"=========================================================================================\n");

	iter = ao2_iterator_init(sorted_container, AO2_ITERATOR_UNLINK);
	for (; (profile = ao2_iterator_next(&iter)); ao2_ref(profile, -1)) {
		ao2_lock(profile);

		location_disposition_to_str(profile, NULL, &disposition);
		ast_cli(a->fd, "%-46.46s %-13s %-6s %-s\n",
			ast_sorcery_object_get_id(profile),
			disposition,
			profile->send_location ? "yes" : "no",
			profile->location_reference);
		ao2_unlock(profile);
		ast_free(disposition);
		count++;
	}
	ao2_iterator_destroy(&iter);
	ao2_ref(sorted_container, -1);
	ast_cli(a->fd, "\nTotal Profile Objects: %d\n\n", count);

	return result;
}

static char *geoloc_config_show_profiles(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator iter;
	struct ao2_container *sorted_container;
	struct ao2_container *unsorted_container;
	struct ast_geoloc_profile *profile;
	int using_regex = 0;
	char *result = CLI_SUCCESS;
	int ret = 0;
	int count = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "geoloc show profiles";
		e->usage = "Usage: geoloc show profiles [ like <pattern> ]\n"
		            "      List Geolocation Profile Objects\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3 && a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 5) {
		if (strcasecmp(a->argv[3], "like")) {
			return CLI_SHOWUSAGE;
		}
		using_regex = 1;
	}

	sorted_container = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		ast_sorcery_object_id_sort, NULL);
	if (!sorted_container) {
		ast_cli(a->fd, "Geolocation Profile Objects: Unable to allocate temporary container\n");
		return CLI_FAILURE;
	}

	/* Get a sorted snapshot of the scheduled tasks */
	if (using_regex) {
		unsorted_container = ast_sorcery_retrieve_by_regex(geoloc_sorcery, "profile", a->argv[4]);
	} else {
		unsorted_container = ast_sorcery_retrieve_by_fields(geoloc_sorcery, "profile",
			AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	}

	ret = ao2_container_dup(sorted_container, unsorted_container, 0);
	ao2_ref(unsorted_container, -1);
	if (ret != 0) {
		ao2_ref(sorted_container, -1);
		ast_cli(a->fd, "Geolocation Profile Objects: Unable to sort temporary container\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Geolocation Profile Objects:\n\n");

	iter = ao2_iterator_init(sorted_container, AO2_ITERATOR_UNLINK);
	for (; (profile = ao2_iterator_next(&iter)); ) {
		char *disposition = NULL;
		struct ast_str *loc_str = NULL;
		struct ast_str *refinement_str = NULL;
		struct ast_str *variables_str = NULL;
		struct ast_str *resolved_str = NULL;
		struct ast_geoloc_effective_profile *eprofile = ast_geoloc_effective_profile_create(profile);
		ao2_ref(profile, -1);

		if (!ast_strlen_zero(eprofile->location_reference)) {
			loc_str = ast_variable_list_join(eprofile->location_vars, ",", "=", "\"", NULL);
			resolved_str = ast_variable_list_join(eprofile->effective_location, ",", "=", "\"", NULL);
		}

		refinement_str = ast_variable_list_join(eprofile->location_refinement, ",", "=", "\"", NULL);
		variables_str = ast_variable_list_join(eprofile->location_variables, ",", "=", "\"", NULL);

		location_disposition_to_str(eprofile, NULL, &disposition);

		ast_cli(a->fd,
			"id:                            %-s\n"
			"received_location_disposition: %-s\n"
			"send_location:                 %-s\n"
			"pidf_section:                  %-s\n"
			"location_reference:            %-s\n"
			"Location_format:               %-s\n"
			"location_reference_details:    %-s\n"
			"location_refinement:           %-s\n"
			"location_variables:            %-s\n"
			"effective_location:            %-s\n\n",
			eprofile->id,
			disposition,
			eprofile->send_location ? "yes" : "no",
			pidf_section_names[eprofile->pidf_section],
			S_OR(eprofile->location_reference, "<none>"),
			format_names[eprofile->format],
			S_COR(loc_str, ast_str_buffer(loc_str), "<none>"),
			S_COR(refinement_str, ast_str_buffer(refinement_str), "<none>"),
			S_COR(variables_str, ast_str_buffer(variables_str), "<none>"),
			S_COR(resolved_str, ast_str_buffer(resolved_str), "<none>"));
		ao2_ref(eprofile, -1);

		ast_free(disposition);
		ast_free(loc_str);
		ast_free(refinement_str);
		ast_free(variables_str);
		ast_free(resolved_str);
		count++;
	}
	ao2_iterator_destroy(&iter);
	ao2_ref(sorted_container, -1);
	ast_cli(a->fd, "\nTotal Profile Objects: %d\n\n", count);

	return result;
}

static char *geoloc_config_cli_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *result = CLI_SUCCESS;

	switch (cmd) {
	case CLI_INIT:
		e->command = "geoloc reload";
		e->usage = "Usage: geoloc reload\n"
		            "      Reload Geolocation Configuration\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 2) {
		return CLI_SHOWUSAGE;
	}

	geoloc_config_reload();
	ast_cli(a->fd, "Geolocation Configuration reloaded.\n");

	return result;
}

static struct ast_cli_entry geoloc_location_cli_commands[] = {
	AST_CLI_DEFINE(geoloc_config_list_locations, "List Geolocation Location Objects"),
	AST_CLI_DEFINE(geoloc_config_list_profiles, "List Geolocation Profile Objects"),
	AST_CLI_DEFINE(geoloc_config_show_profiles, "Show Geolocation Profile Objects"),
	AST_CLI_DEFINE(geoloc_config_cli_reload, "Reload Geolocation Configuration"),
};

int geoloc_config_reload(void)
{
	if (geoloc_sorcery) {
		ast_sorcery_reload(geoloc_sorcery);
	}
	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_config_unload(void)
{
	ast_cli_unregister_multiple(geoloc_location_cli_commands, ARRAY_LEN(geoloc_location_cli_commands));

	if (geoloc_sorcery) {
		ast_sorcery_unref(geoloc_sorcery);
	}
	geoloc_sorcery = NULL;

	return 0;
}

int geoloc_config_load(void)
{
	if (!(geoloc_sorcery = ast_sorcery_open())) {
		ast_log(LOG_ERROR, "Failed to open geolocation sorcery\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_apply_default(geoloc_sorcery, "location", "config", "geolocation.conf,criteria=type=location");
	if (ast_sorcery_object_register(geoloc_sorcery, "location", geoloc_location_alloc, NULL, geoloc_location_apply_handler)) {
		ast_log(LOG_ERROR, "Failed to register geoloc location object with sorcery\n");
		ast_sorcery_unref(geoloc_sorcery);
		geoloc_sorcery = NULL;
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(geoloc_sorcery, "location", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "location", "format", AST_GEOLOC_FORMAT_NONE,
		format_handler, format_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "location", "location", NULL,
		location_vars_handler, location_vars_to_str, location_vars_dup, 0, 0);

	ast_sorcery_apply_default(geoloc_sorcery, "profile", "config", "geolocation.conf,criteria=type=profile");
	if (ast_sorcery_object_register(geoloc_sorcery, "profile", geoloc_profile_alloc, NULL, geoloc_profile_apply_handler)) {
		ast_log(LOG_ERROR, "Failed to register geoloc profile object with sorcery\n");
		ast_sorcery_unref(geoloc_sorcery);
		geoloc_sorcery = NULL;
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(geoloc_sorcery, "profile", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "profile", "pidf_lo_section", AST_PIDF_SECTION_NONE,
		pidf_section_handler, pidf_section_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(geoloc_sorcery, "profile", "location_reference", "", OPT_STRINGFIELD_T,
		0, STRFLDSET(struct ast_geoloc_profile, location_reference));
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "profile", "received_location_disposition", "discard",
		location_disposition_handler, location_disposition_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(geoloc_sorcery, "profile", "send_location", "no",
		OPT_BOOL_T, 1, FLDSET(struct ast_geoloc_profile, send_location));
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "profile", "usage_rules", NULL,
		usage_rules_vars_handler, usage_rules_vars_to_str, usage_rules_vars_dup, 0, 0);
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "profile", "location_refinement", NULL,
		location_refinement_handler, location_refinement_to_str, location_refinement_dup, 0, 0);
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "profile", "location_variables", NULL,
		location_variables_handler, location_variables_to_str, location_variables_dup, 0, 0);

	ast_sorcery_load(geoloc_sorcery);

	ast_cli_register_multiple(geoloc_location_cli_commands, ARRAY_LEN(geoloc_location_cli_commands));

	return AST_MODULE_LOAD_SUCCESS;
}

