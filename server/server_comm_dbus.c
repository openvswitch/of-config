
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

#include <stdbool.h>
#include <string.h>

#include <dbus/dbus.h>

#include <libnetconf_xml.h>

#include "comm.h"
#include "server_ops.h"

/* default flags for DBus bus */
#define BUS_FLAGS DBUS_NAME_FLAG_DO_NOT_QUEUE

comm_t *
comm_init(int __attribute__ ((unused)) crashed)
{
    int i;
    DBusConnection *ret = NULL;
    DBusError dbus_err;

    /* initiate dbus errors */
    dbus_error_init(&dbus_err);

    /* connect to the D-Bus */
    ret = dbus_bus_get_private(DBUS_BUS_SYSTEM, &dbus_err);
    if (dbus_error_is_set(&dbus_err)) {
        nc_verb_verbose("D-Bus connection error (%s)", dbus_err.message);
        dbus_error_free(&dbus_err);
    }
    if (ret == NULL) {
        nc_verb_verbose("Unable to connect to DBus system bus");
        return ret;
    }

    dbus_connection_set_exit_on_disconnect(ret, FALSE);

    /* request a name on the bus */
    i = dbus_bus_request_name(ret, OFC_DBUS_BUSNAME, BUS_FLAGS, &dbus_err);
    if (dbus_error_is_set(&dbus_err)) {
        nc_verb_verbose("D-Bus name error (%s)", dbus_err.message);
        dbus_error_free(&dbus_err);
        if (ret != NULL) {
            dbus_connection_close(ret);
            dbus_connection_unref(ret);
            ret = NULL;
        }
    }
    if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != i) {
        nc_verb_verbose("Unable to became primary owner of the \"%s\" bus.",
                        OFC_DBUS_BUSNAME);
        if (ret != NULL) {
            dbus_connection_close(ret);
            dbus_connection_unref(ret);
            ret = NULL;
        }
    }

    return ret;
}

/**
 * @brief Send error reply message back to agent
 *
 * @param msg            received message with request
 * @param c              Communication handler
 * @param error_name     error name according to the syntax given in the D-Bus
 *                       specification, if NULL then DBUS_ERROR_FAILED is used
 * @param error_message  the error message string
 *
 * @return               EXIT_SUCCESS, EXIT_FAILURE
 */
static int
_dbus_error_reply(DBusMessage *msg, DBusConnection *c,
                  const char *error_name, const char *error_message)
{
    DBusMessage *reply;
    dbus_uint32_t serial = 0;
    int ret = EXIT_SUCCESS;

    /* create a error reply from the message */
    if (error_name == NULL) {
        error_name = DBUS_ERROR_FAILED;
    }
    reply = dbus_message_new_error(msg, error_name, error_message);

    /* send the reply && flush the connection */
    if (!dbus_connection_send(c, reply, &serial)) {
        nc_verb_verbose("Unable to send D-Bus reply message.");
        ret = EXIT_FAILURE;
    }
    dbus_connection_flush(c);

    /* free the reply */
    dbus_message_unref(reply);

    return ret;
}

/**
 * @brief Send positive (method return message with boolean argument set to
 * true) reply message back to agent
 *
 * @param msg            received message with request
 * @param c              communication handler
 *
 * @return               EXIT_SUCCESS, EXIT_FAILURE
 */
static int
_dbus_positive_reply(DBusMessage *msg, DBusConnection *c)
{
    DBusMessage *reply;
    DBusMessageIter args;
    dbus_uint32_t serial = 0;
    dbus_bool_t stat = true;
    int ret = EXIT_SUCCESS;

    /* create a reply from the message */
    reply = dbus_message_new_method_return(msg);

    /* add the arguments to the reply */
    dbus_message_iter_init_append(reply, &args);
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_BOOLEAN, &stat)) {
        nc_verb_verbose("Unable to set D-Bus \"ok\" reply message");
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    /* send the reply && flush the connection */
    if (!dbus_connection_send(c, reply, &serial)) {
        nc_verb_verbose("Unable to send D-Bus reply message");
        ret = EXIT_FAILURE;
    }
    dbus_connection_flush(c);

cleanup:
    /* free the reply */
    dbus_message_unref(reply);

    return ret;
}

