/*
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
#include "geoloc_private.h"


#if 1 //not used yet.
enum geoloc_shape_attrs {
	GEOLOC_SHAPE_ATTR_POS = 0,
	GEOLOC_SHAPE_ATTR_POS3D,
	GEOLOC_SHAPE_ATTR_RADIUS,
	GEOLOC_SHAPE_ATTR_SEMI_MAJOR_AXIS,
	GEOLOC_SHAPE_ATTR_SEMI_MINOR_AXIS,
	GEOLOC_SHAPE_ATTR_VERTICAL_AXIS,
	GEOLOC_SHAPE_ATTR_HEIGHT,
	GEOLOC_SHAPE_ATTR_ORIENTATION,
	GEOLOC_SHAPE_ATTR_ORIENTATION_UOM,
	GEOLOC_SHAPE_ATTR_INNER_RADIUS,
	GEOLOC_SHAPE_ATTR_OUTER_RADIUS,
	GEOLOC_SHAPE_ATTR_STARTING_ANGLE,
	GEOLOC_SHAPE_ATTR_OPENING_ANGLE,
	GEOLOC_SHAPE_ATTR_ANGLE_UOM,
};

struct geoloc_gml_attr_def {
	enum geoloc_shape_attrs attr;
	const char *name;
	int (*validator)(const char *value);
	int (*transformer)(struct ast_variable *value);
};

struct geoloc_gml_attr_def gml_attr_defs[] = {
	{ GEOLOC_SHAPE_ATTR_POS, "pos", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_POS3D,"pos3d", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_RADIUS,"radius", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_SEMI_MAJOR_AXIS,"semiMajorAxis", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_SEMI_MINOR_AXIS,"semiMinorAxis", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_VERTICAL_AXIS,"verticalAxis", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_HEIGHT,"height", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_ORIENTATION,"orientation", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_ORIENTATION_UOM,"orientation_uom", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_INNER_RADIUS,"innerRadius", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_OUTER_RADIUS,"outerRadius", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_STARTING_ANGLE,"startingAngle", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_OPENING_ANGLE,"openingAngle", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_ANGLE_UOM,"angle_uom", NULL, NULL},
};
#endif  //not used yet.

struct geoloc_gml_attr {
	const char *attribute;
	int min_required;
	int max_allowed;
	int (*validator)(const char *value);
};

struct geoloc_gml_shape_def {
	const char *shape_type;
	struct geoloc_gml_attr required_attributes[8];
};

static int pos_validator(const char *value)
{
	float lat;
	float lon;
	return (sscanf(value, "%f %f", &lat, &lon) == 2);
}

static int pos3d_validator(const char *value)
{
	float lat;
	float lon;
	float alt;
	return (sscanf(value, "%f %f %f", &lat, &lon, &alt) == 3);
}

static int float_validator(const char *value)
{
	float val;
	return (sscanf(value, "%f", &val) == 1);
}

static int uom_validator(const char *value)
{
	return (ast_strings_equal(value, "degrees") || ast_strings_equal(value, "radians"));
}


static struct geoloc_gml_shape_def gml_shape_defs[8] = {
	{ "Point", { {"pos", 1, 1, pos_validator}, {NULL, -1, -1} }},
	{ "Polygon", { {"pos", 3, -1, pos_validator}, {NULL, -1, -1} }},
	{ "Circle", { {"pos", 1, 1, pos_validator}, {"radius", 1, 1, float_validator},{NULL, -1, -1}}},
	{ "Ellipse", { {"pos", 1, 1, pos_validator}, {"semiMajorAxis", 1, 1, float_validator},
		{"semiMinorAxis", 1, 1, float_validator}, {"orientation", 1, 1, float_validator},
		{"orientation_uom", 1, 1, uom_validator}, {NULL, -1, -1} }},
	{ "ArcBand", { {"pos", 1, 1, pos_validator}, {"innerRadius", 1, 1, float_validator},
		{"outerRadius", 1, 1, float_validator}, {"startAngle", 1, 1, float_validator},
		{"startAngle_uom", 1, 1, uom_validator}, {"openingAngle", 1, 1, float_validator},
		{"openingAngle_uom", 1, 1, uom_validator}, {NULL, -1, -1} }},
	{ "Sphere", { {"pos3d", 1, 1, pos3d_validator}, {"radius", 1, 1, float_validator}, {NULL, -1, -1} }},
	{ "Ellipse", { {"pos3d", 1, 1, pos3d_validator}, {"semiMajorAxis", 1, 1, float_validator},
		{"semiMinorAxis", 1, 1, float_validator}, {"verticalAxis", 1, 1, float_validator},
		{"orientation", 1, 1, float_validator}, {"orientation_uom", 1, 1, uom_validator}, {NULL, -1, -1} }},
	{ "Prism", { {"pos3d", 3, -1, pos_validator}, {"height", 1, 1, float_validator}, {NULL, -1, -1} }},
};

enum ast_geoloc_validate_result ast_geoloc_gml_validate_varlist(const struct ast_variable *varlist,
	const char **result)
{
	int def_index = -1;
	const struct ast_variable *var;
	int i;
	const char *shape_type = ast_variable_find_in_list(varlist, "type");

	if (!shape_type) {
		return AST_GEOLOC_VALIDATE_MISSING_TYPE;
	}

	for (i = 0; i < ARRAY_LEN(gml_shape_defs); i++) {
		if (ast_strings_equal(gml_shape_defs[i].shape_type, shape_type)) {
			def_index = i;
		}
	}
	if (def_index < 0) {
		return AST_GEOLOC_VALIDATE_INVALID_TYPE;
	}

	for (var = varlist; var; var = var->next) {
		int vname_index = -1;
		if (ast_strings_equal("type", var->name)) {
			continue;
		}
		for (i = 0; i < ARRAY_LEN(gml_shape_defs[def_index].required_attributes); i++) {
			if (gml_shape_defs[def_index].required_attributes[i].attribute == NULL) {
				break;
			}
			if (ast_strings_equal(gml_shape_defs[def_index].required_attributes[i].attribute, var->name)) {
				vname_index = i;
				break;
			}
		}
		if (vname_index < 0) {
			*result = var->name;
			return AST_GEOLOC_VALIDATE_INVALID_VARNAME;
		}
		if (!gml_shape_defs[def_index].required_attributes[vname_index].validator(var->value)) {
			*result = var->name;
			return AST_GEOLOC_VALIDATE_INVALID_VALUE;
		}
	}

	for (i = 0; i < ARRAY_LEN(gml_shape_defs[def_index].required_attributes); i++) {
		int count = 0;
		if (gml_shape_defs[def_index].required_attributes[i].attribute == NULL) {
			break;
		}

		for (var = varlist; var; var = var->next) {
			if (ast_strings_equal(gml_shape_defs[def_index].required_attributes[i].attribute, var->name)) {
				count++;
			}
		}
		if (count < gml_shape_defs[def_index].required_attributes[i].min_required) {
			*result = gml_shape_defs[def_index].required_attributes[i].attribute;
			return AST_GEOLOC_VALIDATE_NOT_ENOUGH_VARNAMES;
		}
		if (gml_shape_defs[def_index].required_attributes[i].max_allowed > 0 &&
			count > gml_shape_defs[def_index].required_attributes[i].max_allowed) {
			*result = gml_shape_defs[def_index].required_attributes[i].attribute;
			return AST_GEOLOC_VALIDATE_TOO_MANY_VARNAMES;
		}
	}
	return AST_GEOLOC_VALIDATE_SUCCESS;
}

static char *handle_gml_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "geoloc show gml_shape_defs";
		e->usage =
			"Usage: geoloc show gml_shape_defs\n"
			"       Show the GML Shape definitions.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "%-16s %-32s\n", "Shape", "Attributes name(min,max)");
	ast_cli(a->fd, "================ ===============================\n");

	for (i = 0; i < ARRAY_LEN(gml_shape_defs); i++) {
		int j;
		ast_cli(a->fd, "%-16s", gml_shape_defs[i].shape_type);
		for (j = 0; j < ARRAY_LEN(gml_shape_defs[i].required_attributes); j++) {
			if (gml_shape_defs[i].required_attributes[j].attribute == NULL) {
				break;
			}
			if (gml_shape_defs[i].required_attributes[j].max_allowed >= 0) {
				ast_cli(a->fd, " %s(%d,%d)", gml_shape_defs[i].required_attributes[j].attribute,
					gml_shape_defs[i].required_attributes[j].min_required,
					gml_shape_defs[i].required_attributes[j].max_allowed);
			} else {
				ast_cli(a->fd, " %s(%d,unl)", gml_shape_defs[i].required_attributes[j].attribute,
					gml_shape_defs[i].required_attributes[j].min_required);
			}
		}
		ast_cli(a->fd, "\n");
	}
	ast_cli(a->fd, "\n");

	return CLI_SUCCESS;
}

static struct ast_cli_entry geoloc_gml_cli[] = {
	AST_CLI_DEFINE(handle_gml_show, "Show the GML Shape definitions"),
};

int geoloc_gml_unload(void)
{
	ast_cli_unregister_multiple(geoloc_gml_cli, ARRAY_LEN(geoloc_gml_cli));

	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_gml_load(void)
{
	ast_cli_register_multiple(geoloc_gml_cli, ARRAY_LEN(geoloc_gml_cli));

	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_gml_reload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}
