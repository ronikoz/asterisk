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

/*** MODULEINFO
	<depend>res_geolocation</depend>
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_session</depend>
	<depend>chan_pjsip</depend>
	<depend>libxml2</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "asterisk/module.h"
#include "asterisk/xml.h"
#include "asterisk/res_geolocation.h"

#include <pjsip_ua.h>
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"

static pj_str_t GEOLOCATION_HDR;

static int find_pidf(const char *session_name, struct pjsip_rx_data *rdata, char *geoloc_uri,
	char **pidf_body, unsigned int *pidf_len)
{
	/*
	 * If the URI is "cid" then we're going to search for a pidf document
	 * in the body of the message.  If there's no body, there's no point.
	 */
	if (!rdata->msg_info.msg->body) {
		ast_log(LOG_WARNING, "%s: There's no message body in which to search for '%s'.  Skipping\n",
			session_name, geoloc_uri);
		return -1;
	}

	/*
	 * If the message content type is 'application/pidf+xml', then the pidf is
	 * the only document in the message and we'll just parse the entire body
	 * as xml.  If it's 'multipart/mixed' then we have to find the part that
	 * has a Content-ID heder value matching the URI.
	 */
	if (ast_sip_are_media_types_equal(&rdata->msg_info.ctype->media,
		&pjsip_media_type_application_pidf_xml)) {
		*pidf_body = rdata->msg_info.msg->body->data;
		*pidf_len = rdata->msg_info.msg->body->len;
	} else if (ast_sip_are_media_types_equal(&rdata->msg_info.ctype->media,
		&pjsip_media_type_multipart_mixed)) {
		pj_str_t cid = pj_str(geoloc_uri);
		pjsip_multipart_part *mp = pjsip_multipart_find_part_by_cid_str(
			rdata->tp_info.pool, rdata->msg_info.msg->body, &cid);

		if (!mp) {
			ast_log(LOG_WARNING, "%s: A Geolocation header was found with URI '%s'"
				" but the associated multipart part was not found in the message body.  Skipping URI",
				session_name, geoloc_uri);
			return -1;
		}
		*pidf_body = mp->body->data;
		*pidf_len = mp->body->len;
	} else {
		ast_log(LOG_WARNING, "%s: A Geolocation header was found with URI '%s'"
			" but no pidf document with that content id was found.  Skipping URI",
			session_name, geoloc_uri);
		return -1;
	}

	return 0;
}