/**
 * @brief Handle standard D-Bus methods on standard interfaces
 * org.freedesktop.DBus.Peer, org.freedesktop.DBus.Introspectable
 * and org.freedesktop.DBus.Properties
 *
 * @param msg            received message with request
 * @param c              communication handler
 * @return               zero when message doesn't contain message call
 *                       of standard method, nonzero if one of standard
 *                       method was received
 */
static int
_dbus_handlestdif(DBusMessage *msg, DBusConnection *c)
{
#define DBUS_STDIF_PEER "org.freedesktop.DBus.Peer"
#define DBUS_STDIF_INTR "org.freedesktop.DBus.Introspectable"
#define DBUS_STDIF_PROP "org.freedesktop.DBus.Properties"

    DBusMessage *reply;
    DBusMessageIter args;
    dbus_uint32_t serial = 0;
    char *machine_uuid;
    char *introspect;
    int ret = 0;

    /* check if message is a method-call for my interface */
    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
        return (EXIT_SUCCESS);
    }

    if (dbus_message_has_interface(msg, DBUS_STDIF_PEER)) {
        /* perform requested operation */
        if (dbus_message_has_member(msg, "Ping")) {
            _dbus_positive_reply(msg, c);
            ret = 1;
        } else if (dbus_message_has_member(msg, "GetMachineId")) {
            /* create a reply from the message */
            reply = dbus_message_new_method_return(msg);

            /* get machine UUID */
            machine_uuid = dbus_get_local_machine_id();

            /* add the arguments to the reply */
            dbus_message_iter_init_append(reply, &args);
            if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING,
                                                &machine_uuid)) {
                nc_verb_verbose("Unable to set D-Bus reply message (%s)",
                                "GetMachineId");
                return -1;
            }

            /* send the reply && flush the connection */
            if (!dbus_connection_send(c, reply, &serial)) {
                nc_verb_verbose("Unable to send D-Bus reply message");
                return -1;
            }
            dbus_connection_flush(c);

            /* free the reply */
            dbus_free(machine_uuid);
            dbus_message_unref(reply);

            ret = 1;
        } else {
            nc_verb_verbose("Call with unknown member (%s) of %s received",
                            dbus_message_get_member(msg), DBUS_STDIF_PEER);
            _dbus_error_reply(msg, c, DBUS_ERROR_UNKNOWN_METHOD,
                              "Unknown method invoked");
            ret = -1;
        }
    } else if (dbus_message_has_interface(msg, DBUS_STDIF_INTR)) {
        /* perform requested operation */
        if (dbus_message_has_member(msg, "Introspect")) {

            /* default value - TODO true structure */
            introspect =
                "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n<node/>";

            /* create a reply from the message */
            reply = dbus_message_new_method_return(msg);

            /* add the arguments to the reply */
            dbus_message_iter_init_append(reply, &args);
            if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING,
                                                &introspect)) {
                nc_verb_verbose("Unable to set D-Bus reply message (%s)",
                                "Introspect");
                return -1;
            }

            /* send the reply && flush the connection */
            nc_verb_verbose("sending introspect information (%s)", introspect);
            if (!dbus_connection_send(c, reply, &serial)) {
                nc_verb_verbose("Unable to send D-Bus reply message");
                return -1;
            }
            dbus_connection_flush(c);

            /* free the reply */
            dbus_message_unref(reply);

            ret = 1;
        } else {
            nc_verb_verbose("Call with unknown member (%s) of %s received",
                            dbus_message_get_member(msg), DBUS_STDIF_INTR);
            _dbus_error_reply(msg, c, DBUS_ERROR_UNKNOWN_METHOD,
                              "Unknown method invoked");
            ret = -1;
        }
    } else if (dbus_message_has_interface(msg, DBUS_STDIF_PROP)) {
        nc_verb_verbose("Call for not used interface %s with method %s",
                        dbus_message_get_interface(msg),
                        dbus_message_get_member(msg));
        _dbus_error_reply(msg, c, DBUS_ERROR_UNKNOWN_METHOD,
                          "Not used interface " DBUS_STDIF_PROP);
        ret = -1;
    }

    return ret;
}

