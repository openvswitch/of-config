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

#include <config.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

/*
 #define _GNU_SOURCE

 #include <stdio.h>

 */

#include <libnetconf.h>

#include "comm.h"

char *recv_msg(int socket, size_t len, struct nc_err **err);

int sock = -1;

comm_t *
comm_init(int __attribute__ ((unused)) crashed)
{
    struct sockaddr_un server;

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        nc_verb_error("Unable to create communication socket (%s).",
                      strerror(errno));
        return NULL;
    }

    server.sun_family = AF_UNIX;
    strncpy(server.sun_path, OFC_SOCK_PATH, (sizeof server.sun_path) - 1);

    if (connect(sock, (struct sockaddr *) &server, sizeof server) == -1) {
        nc_verb_error("Unable to connect to the ofc-server (%s).",
                      strerror(errno));

        /* cleanup */
        close(sock);
        sock = -1;

        return NULL;
    }

    nc_verb_verbose("Agent connected with server via UNIX socket");

    return &sock;
}

char **
comm_get_srv_cpblts(comm_t *c)
{
    msgtype_t op = COMM_SOCK_GET_CPBLTS;
    msgtype_t result = 0;
    int count, i;
    unsigned int len;
    char **cpblts = NULL;

    if (*c == -1) {
        nc_verb_error("Invalid communication channel (%s)", __func__);
        return NULL;
    }

    /* operation ID */
    send(*c, &op, sizeof op, OFC_SOCK_SENDFLAGS);

    /* done, now get the result */
    recv(*c, &result, sizeof(result), OFC_SOCK_SENDFLAGS);
    if (op != result) {
        nc_verb_error("Communication failed, sending %d, but received %d.", op,
                      result);
        return NULL;
    }

    /* get the data */
    recv(*c, &count, sizeof count, OFC_SOCK_SENDFLAGS);
    cpblts = calloc(count + 1, sizeof *cpblts);
    cpblts[count] = NULL;
    for (i = 0; i < count; i++) {
        recv(*c, &len, sizeof len, OFC_SOCK_SENDFLAGS);
        if ((cpblts[i] = recv_msg(*c, len, NULL)) == NULL) {
            /* something went wrong */
            for (i--; i >= 0; i--) {
                free(cpblts[i]);
            }
            free(cpblts);
            return NULL;
        }
    }

    return cpblts;
}

int
comm_session_info_send(comm_t *c, const char *username, const char *sid,
                       struct nc_cpblts *cpblts)
{
    msgtype_t op = COMM_SOCK_SET_SESSION;
    msgtype_t result = 0;
    const char *cpblt;
    unsigned int len;
    uint16_t pid;
    int ccount;

    if (*c == -1) {
        nc_verb_error("Invalid communication channel (%s)", __func__);
        return EXIT_FAILURE;
    }

    /* operation ID */
    send(*c, &op, sizeof op, OFC_SOCK_SENDFLAGS);

    /* send session attributes */
    len = strlen(sid) + 1;
    send(*c, &len, sizeof len, OFC_SOCK_SENDFLAGS);
    send(*c, sid, len, OFC_SOCK_SENDFLAGS);

    pid = (uint16_t) getpid();
    send(*c, &pid, sizeof pid, OFC_SOCK_SENDFLAGS);

    len = strlen(username) + 1;
    send(*c, &len, sizeof len, OFC_SOCK_SENDFLAGS);
    send(*c, username, len, OFC_SOCK_SENDFLAGS);

    ccount = nc_cpblts_count(cpblts);
    send(*c, &ccount, sizeof ccount, OFC_SOCK_SENDFLAGS);
    while ((cpblt = nc_cpblts_iter_next(cpblts)) != NULL) {
        len = strlen(cpblt) + 1;
        send(*c, &len, sizeof len, OFC_SOCK_SENDFLAGS);
        send(*c, cpblt, len, OFC_SOCK_SENDFLAGS);
    }