static int handle_incoming_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	const char *session_name = ast_sip_session_get_name(session);
	struct ast_sip_endpoint *endpoint = session->endpoint;
	struct ast_channel *channel = session->channel;
	RAII_VAR(struct ast_geoloc_profile *, config_profile, NULL, ao2_cleanup);
	RAII_VAR(struct ast_geoloc_eprofile *, config_eprofile, NULL, ao2_cleanup);
	RAII_VAR(struct ast_datastore *, ds, NULL, ast_datastore_free);
	size_t eprofile_count = 0;
	char *geoloc_hdr_value = NULL;
	char *duped_geoloc_hdr_value = NULL;
	char *geoloc_uri = NULL;
	int rc = 0;
	pjsip_generic_string_hdr *geoloc_hdr =
		pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &GEOLOCATION_HDR, NULL);
	SCOPE_ENTER(3, "%s\n", session_name);

	if (!endpoint) {
		SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING, "%s: Session has no endpoint.  Skipping.\n",
			session_name);
	}

	if (!channel) {
		SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING, "%s: Session has no channel.  Skipping.\n",
			session_name);
	}

	if (!geoloc_hdr) {
		ast_trace(4, "%s: Message has no Geolocation header\n", session_name);
	} else {
		ast_trace(4, "%s: Geolocation: " PJSTR_PRINTF_SPEC, session_name,
			PJSTR_PRINTF_VAR(geoloc_hdr->hvalue));
	}

	if (ast_strlen_zero(endpoint->geoloc_incoming_call_profile)) {
		if (geoloc_hdr) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_NOTICE, "%s: Message has Geolocation header '"
				PJSTR_PRINTF_SPEC "' but endpoint has no geoloc_incoming_call_profile. "
				"Geolocation info discarded.\n", session_name,
				PJSTR_PRINTF_VAR(geoloc_hdr->hvalue));
		} else {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_NOTICE, "%s: Endpoint has no geoloc_incoming_call_profile. "
				"Skipping.\n", session_name);
		}
	}

	config_profile = ast_geoloc_get_profile(endpoint->geoloc_incoming_call_profile);
	if (!config_profile) {
		SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_NOTICE, "%s: Message has Geolocation header '"
			PJSTR_PRINTF_SPEC "' but endpoint's geoloc_incoming_call_profile doesn't exist. "
			"Geolocation info discarded.\n", session_name,
			PJSTR_PRINTF_VAR(geoloc_hdr->hvalue));
	}

	ds = ast_geoloc_datastore_create(session_name);
	if (!ds) {
		SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING,
			"%s: Couldn't allocate a geoloc datastore\n", session_name);
	}

	if (config_profile->action == AST_GEOLOC_ACTION_DISCARD) {
		ast_trace(4, "%s: Profile '%s' location_disposition is 'discard' so "
			"discarding Geolocation: " PJSTR_PRINTF_SPEC, session_name,
			ast_sorcery_object_get_id(config_profile),
			PJSTR_PRINTF_VAR(geoloc_hdr->hvalue));

		config_eprofile = ast_geoloc_eprofile_create_from_profile(config_profile);
		if (!config_eprofile) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING, "%s: Unable to create eprofile from "
				"profile '%s'\n", session_name, ast_sorcery_object_get_id(config_profile));
		}

		rc = ast_geoloc_datastore_add_eprofile(ds, config_eprofile);
		if (rc <= 0) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING,
				"%s: Couldn't add eprofile '%s' to datastore\n", session_name,
				config_eprofile->id);
		}

		ast_channel_lock(channel);
		ast_channel_datastore_add(channel, ds);
		ast_channel_unlock(channel);
		/*
		 * We gave the datastore to the channel so don't let RAII_VAR clean it up.
		 */
		ds = NULL;

		SCOPE_EXIT_RTN_VALUE(0, "%s: Added geoloc datastore with 1 eprofile\n",
			session_name);
	} else if (config_profile->action == AST_GEOLOC_ACTION_PREPEND) {
		ast_trace(4, "%s: Profile '%s' location_disposition is 'prepend' so "
			"adding to datastore first", session_name, ast_sorcery_object_get_id(config_profile));

		config_eprofile = ast_geoloc_eprofile_create_from_profile(config_profile);
		if (!config_eprofile) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING, "%s: Unable to create eprofile from"
				" profile '%s'\n", session_name, ast_sorcery_object_get_id(config_profile));
		}

		rc = ast_geoloc_datastore_add_eprofile(ds, config_eprofile);
		if (rc <= 0) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING,
				"%s: Couldn't add eprofile '%s' to datastore\n", session_name,
				config_eprofile->id);
		}

		if (!geoloc_hdr) {
			ast_channel_lock(channel);
			ast_channel_datastore_add(channel, ds);
			ast_channel_unlock(channel);
			ds = NULL;

			SCOPE_EXIT_RTN_VALUE(0, "%s: No Geolocation header so just adding config profile "
				"'%s' to datastore\n", session_name, ast_sorcery_object_get_id(config_profile));
		}
	} else if (config_profile->action == AST_GEOLOC_ACTION_REPLACE) {
		if (geoloc_hdr) {
			ast_trace(4, "%s: Profile '%s' location_disposition is 'replace' so "
				"we don't need to do anything with the configured profile", session_name,
				ast_sorcery_object_get_id(config_profile));
		} else {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING,
				"%s: Profile '%s' location_disposition is 'replace' but there's"
				"no Geolocation header and therefore no location info to replace"
				"it with\n", session_name, ast_sorcery_object_get_id(config_profile));
		}
	}

	geoloc_hdr_value = ast_alloca(geoloc_hdr->hvalue.slen + 1);
	ast_copy_pj_str(geoloc_hdr_value, &geoloc_hdr->hvalue, geoloc_hdr->hvalue.slen + 1);
	duped_geoloc_hdr_value = ast_strdupa(geoloc_hdr_value);

	/*
	 * From RFC-6442:
	 * Geolocation-header = "Geolocation" HCOLON locationValue
	 *                      *( COMMA locationValue )
	 * locationValue      = LAQUOT locationURI RAQUOT
	 *                      *(SEMI geoloc-param)
	 * locationURI        = sip-URI / sips-URI / pres-URI
	 *                        / http-URI / https-URI
	 *	                      / cid-url ; (from RFC 2392)
	 *                        / absoluteURI ; (from RFC 3261)
	 */
	while((geoloc_uri = ast_strsep(&duped_geoloc_hdr_value, ',', AST_STRSEP_TRIM))) {
		/* geoloc_uri should now be <scheme:location> */
		int uri_len = strlen(geoloc_uri);
		char *pidf_body = NULL;
		unsigned int pidf_len = 0;
		struct ast_xml_doc *incoming_doc = NULL;

		struct ast_geoloc_eprofile *eprofile = NULL;
		int rc = 0;

		ast_trace(4, "Processing URI '%s'\n", geoloc_uri);

		if (geoloc_uri[0] != '<' || geoloc_uri[uri_len - 1] != '>') {
			ast_log(LOG_WARNING, "%s: Geolocation header has bad URI '%s'.  Skipping\n", session_name,
				geoloc_uri);
			continue;
		}
		/* Trim the trailing '>' and skip the leading '<' */
		geoloc_uri[uri_len - 1] = '\0';
		geoloc_uri++;
		/*
		 * If the URI isn't "cid" then we're just going to pass it through.
		 */
		if (!ast_begins_with(geoloc_uri, "cid:")) {
			ast_trace(4, "Processing URI '%s'.  Reference\n", geoloc_uri);

			eprofile = ast_geoloc_eprofile_create_from_uri(geoloc_uri, session_name);
			if (!eprofile) {
				ast_log(LOG_WARNING, "%s: Unable to create effective profile for URI '%s'.  Skipping\n",
					session_name, geoloc_uri);
				continue;
			}
		} else {
			ast_trace(4, "Processing URI '%s'.  PIDF\n", geoloc_uri);
			rc = find_pidf(session_name, rdata, geoloc_uri, &pidf_body, &pidf_len);
			if (rc != 0 || !pidf_body || pidf_len == 0) {
				continue;
			}

			incoming_doc = ast_xml_read_memory(pidf_body, pidf_len);
			if (!incoming_doc) {
				ast_log(LOG_WARNING, "%s: Unable to parse pidf document for URI '%s'\n",
					session_name, geoloc_uri);
				continue;
			}

			eprofile = ast_geoloc_eprofile_create_from_pidf(incoming_doc, session_name);
		}
		eprofile->action = config_profile->action;
		eprofile->send_location = config_profile->send_location;

		ast_trace(4, "Processing URI '%s'.  Adding to datastore\n", geoloc_uri);
		rc = ast_geoloc_datastore_add_eprofile(ds, eprofile);
		ao2_ref(eprofile, -1);
		if (rc <= 0) {
			ast_log(LOG_WARNING, "%s: Unable to add effective profile for URI '%s' to datastore.  Skipping\n",
				session_name, geoloc_uri);
		}
	}

	if (config_profile->action == AST_GEOLOC_ACTION_APPEND) {
		ast_trace(4, "%s: Profile '%s' location_disposition is 'prepend' so "
			"adding to datastore first", session_name, ast_sorcery_object_get_id(config_profile));

		config_eprofile = ast_geoloc_eprofile_create_from_profile(config_profile);
		if (!config_eprofile) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING, "%s: Unable to create eprofile from"
				" profile '%s'\n", session_name, ast_sorcery_object_get_id(config_profile));
		}

		rc = ast_geoloc_datastore_add_eprofile(ds, config_eprofile);
		if (rc <= 0) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING,
				"%s: Couldn't add eprofile '%s' to datastore\n", session_name,
				config_eprofile->id);
		}
		config_eprofile = NULL;
	}

	eprofile_count = ast_geoloc_datastore_size(ds);
	if (eprofile_count == 0) {
		SCOPE_EXIT_RTN_VALUE(0,
			"%s: Unable to add any effective profiles.  Not adding datastore to channel.\n",
			session_name);
	}

	ast_channel_lock(channel);
	ast_channel_datastore_add(channel, ds);
	ast_channel_unlock(channel);
	ds = NULL;

	SCOPE_EXIT_RTN_VALUE(0, "%s: Added geoloc datastore with %" PRIu64 " eprofiles\n",
		session_name, eprofile_count);
}

static void handle_outgoing_request(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{

}

static struct ast_sip_session_supplement geolocation_supplement = {
	.method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL + 10,
	.incoming_request = handle_incoming_request,
	.outgoing_request = handle_outgoing_request,
};

static int reload_module(void)
{
	return 0;
}

static int unload_module(void)
{
	int res = 0;
	ast_sip_session_unregister_supplement(&geolocation_supplement);

	return res;
}

static int load_module(void)
{
	int res = 0;
	GEOLOCATION_HDR = pj_str("Geolocation");

	ast_sip_session_register_supplement(&geolocation_supplement);

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "res_pjsip_geolocation Module for Asterisk",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 1,
	.requires = "res_geolocation,res_pjsip,res_pjsip_session,chan_pjsip",
);
