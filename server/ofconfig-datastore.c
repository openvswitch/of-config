
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

/* TODO:
 * - NACM
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <libnetconf.h>

#include "data.h"

#ifdef __GNUC__
#   define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#   define UNUSED(x) UNUSED_ ## x
#endif

#define XML_READ_OPT XML_PARSE_NOBLANKS|XML_PARSE_NSCLEAN

/* daemonize flag from server.c */
extern int daemonize;

/* local locks info */
struct {
    int running;
    char *running_sid;
    int startup;
    char *startup_sid;
    int cand;
    char *cand_sid;
} locks = {0, NULL, 0, NULL, 0, NULL};

/* localy maintained datastores */
xmlDocPtr gds_startup = NULL;
xmlDocPtr gds_cand = NULL;

int ofcds_deleteconfig(void *UNUSED(data), NC_DATASTORE UNUSED(target),
                       struct nc_err **UNUSED(error));

static int
ofc_apply(xmlDocPtr doc, struct nc_err **e)
{
    xmlNodePtr root, l1, l2;

    /* start transaction preparation */
    txn_init();

    /* first, remove current content */
    txn_del_all();
    ofc_set_switchid(NULL);

    if (!doc || !(root = xmlDocGetRootElement(doc))) {
        /* no data -> content deleted, so we're done */
        return txn_commit(e);
    }

    /* TODO: apply to OVSDB */
    for (l1 = root->children; l1; l1 = l1->next) {
        if (l1->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(l1->name, BAD_CAST "id")) {
            /* TODO: check that there is id, since it is mandatory */
            ofc_set_switchid(l1);
        } else if (xmlStrEqual(l1->name, BAD_CAST "resources")) {
            for (l2 = l1->children; l2; l2 = l2->next) {
                if (l2->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (xmlStrEqual(l2->name, BAD_CAST "port")) {
                    txn_set_port(l2, e);
                }
                /* TODO:
                 * queue
                 * owned-certificate
                 * external-certificate
                 * flow-table
                 */
            }
        } else if (xmlStrEqual(l1->name, BAD_CAST "logical-switches")) {
            for (l2 = l1->children; l2; l2 = l2->next) {
                if (l2->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (xmlStrEqual(l2->name, BAD_CAST "switch")) {
                    txn_set_bridge(l2, e);
                }
            }
        }

        /* TODO: handle the rest of nodes */
    }

    return txn_commit(e);
}

int
ofcds_init(void *UNUSED(data))
{
    /* TODO replace OFC_OVS_DBPATH with some parameter */
    if (ofc_init(OFC_OVS_DBPATH) == false) {
        return EXIT_FAILURE;
    }

    /* hack - OVS calls openlog() and rewrites the syslog settings of the
     * ofc-server. So we have to rewrite syslog settings back by another
     * openlog() call
     */
    if (daemonize) {
        openlog("ofc-server", LOG_PID, LOG_DAEMON);
    } else {
        openlog("ofc-server", LOG_PID | LOG_PERROR, LOG_DAEMON);
    }

    /* get startup data */
    gds_startup = xmlReadFile(OFC_DATADIR"/startup.xml", NULL, XML_READ_OPT);
    /* check that there are some data, if not, continue with empty startup */
    if (!xmlDocGetRootElement(gds_startup)) {
        xmlFreeDoc(gds_startup);
        gds_startup = NULL;
    }

    nc_verb_verbose("OF-CONFIG datastore initialized.");
    return EXIT_SUCCESS;
}

void
ofcds_free(void *UNUSED(data))
{
    ofc_destroy();

    /* dump startup to persistent storage */
    if (gds_startup) {
        xmlSaveFormatFile(OFC_DATADIR"/startup.xml", gds_startup, 1);
    }

    /* cleanup locks */
    free(locks.running_sid);
    free(locks.startup_sid);
    free(locks.cand_sid);

    return;
}

int
ofcds_changed(void *UNUSED(data))
{
    /* always false
     * the function is not needed now, we can implement it later for internal
     * purposes, but for now the datastore content is synced continuously
     */
    return (0);
}

int
ofcds_rollback(void *UNUSED(data))
{
    return EXIT_SUCCESS;
}

int
ofcds_lock(void *UNUSED(data), NC_DATASTORE target, const char *session_id,
           struct nc_err **error)
{
    int *locked;
    char **sid;

    switch (target) {
    case NC_DATASTORE_RUNNING:
        locked = &(locks.running);
        sid = &(locks.running_sid);
        break;
    case NC_DATASTORE_STARTUP:
        locked = &(locks.startup);
        sid = &(locks.startup_sid);
        break;
    case NC_DATASTORE_CANDIDATE:
        locked = &(locks.cand);
        sid = &(locks.cand_sid);
        break;
    default:
        /* handled by libnetconf */
        return EXIT_FAILURE;
    }

    if (*locked) {
        /* datastore is already locked */
        *error = nc_err_new(NC_ERR_LOCK_DENIED);
        nc_err_set(*error, NC_ERR_PARAM_INFO_SID, *sid);
        return EXIT_FAILURE;
    } else {
        /* remember the lock */
        *locked = 1;
        *sid = strdup(session_id);
        nc_verb_verbose("OFC datastore %d locked by %s.", target,
                        session_id);
    }

    return EXIT_SUCCESS;
}

int
ofcds_unlock(void *UNUSED(data), NC_DATASTORE target, const char *session_id,
             struct nc_err **error)
{
    int *locked;
    char **sid;

    switch (target) {
    case NC_DATASTORE_RUNNING:
        locked = &(locks.running);
        sid = &(locks.running_sid);
        break;
    case NC_DATASTORE_STARTUP:
        locked = &(locks.startup);
        sid = &(locks.startup_sid);
        break;
    case NC_DATASTORE_CANDIDATE:
        locked = &(locks.cand);
        sid = &(locks.cand_sid);
        break;
    default:
        /* handled by libnetconf */
        return EXIT_FAILURE;
    }

    if (*locked) {
        if (strcmp(*sid, session_id) == 0) {
            /* correct request, unlock */
            *locked = 0;
            free(*sid);
            *sid = NULL;
            nc_verb_verbose("OFC datastore %d unlocked by %s.", target,
                            session_id);
        } else {
            /* locked by another session */
            *error = nc_err_new(NC_ERR_LOCK_DENIED);
            nc_err_set(*error, NC_ERR_PARAM_INFO_SID, *sid);
            nc_err_set(*error, NC_ERR_PARAM_MSG,
                       "Target datastore is locked by another session.");
            return EXIT_FAILURE;
        }
    } else {
        /* not locked */
        *error = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(*error, NC_ERR_PARAM_MSG, "Target datastore is not locked.");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

char *
ofcds_getconfig(void *UNUSED(data), NC_DATASTORE target, struct nc_err **error)
{
    xmlChar *config_data = NULL;

    switch(target) {
    case NC_DATASTORE_RUNNING:
        /* If there is no id of the capable-switch (no configuration data were
         * provided), continue as there is no OVSDB
         */
        return ofc_get_config_data();
    case NC_DATASTORE_STARTUP:
        if (!gds_startup) {
            config_data = xmlStrdup(BAD_CAST "");
        } else {
            xmlDocDumpMemory(gds_startup, &config_data, NULL);
        }
        break;
    case NC_DATASTORE_CANDIDATE:
        if (!gds_cand) {
            config_data = xmlStrdup(BAD_CAST "");
        } else {
            xmlDocDumpMemory(gds_cand, &config_data, NULL);
        }
        break;
    default:
        nc_verb_error("Invalid <get-config> source.");
        *error = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*error, NC_ERR_PARAM_INFO_BADELEM, "source");
    }

    return (char*) config_data;
}

int
ofcds_copyconfig(void *UNUSED(data), NC_DATASTORE target,
                 NC_DATASTORE source, char *config,
                 struct nc_err **error)
{
    int ret = EXIT_FAILURE;
    char *s;
    xmlDocPtr src_doc = NULL;
    xmlDocPtr dst_doc = NULL;

    nc_verb_verbose("OFC COPY-CONFIG (from %d to %d)", source, target);

    switch(source) {
    case NC_DATASTORE_RUNNING:
        s = ofcds_getconfig(NULL, NC_DATASTORE_RUNNING, error);
        if (!s) {
            nc_verb_error("copy-config: unable to get running source repository");
            return EXIT_FAILURE;
        }
        src_doc = xmlReadMemory(s, strlen(s), NULL, NULL, XML_READ_OPT);
        free(s);
        if (!src_doc) {
            nc_verb_error("copy-config: invalid running source data");
            *error = nc_err_new(NC_ERR_OP_FAILED);
            nc_err_set(*error, NC_ERR_PARAM_INFO_BADELEM,
                       "invalid running source data");
            return EXIT_FAILURE;
        }
        break;
    case NC_DATASTORE_STARTUP:
        src_doc = gds_startup;
        break;
    case NC_DATASTORE_CANDIDATE:
        src_doc = gds_cand;
        break;
    case NC_DATASTORE_CONFIG:
        if (config && strlen(config) > 0) {
            src_doc = xmlReadMemory(config, strlen(config), NULL, NULL,
                                    XML_READ_OPT);
        }
        if (!config || (strlen(config) > 0 && !src_doc)) {
            nc_verb_error("Invalid source configuration data.");
            *error = nc_err_new(NC_ERR_BAD_ELEM);
            nc_err_set(*error, NC_ERR_PARAM_INFO_BADELEM, "config");
            return EXIT_FAILURE;
        }

        break;
    default:
        nc_verb_error("Invalid <get-config> source.");
        *error = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*error, NC_ERR_PARAM_INFO_BADELEM, "source");
        return EXIT_FAILURE;
    }

    switch(target) {
    case NC_DATASTORE_RUNNING:
        /* apply source to OVSDB */
        if (ofc_apply(src_doc, error) != 0) {
            goto cleanup;
        }
        break;
    case NC_DATASTORE_STARTUP:
    case NC_DATASTORE_CANDIDATE:
        /* create copy */
        if (src_doc) {
            dst_doc = xmlCopyDoc(src_doc, 1);
            if (!dst_doc) {
                nc_verb_error("copy-config: making source copy failed");
                *error = nc_err_new(NC_ERR_OP_FAILED);
                nc_err_set(*error, NC_ERR_PARAM_MSG,
                           "making source copy failed");
                goto cleanup;
            }
        }

        /* store the copy */
        if (target == NC_DATASTORE_STARTUP) {
            xmlFreeDoc(gds_startup);
            gds_startup = dst_doc;
        } else { /* NC_DATASTORE_CANDIDATE */
            xmlFreeDoc(gds_cand);
            gds_cand = dst_doc;
        }

        break;
    default:
        nc_verb_error("Invalid <get-config> source.");
        *error = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*error, NC_ERR_PARAM_INFO_BADELEM, "source");
        goto cleanup;
    }

    ret = EXIT_SUCCESS;

cleanup:
    if (source == NC_DATASTORE_RUNNING ||
            source == NC_DATASTORE_CONFIG) {
        xmlFreeDoc(src_doc);
    }

    return ret;
}

int
ofcds_deleteconfig(void *UNUSED(data), NC_DATASTORE target,
                   struct nc_err **error)
{
    switch(target) {
    case NC_DATASTORE_RUNNING:
        txn_init();
        txn_del_all();
        ofc_set_switchid(NULL);
        return txn_commit(error);
    case NC_DATASTORE_STARTUP:
        xmlFreeDoc(gds_startup);
        gds_startup = NULL;
        break;
    case NC_DATASTORE_CANDIDATE:
        xmlFreeDoc(gds_cand);
        gds_cand = NULL;
        break;
    default:
        nc_verb_error("Invalid <delete-config> target.");
        *error = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*error, NC_ERR_PARAM_INFO_BADELEM, "target");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int
ofcds_editconfig(void *UNUSED(data), const nc_rpc * UNUSED(rpc),
                 NC_DATASTORE UNUSED(target), const char *UNUSED(config),
                 NC_EDIT_DEFOP_TYPE UNUSED(defop),
                 NC_EDIT_ERROPT_TYPE UNUSED(errop),
                 struct nc_err **UNUSED(error))
{
    return EXIT_SUCCESS;
}

struct ncds_custom_funcs ofcds_funcs = {
    .init = ofcds_init,
    .free = ofcds_free,
    .was_changed = ofcds_changed,
    .rollback = ofcds_rollback,
    .lock = ofcds_lock,
    .unlock = ofcds_unlock,
    .is_locked = NULL,
    .getconfig = ofcds_getconfig,
    .copyconfig = ofcds_copyconfig,
    .deleteconfig = ofcds_deleteconfig,
    .editconfig = ofcds_editconfig,
};
