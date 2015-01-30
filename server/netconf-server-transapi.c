
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

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libxml/tree.h>
#include <libnetconf_xml.h>
#include <libnetconf_ssh.h>

#ifdef __GNUC__
#	define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#	define UNUSED(x) UNUSED_ ## x
#endif

/* TODO: move this into configure */
#define SSHD_EXEC "/usr/sbin/sshd"

/* Name of the environment variable where other modules of the server can get
 * PID of the SSH daemon
 */
#define SSHDPID_ENV "SSHD_PID"

/* transAPI version which must be compatible with libnetconf */
/* int transapi_version = 5; */

/* Signal to libnetconf that configuration data were modified by any callback.
 * 0 - data not modified
 * 1 - data have been modified
 */
int server_config_modified = 0;

/* Do not modify or set! This variable is set by libnetconf to announce
 * edit-config's error-option. Feel free to use it to distinguish module
 * behavior for different error-option values.
 * Possible values:
 * NC_EDIT_ERROPT_STOP - Following callback after failure are not executed, all
 *     successful callbacks executed till failure point must be applied to the
 *     device.
 * NC_EDIT_ERROPT_CONT - Failed callbacks are skipped, but all callbacks needed
 *     to apply configuration changes are executed
 * NC_EDIT_ERROPT_ROLLBACK - After failure, following callbacks are not
 *     executed, but previous successful callbacks are executed again with
 *     previous configuration data to roll it back.
 */
NC_EDIT_ERROPT_TYPE server_erropt = NC_EDIT_ERROPT_NOTSET;

static u_int16_t sshd_pid = 0;
static char *sshd_listen = NULL;

/* Routine to kill listening SSH daemon */
static void
kill_sshd(void)
{
    if (sshd_pid != 0) {
        kill(sshd_pid, SIGTERM);
        sshd_pid = 0;
        unsetenv(SSHDPID_ENV);
    }
}

/* Retrieve state data from device and return them as XML document
 *
 * Model doesn't contain any status data
 */
xmlDocPtr
server_get_state_data(xmlDocPtr UNUSED(model), xmlDocPtr UNUSED(running),
                      struct nc_err **UNUSED(err))
{
    return (NULL);
}

/* Mapping between prefixes and namespaces. */
struct ns_pair server_namespace_mapping[] = {
    {"srv", "urn:ietf:params:xml:ns:yang:ietf-netconf-server"},
    {NULL, NULL}
};

/*
 * CONFIGURATION callbacks
 */

/* Callback for /srv:netconf/srv:ssh/srv:listen/srv:port changes.
 *
 * It sets configuration for SSH daemon for listening on a single interface.
 */
int
callback_srv_netconf_srv_ssh_srv_listen_oneport(void **UNUSED(data),
                                                XMLDIFF_OP op,
                                                xmlNodePtr node,
                                                struct nc_err **error)
{
    char *port;

    if (op != XMLDIFF_REM) {
        port = (char *) xmlNodeGetContent(node);
        nc_verb_verbose("%s: port %s", __func__, port);
        if (asprintf
            (&sshd_listen, "Port %s\nListenAddress 0.0.0.0\nListenAddress ::",
             port) == -1) {
            sshd_listen = NULL;
            nc_verb_error("asprintf() failed (%s at %s:%d).", __func__,
                          __FILE__, __LINE__);
            *error = nc_err_new(NC_ERR_OP_FAILED);
            nc_err_set(*error, NC_ERR_PARAM_MSG,
                       "ietf-netconf-server module internal error");
            return (EXIT_FAILURE);
        }
        free(port);
    }

    return (EXIT_SUCCESS);
}

/* Callback for /srv:netconf/srv:ssh/srv:listen/srv:interface changes.
 *
 * It sets configuration for SSH daemon for listening on multiple interfaces.
 */
int
callback_srv_netconf_srv_ssh_srv_listen_manyports(void **UNUSED(data),
                                                  XMLDIFF_OP op,
                                                  xmlNodePtr node,
                                                  struct nc_err **error)
{
    xmlNodePtr n;
    char *addr = NULL, *port = NULL, *result = NULL;
    int ret = EXIT_SUCCESS;