/* Process GetCapabilities request
 *
 * Reply format:
 * DBUS_TYPE_UINT16  - Number of capabilities (following attributes)
 * DBUS_TYPE_STRING  - NETCONF capability supported by server (repeats)
 */
static void
get_capabilities(DBusConnection *c, DBusMessage *msg)
{
    DBusMessage *reply = NULL;
    DBusMessageIter args;
    const char *cpblt;
    int cpblts_cnt;
    struct nc_cpblts *cpblts;

    /* create reply message */
    reply = dbus_message_new_method_return(msg);

    /* add the arguments to the reply */
    dbus_message_iter_init_append(reply, &args);

    cpblts = nc_session_get_cpblts_default();
    cpblts_cnt = nc_cpblts_count(cpblts);
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT16, &cpblts_cnt)) {
        nc_verb_verbose("Unable to set D-Bus reply message (%s:%s)",
                        OFC_DBUS_GETCAPABILITIES, "CapabilitiesCount");
        _dbus_error_reply(msg, c, DBUS_ERROR_FAILED,
                          "Unable to create reply message content");
        goto cleanup;
    }

    nc_cpblts_iter_start(cpblts);
    while ((cpblt = nc_cpblts_iter_next(cpblts)) != NULL) {
        if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &cpblt)) {
            nc_verb_verbose("Unable to set D-Bus reply message (%s:%s)",
                            OFC_DBUS_GETCAPABILITIES, "Capabilities");
            _dbus_error_reply(msg, c, DBUS_ERROR_FAILED,
                              "Unable to create reply message content");
            goto cleanup;
        }
    }

    nc_cpblts_free(cpblts);

    nc_verb_verbose("Sending capabilities to agent.");
    /* send the reply && flush the connection */
    if (!dbus_connection_send(c, reply, NULL)) {
        nc_verb_verbose("Unable to send D-Bus reply message");
    }
    dbus_connection_flush(c);

cleanup:
    /* free the reply */
    dbus_message_unref(reply);
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
static void
set_session(DBusConnection *c, DBusMessage *msg)
{
    DBusMessageIter args;
    char *aux_string = NULL, *session_id = NULL, *username = NULL;
    const char *dbus_id;
    const char *errattr, *errmsg;
    struct nc_cpblts *cpblts = NULL;
    int i = 0, cpblts_count = 0;
    uint16_t pid;

    if (!dbus_message_iter_init(msg, &args)) {
        nc_verb_error("DBus message has no arguments (%s)",
                      OFC_DBUS_SETSESSION);
        _dbus_error_reply(msg, c, DBUS_ERROR_FAILED,
                          "Invalid format (no attributes).");
        return;
    }

    /* dbus session-id */
    dbus_id = dbus_message_get_sender(msg);

    /* parse message */
    /* session ID */
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) {
        errattr = "SessionID";
        errmsg = "SessionID expected as a string value";
        goto paramerr;
    }
    dbus_message_iter_get_basic(&args, &session_id);
    dbus_message_iter_next(&args);

    /* PID */
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_UINT16) {
        errattr = "PID";
        errmsg = "PID expected as a uint16 value";
        goto paramerr;
    }
    dbus_message_iter_get_basic(&args, &pid);
    dbus_message_iter_next(&args);

    /* username */
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) {
        errattr = "Username";
        errmsg = "Username expected as a string value";
        goto paramerr;
    }
    dbus_message_iter_get_basic(&args, &username);
    dbus_message_iter_next(&args);

    /* number of capabilities */
    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_UINT16) {
        errattr = "CapabilitiesCount";
        errmsg = "CapabilitiesCount expected as a uint16 value";
        goto paramerr;
    }
    dbus_message_iter_get_basic(&args, &cpblts_count);
    dbus_message_iter_next(&args);

    /* capabilities strings */
    cpblts = nc_cpblts_new(NULL);
    for (i = 0; i < cpblts_count; i++) {
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) {
            errattr = "Capabilities";
            errmsg = "Capability expected as a string value";
            nc_cpblts_free(cpblts);
            goto paramerr;
        } else {
            dbus_message_iter_get_basic(&args, &aux_string);
            nc_cpblts_add(cpblts, aux_string);
        }
        dbus_message_iter_next(&args);
    }

    /* add session to the list */
    srv_agent_new(session_id, username, cpblts, dbus_id, pid);

    /* clean */
    nc_cpblts_free(cpblts);

    /* positive result */
    nc_verb_verbose("New agent ID set to %s.", dbus_id);
    _dbus_positive_reply(msg, c);
    return;

