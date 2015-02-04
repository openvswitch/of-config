
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

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <dbus/dbus.h>

#include "comm.h"

comm_t *
comm_init(int __attribute__ ((unused)) crashed)
{
    DBusError dbus_err;
    DBusConnection *ret = NULL;
    DBusMessage *msg = NULL, *reply = NULL;

    /* initiate dbus errors */
    dbus_error_init(&dbus_err);

    /* connect to the D-Bus */
    ret = dbus_bus_get(DBUS_BUS_SYSTEM, &dbus_err);
    if (dbus_error_is_set(&dbus_err) || ret == NULL) {
        nc_verb_error("D-Bus connection error (%s)", dbus_err.message);
        dbus_error_free(&dbus_err);
        return NULL;
    }

    /* try connection using Ping */
    msg = dbus_message_new_method_call(OFC_DBUS_BUSNAME, OFC_DBUS_PATH,
                                       "org.freedesktop.DBus.Peer", "Ping");
    reply = dbus_connection_send_with_reply_and_block(ret, msg,
                                                      OFC_DBUS_TIMEOUT,
                                                      &dbus_err);
    if (dbus_error_is_set(&dbus_err) || reply == NULL) {
        nc_verb_error("Starting communication with server failed (%s)",
                      dbus_err.message);
        dbus_error_free(&dbus_err);
        comm_destroy(ret);
        return NULL;
    }

    return ret;
}

/* Process GetCapabilities request
 *
 * Reply format:
 * DBUS_TYPE_BOOLEAN - ok/error flag
 * DBUS_TYPE_UINT16  - Number of capabilities (following attributes)
 * DBUS_TYPE_STRING  - NETCONF capability supported by server (repeats)
 */
char **
comm_get_srv_cpblts(comm_t *c)
{
    DBusError dbus_err;
    DBusMessage *msg = NULL, *reply = NULL;
    DBusMessageIter args;
    char **cpblts = NULL, *cpblt;
    uint16_t cpblts_count = 0;
    int i;

    /* initiate dbus errors */
    dbus_error_init(&dbus_err);

    msg = dbus_message_new_method_call(OFC_DBUS_BUSNAME, OFC_DBUS_PATH,
                                       OFC_DBUS_IF, OFC_DBUS_GETCAPABILITIES);

    reply = dbus_connection_send_with_reply_and_block(c, msg, OFC_DBUS_TIMEOUT,
                                                      &dbus_err);
    if (dbus_error_is_set(&dbus_err) || reply == NULL) {
        nc_verb_error("%s failed (%s)", OFC_DBUS_GETCAPABILITIES,
                      dbus_err.message);
        dbus_error_free(&dbus_err);
        goto cleanup;
    }

    /* initialize message arguments iterator */
    if (!dbus_message_iter_init(reply, &args)) {
        nc_verb_error("DBus reply has no argument (%s)",
                      OFC_DBUS_GETCAPABILITIES);
        goto cleanup;
    }

    /* get number of capabilities that follow */
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_UINT16) {
        nc_verb_error("Invalid data in the message (%s:%s)",
                      OFC_DBUS_GETCAPABILITIES, "CapabilitiesCount");
        goto cleanup;
    }
    dbus_message_iter_get_basic(&args, &cpblts_count);
    dbus_message_iter_next(&args);

    if (cpblts_count <= 0) {
        nc_verb_error("%s: Unexpected number of capabilities (%d)",
                      OFC_DBUS_GETCAPABILITIES, cpblts_count);
        goto cleanup;
    }

    cpblts = calloc(cpblts_count + 1, sizeof (char *));
    cpblts[cpblts_count] = NULL;        /* list end NULL */
    for (i = 0; i < cpblts_count; i++) {
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) {
            nc_verb_error("Invalid data in the message (%s:%s)",
                          OFC_DBUS_GETCAPABILITIES, "Capabilities");
            for (--i; i >= 0; i--) {
                free(cpblts[i]);
            }
            goto cleanup;
        }
        dbus_message_iter_get_basic(&args, &cpblt);
        dbus_message_iter_next(&args);

        cpblts[i] = strdup(cpblt);
    }

cleanup:
    if (msg) {
        dbus_message_unref(msg);
    }
    if (reply) {
        dbus_message_unref(reply);
    }

    return (cpblts);
}

