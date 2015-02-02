
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

#ifndef OFC_SERVER_OPS_H_
#define OFC_SERVER_OPS_H_

#include <libnetconf_xml.h>
#include <libxml/tree.h>

struct agent_info {
    /* Agent ID */
    char *id;
    /* PID of the agent */
    pid_t pid;
    /* libnetconf session structure. Remember that this session is dummy, we
     * use DBus or UNIX socket as communication channel since there is agent
     * as proxy */
    struct nc_session *session;
    /* Double linked list links. */
    struct agent_info *next, *prev;
};

/**
 * @brief Process NETCONF RPC request
 *
 * @param[in] session Session that sends rpc
 * @param[in] rpc RPC to apply
 *
 * @return nc_reply with response to rpc
 */
nc_reply *srv_process_rpc(struct nc_session *session, const nc_rpc *rpc);

/**
 * @brief Get pointer to the session info structure specified by NETCONF
 * session ID
 *
 * @param id Key for searching
 *
 * @return Pointer to session info structure or NULL on error
 */
struct agent_info *srv_get_agent_by_ncsid(const char *id);

/**
 * @brief Get pointer to the session info structure by the agent ID.
 *
 * @param id ID of agent holding the session
 *
 * @return Session information structure or NULL if no such session exists.
 */
struct agent_info *srv_get_agent_by_agentid(const char *id);

/**
 * @brief Add session to server internal list
 *
 * @param[in] ncsid NETCONF session ID
 * @param[in] username Name of user owning the session
 * @param[in] cpblts List of capabilities session supports
 * @param[in] agentid ID of the agent providing communication for session
 * @param[in] pid PID of the agent process
 */
void
srv_agent_new(const char *ncsid, const char *username, struct nc_cpblts *cpblts,
              const char *agentid, const uint16_t pid);

/**
 * @brief Close and remove session and stop agent
 *
 * @param agent Session to stop.
 */
void
srv_agent_stop(struct agent_info *agent);

/**
 * @brief Stop the other session/agent
 *
 * @param agent Session to kill.
 */
void
 srv_agent_kill(struct agent_info *agent);

#endif /* OFC_SERVER_OPS_H_ */
