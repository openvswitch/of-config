
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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <libnetconf.h>

#include "data.h"
#include "edit-config.c"

#ifdef __GNUC__
#   define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#   define UNUSED(x) UNUSED_ ## x
#endif

#define XML_READ_OPT XML_PARSE_NOBLANKS|XML_PARSE_NSCLEAN

/* daemonize flag from server.c */
extern int ofc_daemonize;

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
    if (ofc_daemonize) {
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
                 NC_DATASTORE target, const char *config,
                 NC_EDIT_DEFOP_TYPE defop,
                 NC_EDIT_ERROPT_TYPE UNUSED(errop),
                 struct nc_err **error)
{
    int ret = EXIT_FAILURE, running = 0;
    char *aux;
    xmlDocPtr cfgds = NULL, cfg = NULL;
    xmlNodePtr rootcfg;

    if (defop == NC_EDIT_DEFOP_NOTSET) {
        defop = NC_EDIT_DEFOP_MERGE;
    }

    cfg = xmlReadMemory(config, strlen(config), NULL, NULL, XML_READ_OPT);
    rootcfg = xmlDocGetRootElement(cfg);
    if (!cfg || (rootcfg && !xmlStrEqual(rootcfg->name,
    BAD_CAST "capable-switch"))) {
        nc_verb_error("Invalid <edit-config> configuration data.");
        *error = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*error, NC_ERR_PARAM_INFO_BADELEM, "config");
        return EXIT_FAILURE;
    }

    switch (target) {
    case NC_DATASTORE_RUNNING:
        aux = ofc_get_config_data();
        if (!aux) {
            *error = nc_err_new(NC_ERR_OP_FAILED);
            goto error_cleanup;
        }
        cfgds = xmlReadMemory(aux, strlen(aux), NULL, NULL, XML_READ_OPT);
        running = 1;
        break;
    case NC_DATASTORE_STARTUP:
        cfgds = gds_startup;
        break;
    case NC_DATASTORE_CANDIDATE:
        cfgds = gds_cand;
        break;
    default:
        nc_verb_error("Invalid <edit-config> target.");
        *error = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*error, NC_ERR_PARAM_INFO_BADELEM, "target");
        goto error_cleanup;
    }

    /* check operations */
    if (check_edit_ops(NC_EDIT_OP_DELETE, defop, cfgds, cfg, error) != EXIT_SUCCESS) {
        goto error_cleanup;
    }
    if (check_edit_ops(NC_EDIT_OP_CREATE, defop, cfgds, cfg, error) != EXIT_SUCCESS) {
        goto error_cleanup;
    }

    if (compact_edit_operations(cfg, defop) != EXIT_SUCCESS) {
        nc_verb_error("Compacting edit-config operations failed.");
        if (error != NULL) {
            *error = nc_err_new(NC_ERR_OP_FAILED);
        }
        goto error_cleanup;
    }

    /* perform operations */
    if (edit_operations(cfgds, cfg, defop, running, error) != EXIT_SUCCESS) {
        goto error_cleanup;
    }

    /* with defaults capability */
    if (ncdflt_get_basic_mode() == NCWD_MODE_TRIM) {
        /* server work in trim basic mode and therefore all default
         * values must be removed from the datastore.
         */
        /* TODO */
    }

    if (target == NC_DATASTORE_RUNNING) {
        ret = txn_commit(error);
    }
    xmlFreeDoc(cfg);

    return ret;

error_cleanup:

    if (target == NC_DATASTORE_RUNNING) {
        txn_abort();
    }
    xmlFreeDoc(cfg);

    return ret;













#if 0


    if (target == NC_DATASTORE_RUNNING) {
        txn_init();
        switch (op) {
        case NC_EDIT_OP_REPLACE:
            txn_del_all();
            ofc_set_switchid(NULL);
            ret = edit_ovs_root(rootcfg, NC_EDIT_DEFOP_REPLACE, error);
            break;
        case NC_EDIT_OP_CREATE:
            /* rootds must not exist, detect it via switchid */
            if (ofc_get_switchid()) {
                *error = nc_err_new(NC_ERR_DATA_EXISTS);
                goto cleanup;
            } else {
                /* defop is replace, but target data do not exist so they will
                 * be created
                 */
                ret = edit_ovs_root(rootcfg, NC_EDIT_DEFOP_REPLACE, error);
            }
            break;
        case NC_EDIT_OP_DELETE:
            /* rootds must exist, detect it via switchid */
            if (!ofc_get_switchid()) {
                *error = nc_err_new(NC_ERR_DATA_MISSING);
                goto cleanup;
            }
            /* no break */
        case NC_EDIT_OP_REMOVE:
            txn_del_all();
            ofc_set_switchid(NULL);
            break;
        default:
            /* MERGE, NONE */
            if (edit_ovs_root(rootcfg, defop, error)) {
                goto cleanup;
            }
        }
        ret = txn_commit(error);
    } else { /* STARTUP or CANDIDATE */
        if (edit_xml_root(rootcfg, xmlDocGetRootElement(cfgds), defop,
                        error)) {
            goto cleanup;
        }
    }


    if (target == NC_DATASTORE_RUNNING) {
        ret = txn_commit(error);
    }

cleanup:
    if (target == NC_DATASTORE_RUNNING) {
        txn_abort();
    }
    xmlFreeDoc(cfg);

    return ret;
#endif
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
    xmlNodePtr root;

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
        src_doc = xmlCopyDoc(gds_startup, 1);
        break;
    case NC_DATASTORE_CANDIDATE:
        src_doc = xmlCopyDoc(gds_cand, 1);
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

        s = ofcds_getconfig(NULL, NC_DATASTORE_RUNNING, error);
        if (!s) {
            nc_verb_error("copy-config: unable to get running source data");
            goto cleanup;
        }
        dst_doc = xmlReadMemory(s, strlen(s), NULL, NULL, XML_READ_OPT);
        free(s);

        root = xmlDocGetRootElement(src_doc);
        if (!dst_doc) {
            /* create envelope */
            dst_doc = xmlNewDoc(BAD_CAST "1.0");
        }

        txn_init();
        if (edit_replace(dst_doc, root, 1, error)) {
            txn_abort();
        } else {
            ret = txn_commit(error);
        }
        xmlFreeDoc(dst_doc);
        goto cleanup;
        break;
    case NC_DATASTORE_STARTUP:
    case NC_DATASTORE_CANDIDATE:
        /* create copy */
        if (src_doc) {
            dst_doc = src_doc;
            src_doc = NULL;
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
    xmlFreeDoc(src_doc);

    return ret;
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