/* Process SetSessionParams request
 *
 * Request format:
 * DBUS_TYPE_STRING - NETCONF session ID
 * DBUS_TYPE_UINT16 - Agent's PID
 * DBUS_TYPE_STRING - Username for the NETCONF session
 * DBUS_TYPE_UINT16 - Number of capabilities (following attributes)
 * DBUS_TYPE_STRING - NETCONF session capability (repeats)
 */
int
comm_session_info_send(comm_t *c, const char *username, const char *sid,
                       struct nc_cpblts *cpblts)
{
    DBusError dbus_err;
    DBusMessage *msg = NULL, *reply = NULL;
    DBusMessageIter args;
    int i;
    int ret = EXIT_FAILURE;
    uint16_t pid;
    const char *cpblt;
    int ccount;

    /* initiate dbus errors */
    dbus_error_init(&dbus_err);

    /* prepare dbus message */
    msg = dbus_message_new_method_call(OFC_DBUS_BUSNAME, OFC_DBUS_PATH,
                                       OFC_DBUS_IF, OFC_DBUS_SETSESSION);
    dbus_message_iter_init_append(msg, &args);

    /* session id */
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &sid)) {
        nc_verb_error("Unable to set D-Bus method call (%s)",
                      OFC_DBUS_SETSESSION, "SessionID");
        goto cleanup;
    }

    /* agent PID */
    pid = (uint16_t) getpid();
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT16, &pid)) {
        nc_verb_error("Unable to set D-Bus method call (%s)",
                      OFC_DBUS_SETSESSION, "PID");
        goto cleanup;
    }

    /* username */
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &username)) {
        nc_verb_error("Unable to set D-Bus method call (%s)",
                      OFC_DBUS_SETSESSION, "Username");
        goto cleanup;
    }

    /* number of following capabilities */
    ccount = nc_cpblts_count(cpblts);
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT16, &ccount)) {
        nc_verb_error("Unable to set D-Bus method call (%s)",
                      OFC_DBUS_SETSESSION, "CapabilitiesCount");
        goto cleanup;
    }

    /* append all capabilities */
    nc_cpblts_iter_start(cpblts);
    for (i = 0; i < ccount; i++) {
        cpblt = nc_cpblts_iter_next(cpblts);
        if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &cpblt)) {
            nc_verb_error("Unable to set D-Bus method call (%s)",
                          OFC_DBUS_SETSESSION, "CapabilitiesCount");
            goto cleanup;
        }
    }

    reply = dbus_connection_send_with_reply_and_block(c, msg, OFC_DBUS_TIMEOUT,
                                                      &dbus_err);
    if (dbus_error_is_set(&dbus_err) || reply == NULL) {
        nc_verb_error("%s failed (%s)", OFC_DBUS_SETSESSION, dbus_err.message);
        dbus_error_free(&dbus_err);
        goto cleanup;
    }

    /* if the dbus_err not set, we've received positive reply */
    ret = EXIT_SUCCESS;

cleanup:
    if (msg) {
        dbus_message_unref(msg);
    }
    if (reply) {
        dbus_message_unref(reply);
    }

    return ret;
}

/* Process GenericOperation request
 *
 * Request format:
 * DBUS_TYPE_STRING - serialized RPC to perform
 *
 * Reply format:
 * DBUS_TYPE_STRING - serialized RPC-REPLY to return
 */
