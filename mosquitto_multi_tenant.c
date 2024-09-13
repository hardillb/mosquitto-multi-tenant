/*
Copyright (c) 2020-2024 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License 2.0
and Eclipse Distribution License v1.0 which accompany this distribution.

The Eclipse Public License is available at
   https://www.eclipse.org/legal/epl-2.0/
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.

SPDX-License-Identifier: EPL-2.0 OR BSD-3-Clause

Contributors:
   Abilio Marques - initial implementation and documentation.
   Ben Hardill - convert to regex based multi-tenant
*/

/*
 * A very simple Multi-Tenant broker plugin
 *
 * A enhanced version of the mosquitto_topic_jail example.
 *
 * It uses a supplied regex to extract a "team" name from the username.
 * The regex must include a single capture group that extracts the team name,
 * with a default '[a-z0-9]+@([a-z0-9]+)'
 * 
 * e.g. username 'foo@bar' would give a team of 'bar'
 * 
 * Compile with:
 *   gcc -I<path to mosquitto-repo/include> -fPIC -shared mosquitto_multi_tenant.c -o mosquitto_multi_tenant.so
 *
 * Use in config with:
 *
 *   plugin /path/to/mosquitto_multi_tenant.so
 *
 * Note that this only works on Mosquitto 2.1 or later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "mosquitto.h"

#define PLUGIN_NAME "multi-tenant"
#define PLUGIN_VERSION "1.1.0"

#define UNUSED(A) (void)(A)

MOSQUITTO_PLUGIN_DECLARE_VERSION(5);

static mosquitto_plugin_id_t *mosq_pid = NULL;

static regex_t username_match;
static regex_t shared_sub_match;

static char* get_team( const char *str) {
	regmatch_t pmatch[2];
	char *team;
	size_t size;

	int result = regexec(&username_match, str, 2, pmatch, 0);
	if (result == 0) {
		size = (size_t)(pmatch[1].rm_eo - pmatch[1].rm_so);
		team = mosquitto_malloc(size+1);
		strncpy(team, str+pmatch[1].rm_so, size);
		team[size] = 0;
		return team;
	} else {
		return NULL;
	}
}

static int connect_callback(int event, void *event_data, void *userdata)
{
	struct mosquitto_evt_message *ed = event_data;
	const char *id, *username;
	char *new_id;
	size_t idlen, new_id_len;

	UNUSED(event);
	UNUSED(userdata);
	username = mosquitto_client_username(ed->client);

	if (!username) {
		return MOSQ_ERR_SUCCESS;
	}

	id = mosquitto_client_id(ed->client);
	idlen = strlen(id);

	const char *team = get_team(username);
	if(!team){
		/* will only modify the topic of team clients */
		return MOSQ_ERR_SUCCESS;
	}

	/* calculate new client id length */
	new_id_len = strlen(team) + sizeof('@') + idlen + 1;

	new_id = mosquitto_calloc(1, new_id_len);
	if(new_id == NULL){
		return MOSQ_ERR_NOMEM;
	}

	/* generate new client id with team name */
	snprintf(new_id, new_id_len, "%s@%s", id, team);

	mosquitto_free((void *)team);

	mosquitto_set_clientid(ed->client, new_id);

	return MOSQ_ERR_SUCCESS;
}


static int callback_message_in(int event, void *event_data, void *userdata)
{
	struct mosquitto_evt_message *ed = event_data;
	char *new_topic;
	size_t new_topic_len;

	UNUSED(event);
	UNUSED(userdata);

	const char *username = mosquitto_client_username(ed->client);
	if (!username) {
		return MOSQ_ERR_SUCCESS;
	}

	const char *team = get_team(username);
	if(!team){
		/* will only modify the topic of team clients */
		return MOSQ_ERR_SUCCESS;
	}

	/* put the team on front of the topic */

	/* calculate the length of the new payload */
	new_topic_len = strlen(team) + sizeof('/') + strlen(ed->topic) + 1;

	/* Allocate some memory - use
	 * mosquitto_calloc/mosquitto_malloc/mosquitto_strdup when allocating, to
	 * allow the broker to track memory usage */
	new_topic = mosquitto_calloc(1, new_topic_len);
	if(new_topic == NULL){
		return MOSQ_ERR_NOMEM;
	}

	/* prepend the team to the topic */
	snprintf(new_topic, new_topic_len, "%s/%s", team, ed->topic);

	mosquitto_free((void *)team);

	/* Assign the new topic to the event data structure. You
	 * must *not* free the original topic, it will be handled by the
	 * broker. */
	ed->topic = new_topic;

	return MOSQ_ERR_SUCCESS;
}

