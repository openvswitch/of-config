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
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

/*
 #define _GNU_SOURCE
 #include <libnetconf.h>
 #include <stdlib.h>
 #include <sys/stat.h>
 #include <pwd.h>
 #include <grp.h>

 #include "comm.h"
 #include "netopeer_socket.h"
 #include "server_operations.h"
 */

#include <libnetconf_xml.h>

#include "comm.h"
#include "server_ops.h"

char *recv_msg(int socket, size_t len, struct nc_err **err);

#define AGENTS_QUEUE 10
int sock = -1;

/* Active connections to the agents */
struct pollfd agents[AGENTS_QUEUE + 1];

static int connected_agents = 0;

static struct sockaddr_un server;

comm_t *
comm_init(int crashed)
{
    int i, flags;
    mode_t mask;

#ifdef OFC_SOCK_GROUP
    struct group *grp;
#endif

    if (sock != -1) {
        return (&sock);
    }

    /* check another instance of the netopeer-server */
    if (access(OFC_SOCK_PATH, F_OK) == 0) {
        if (crashed) {
            if (unlink(OFC_SOCK_PATH) != 0) {
                nc_verb_error("Failed to remove leftover communication socket, please remove \'%s\' file manually.",
                               OFC_SOCK_PATH);
                return (NULL);
            }
        } else {
            nc_verb_error("Communication socket \'%s\' already exists.",
            OFC_SOCK_PATH);
            nc_verb_error("Another instance of the ofc-server is running. If not, please remove \'%s\' file manually.",
                          OFC_SOCK_PATH);
            return (NULL);
        }
    }

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        nc_verb_error("Unable to create communication socket (%s).",
                      strerror(errno));
        return (NULL);
    }
    /* set the socket non-blocking */
    flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    /* prepare structure */
    memset(&server, 0, sizeof server);
    server.sun_family = AF_UNIX;
    strncpy(server.sun_path, OFC_SOCK_PATH, (sizeof server.sun_path) - 1);

    /* set socket permission using umask */
    mask = umask(~OFC_SOCK_PERM);
    /* bind socket to the file path */
    if (bind(sock, (struct sockaddr *) &server, sizeof server) == -1) {
        nc_verb_error("Unable to bind to a UNIX socket \'%s\' (%s).",
                        server.sun_path, strerror(errno));
        goto error_cleanup;
    }
    umask(mask);

#ifdef OFC_SOCK_GROUP
    grp = getgrnam(COMM_SOCKET_GROUP);
    if (grp == NULL || chown(server.sun_path, -1, grp->gr_gid) == -1) {
        nc_verb_error("Setting communication socket permissions failed (%s)",
                      strerror(errno));
        goto error_cleanup;
    }
#endif

    /* start listening */
    if (listen(sock, AGENTS_QUEUE) == -1) {
        nc_verb_error("Unable to switch a socket into a listening mode (%s).",
                      strerror(errno));
        goto error_cleanup;
    }

    /* the first agent is actually server's listen socket */
    agents[0].fd = sock;
    agents[0].events = POLLIN;
    agents[0].revents = 0;

    /* initiate agents list */
    for (i = 1; i <= AGENTS_QUEUE; i++) {
        agents[i].fd = -1;
        agents[i].events = POLLIN;
        agents[i].revents = 0;
    }

    return (&sock);

    error_cleanup: close(sock);
    sock = -1;
    unlink(server.sun_path);
    return (NULL);
}

