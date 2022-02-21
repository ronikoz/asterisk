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
#include "asterisk/datastore.h"
#include "asterisk/res_geolocation.h"
#include "geoloc_private.h"


struct ast_sorcery *geoloc_sorcery;

static void geoloc_datastore_destructor(void *obj)
{
	struct ast_geoloc_effective_profile *eprofile = obj;
	ao2_ref(eprofile, -1);
}


static const struct ast_datastore_info geoloc_datastore_info = {
	.type = "geolocation",
	.destroy = geoloc_datastore_destructor
};


struct ast_datastore *ast_geoloc_datastore_create_from_profile_name(const char *profile_name)
{
	struct ast_datastore *ds = NULL;
	struct ast_geoloc_effective_profile *eprofile = NULL;
	struct ast_geoloc_profile *profile = NULL;

	if (ast_strlen_zero(profile_name)) {
		return NULL;
	}

	ds = ast_datastore_alloc(&geoloc_datastore_info, NULL);
	if (!ds) {
		ast_log(LOG_ERROR, "A datasatore couldn't be allocated for profile '%s'", profile_name);
		return NULL;
	}

	profile = ast_sorcery_retrieve_by_id(geoloc_sorcery, "profile", profile_name);
	if (!profile) {
		ast_datastore_free(ds);
		ast_log(LOG_ERROR, "A profile with the name '%s' was not found", profile_name);
		return NULL;
	}

	eprofile = ast_geoloc_eprofile_create_from_profile(profile);
	ao2_ref(profile, -1);
	if (!eprofile) {
		ast_datastore_free(ds);
		ast_log(LOG_ERROR, "An effective profile with the name '%s' couldn't be allocated", profile_name);
		return NULL;
	}

	ds->data = eprofile;

	return ds;
}

int geoloc_channel_unload(void)
{
	if (geoloc_sorcery) {
		ast_sorcery_unref(geoloc_sorcery);
	}
	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_channel_load(void)
{
	geoloc_sorcery = geoloc_get_sorcery();
	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_channel_reload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}
