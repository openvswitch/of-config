
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

#include <getopt.h>
#include <signal.h>
#include <stdlib.h>
#include <syslog.h>

#include <libnetconf_xml.h>

#include "common.h"

/* default poll timeout, ms */
#define TIMEOUT 500

/* Define libnetconf submodules necessary for the NETCONF agent */
#define NC_INIT_AGENT (NC_INIT_NOTIF | NC_INIT_MONITORING | NC_INIT_WD | NC_INIT_SINGLELAYER)

/* main loop flag */
volatile int mainloop = 0;

struct ntf_thread_config {
    struct nc_session *session;
    nc_rpc *subscribe_rpc;
};

static void *
notification_thread(void *arg)
{
    struct ntf_thread_config *config = (struct ntf_thread_config *) arg;

    ncntf_dispatch_send(config->session, config->subscribe_rpc);
    nc_rpc_free(config->subscribe_rpc);
    free(config);

    return (NULL);
}

/* Signal handler - controls main loop */
void
signal_handler(int sig)
{
    nc_verb_verbose("Signal %d received.", sig);

    switch (sig) {
    case SIGINT:
    case SIGTERM:
    case SIGQUIT:
    case SIGABRT:
    case SIGKILL:
        if (mainloop == 0) {
            /* first attempt */
            mainloop = 1;
        } else {
            /* second attempt */
            nc_verb_error("Hey! I need some time, be patient next time!");
            exit(EXIT_FAILURE);
        }
        break;
    default:
        nc_verb_error("Exiting on signal: %d", sig);
        exit(EXIT_FAILURE);
        break;
    }
}

#if 0
static struct nc_cpblts *
get_server_capabilities(conn_t * conn)
{
    struct nc_cpblts *srv_cpblts;
    char **cpblts_list = NULL;
    int i;

    if ((cpblts_list = comm_get_srv_cpblts(conn)) == NULL) {
        nc_verb_error("Cannot get server capabilities!");
        return (NULL);
    }

    /* Fill server capabilities structure */
    srv_cpblts = nc_cpblts_new((const char *const *) cpblts_list);

    /* cleanup */
    for (i = 0; cpblts_list != NULL && cpblts_list[i] != NULL; i++) {
        free(cpblts_list[i]);
    }
    free(cpblts_list);

    return srv_cpblts;
}

int
process_message(struct nc_session *session, conn_t * conn, const nc_rpc * rpc)
{
    nc_reply *reply = NULL;
    struct nc_err *err;
    pthread_t thread;
    struct ntf_thread_config *ntf_config;
    xmlNodePtr op;
    char *sid;

    if (rpc == NULL) {
        nc_verb_error("Invalid RPC to process.");
        return (EXIT_FAILURE);
    }

    /* close-session message */
    switch (nc_rpc_get_op(rpc)) {
    case NC_OP_CLOSESESSION:
        if (comm_close(conn) != EXIT_SUCCESS) {
            err = nc_err_new(NC_ERR_OP_FAILED);
            reply = nc_reply_error(err);
        } else {
            reply = nc_reply_ok();
        }
        mainloop = 1;
        break;
    case NC_OP_KILLSESSION:
        if ((op = ncxml_rpc_get_op_content(rpc)) == NULL || op->name == NULL ||
            xmlStrEqual(op->name, BAD_CAST "kill-session") == 0) {
            nc_verb_error("Corrupted RPC message.");
            reply = nc_reply_error(nc_err_new(NC_ERR_OP_FAILED));
            xmlFreeNodeList(op);
            goto send_reply;
        }
        if (op->children == NULL
            || xmlStrEqual(op->children->name, BAD_CAST "session-id") == 0) {
            nc_verb_error("No session id found.");
            err = nc_err_new(NC_ERR_MISSING_ELEM);
            nc_err_set(err, NC_ERR_PARAM_INFO_BADELEM, "session-id");
            reply = nc_reply_error(err);
            xmlFreeNodeList(op);
            goto send_reply;
        }
        sid = (char *) xmlNodeGetContent(op->children);
        reply = comm_kill_session(conn, sid);
        xmlFreeNodeList(op);
        free(sid);
        break;
    case NC_OP_CREATESUBSCRIPTION:
        /* create-subscription message */
        if (nc_cpblts_enabled
            (session,
             "urn:ietf:params:netconf:capability:notification:1.0") == 0) {
            reply = nc_reply_error(nc_err_new(NC_ERR_OP_NOT_SUPPORTED));
            goto send_reply;
        }

        /* check if notifications are allowed on this session */
        if (nc_session_notif_allowed(session) == 0) {
            nc_verb_error("Notification subscription is not allowed on this session.");
            err = nc_err_new(NC_ERR_OP_FAILED);
            nc_err_set(err, NC_ERR_PARAM_TYPE, "protocol");
            nc_err_set(err, NC_ERR_PARAM_MSG,
                       "Another notification subscription is currently active on this session.");
            reply = nc_reply_error(err);
            goto send_reply;
        }

        reply = ncntf_subscription_check(rpc);
        if (nc_reply_get_type(reply) != NC_REPLY_OK) {
            goto send_reply;
        }

        if ((ntf_config = malloc(sizeof (struct ntf_thread_config))) == NULL) {
            nc_verb_error("Memory allocation failed.");
            err = nc_err_new(NC_ERR_OP_FAILED);
            nc_err_set(err, NC_ERR_PARAM_MSG, "Memory allocation failed.");
            reply = nc_reply_error(err);
            err = NULL;
            goto send_reply;
        }
        ntf_config->session = (struct nc_session *) session;
        ntf_config->subscribe_rpc = nc_rpc_dup(rpc);

        /* perform notification sending */
        if ((pthread_create(&thread, NULL, notification_thread, ntf_config)) !=
            0) {
            nc_reply_free(reply);
            err = nc_err_new(NC_ERR_OP_FAILED);
            nc_err_set(err, NC_ERR_PARAM_MSG,
                       "Creating thread for sending Notifications failed.");
            reply = nc_reply_error(err);
            err = NULL;
            goto send_reply;
        }
        pthread_detach(thread);
        break;
    default:
        /* other messages */
        reply = comm_operation(conn, rpc);
        break;
    }

send_reply:
    nc_session_send_reply(session, rpc, reply);
    nc_reply_free(reply);
    return EXIT_SUCCESS;
}

