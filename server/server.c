
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
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <libnetconf_xml.h>

#include "common.h"
#include "comm.h"

/* default timeout, ms */
#define TIMEOUT 500

/* ietf-netconf-server transAPI structure from netconf-server-transapi.c */
extern struct transapi server_transapi;

/* of-config transAPI structure from ofconfig-transapi.c */
extern struct transapi ofc_transapi;

/* OF-CONFIG datastore functions from ofconfig-datastore.c */
extern struct ncds_custom_funcs ofcds_funcs;

/* main loop flag */
volatile int mainloop = 0;

/* daemonize flag for hack in ofconfig-datastore.c */
volatile int ofc_daemonize = 1;

/* OVSDB socket path shared with ofconfig-datastore.c */
extern char *ovsdb_path;

/* Print usage help */
static void
print_usage(char *progname)
{
    fprintf(stdout, "Usage: %s [-fh] [-d OVSDB] [-v level]\n", progname);
    fprintf(stdout, " -d,--db  OVSDB         socket path to communicate with OVSDB\n"
                    "                        (e.g. -d unix://var/run/openvswitch/db.sock)\n");
    fprintf(stdout, " -f,--foreground        run in foreground\n");
    fprintf(stdout, " -h,--help              display help\n");
    fprintf(stdout, " -v,--verbose level     verbose output level\n");
    exit(0);
}

#define OPTSTRING "fhv:"

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

int
main(int argc, char **argv)
{
    const char *optstring = "d:fhv:";

    const struct option longopts[] = {
        {"db", required_argument, 0, 'd'},
        {"foreground", no_argument, 0, 'f'},
        {"help", no_argument, 0, 'h'},
        {"verbose", required_argument, 0, 'v'},
        {0, 0, 0, 0}
    };
    int longindex, next_option;
    int verbose = 0;
    int retval = EXIT_SUCCESS, r;
    char *aux_string;
    struct sigaction action;
    sigset_t block_mask;

    struct {
        struct ncds_ds *server;
        ncds_id server_id;
        struct ncds_ds *ofc;
        ncds_id ofc_id;
    } ds = {NULL, -1, NULL, -1};

    /* connection channel to agents */
    comm_t *c = NULL;

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
        case 'd':
            ovsdb_path = strdup(optarg);
            break;
        case 'f':
            ofc_daemonize = 0;
            break;
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

    /* normalize value if not from the enum */
    if (verbose < NC_VERB_ERROR) {
        nc_verbosity(NC_VERB_ERROR);
    } else if (verbose > NC_VERB_DEBUG) {
        nc_verbosity(NC_VERB_DEBUG);
    } else {
        nc_verbosity(verbose);
    }

    /* go to the background as a daemon */
    if (ofc_daemonize == 1) {
        if (daemon(0, 0) != 0) {
            nc_verb_error("Going to background failed (%s)", strerror(errno));
            return (EXIT_FAILURE);
        }
        openlog("ofc-server", LOG_PID, LOG_DAEMON);
    } else {
        openlog("ofc-server", LOG_PID | LOG_PERROR, LOG_DAEMON);
    }

    /* make sure we have sufficient rights to communicate with OVSDB */
    /* TODO */

    /* init libnetconf for a multilayer server */
    r = nc_init((NC_INIT_ALL & ~NC_INIT_NACM) | NC_INIT_MULTILAYER);
    if (r < 0) {
        nc_verb_error("libnetconf initialization failed.");
        return (EXIT_FAILURE);
    }

    /* Initiate communication subsystem for communication with agents */
    if ((c = comm_init(r)) == NULL) {
        nc_verb_error("Communication subsystem not initiated.");
        return (EXIT_FAILURE);
    }

    /* prepare the ietf-netconf-server module */
    ncds_add_model(OFC_DATADIR
                   "/ietf-netconf-server/ietf-x509-cert-to-name.yin");
    ds.server =
        ncds_new_transapi_static(NCDS_TYPE_FILE,
                                 OFC_DATADIR
                                 "/ietf-netconf-server/ietf-netconf-server.yin",
                                 &server_transapi);
    if (ds.server == NULL) {
        retval = EXIT_FAILURE;
        nc_verb_error("Creating ietf-netconf-server datastore failed.");
        goto cleanup;
    }
    ncds_file_set_path(ds.server,
                       OFC_DATADIR "/ietf-netconf-server/datastore.xml");
    ncds_feature_enable("ietf-netconf-server", "ssh");
    ncds_feature_enable("ietf-netconf-server", "inbound-ssh");
    if ((ds.server_id = ncds_init(ds.server)) < 0) {
        retval = EXIT_FAILURE;
        nc_verb_error
            ("Initiating ietf-netconf-server datastore failed (error code %d).",
             ds.ofc_id);
        goto cleanup;
    }

    /* prepare the of-config module */
    ds.ofc = ncds_new_transapi_static(NCDS_TYPE_CUSTOM,
                                      OFC_DATADIR "/of-config/of-config.yin",
                                      &ofc_transapi);
    if (ds.ofc == NULL) {
        retval = EXIT_FAILURE;
        nc_verb_error("Creating of-config datastore failed.");
        goto cleanup;
    }
    ncds_custom_set_data(ds.ofc, NULL, &ofcds_funcs);
    if ((ds.ofc_id = ncds_init(ds.ofc)) < 0) {
        retval = EXIT_FAILURE;
        nc_verb_error("Initiating of-config datastore failed (error code %d).",
                      ds.ofc_id);
        goto cleanup;
    }

    if (ncds_consolidate() != 0) {
        retval = EXIT_FAILURE;
        nc_verb_error("Consolidating data models failed.");
        goto cleanup;
    }

    if (ncds_device_init(&(ds.server_id), NULL, 1) != 0) {
        retval = EXIT_FAILURE;
        nc_verb_error("Initiating ietf-netconf-server module failed.");
        goto cleanup;
    }

    if (ncds_device_init(&(ds.ofc_id), NULL, 1) != 0) {
        retval = EXIT_FAILURE;
        nc_verb_error("Initiating of-config module failed.");
        goto cleanup;
    }

    nc_verb_verbose("OF-CONFIG server successfully initialized.");

    while (!mainloop) {
        comm_loop(c, 500);
    }

cleanup:

    /* cleanup */
    nc_close();

    return (retval);
}