static void
get_capabilities(int socket)
{
    const char *cpblt;
    struct nc_cpblts *cpblts;
    unsigned int len;
    int count;
    msgtype_t result;

    result = COMM_SOCK_GET_CPBLTS;
    send(socket, &result, sizeof result, OFC_SOCK_SENDFLAGS);

    cpblts = nc_session_get_cpblts_default();
    count = nc_cpblts_count(cpblts);
    send(socket, &count, sizeof count, OFC_SOCK_SENDFLAGS);

    nc_cpblts_iter_start(cpblts);
    while ((cpblt = nc_cpblts_iter_next(cpblts)) != NULL) {
        len = strlen(cpblt) + 1;
        send(socket, &len, sizeof len, OFC_SOCK_SENDFLAGS);
        send(socket, cpblt, len, OFC_SOCK_SENDFLAGS);
    }
    nc_cpblts_free(cpblts);
}

static void
set_session(int socket)
{
    char *session_id = NULL, *username = NULL;
    struct nc_cpblts *cpblts;
    char **cpblts_list;
    char id[6];
    int i, cpblts_count;
    uint16_t pid;
    unsigned int len;

    msgtype_t result;

    /* session ID */
    recv(socket, &len, sizeof len, OFC_SOCK_SENDFLAGS);
    session_id = malloc((sizeof *session_id) * len);
    recv(socket, session_id, len, OFC_SOCK_SENDFLAGS);

    /* agent's PID */
    recv(socket, &pid, sizeof pid, OFC_SOCK_SENDFLAGS);

    /* username */
    recv(socket, &len, sizeof len, OFC_SOCK_SENDFLAGS);
    username = malloc((sizeof *username) * len);
    recv(socket, username, len, OFC_SOCK_SENDFLAGS);

    /* capabilities */
    recv(socket, &cpblts_count, sizeof cpblts_count, OFC_SOCK_SENDFLAGS);
    cpblts_list = calloc(cpblts_count + 1, sizeof *cpblts_list);
    cpblts_list[cpblts_count] = NULL;
    for (i = 0; i < cpblts_count; i++) {
        recv(socket, &len, sizeof len, OFC_SOCK_SENDFLAGS);
        if ((cpblts_list[i] = recv_msg(socket, len, NULL)) == NULL) {
            /* something went wrong */
            for (i--; i >= 0; i--) {
                free(cpblts_list[i]);
            }
            free(cpblts_list);
            result = COMM_SOCK_RESULT_ERROR;
            send(socket, &result, sizeof result, OFC_SOCK_SENDFLAGS);
            return;
        }
    }
    cpblts = nc_cpblts_new((const char * const *) cpblts_list);

    /* add session to the list */
    snprintf(id, sizeof(id), "%d", socket);
    srv_agent_new(session_id, username, cpblts, id, pid);
    nc_verb_verbose("New agent ID set to %s (PID %d, NCSID %s)", id, pid,
                    session_id);

    /* clean */
    free(session_id);
    free(username);
    nc_cpblts_free(cpblts);
    for (i = 0; i < cpblts_count; i++) {
        free(cpblts_list[i]);
    }
    free(cpblts_list);

    /* send reply */
    result = COMM_SOCK_SET_SESSION;
    send(socket, &result, sizeof result, OFC_SOCK_SENDFLAGS);
}

static void
close_session(int socket)
{
    char id[6];
    struct agent_info *sender_session;
    msgtype_t result;

    snprintf(id, sizeof(id), "%d", socket);
    sender_session = srv_get_agent_by_agentid(id);
    if (sender_session == NULL) {
        nc_verb_warning("Unable to close session (not found)");
        return;
    }
    srv_agent_stop(sender_session);

    /* send reply */
    result = COMM_SOCK_CLOSE_SESSION;
    send(socket, &result, sizeof result, OFC_SOCK_SENDFLAGS);

    nc_verb_verbose("Agent %s removed.", id);
}

