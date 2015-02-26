
/* Copyright (c) 2015 Open Networking Foundation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <signal.h>
#include <string.h>

#include "server_ops.h"

/* Internal list of NETCONF sessions - agents connected via DBus */
static struct agent_info *agents = NULL;

struct agent_info *
srv_get_agent_by_ncsid(const char *id)
{
    struct agent_info *aux_session = agents;

    while (aux_session != NULL) {
        if (strcmp(id, nc_session_get_id(aux_session->session)) == 0) {
            break;
        }
        aux_session = aux_session->next;
    }

    return (aux_session);
}

struct agent_info *
srv_get_agent_by_agentid(const char *id)
{
    struct agent_info *aux_session = agents;

    while (aux_session != NULL) {
        if (strcmp(id, aux_session->id) == 0) {
            break;
        }
        aux_session = aux_session->next;
    }

    return (aux_session);
}

void
srv_agent_new(const char *ncsid, const char *username,
              struct nc_cpblts *cpblts, const char *agentid,
              const uint16_t pid)
{
    struct agent_info *agent, *agents_iter = agents;

    agent = calloc(1, sizeof (struct agent_info));

    /* create dummy session */
    agent->session = nc_session_dummy(ncsid, username, NULL, cpblts);

    /* add to monitored session list, library will connect this dummy session
     * with real session in agent */
    nc_session_monitor(agent->session);

    agent->id = strdup(agentid);
    agent->pid = pid;

    if (agents == NULL) {
        /* first session */
        agents = agent;
        agent->prev = NULL;
    } else {
        while (agents_iter->next != NULL) {
            agents_iter = agents_iter->next;
        }
        agents_iter->next = agent;
        agent->prev = agents_iter;
    }
}

void
srv_agent_stop(struct agent_info *agent)
{
    assert(agent);

    /* remove from the list */
    if (agent->prev != NULL) {
        agent->prev->next = agent->next;
    } else {
        agents = agent->next;
    }
    if (agent->next != NULL) {
        agent->next->prev = agent->prev;
    }

    /* close & free libnetconf session */
    nc_session_free(agent->session);

    /* free agent structure */
    free(agent->id);
    free(agent);
}

void
srv_agent_kill(struct agent_info *agent)
{
    assert(agent);

    /* kill agent process */
    kill(agent->pid, SIGTERM);

    /* remove agent record from internal records */
    srv_agent_stop(agent);
}

nc_reply *
srv_process_rpc(struct nc_session *session, const nc_rpc *rpc)
{
    nc_reply *reply;

    struct nc_err *err;

    if ((reply = ncds_apply_rpc2all(session, rpc, NULL)) == NULL) {
        err = nc_err_new(NC_ERR_OP_FAILED);
        reply = nc_reply_error(err);
    } else if (reply == NCDS_RPC_NOT_APPLICABLE) {
        err = nc_err_new(NC_ERR_OP_FAILED);
        reply = nc_reply_error(err);
    }

    return reply;
}