static int callback_message_out(int event, void *event_data, void *userdata)
{
	struct mosquitto_evt_message *ed = event_data;
	size_t team_len;

	UNUSED(event);
	UNUSED(userdata);

	const char *username = mosquitto_client_username(ed->client);
	if (!username) {
		return MOSQ_ERR_SUCCESS;
	}

	const char *team = get_team(username);
	if(!team){
		/* will only modify the topic of team clients */
		return MOSQ_ERR_SUCCESS;
	}

	/* remove the team from the front of the topic */
	team_len = strlen(team);

	if(strlen(ed->topic) <= team_len + 1){
		/* the topic is not long enough to contain the
		 * team + '/' */
		return MOSQ_ERR_SUCCESS;
	}

	if(!strncmp(team, ed->topic, team_len) && ed->topic[team_len] == '/'){
		/* Allocate some memory - use
		 * mosquitto_calloc/mosquitto_malloc/mosquitto_strdup when allocating, to
		 * allow the broker to track memory usage */

		/* skip the team + '/' */
		char *new_topic = mosquitto_strdup(ed->topic + team_len + 1);

		if(new_topic == NULL){
			return MOSQ_ERR_NOMEM;
		}

		/* Assign the new topic to the event data structure. You
		 * must *not* free the original topic, it will be handled by the
		 * broker. */
		ed->topic = new_topic;
	}

	mosquitto_free((void *)team);

	return MOSQ_ERR_SUCCESS;
}

static int callback_subscribe(int event, void *event_data, void *userdata)
{
	struct mosquitto_evt_subscribe *ed = event_data;
	char *new_sub, *share_group, *topic;
	regmatch_t pmatch[3];
	size_t new_sub_len, group_size, topic_size;

	UNUSED(event);
	UNUSED(userdata);

	const char *username = mosquitto_client_username(ed->client);
	if (!username) {
		return MOSQ_ERR_SUCCESS;
	}

	const char *team = get_team(username);
	if(!team){
		/* will only modify the topic of team clients */
		return MOSQ_ERR_SUCCESS;
	}

	if (!strncmp(ed->data.topic_filter, "$share/", 7)) {
		new_sub_len = strlen(team) + (sizeof('/') * 2) + strlen(ed->data.topic_filter) + 1;

		new_sub = mosquitto_calloc(1, new_sub_len);
		if(new_sub == NULL){
			return MOSQ_ERR_NOMEM;
		}

		int rc = regexec(&shared_sub_match, ed->data.topic_filter, 3, pmatch, 0);
		if (rc == 0) {
			group_size = (size_t)(pmatch[1].rm_eo - pmatch[1].rm_so);
			topic_size = (size_t)(pmatch[2].rm_eo - pmatch[2].rm_so);
			share_group = mosquitto_malloc(group_size+1);
			topic = mosquitto_malloc(topic_size+1);
			strncpy(share_group, ed->data.topic_filter + pmatch[1].rm_so, group_size);
			share_group[group_size] = 0;
			strncpy(topic, ed->data.topic_filter + pmatch[2].rm_so, topic_size);
			topic[topic_size+1] = 0;
			snprintf(new_sub, new_sub_len, "%s/%s/%s", share_group, team, topic);
			
			mosquitto_free((void *)share_group);
			mosquitto_free((void *)topic);
		} else {
			return MOSQ_ERR_NOMEM;
		}
		
	} else {
		/* put the team on front of the topic */

		/* calculate the length of the new payload */
		new_sub_len = strlen(team) + sizeof('/') + strlen(ed->data.topic_filter) + 1;

		/* Allocate some memory - use
		* mosquitto_calloc/mosquitto_malloc/mosquitto_strdup when allocating, to
		* allow the broker to track memory usage */
		new_sub = mosquitto_calloc(1, new_sub_len);
		if(new_sub == NULL){
			return MOSQ_ERR_NOMEM;
		}

		/* prepend the team to the subscription */
		snprintf(new_sub, new_sub_len, "%s/%s", team, ed->data.topic_filter);
	}
	/* Assign the new topic to the event data structure. You
	 * must *not* free the original topic, it will be handled by the
	 * broker. */
	ed->data.topic_filter = new_sub;

	mosquitto_free((void *) team);

	return MOSQ_ERR_SUCCESS;
}