paramerr:
    /* negative result */
    nc_verb_verbose("Invalid data in the message (%s:%s)", OFC_DBUS_SETSESSION,
                    errattr);
    _dbus_error_reply(msg, c, DBUS_ERROR_FAILED, errmsg);
}

/* Process CloseSession request
 *
 * Request format:
 * no attribute
 *
 * Note: no reply
 */
static void
close_session(DBusMessage *msg)
{
    struct agent_info *sender_session;
    const char *id;

    /* 
     * get session information about sender which will be removed from active
     * sessions
     */
    id = dbus_message_get_sender(msg);
    sender_session = srv_get_agent_by_agentid(id);
    if (sender_session == NULL) {
        nc_verb_warning("Unable to close session (not found)");
        return;
    }

    srv_agent_stop(sender_session);

    nc_verb_verbose("Agent %s removed.", id);
}

/* Process KillSession request
 *
 * Request format:
 * DBUS_TYPE_STRING - session ID to kill
 */
static void
kill_session(DBusConnection *c, DBusMessage *msg)
{
    const char *sid = NULL, *id2kill = NULL;
    DBusMessageIter args;
    struct agent_info *session;
    struct agent_info *sender;

    if (!dbus_message_iter_init(msg, &args)) {
        nc_verb_error("DBus message has no arguments (%s)",
                      OFC_DBUS_KILLSESSION);
        _dbus_error_reply(msg, c, DBUS_ERROR_FAILED,
                          "Invalid format (no attributes).");
        return;
    }

    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) {
        nc_verb_verbose("Invalid data in the message (%s:%s)",
                        OFC_DBUS_KILLSESSION, "SessionID");
        _dbus_error_reply(msg, c, DBUS_ERROR_FAILED,
                          "SessionID expected as a string value");
        return;
    }
    dbus_message_iter_get_basic(&args, &id2kill);
    dbus_message_iter_next(&args);

    if ((session = srv_get_agent_by_ncsid(id2kill)) == NULL) {
        nc_verb_error("Unable to kill session %s (not found)", id2kill);
        _dbus_error_reply(msg, c, DBUS_ERROR_FAILED,
                          "Session to kill does not exists");
        return;
    }

    /* check if the request does not relate to the current session */
    sid = dbus_message_get_sender(msg);
    if ((sender = srv_get_agent_by_agentid(sid)) == NULL) {
        if (strcmp(nc_session_get_id(sender->session), sid) == 0) {
            nc_verb_verbose("Killing own session requested");
            _dbus_error_reply(msg, c, DBUS_ERROR_FAILED,
                              "illing own session requested");
            return;
        }
    }

    srv_agent_kill(session);

    /* positive result */
    nc_verb_verbose("Session %s killed (requested by %s).", id2kill, sid);
    _dbus_positive_reply(msg, c);
}

/* Process GenericOperation request
 *
 * Request format:
 * DBUS_TYPE_STRING - serialized RPC to perform
 *
 * Reply format:
 * DBUS_TYPE_STRING - serialized RPC-REPLY to return
 */
