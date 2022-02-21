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

#ifndef INCLUDE_ASTERISK_RES_GEOLOCATION_H_
#define INCLUDE_ASTERISK_RES_GEOLOCATION_H_

#include "asterisk/sorcery.h"
#include "asterisk/config.h"
#include "asterisk/xml.h"

enum ast_geoloc_pidf_element {
	AST_PIDF_ELEMENT_NONE = 0,
	AST_PIDF_ELEMENT_TUPLE,
	AST_PIDF_ELEMENT_DEVICE,
	AST_PIDF_ELEMENT_PERSON
};

enum ast_geoloc_format {
	AST_GEOLOC_FORMAT_NONE = 0,
	AST_GEOLOC_FORMAT_CIVIC_ADDRESS,
	AST_GEOLOC_FORMAT_GML,
	AST_GEOLOC_FORMAT_URI,
};

enum ast_geoloc_location_disposition {
	AST_GEOLOC_LOC_DISP_DISCARD = 0,
	AST_GEOLOC_LOC_DISP_APPEND,
	AST_GEOLOC_LOC_DISP_PREPEND,
	AST_GEOLOC_LOC_DISP_REPLACE,
};

struct ast_geoloc_location {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(method);
	);
	enum ast_geoloc_format format;
	struct ast_variable *location_vars;
};

struct ast_geoloc_profile {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(location_reference);
	);
	enum ast_geoloc_pidf_element pidf_element;
	enum ast_geoloc_location_disposition location_disposition;
	int geolocation_routing;
	int send_location;
	struct ast_variable *location_refinement;
	struct ast_variable *location_variables;
	struct ast_variable *usage_rules_vars;
};

struct ast_geoloc_effective_profile {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(id);
		AST_STRING_FIELD(location_reference);
		AST_STRING_FIELD(method);
	);
	enum ast_geoloc_pidf_element pidf_element;
	enum ast_geoloc_location_disposition location_disposition;
	int geolocation_routing;
	int send_location;
	enum ast_geoloc_format format;
	struct ast_variable *location_vars;
	struct ast_variable *location_refinement;
	struct ast_variable *location_variables;
	struct ast_variable *effective_location;
	struct ast_variable *usage_rules_vars;
};


/*!
 * \brief Given an official civicAddress code, return its friendly name.
 *
 * \param code Pointer to the code to check
 *
 * \return Pointer to the friendly name or NULL if code wasn't found.
 */
const char *ast_geoloc_civicaddr_get_name_from_code(const char *code);

/*!
 * \brief Given a civicAddress friendly name, return its official code.
 *
 * \param name Pointer to the name to check
 *
 * \return Pointer to the official code or NULL if name wasn't found.
 */
const char *ast_geoloc_civicaddr_get_code_from_name(const char *name);

/*!
 * \brief Given an unknown location variable, return its official civicAddress code.
 *
 * \param variable Pointer to the name or code to check
 *
 * \return Pointer to the official code or NULL if variable wasn't a name or code.
 */
const char *ast_geoloc_civicaddr_resolve_variable(const char *variable);

enum ast_geoloc_validate_result {
	AST_GEOLOC_VALIDATE_SUCCESS = 0,
	AST_GEOLOC_VALIDATE_MISSING_TYPE,
	AST_GEOLOC_VALIDATE_INVALID_TYPE,
	AST_GEOLOC_VALIDATE_INVALID_VARNAME,
	AST_GEOLOC_VALIDATE_NOT_ENOUGH_VARNAMES,
	AST_GEOLOC_VALIDATE_TOO_MANY_VARNAMES,
	AST_GEOLOC_VALIDATE_INVALID_VALUE,
};

const char *ast_geoloc_validate_result_to_str(enum ast_geoloc_validate_result result);

/*!
 * \brief Validate that the names of the variables in the list are valid codes or synonyms
 *
 * \param varlist Variable list to check.
 * \param result[OUT] Pointer to char * to receive failing item.
 *
 * \return result code.
 */
enum ast_geoloc_validate_result ast_geoloc_civicaddr_validate_varlist(
	const struct ast_variable *varlist, const char **result);

/*!
 * \brief Validate that the variables in the list represent a valid GML shape
 *
 * \param varlist Variable list to check.
 * \param result[OUT] Pointer to char * to receive failing item.
 *
 * \return result code.
 */
enum ast_geoloc_validate_result ast_geoloc_gml_validate_varlist(const struct ast_variable *varlist,
	const char **result);

/*!
 * \brief Create a geoloc datastore from a profile name
 *
 * \param profile_name The name of the profile to use.
 *
 * \return The datastore.
 */
struct ast_datastore *ast_geoloc_datastore_create_from_profile_name(const char *profile_name);

/*!
 * \brief Create a geoloc datastore from an effective profile.
 *
 * \param eprofile The effective profile to use.
 *
 * \return The datastore.
 */
struct ast_datastore *ast_geoloc_datastore_create_from_eprofile(
	struct ast_geoloc_effective_profile *eprofile);

/*!
 * \brief Allocate a new, empty effective profile.
 *
 * \param name The profile's name
 *
 * \return The effective profile ao2 object.
 */
struct ast_geoloc_effective_profile *ast_geoloc_eprofile_alloc(const char *name);

/*!
 * \brief Allocate a new effective profile from an existing profile.
 *
 * \param profile The profile to use.
 *
 * \return The effective profile ao2 object.
 */
struct ast_geoloc_effective_profile *ast_geoloc_eprofile_create_from_profile(struct ast_geoloc_profile *profile);

/*!
 * \brief Allocate a new effective profile from an XML PIDF-LO document
 *
 * \param pidf_xmldoc       The ast_xml_doc to use.
 * \param reference_string  An identifying string to use in error messages.
 *
 * \return The effective profile ao2 object.
 */
struct ast_geoloc_effective_profile *ast_geoloc_eprofile_create_from_pidf(
	struct ast_xml_doc *pidf_xmldoc, const char *reference_string);

/*!
 * \brief Allocate a new effective profile from a URI.
 *
 * \param uri               The URI to use.
 * \param reference_string  An identifying string to use in error messages.
 *
 * \return The effective profile ao2 object.
 */
struct ast_geoloc_effective_profile *ast_geoloc_eprofile_create_from_uri(const char *uri,
	const char *reference_string);

/*!
 * \brief Refresh the effective profile with any changed info.
 *
 * \param eprofile The eprofile to refresh.
 *
 * \return 0 on success, any other value on error.
 */
int ast_geoloc_eprofile_refresh_location(struct ast_geoloc_effective_profile *eprofile);

#endif /* INCLUDE_ASTERISK_RES_GEOLOCATION_H_ */