    if (op != XMLDIFF_REM) {
        for (n = node->children; n != NULL && (addr == NULL || port == NULL);
             n = n->next) {
            if (n->type != XML_ELEMENT_NODE) {
                continue;
            }

            if (addr == NULL && xmlStrcmp(n->name, BAD_CAST "address") == 0) {
                addr = (char *) xmlNodeGetContent(n);
            } else if (port == NULL
                       && xmlStrcmp(n->name, BAD_CAST "port") == 0) {
                port = (char *) xmlNodeGetContent(n);
            }
        }
        nc_verb_verbose("%s: addr %s, port %s", __func__, addr, port);
        if (asprintf(&result, "%sListenAddress %s:%s\n",
                     (sshd_listen == NULL) ? "" : sshd_listen, addr,
                     port) == -1) {
            result = NULL;
            nc_verb_error("asprintf() failed (%s at %s:%d).", __func__,
                          __FILE__, __LINE__);
            *error = nc_err_new(NC_ERR_OP_FAILED);
            nc_err_set(*error, NC_ERR_PARAM_MSG,
                       "ietf-netconf-server module internal error");
            ret = EXIT_FAILURE;
        }
        free(addr);
        free(port);
        free(sshd_listen);
        sshd_listen = result;
    }

    return (ret);
}

/* Callback for /srv:netconf/srv:ssh/srv:listen changes.
 *
 * It sets SSH server listening for the incoming netconf connections.
 */
int
callback_srv_netconf_srv_ssh_srv_listen(void **UNUSED(data), XMLDIFF_OP op,
                                        xmlNodePtr UNUSED(node),
                                        struct nc_err **error)
{
    int cfgfile, running_cfgfile;
    int pid;
    char pidbuf[10];
    struct stat stbuf;

    if (op == XMLDIFF_REM) {
        /* stop currently running sshd */
        kill_sshd();
        /* and exit */
        return (EXIT_SUCCESS);
    }

    /* 
     * settings were modified or created
     */

    /* prepare sshd_config */
    if ((cfgfile = open(OFC_CONFDIR "/sshd_config", O_RDONLY)) == -1) {
        nc_verb_error("Unable to open SSH server configuration template (%s)",
                      strerror(errno));
        goto err_return;
    }

    if ((running_cfgfile =
         open(OFC_CONFDIR "/sshd_config.running", O_WRONLY | O_TRUNC | O_CREAT,
              S_IRUSR | S_IWUSR)) == -1) {
        nc_verb_error("Unable to prepare SSH server configuration (%s)",
                      strerror(errno));
        goto err_return;
    }

    if (fstat(cfgfile, &stbuf) == -1) {
        nc_verb_error
            ("Unable to get info about SSH server configuration template file (%s)",
             strerror(errno));
        goto err_return;
    }
    if (sendfile(running_cfgfile, cfgfile, 0, stbuf.st_size) == -1) {
        nc_verb_error
            ("Duplicating SSH server configuration template failed (%s)",
             strerror(errno));
        goto err_return;
    }

    /* append listening settings */
    dprintf(running_cfgfile, "\n# NETCONF listening settings\n%s",
            sshd_listen);
    free(sshd_listen);
    sshd_listen = NULL;

    /* close config files */
    close(running_cfgfile);
    close(cfgfile);

    if (sshd_pid != 0) {
        /* tell sshd to reconfigure */
        kill(sshd_pid, SIGHUP);
        /* give him some time to restart */
        usleep(500000);
    } else {
        /* start sshd */
        pid = fork();
        if (pid < 0) {
            nc_verb_error("fork() for SSH server failed (%s)",
                          strerror(errno));
            goto err_return;
        } else if (pid == 0) {
            /* child */
            execl(SSHD_EXEC, SSHD_EXEC, "-D", "-f",
                  OFC_CONFDIR "/sshd_config.running", NULL);

            /* wtf ?!? */
            nc_verb_error("%s; starting \"%s\" failed (%s).", __func__,
                          SSHD_EXEC, strerror(errno));
            exit(1);
        } else {
            nc_verb_verbose("%s: started sshd (PID %d)", __func__, pid);
            /* store sshd's PID */
            sshd_pid = pid;

            /* store it for other modules into environ */
            snprintf(pidbuf, 10, "%u", sshd_pid);
            setenv(SSHDPID_ENV, pidbuf, 1);
        }
    }

    return EXIT_SUCCESS;

err_return:

    *error = nc_err_new(NC_ERR_OP_FAILED);
    nc_err_set(*error, NC_ERR_PARAM_MSG,
               "ietf-netconf-server module internal error - unable to start SSH server.");
    return (EXIT_FAILURE);
}

/* Initialize ietf-netconf-server module
 *
 * It sets all necessary things (starts SSH daemon) according to the default
 * values of the data model. If the startup datastore specify different settings
 * it will be changed automatically by callbacks as the startup will be copied
 * into the running datastore. Therefore we start SSH daemon, but do not return
 * the running content (empty means that we follow the default values).
 */
int
server_transapi_init(xmlDocPtr * UNUSED(running))
{
    xmlDocPtr doc;
    struct nc_err *error = NULL;
    const char *str_err;

    /* set device according to defaults */
    nc_verb_verbose
        ("Setting default configuration for ietf-netconf-server module");

    if (ncds_feature_isenabled("ietf-netconf-server", "ssh") &&
        ncds_feature_isenabled("ietf-netconf-server", "inbound-ssh")) {
        doc =
            xmlReadDoc(BAD_CAST
                       "<netconf xmlns=\"urn:ietf:params:xml:ns:yang:ietf-netconf-server\"><ssh><listen><port>830</port></listen></ssh></netconf>",
                       NULL, NULL, 0);
        if (doc == NULL) {
            nc_verb_error("Unable to parse default configuration.");
            xmlFreeDoc(doc);
            return (EXIT_FAILURE);
        }

        if (callback_srv_netconf_srv_ssh_srv_listen_oneport
            (NULL, XMLDIFF_ADD, doc->children->children->children->children,
             &error) != EXIT_SUCCESS) {
            if (error != NULL) {
                str_err = nc_err_get(error, NC_ERR_PARAM_MSG);
                if (str_err != NULL) {
                    nc_verb_error(str_err);
                }
                nc_err_free(error);
            }
            xmlFreeDoc(doc);
            return (EXIT_FAILURE);
        }
        if (callback_srv_netconf_srv_ssh_srv_listen
            (NULL, XMLDIFF_ADD, doc->children->children->children,
             &error) != EXIT_SUCCESS) {
            if (error != NULL) {
                str_err = nc_err_get(error, NC_ERR_PARAM_MSG);
                if (str_err != NULL) {
                    nc_verb_error(str_err);
                }
                nc_err_free(error);
            }
            xmlFreeDoc(doc);
            return (EXIT_FAILURE);
        }
        xmlFreeDoc(doc);
    }

    return EXIT_SUCCESS;
}

/* Free all resources allocated on plugin runtime and prepare plugin for
 * removal.
 */
void
server_transapi_close(void)
{
    /* kill transport daemons */
    kill_sshd();

    return;
}

/*
 * Structure transapi_config_callbacks provide mapping between callback and path
 * in configuration datastore.
 */
struct transapi_data_callbacks server_clbks = {
    .callbacks_count = 3,
    .data = NULL,
    .callbacks = {
                  {.path = "/srv:netconf/srv:ssh/srv:listen/srv:port",
                   .func = callback_srv_netconf_srv_ssh_srv_listen_oneport},
                  {.path = "/srv:netconf/srv:ssh/srv:listen/srv:interface",
                   .func = callback_srv_netconf_srv_ssh_srv_listen_manyports},
                  {.path = "/srv:netconf/srv:ssh/srv:listen",
                   .func = callback_srv_netconf_srv_ssh_srv_listen},
                  }
};

/*
 * RPC callbacks - in this module we don't have any RPC
 */

/*
 * Structure transapi_rpc_callbacks provide mapping between callbacks and RPC
 * messages.
 */
struct transapi_rpc_callbacks server_rpc_clbks = {
    .callbacks_count = 0,
    .callbacks = {}
};

/* overall structure providing content of this module to the libnetconf */
struct transapi server_transapi = {
    .init = server_transapi_init,
    .close = server_transapi_close,
    .get_state = server_get_state_data,
    .clbks_order = TRANSAPI_CLBCKS_ORDER_DEFAULT,
    .data_clbks = &server_clbks,
    .rpc_clbks = &server_rpc_clbks,
    .ns_mapping = server_namespace_mapping,
    .config_modified = &server_config_modified,
    .erropt = &server_erropt,
    .file_clbks = NULL,
};
