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

enum ast_geoloc_pidf_section {
	AST_PIDF_SECTION_NONE = 0,
	AST_PIDF_SECTION_TUPLE,
	AST_PIDF_SECTION_DEVICE,
	AST_PIDF_SECTION_PERSON
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
	enum ast_geoloc_format format;
	struct ast_variable *location_vars;
};

struct ast_geoloc_profile {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(location_reference);
	);
	enum ast_geoloc_pidf_section pidf_section;
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
	);
	enum ast_geoloc_pidf_section pidf_section;
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

struct ast_datastore *ast_geoloc_datastore_create(const char *profile_name);

struct ast_geoloc_effective_profile *ast_geoloc_effective_profile_create(struct ast_geoloc_profile *profile);

#endif /* INCLUDE_ASTERISK_RES_GEOLOCATION_H_ */