static void
kill_session(int socket)
{
    struct agent_info *session, *sender;
    char *ncsid2kill = NULL;
    unsigned int len = 0;
    char id[6];
    const char *errmsg = NULL;
    msgtype_t result = COMM_SOCK_KILL_SESSION;

    /* session ID */
    recv(socket, &len, sizeof len, OFC_SOCK_SENDFLAGS);
    ncsid2kill = recv_msg(socket, len, NULL);
    if (!ncsid2kill) {
        nc_verb_warning("Invalid data in the message");
        result = COMM_SOCK_RESULT_ERROR;
        errmsg = "SessionID expected as a string value";
        goto sendreply;
    }
    if ((session = srv_get_agent_by_ncsid(ncsid2kill)) == NULL) {
        nc_verb_error("Unable to kill session %s (not found)", ncsid2kill);
        result = COMM_SOCK_RESULT_ERROR;
        errmsg = "Session to kill does not exists";
        goto sendreply;
    }

    /* check if the request does not relate to the current session */
    snprintf(id, sizeof(id), "%d", socket);
    if ((sender = srv_get_agent_by_agentid(id)) != NULL) {
        if (strcmp(nc_session_get_id(sender->session), ncsid2kill) == 0) {
            nc_verb_warning("Killing own session requested");
            result = COMM_SOCK_RESULT_ERROR;
            errmsg = "Killing own session requested";
            goto sendreply;
        }
    } else {
        /* something is wrong, the sender's session does not exist */
        nc_verb_error("Kill session requested by unknown agent (%s)", id);
        result = COMM_SOCK_RESULT_ERROR;
        errmsg = "You are unknown client";
        goto sendreply;
    }

    srv_agent_kill(session);

sendreply:
    /* send reply */
    send(socket, &result, sizeof(result), OFC_SOCK_SENDFLAGS);

    if (errmsg) {
        len = strlen(errmsg) + 1;
        send(socket, &len, sizeof len, OFC_SOCK_SENDFLAGS);
        send(socket, errmsg, len, OFC_SOCK_SENDFLAGS);
    }

    /* cleanup */
    free(ncsid2kill);
}

static void
process_operation(int socket)
{
    struct agent_info *session;
    struct nc_err *err = NULL;
    char *msg_dump = NULL;
    unsigned int len;
    char id[6];
    nc_reply *reply;
    nc_rpc *rpc;
    msgtype_t result;

    /* RPC dump */
    recv(socket, &len, sizeof len, OFC_SOCK_SENDFLAGS);
    msg_dump = recv_msg(socket, len, &err);
    if (err != NULL) {
        reply = nc_reply_error(err);
        goto send_reply;
    }

    snprintf(id, sizeof id, "%d", socket);
    if ((session = srv_get_agent_by_agentid(id)) == NULL) {
        /* something is wrong, the sender's session does not exist */
        nc_verb_error("Unknown session %s", id);
        err = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(err, NC_ERR_PARAM_MSG, "request from unknown agent");
        reply = nc_reply_error(err);
        goto send_reply;
    }

    rpc = nc_rpc_build(msg_dump, session->session);
    free(msg_dump);

    if ((reply = srv_process_rpc(session->session, rpc)) == NULL) {
        err = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(err, NC_ERR_PARAM_MSG,
                   "For unknown reason no reply was returned by device.");
        reply = nc_reply_error(err);
    } else if (reply == NCDS_RPC_NOT_APPLICABLE) {
        err = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(err, NC_ERR_PARAM_MSG,
                        "There is no device/data that could be affected.");
        reply = nc_reply_error(err);
    }
    nc_rpc_free(rpc);

send_reply:
    msg_dump = nc_reply_dump(reply);
    nc_reply_free(reply);

    /* send reply */
    result = COMM_SOCK_GENERICOP;
    send(socket, &result, sizeof result, OFC_SOCK_SENDFLAGS);

    len = strlen(msg_dump) + 1;
    send(socket, &len, sizeof len, OFC_SOCK_SENDFLAGS);
    send(socket, msg_dump, len, OFC_SOCK_SENDFLAGS);

    /* cleanup */
    free(msg_dump);
}

