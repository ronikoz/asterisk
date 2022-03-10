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
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/res_geolocation.h"
#include "asterisk/xml.h"
#include "geoloc_private.h"

struct addr_field_entry {
	const char *code;
	const char *name;
};

static struct addr_field_entry addr_code_name_entries[] = {
	{"A1", "state_province"},
	{"A2", "county_district"},
	{"A3", "city"},
	{"A4", "city_district"},
	{"A5", "neighborhood"},
	{"A6", "street_group"},
	{"ADDCODE", "additional_code"},
	{"BLD", "building"},
	{"country", "country"},
	{"FLR", "floor"},
	{"HNO", "house_number"},
	{"HNS", "house_number_suffix"},
	{"LMK", "landmark"},
	{"LOC", "additional_location"},
	{"NAM", "location_name"},
	{"PC", "postal_code"},
	{"PCN", "postal_community"},
	{"PLC", "place_type"},
	{"POBOX", "po_box"},
	{"POD", "trailing_street_suffix"},
	{"POM", "road_post_modifier"},
	{"PRD", "leading_road_direction"},
	{"PRM", "road_pre_modifier"},
	{"RD", "road"},
	{"RD", "street"},
	{"RDBR", "road_branch"},
	{"RDSEC", "road_section"},
	{"RDSUBBR", "road_sub_branch"},
	{"ROOM", "room"},
	{"SEAT", "seat"},
	{"STS", "street_suffix"},
	{"UNIT", "unit"},
};

static struct addr_field_entry addr_name_code_entries[ARRAY_LEN(addr_code_name_entries)];

static int compare_civicaddr_codes(const void *_a, const void *_b)
{
	const struct addr_field_entry *a = _a;
	const struct addr_field_entry *b = _b;

	return strcmp(a->code, b->code);
}

static int compare_civicaddr_names(const void *_a, const void *_b)
{
	const struct addr_field_entry *a = _a;
	const struct addr_field_entry *b = _b;

	return strcmp(a->name, b->name);
}

const char *ast_geoloc_civicaddr_get_name_from_code(const char *code)
{
	struct addr_field_entry key = { .code = code };
	struct addr_field_entry *entry = bsearch(&key, addr_code_name_entries, ARRAY_LEN(addr_code_name_entries),
		sizeof(struct addr_field_entry), compare_civicaddr_codes);
	return entry ? entry->name : NULL;
}

const char *ast_geoloc_civicaddr_get_code_from_name(const char *name)
{
	struct addr_field_entry key = { .name = name };
	struct addr_field_entry *entry = bsearch(&key, addr_name_code_entries, ARRAY_LEN(addr_name_code_entries),
		sizeof(struct addr_field_entry), compare_civicaddr_names);
	return entry ? entry->code : name;
}

const char *ast_geoloc_civicaddr_resolve_variable(const char *variable)
{
	const char *result = ast_geoloc_civicaddr_get_code_from_name(variable);
	if (result) {
		return result;
	}
	return ast_geoloc_civicaddr_get_name_from_code(variable);
}

enum ast_geoloc_validate_result ast_geoloc_civicaddr_validate_varlist(
	const struct ast_variable *varlist,	const char **result)
{
	const struct ast_variable *var = varlist;
	for (; var; var = var->next) {
		const char *newname = ast_geoloc_civicaddr_resolve_variable(var->name);
		if (!newname) {
			*result = var->name;
			return AST_GEOLOC_VALIDATE_INVALID_VARNAME;
		}
	}
	return AST_GEOLOC_VALIDATE_SUCCESS;
}

static char *handle_civicaddr_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "geoloc show civicAddr_mappings";
		e->usage =
			"Usage: geoloc show civicAddr_mappings\n"
			"       Show the mappings between civicAddress official codes and synonyms.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "%-16s %-32s\n", "Official Code", "Synonym");
	ast_cli(a->fd, "================ ================================\n");

	for (i = 0; i < ARRAY_LEN(addr_code_name_entries); i++) {
		ast_cli(a->fd, "%-16s %-32s\n", addr_code_name_entries[i].code, addr_code_name_entries[i].name);
	}
	ast_cli(a->fd, "\n");

	return CLI_SUCCESS;
}

static struct ast_cli_entry geoloc_civicaddr_cli[] = {
	AST_CLI_DEFINE(handle_civicaddr_show, "Show the mappings between civicAddress official codes and synonyms"),
};

struct ast_xml_node *geoloc_civicaddr_list_to_xml(const struct ast_variable *resolved_location,
	const char *ref_string)
{
	char *lang = NULL;
	char *s = NULL;
	struct ast_variable *var;
	struct ast_xml_node *ca_node;
	struct ast_xml_node *child_node;
	int rc = 0;
	SCOPE_ENTER(3, "%s", ref_string);

	lang = (char *)ast_variable_find_in_list(resolved_location, "lang");
	if (ast_strlen_zero(lang)) {
		lang = ast_strdupa(ast_defaultlanguage);
		for (s = lang; *s; s++) {
			if (*s == '_') {
				*s = '-';
			}
		}
	}

	ca_node = ast_xml_new_node("civicAddress");
	if (!ca_node) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create 'civicAddress' XML node\n", ref_string);
	}
	rc = ast_xml_set_attribute(ca_node, "lang", lang);
	if (rc != 0) {
		ast_xml_free_node(ca_node);
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create 'lang' XML attribute\n", ref_string);
	}

	for (var = (struct ast_variable *)resolved_location; var; var = var->next) {
		const char *n;
		if (ast_strings_equal(var->name, "lang")) {
			continue;
		}
		n = ast_geoloc_civicaddr_get_code_from_name(var->name);
		child_node = ast_xml_new_child(ca_node, n);
		if (!child_node) {
			ast_xml_free_node(ca_node);
			SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create '%s' XML node\n", n, ref_string);
		}
		ast_xml_set_text(child_node, var->value);
	}

	SCOPE_EXIT_RTN_VALUE(ca_node, "%s: Done\n", ref_string);
}

int geoloc_civicaddr_unload(void)
{
	ast_cli_unregister_multiple(geoloc_civicaddr_cli, ARRAY_LEN(geoloc_civicaddr_cli));

	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_civicaddr_load(void)
{
	memcpy(addr_name_code_entries, addr_code_name_entries, sizeof(addr_code_name_entries));
	qsort(addr_code_name_entries, ARRAY_LEN(addr_code_name_entries), sizeof(struct addr_field_entry), compare_civicaddr_codes);
	qsort(addr_name_code_entries, ARRAY_LEN(addr_name_code_entries), sizeof(struct addr_field_entry), compare_civicaddr_names);

	ast_cli_register_multiple(geoloc_civicaddr_cli, ARRAY_LEN(geoloc_civicaddr_cli));

	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_civicaddr_reload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}