#endif


static void
print_usage(char *progname)
{
    fprintf(stdout, "This program is not supposed for manual use.\n");
    exit(0);
}

int
main(int argc, char **argv)
{
    int ret;
    NC_MSG_TYPE rpc_type;

    const char *optstring = "hv:";
    const struct option longopts[] = {
        {"help", no_argument, 0, 'h'},
        {"verbose", required_argument, 0, 'v'},
        {0, 0, 0, 0}
    };
    int longindex, next_option;
    int verbose = 0;
    char *aux_string = NULL;

    struct sigaction action;
    sigset_t block_mask;
#if 0
    conn_t *con;
#endif
    struct nc_cpblts *capabilities = NULL;
    struct nc_session *ncs;
    nc_rpc *rpc = NULL;

    /* initialize message system and set verbose and debug variables */
    if ((aux_string = getenv(ENVIRONMENT_VERBOSE)) == NULL) {
        /* default verbose level */
        verbose = NC_VERB_ERROR;
    } else {
        verbose = atoi(aux_string);
    }

    /* parse given options */
    while ((next_option = getopt_long(argc, argv, optstring, longopts,
                                      &longindex)) != -1) {
        switch (next_option) {
        case 'h':
            print_usage(argv[0]);
            break;
        case 'v':
            verbose = atoi(optarg);
            break;
        default:
            print_usage(argv[0]);
            break;
        }
    }

    /* set signal handler */
    sigfillset(&block_mask);
    action.sa_handler = signal_handler;
    action.sa_mask = block_mask;
    action.sa_flags = 0;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGQUIT, &action, NULL);
    sigaction(SIGABRT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGKILL, &action, NULL);

    /* set verbose message printer callback */
    nc_callback_print(clb_print);
    openlog("netopeer-agent", LOG_PID, LOG_DAEMON);

    /* normalize value if not from the enum */
    if (verbose < NC_VERB_ERROR) {
        nc_verbosity(NC_VERB_ERROR);
    } else if (verbose > NC_VERB_DEBUG) {
        nc_verbosity(NC_VERB_DEBUG);
    } else {
        nc_verbosity(verbose);
    }

    /* initiate libnetconf */
    if (nc_init(NC_INIT_NOTIF | NC_INIT_MONITORING
                    | NC_INIT_WD | NC_INIT_SINGLELAYER) < 0) {
        nc_verb_error("Library initialization failed");
        return EXIT_FAILURE;
    }

#if 0
    /* connect to server */
    if ((con = comm_connect()) == NULL) {
        nc_verb_error("Cannot connect to Netopeer server.");
        return EXIT_FAILURE;
    }
    nc_verb_verbose("Connected with Netopeer server");

    /* get server capabilities */
    if ((capabilities = get_server_capabilities(con)) == NULL) {
        nc_verb_error("Cannot get server capabilities.");
        return EXIT_FAILURE;
    }
#endif

    /* accept NETCONF session */
    ncs = nc_session_accept(capabilities);
    nc_cpblts_free(capabilities);
    if (ncs == NULL) {
        nc_verb_error("Failed to connect agent.");
        return EXIT_FAILURE;
    }

    /* monitor this session and build statistics */
    nc_session_monitor(ncs);

#if 0
    /* create the session */
    if (comm_session_info(con, ncs)) {
        nc_verb_error("Failed to comunicate with server.");
        return EXIT_FAILURE;
    }
#endif

    nc_verb_verbose("Init finished, starting main loop");

    while (!mainloop) {
        /* read data from input */
        switch (nc_session_recv_rpc(ncs, TIMEOUT, &rpc)) {
        case NC_MSG_RPC:
            nc_verb_verbose("Processing client message");
#if 0
            if (process_message(ncs, con, rpc) != EXIT_SUCCESS) {
                nc_verb_warning("Message processing failed");
            }
#endif
            nc_rpc_free(rpc);
            rpc = NULL;

            break;
        case NC_MSG_NONE:
            /* the request was already processed by libnetconf or no
             * message available, continue in main loop */
            break;
        case NC_MSG_UNKNOWN:
            if (nc_session_get_status(ncs) != NC_SESSION_STATUS_WORKING) {
                /* something really bad happened, and communication is not
                 * possible anymore */
                nc_verb_error("Failed to receive clinets message");
                goto cleanup;
            }
            /* continue in main while loop */
            break;
        default:
            /* continue in main while loop */
            break;
        }
    }

cleanup:
    nc_rpc_free(rpc);
    nc_session_free(ncs);
    nc_close();

    return (EXIT_SUCCESS);
}
