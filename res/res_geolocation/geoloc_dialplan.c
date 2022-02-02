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
#include "geoloc_private.h"

int geoloc_dialplan_unload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_dialplan_load(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_dialplan_reload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}