static int callback_unsubscribe(int event, void *event_data, void *userdata)
{
	struct mosquitto_evt_unsubscribe *ed = event_data;
	char *new_sub, *share_group, *topic;
	regmatch_t pmatch[3];
	size_t new_sub_len, group_size, topic_size;

	UNUSED(event);
	UNUSED(userdata);

	const char *username = mosquitto_client_username(ed->client);
	if (!username) {
		return MOSQ_ERR_SUCCESS;
	}

	const char *team = get_team(username);
	if(!team){
		/* will only modify the topic of team clients */
		// mosquitto_free((void *)team);
		return MOSQ_ERR_SUCCESS;
	}

		if (!strncmp(ed->data.topic_filter, "$share/", 7)) {
		new_sub_len = strlen(team) + (sizeof('/') * 2) + strlen(ed->data.topic_filter) + 1;

		new_sub = mosquitto_calloc(1, new_sub_len);
		if(new_sub == NULL){
			return MOSQ_ERR_NOMEM;
		}

		int rc = regexec(&shared_sub_match, ed->data.topic_filter, 3, pmatch, 0);
		if (rc == 0) {
			group_size = (size_t)(pmatch[1].rm_eo - pmatch[1].rm_so);
			topic_size = (size_t)(pmatch[2].rm_eo - pmatch[2].rm_so);
			share_group = mosquitto_malloc(group_size+1);
			topic = mosquitto_malloc(topic_size+1);
			strncpy(share_group, ed->data.topic_filter + pmatch[1].rm_so, group_size);
			share_group[group_size] = 0;
			strncpy(topic, ed->data.topic_filter + pmatch[2].rm_so, topic_size);
			topic[topic_size+1] = 0;
			snprintf(new_sub, new_sub_len, "%s/%s/%s", share_group, team, topic);
			
			mosquitto_free((void *)share_group);
			mosquitto_free((void *)topic);
		} else {
			return MOSQ_ERR_NOMEM;
		}
		
	} else {

		/* put the team on front of the topic */

		/* calculate the length of the new payload */
		new_sub_len = strlen(team) + sizeof('/') + strlen(ed->data.topic_filter) + 1;

		/* Allocate some memory - use
		* mosquitto_calloc/mosquitto_malloc/mosquitto_strdup when allocating, to
		* allow the broker to track memory usage */
		new_sub = mosquitto_calloc(1, new_sub_len);
		if(new_sub == NULL){
			return MOSQ_ERR_NOMEM;
		}

		/* prepend the team to the subscription */
		snprintf(new_sub, new_sub_len, "%s/%s", team, ed->data.topic_filter);
	}
	/* Assign the new topic to the event data structure. You
	 * must *not* free the original topic, it will be handled by the
	 * broker. */
	ed->data.topic_filter = new_sub;

	mosquitto_free((void *)team);

	return MOSQ_ERR_SUCCESS;
}


int mosquitto_plugin_init(mosquitto_plugin_id_t *identifier, void **user_data, struct mosquitto_opt *opts, int opt_count)
{
	int i, found = 0;
	UNUSED(user_data);

	mosq_pid = identifier;
	mosquitto_plugin_set_info(identifier, PLUGIN_NAME, PLUGIN_VERSION);

	/* Find the configuration regex*/
	for(i=0; i<opt_count; i++) {
		if (!strcasecmp(opts[i].key, "regex")) {
			regcomp(&username_match, opts[i].value, REG_EXTENDED);
			found = 1;
		}
	}

	/* If not found use the default */
	if (!found) {
		regcomp(&username_match, "^[a-z0-9]+@([a-z0-9]+)$", REG_EXTENDED);
	}

	regcomp(&shared_sub_match, "^(\\$share/[^/]+)/(.+)$", REG_EXTENDED);

	int rc;
	rc = mosquitto_callback_register(mosq_pid, MOSQ_EVT_CONNECT, connect_callback, NULL, NULL);
	if(rc) return rc;
	rc = mosquitto_callback_register(mosq_pid, MOSQ_EVT_MESSAGE_IN, callback_message_in, NULL, NULL);
	if(rc) return rc;
	rc = mosquitto_callback_register(mosq_pid, MOSQ_EVT_MESSAGE_OUT, callback_message_out, NULL, NULL);
	if(rc) return rc;
	rc = mosquitto_callback_register(mosq_pid, MOSQ_EVT_SUBSCRIBE, callback_subscribe, NULL, NULL);
	if(rc) return rc;
	rc = mosquitto_callback_register(mosq_pid, MOSQ_EVT_UNSUBSCRIBE, callback_unsubscribe, NULL, NULL);
	return rc;
}