    /* done, now get the result */
    recv(*c, &result, sizeof result, OFC_SOCK_SENDFLAGS);
    if (op != result) {
        nc_verb_error("Communication failed, sending %d, but received %d.", op,
                      result);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

nc_reply *
comm_operation(comm_t *c, const nc_rpc *rpc)
{
    msgtype_t result = 0, op = COMM_SOCK_GENERICOP;
    struct nc_err *err = NULL;
    nc_reply *reply;
    char *msg_dump;
    unsigned int len;

    if (*c == -1) {
        nc_verb_error("Invalid communication channel (%s)", __func__);
        return NULL;
    }

    /* operation ID */
    send(*c, &op, sizeof op, OFC_SOCK_SENDFLAGS);

    /* rpc */
    msg_dump = nc_rpc_dump(rpc);
    len = strlen(msg_dump) + 1;
    send(*c, &len, sizeof len, OFC_SOCK_SENDFLAGS);
    send(*c, msg_dump, len, OFC_SOCK_SENDFLAGS);
    free(msg_dump);

    /* done, now get the result */
    recv(*c, &result, sizeof result, OFC_SOCK_SENDFLAGS);
    if (op != result) {
        nc_verb_error("Communication failed, sending %d, but received %d.", op,
                      result);
        err = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(err, NC_ERR_PARAM_MSG, "agent-server communication failed.");
        return nc_reply_error(err);
    }

    /* get the reply message */
    recv(*c, &len, sizeof len, OFC_SOCK_SENDFLAGS);
    msg_dump = recv_msg(*c, len, &err);
    if (err != NULL) {
        return nc_reply_error(err);
    }
    reply = nc_reply_build(msg_dump);

    /* cleanup */
    free(msg_dump);

    return reply;
}

static int
comm_close(comm_t *c)
{
    msgtype_t result = 0, op = COMM_SOCK_CLOSE_SESSION;

    if (*c == -1) {
        nc_verb_error("Invalid communication channel (%s)", __func__);
        return EXIT_FAILURE;
    }

    /* operation ID */
    send(*c, &op, sizeof op, OFC_SOCK_SENDFLAGS);

    /* done, now get the result */
    recv(*c, &result, sizeof result, OFC_SOCK_SENDFLAGS);
    if (op != result) {
        nc_verb_error("Communication failed, sending %d, but received %d.", op,
                      result);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

nc_reply *
comm_kill_session(comm_t *c, const char *sid)
{
    struct nc_err *err = NULL;
    msgtype_t result = 0, op = COMM_SOCK_KILL_SESSION;
    unsigned int len;
    char *errmsg = NULL;

    if (*c == -1) {
        nc_verb_error("Invalid communication channel (%s)", __func__);
        return NULL;
    }

    /* operation ID */
    send(*c, &op, sizeof op, OFC_SOCK_SENDFLAGS);

    /* session to kill */
    len = strlen(sid) + 1;
    send(*c, &len, sizeof len, OFC_SOCK_SENDFLAGS);
    send(*c, sid, len, OFC_SOCK_SENDFLAGS);

    /* done, now get the result */
    recv(*c, &result, sizeof result, OFC_SOCK_SENDFLAGS);
    if (result == COMM_SOCK_RESULT_ERROR) {
        recv(*c, &len, sizeof len, OFC_SOCK_SENDFLAGS);
        errmsg = recv_msg(*c, len, NULL);
        goto fillerr;
    } else if (op != result) {
        nc_verb_error("Communication failed, sending %d, but received %d.", op,
                      result);
        goto fillerr;
    }

    return nc_reply_ok();

fillerr:
    err = nc_err_new(NC_ERR_OP_FAILED);
    if (errmsg) {
        nc_err_set(err, NC_ERR_PARAM_MSG, errmsg);
    }

    return nc_reply_error(err);
}

void
comm_destroy(comm_t *c)
{
    comm_close(c);

    if (*c == -1) {
        return;
    }

    /* close listen socket */
    close(*c);
    *c = -1;
}