nc_reply *
comm_operation(comm_t *c, const nc_rpc *rpc)
{
    DBusMessage *msg = NULL, *reply = NULL;
    DBusError dbus_err;
    DBusMessageIter args;
    char *dump = NULL;
    const char *err_message;
    int boolean;
    nc_reply *rpc_reply;
    struct nc_err *err;

    /* initiate dbus errors */
    dbus_error_init(&dbus_err);

    /* prepare dbus message */
    msg = dbus_message_new_method_call(OFC_DBUS_BUSNAME, OFC_DBUS_PATH_OP,
                                       OFC_DBUS_IF, OFC_DBUS_PROCESSOP);
    dbus_message_iter_init_append(msg, &args);

    dump = nc_rpc_dump(rpc);
    dbus_message_iter_init_append(msg, &args);
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &dump)) {
        free(dump);
        nc_verb_error("Unable to set D-Bus method call (%s)",
                      OFC_DBUS_SETSESSION, "RPC");
        err_message = "Unable to set D-Bus method call";
        goto fillerr;
    }
    free(dump);
    dump = NULL;

    reply = dbus_connection_send_with_reply_and_block(c, msg, OFC_DBUS_TIMEOUT,
                                                      &dbus_err);
    if (dbus_error_is_set(&dbus_err) || reply == NULL) {
        nc_verb_error("%s failed (%s)", OFC_DBUS_PROCESSOP, dbus_err.message);
        err_message = dbus_err.message;
        goto fillerr;
    }

    /* initialize message arguments iterator */
    if (!dbus_message_iter_init(reply, &args)) {
        nc_verb_error("DBus reply has no argument (%s)", OFC_DBUS_PROCESSOP);
        goto fillerr;
    }

    /* get number of capabilities that follow */
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) {
        nc_verb_error("Invalid data in the message (%s:%s)",
                      OFC_DBUS_PROCESSOP, "Reply");
        goto fillerr;
    }
    dbus_message_iter_get_basic(&args, &dump);
    dbus_message_iter_next(&args);

    /* build nc_reply from string */
    rpc_reply = nc_reply_build(dump);

    /* cleanup */
    if (msg) {
        dbus_message_unref(msg);
    }
    if (reply) {
        dbus_message_unref(reply);
    }

    return rpc_reply;

fillerr:
    err = nc_err_new(NC_ERR_OP_FAILED);
    nc_err_set(err, NC_ERR_PARAM_MSG, err_message);

    /* cleanup */
    dbus_error_free(&dbus_err);
    if (msg) {
        dbus_message_unref(msg);
    }
    if (reply) {
        dbus_message_unref(reply);
    }

    return (nc_reply_error(err));
}

int
comm_close(comm_t *c)
{
    DBusMessage *msg = NULL;
    DBusError dbus_err;
    int ret = EXIT_SUCCESS;

    /* initiate dbus errors */
    dbus_error_init(&dbus_err);

    msg = dbus_message_new_method_call(OFC_DBUS_BUSNAME, OFC_DBUS_PATH_OP,
                                       OFC_DBUS_IF, OFC_DBUS_CLOSESESSION);

    if (!dbus_connection_send(c, msg, NULL)) {
        nc_verb_error("%s failed (%s)", OFC_DBUS_PROCESSOP, dbus_err.message);
        dbus_error_free(&dbus_err);
        ret = EXIT_FAILURE;
    }

    /* cleanup */
    dbus_connection_flush(c);
    if (msg) {
        dbus_message_unref(msg);
    }

    return ret;
}

nc_reply *
comm_kill_session(comm_t *c, const char *sid)
{
    DBusMessage *msg = NULL, *reply = NULL;
    DBusError dbus_err;
    DBusMessageIter args;
    struct nc_err *err;
    const char *errmsg;
    char *aux_string;
    dbus_bool_t boolean;
    nc_reply *rpc_reply;

    /* initiate dbus errors */
    dbus_error_init(&dbus_err);

    msg = dbus_message_new_method_call(OFC_DBUS_BUSNAME, OFC_DBUS_PATH_OP,
                                       OFC_DBUS_IF, OFC_DBUS_KILLSESSION);
    dbus_message_iter_init_append(msg, &args);
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &sid)) {
        nc_verb_error("Unable to set D-Bus method call (%s)",
                      OFC_DBUS_KILLSESSION, "SessionID");
        goto fillerr;
    }

    reply = dbus_connection_send_with_reply_and_block(c, msg, OFC_DBUS_TIMEOUT,
                                                      &dbus_err);
    if (dbus_error_is_set(&dbus_err) || reply == NULL) {
        nc_verb_error("%s failed (%s)", OFC_DBUS_PROCESSOP, dbus_err.message);
        errmsg = dbus_err.message;
        goto fillerr;
    }

    /* cleanup */
    if (msg) {
        dbus_message_unref(msg);
    }

    return nc_reply_ok();

fillerr:
    err = nc_err_new(NC_ERR_OP_FAILED);
    nc_err_set(err, NC_ERR_PARAM_MSG, errmsg);

    /* cleanup */
    dbus_error_free(&dbus_err);
    if (msg) {
        dbus_message_unref(msg);
    }

    return nc_reply_error(err);
}

void
comm_destroy(comm_t *c)
{
    if (c != NULL) {
        dbus_connection_flush(c);
        dbus_connection_unref(c);
    }
}