/* Main communication loop */
int
comm_loop(comm_t *c, int timeout)
{
    int ret, i, new_sock, n;
    msgtype_t op, result;

    if (*c == -1) {
        return EXIT_FAILURE;
    }

poll_restart:
    ret = poll(agents, AGENTS_QUEUE + 1, timeout);
    if (ret == -1) {
        if (errno == EINTR) {
            goto poll_restart;
        }
        nc_verb_error("Communication failed (poll: %s).", strerror(errno));
        comm_destroy(c);
        return EXIT_FAILURE;
    }

    if (ret == 0) {
        /* timeouted */
        return (EXIT_SUCCESS);
    }


        /* check agent's communication sockets */
    for (i = 1; i <= AGENTS_QUEUE && connected_agents > 0; i++) {
        if (agents[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            nc_verb_error("Communication socket is unexpectedly closed");

            /* close client's socket */
            agents[i].fd = -1;
            /* if disabled accepting new clients, enable it */
            connected_agents--;
            if (agents[0].fd == -1) {
                agents[0].fd = *c;
            }

            /* continue with the following client */
            continue;

        } else if (agents[i].revents & POLLIN) {
            ret--;

            n = recv(agents[i].fd, &op, sizeof(msgtype_t), 0);
            if (n <= 0) {
                continue;
            }
            switch (op) {
            case COMM_SOCK_GET_CPBLTS:
                get_capabilities(agents[i].fd);
                break;
            case COMM_SOCK_SET_SESSION:
                set_session(agents[i].fd);
                break;
            case COMM_SOCK_CLOSE_SESSION:
                close_session(agents[i].fd);

                /* close the socket */
                close(agents[i].fd);
                agents[i].fd = -1;

                /* if disabled accepting new clients, enable it */
                connected_agents--;
                if (agents[0].fd == -1) {
                    agents[0].fd = *c;
                }

                break;
            case COMM_SOCK_KILL_SESSION:
                kill_session(agents[i].fd);
                break;
            case COMM_SOCK_GENERICOP:
                process_operation(agents[i].fd);
                break;
            default:
                nc_verb_warning("Unsupported UNIX socket message received.");
                result = COMM_SOCK_RESULT_ERROR;
                send(agents[i].fd, &result, sizeof result, OFC_SOCK_SENDFLAGS);
            }

            if (ret == 0) {
                /* we are done */
                return (EXIT_SUCCESS);
            }
        }
    }

    /* check new incoming connection(s) */
    if (agents[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        nc_verb_error("Communication closed.");
        comm_destroy(c);
        return (EXIT_FAILURE);
    } else if (agents[0].revents & POLLIN) {
        while ((new_sock = accept(agents[0].fd, NULL, NULL)) != -1) {
            /* new agent connection */
            for (i = 1; i <= AGENTS_QUEUE && agents[i].fd != -1; i++)
                ;
            agents[i].fd = new_sock;
            connected_agents++;
            if (connected_agents == AGENTS_QUEUE) {
                /* we have no more space for new connection */
                /* temporary disable poll on listen socket */
                agents[0].fd = -1;
                break;
            }
            nc_verb_verbose("Some ofc-agent connected to the UNIX socket.");
        }
        if (new_sock == -1 && errno != EAGAIN) {
            nc_verb_error("Communication failed (accept: %s).",
                            strerror(errno));
            comm_destroy(c);
            return (EXIT_FAILURE);
        }
        /* else as expected - no more new connection (or no more space for new
         * connection), so continue
         */
        if (connected_agents < AGENTS_QUEUE) {
            agents[0].revents = 0;
        }
    }

    return (EXIT_SUCCESS);
}

void
comm_destroy(comm_t *c)
{
    int i;

    if (*c == -1) {
        return;
    }

    if (connected_agents > 0) {
        for (i = 1; i <= AGENTS_QUEUE; i++) {
            close(agents[i].fd);
        }
        connected_agents = 0;
    }
    /* close listen socket */
    close(sock);
    sock = -1;

    unlink(server.sun_path);
}