static void
process_operation(DBusConnection *c, DBusMessage *msg)
{
    char *rpcdump, *replydump;
    DBusMessageIter args;
    DBusMessage *dbus_reply;
    struct agent_info *session;
    nc_rpc *rpc = NULL;
    nc_reply *reply;

    session = srv_get_agent_by_agentid(dbus_message_get_sender(msg));
    if (session == NULL) {
        /* in case session was closed but agent is still active */
        nc_verb_error("Received message from invalid session.");
        _dbus_error_reply(msg, c, DBUS_ERROR_FAILED,
                          "Your session is no longer valid!");
        return;
    }

    if (!dbus_message_iter_init(msg, &args)) {
        nc_verb_error("DBus message has no arguments (%s)",
                      OFC_DBUS_PROCESSOP);
        _dbus_error_reply(msg, c, DBUS_ERROR_FAILED,
                          "Invalid format (no attributes).");
        return;
    }

    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) {
        nc_verb_verbose("Invalid data in the message (%s:%s)",
                        OFC_DBUS_PROCESSOP, "RPC");
        _dbus_error_reply(msg, c, DBUS_ERROR_FAILED,
                          "RPC expected as a string value");
        return;
    }
    dbus_message_iter_get_basic(&args, &rpcdump);
    dbus_message_iter_next(&args);
    rpc = nc_rpc_build(rpcdump, session->session);
    nc_verb_verbose("Processing request %s", rpcdump);

    reply = srv_process_rpc(session->session, rpc);

    replydump = nc_reply_dump(reply);
    nc_reply_free(reply);
    nc_rpc_free(rpc);

    if (replydump == NULL) {
        nc_verb_verbose("Invalid rpc-reply to send via D-Bus");
        _dbus_error_reply(msg, c, DBUS_ERROR_FAILED,
                          "Invalid rpc-reply to send via D-Bus");
        return;
    }

    dbus_reply = dbus_message_new_method_return(msg);

    dbus_message_iter_init_append(dbus_reply, &args);
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &replydump)) {
        nc_verb_verbose("Unable to set D-Bus rpc-reply message");
        _dbus_error_reply(msg, c, DBUS_ERROR_FAILED,
                          "Unable to create reply message content");
        goto cleanup;
    }

    dbus_connection_send(c, dbus_reply, NULL);

cleanup:
    dbus_connection_flush(c);
    free(replydump);
    dbus_message_unref(dbus_reply);
}

/* Main communication loop */
int
comm_loop(comm_t *c, int timeout)
{
    DBusMessage *msg;

    /* blocking read of the next available message */
    dbus_connection_read_write(c, timeout);

    while ((msg = dbus_connection_pop_message(c)) != NULL) {
        if (_dbus_handlestdif(msg, c) != 0) {
            /* free the message */
            dbus_message_unref(msg);

            /* go for next message */
            continue;
        }

        nc_verb_verbose("Some message received");

        /* check if message is a method-call */
        if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL) {
            /* process specific members in interface NTPR_DBUS_SRV_IF */
            if (dbus_message_is_method_call(msg, OFC_DBUS_IF,
                                            OFC_DBUS_GETCAPABILITIES) ==
                TRUE) {
                /* GetCapabilities request */
                get_capabilities(c, msg);
            } else if (dbus_message_is_method_call(msg, OFC_DBUS_IF,
                                                   OFC_DBUS_SETSESSION) ==
                       TRUE) {
                /* SetSessionParams request */
                set_session(c, msg);
            } else if (dbus_message_is_method_call(msg, OFC_DBUS_IF,
                                                   OFC_DBUS_CLOSESESSION) ==
                       TRUE) {
                /* CloseSession request */
                close_session(msg);
            } else if (dbus_message_is_method_call(msg, OFC_DBUS_IF,
                                                   OFC_DBUS_KILLSESSION) ==
                       TRUE) {
                /* KillSession request */
                kill_session(c, msg);
            } else if (dbus_message_is_method_call(msg, OFC_DBUS_IF,
                                                   OFC_DBUS_PROCESSOP) ==
                       TRUE) {
                /* All other requests */
                process_operation(c, msg);
            } else {
                nc_verb_warning
                    ("Unsupported DBus request received (interface %s, member %s)",
                     dbus_message_get_destination(msg),
                     dbus_message_get_member(msg));
            }
        } else {
            nc_verb_warning("Unsupported DBus message type received.");
        }

        /* free the message */
        dbus_message_unref(msg);
    }

    return (EXIT_SUCCESS);
}

void
comm_destroy(comm_t *c)
{
    if (c != NULL) {
        dbus_connection_flush(c);
        dbus_connection_close(c);
        dbus_connection_unref(c);
    }
}
