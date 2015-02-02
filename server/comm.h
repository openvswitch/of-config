
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

#ifndef OFC_COMM_H_
#define OFC_COMM_H_

#include <libnetconf.h>

#ifndef DISABLE_DBUS
#	include "comm_dbus.h"
#else
#	include "comm_socket.h"
#endif

/**
 * @brief Connect to D-Bus
 * @param[in] side Caller's type to distinguish server and agents
 * @param[in] crashed 0 if the server finished correctly, otherwise if it crashed
 * @return Connection handler
 */
comm_t *comm_init(int crashed);

/**
 * @brief Destroy all communication structures
 * @param[in] conn Connection handler
 * @return NULL on success, NETCONF error structure in case of failure
 */
void comm_destroy(comm_t *c);

/**
 * @brief Communication loop
 * @param[in] conn Connection handler
 * @param[in] timeout Timeout in milliseconds
 * @return EXIT_FAILURE on fatal error (communication is broken), EXIT_SUCCESS
 * otherwise
 */
int comm_loop(comm_t *c, int timeout);

/*
 * internal function for comm_session_info(), only comm_session_info_send()
 * is supposed to be implemented by specific communication implementation
 */
int comm_session_info_send(comm_t *c, const char *username, const char *sid,
                           struct nc_cpblts *cpblts);

/**
 * @brief Get list of NETCONF capabilities from the server
 * @param[in] conn Connection handler
 * @return List of strings  with the server capabilities
 */
char **comm_get_srv_cpblts(comm_t *c);

/**
 * @brief Perform Netopeer operation
 * @param[in] c Communication handler
 * @param[in] rpc NETCONF RPC request
 * @return NETCONF rpc-reply message with the result.
 */
nc_reply *comm_operation(comm_t *c, const nc_rpc *rpc);

/**
 * @brief Request termination of the specified NETCONF session
 * @param[in] conn Connection handler
 * @param[in] sid NETCONF session identifier
 * @return NETCONF rpc-reply message with the result.
 */
nc_reply *comm_kill_session(comm_t *c, const char *sid);

#endif /* OFC_COMM_H_ */
