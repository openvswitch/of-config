
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <libnetconf.h>

#include "comm_socket.h"

char *
recv_msg(int socket, size_t len, struct nc_err **err)
{
    size_t recv_len = 0;
    ssize_t ret = 0;
    char *msg_dump;

    msg_dump = malloc(sizeof(*msg_dump) * len);
    if (msg_dump == NULL) {
        nc_verb_error("Memory allocation failed - %s (%s:%d).",
                      strerror(errno), __FILE__, __LINE__);
        if (err) {
            *err = nc_err_new(NC_ERR_OP_FAILED);
            nc_err_set(*err, NC_ERR_PARAM_MSG, "Memory allocation failed.");
        }
        return NULL;
    }
    while (recv_len < len) {
        /* recv in loop to pass transfer capacity of the socket */
        ret = recv(socket, &(msg_dump[recv_len]), len - recv_len,
                   OFC_SOCK_SENDFLAGS);
        if (ret <= 0) {
            if (ret == 0) {
                nc_verb_error("Communication failed, socket \"%s\"is closed",
                              OFC_SOCK_PATH);
            } else { /* ret == -1 */
                if (errno == EAGAIN || errno == EINTR) {
                    /* ignore error and try it again */
                    continue;
                }
                nc_verb_error("Communication failed, %s", strerror(errno));
            }
            if (err) {
                *err = nc_err_new(NC_ERR_OP_FAILED);
                nc_err_set(*err, NC_ERR_PARAM_MSG,
                           "agent-server communication failed.");
            }
            free(msg_dump);
            return NULL;
        }
        recv_len += ret;
    }

    return (msg_dump);
}
