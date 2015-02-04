
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
#include <stdint.h>
#include <dynamic-string.h>
#include <ovsdb-idl-provider.h>
#include <vswitch-idl.h>
#include <dpif.h>
#include <vconn.h>
#include <socket-util.h>
#include <ofp-util.h>
#include <ofp-msgs.h>
#include <poll-loop.h>

#include <libnetconf/netconf.h>

#include "ovs-data.h"

ofconf_t *ofc_global_context = (ofconf_t *) NULL;

static const char *
print_uuid_ro(const struct uuid *uuid)
{
    static char str[38];

    snprintf(str, 37, UUID_FMT, UUID_ARGS(uuid));
    str[37] = 0;
    return str;
}
static char *
print_uuid(const struct uuid *uuid)
{
    return strdup(print_uuid_ro(uuid));
}

/*
 * If key is in the string map s, append the it's value into string,
 * otherwise don't append anything.
 *
 * s    string map with data
 * key  value of key in string map to find
 * elem name of element to append into XML
 * string   dynamic string containing XML
 */
static void
find_and_append_smap_val(const struct smap *s, const char *key,
                         const char *elem, struct ds *string)
{

    const char *value = smap_get(s, key);
    if (value != NULL) {
        ds_put_format(string, "<%s>%s</%s>", elem, value, elem);
    }
}


static char *
get_flow_tables_state(void)
{
    const struct ovsrec_flow_table *row, *next;
    struct ds string;

    ds_init(&string);
    OVSREC_FLOW_TABLE_FOR_EACH_SAFE(row, next, ofc_global_context->idl) {
        char *uuid = print_uuid(&row->header_.uuid);

        ds_put_format(&string, "<flow-table><resource-id>%s</resource-id>"
                      "<max-entries>%"PRId64"</max-entries></flow-table>",
                      uuid, (row->n_flow_limit > 0 ? row->flow_limit[0] : 0));
        free(uuid);
    }
    return ds_steal_cstr(&string);
}

static char *
get_flow_tables_config(void)
{
    /* TODO flow-table "<flow-table>"
     * "<table-id>%s</table-id>"
     "</flow-table>" */

    const struct ovsrec_flow_table *row, *next;
    struct ds string;

    ds_init(&string);
    OVSREC_FLOW_TABLE_FOR_EACH_SAFE(row, next, ofc_global_context->idl) {
        ds_put_format(&string, "<flow-table><resource-id>%s</resource-id>",
                      print_uuid_ro(&row->header_.uuid));
        ds_put_format(&string, "<name>%s</name", row->name);
        ds_put_format(&string, "</flow-table>");
    }
    return ds_steal_cstr(&string);
}

static char *
get_queues_config(void)
{
    /* TODO
       "<queue><id>%s</id>"
       "<port>%s</port>"
       "<properties>"
       "<experimenter-id>%s</experimenter-id>"
       "<experimenter-data>%s</experimenter-data>"
       "</properties></queue>"
       */
    const struct ovsrec_queue *row, *next;
    struct ds string;

    ds_init(&string);
    OVSREC_QUEUE_FOR_EACH_SAFE(row, next, ofc_global_context->idl) {
        char *uuid = print_uuid(&row->header_.uuid);

        ds_put_format(&string,
                      "<queue><resource-id>%s</resource-id><properties>",
                      uuid);
        find_and_append_smap_val(&row->other_config, "min-rate", "min-rate",
                                 &string);
        find_and_append_smap_val(&row->other_config, "max-rate", "max-rate",
                                 &string);
        ds_put_format(&string, "</properties></queue>");
        free(uuid);
    }
    return ds_steal_cstr(&string);
}

static char *
get_ports_config(void)
{
    const struct ovsrec_interface *row, *next;
    struct ds string;

    ds_init(&string);
    OVSREC_INTERFACE_FOR_EACH_SAFE(row, next, ofc_global_context->idl) {
        char *uuid = print_uuid(&row->header_.uuid);

        ds_put_format(&string, "<port>");
        ds_put_format(&string, "<resource-id>%s</resource-id>", uuid);
        ds_put_format(&string, "<requested-number>%" PRIu64 "</requested-number>",
                      (row->n_ofport_request > 0 ? row->ofport_request[0] : 0));
        ds_put_format(&string, "<configuration>");
        /* TODO ioctl:
           ds_put_format(&string, "<admin-state>%s</admin-state>", "XXX");
           */
        /* TODO openflow:
           ds_put_format(&string, "<no-receive>%s</no-receive>", "XXX");
           ds_put_format(&string, "<no-forward>%s</no-forward>", "XXX");
           ds_put_format(&string, "<no-packet-in>%s</no-packet-in>", "XXX");
           */
        ds_put_format(&string, "</configuration>");
        /* TODO ioctl:
           "<features>" "<advertised>" "<rate>%s</rate>"
           "<auto-negotiate>%s</auto-negotiate>" "<medium>%s</medium>"
           "<pause>%s</pause>" "</advertised>" "</features>"
           */

        if (!strcmp(row->type, "gre")) {
            ds_put_format(&string, "<ipgre-tunnel>");
            find_and_append_smap_val(&row->options, "local_ip",
                                     "local-endpoint-ipv4-adress", &string);
            find_and_append_smap_val(&row->options, "remote_ip",
                                     "remote-endpoint-ipv4-adress", &string);
            find_and_append_smap_val(&row->options, "csum",
                                     "checksum-present", &string);
            find_and_append_smap_val(&row->options, "key", "key", &string);
            ds_put_format(&string, "</ipgre-tunnel>");
        } else if (!strcmp(row->type, "vxlan")) {
            ds_put_format(&string, "<vxlan-tunnel>");
            find_and_append_smap_val(&row->options, "local_ip",
                                     "local-endpoint-ipv4-adress", &string);
            find_and_append_smap_val(&row->options, "remote_ip",
                                     "remote-endpoint-ipv4-adress", &string);

            find_and_append_smap_val(&row->options, "key", "vni", &string);
            ds_put_format(&string, "</vxlan-tunnel>");
        } else if ((!strcmp(row->type, "gre64"))
                    || (!strcmp(row->type, "geneve"))
                    || (!strcmp(row->type, "lisp"))) {
            ds_put_format(&string, "<tunnel>");
            find_and_append_smap_val(&row->options, "local_ip",
                                     "local-endpoint-ipv4-adress", &string);
            find_and_append_smap_val(&row->options, "remote_ip",
                                     "remote-endpoint-ipv4-adress", &string);
            ds_put_format(&string, "</tunnel>");
        }
        ds_put_format(&string, "</port>");
        free(uuid);
    }
    return ds_steal_cstr(&string);
}

static char *
get_ports_state(void)
{
    const struct ovsrec_interface *row, *next;
    struct ds string;

    ds_init(&string);
    OVSREC_INTERFACE_FOR_EACH_SAFE(row, next, ofc_global_context->idl) {
        char *uuid = print_uuid(&row->header_.uuid);

        ds_put_format(&string, "<port>");
        ds_put_format(&string, "<resource-id>%s</resource-id>", uuid);
        ds_put_format(&string, "<number>%" PRIu64 "</number>",
                      (row->n_ofport > 0 ? row->ofport[0] : 0));
        ds_put_format(&string, "<name>%s</name>", row->name);
        /* TODO ioctl():
           <current-rate>%s</current-rate>
           <max-rate>%s</max-rate> */
        ds_put_format(&string, "<state>");
        ds_put_format(&string, "<oper-state>%s</oper-state>", row->link_state);

        find_and_append_smap_val(&row->other_config, "stp_state", "blocked",
                                 &string);
        /* TODO openflow:
            <live>%s</live> */
        ds_put_format(&string, "</state>");

        /* TODO ioctl()
           "<features>" "<current>" "<rate>%s</rate>"
           "<auto-negotiate>%s</auto-negotiate>" "<medium>%s</medium>"
           "<pause>%s</pause>" "</current>" "<supported>" "<rate>%s</rate>"
           "<auto-negotiate>%s</auto-negotiate>" "<medium>%s</medium>"
           "<pause>%s</pause>" "</supported>" "<advertised-peer>"
           "<rate>%s</rate>" "<auto-negotiate>%s</auto-negotiate>"
           "<medium>%s</medium>" "<pause>%s</pause></advertised-peer>"
           "</features>" */

        ds_put_format(&string, "</port>");
        free(uuid);
    }
    return ds_steal_cstr(&string);
}

static void
get_controller_state(struct ds *string, const struct ovsrec_controller *row)
{
    ds_put_format(string, "<controller>");
    /* TODO?
       <id>%s</id>
       */
    ds_put_format(string, "<state>");
    ds_put_format(string, "<connection-state>%s</connection-state>",
                  (row->is_connected ? "up" : "down"));
    /* XXX not mapped: ds_put_format(string,
     * "<current-version>%s</current-version>", ); ds_put_format(string,
     * "<supported-versions>%s</supported-versions>", ); */
    /* XXX local-*-in-use  - TODO use netstat */
    ds_put_format(string, "<local-ip-address-in-use>%s</local-ip-address-in-use>", "XXX");
    ds_put_format(string, "<local-port-in-use>%s</local-port-in-use>", "XXX");
    ds_put_format(string, "</state>");

    ds_put_format(string, "</controller>");
}

/* parses target t: rewrites delimiters to \0 and sets output pointers */
static void
parse_target_to_addr(char *t, char **protocol, char **address, char **port)
{
    /* XXX write some test for this... */
    char *is_ipv6 = NULL;
    if (t == NULL) {
        (*protocol) = NULL;
        (*address) = NULL;
        (*port) = NULL;
    }

    /* t begins with protocol */
    (*protocol) = t;

    /* address is after delimiter ':' */
    (*address) = strchr(*protocol, ':');
    is_ipv6 = strchr(*address, '[');
    if (*address != NULL) {
        *(*address) = 0;
        (*address)++;
        if (is_ipv6 != NULL) {
            (*port) = strchr(*address, ']');
            (*port)++;
        } else {
            (*port) = strchr(*address, ':');
        }
        if (*port != NULL) {
            *(*port) = 0;
            (*port)++;
        }
    } else {
        (*port) = NULL;
    }
}

static void
get_controller_config(struct ds *string, const struct ovsrec_controller *row)
{
    char *protocol, *address, *port;
    char *target = strdup(row->target);

    parse_target_to_addr(target, &protocol, &address, &port);

    ds_put_format(string, "<controller>");
    ds_put_format(string, "<id>%s</id>", print_uuid_ro(&row->header_.uuid));
    ds_put_format(string, "<ip-address>%s</ip-address>", address);
    ds_put_format(string, "<port>%s</port>", port);
    ds_put_format(string, "<protocol>%s</protocol>", protocol);

    if (!strcmp(row->connection_mode, "in-band")) {
        ds_put_format(string, "<local-ip-address>%s</local-ip-address>",
                      row->local_ip);
    }
    ds_put_format(string, "</controller>");
}

static char *
get_bridges_state(void)
{
    const struct ovsrec_bridge *row, *next;
    struct ds string;
    size_t i;

    ds_init(&string);
    OVSREC_BRIDGE_FOR_EACH_SAFE(row, next, ofc_global_context->idl) {
        /* char *uuid = print_uuid(&row->header_.uuid); */
        /* ds_put_format(&string, "<resource-id>%s</resource-id>", uuid);
         * free(uuid); */
        ds_put_format(&string, "<switch>");
        ds_put_format(&string, "<id>%s</id>", row->name);
        ds_put_format(&string, "<capabilities><max-buffered-packets>%d"
                      "</max-buffered-packets>", 256);
        /* XXX max-tables: "no explicit limit"? */
        /* ds_put_format(&string, "<max-tables>%s</max-tables>"); */
        ds_put_format(&string, "<max-ports>%d</max-ports>", 255);
        ds_put_format(&string, "<flow-statistics>%s</flow-statistics>",
                      "true");
        ds_put_format(&string, "<table-statistics>%s</table-statistics>",
                      "true");
        ds_put_format(&string, "<port-statistics>%s</port-statistics>",
                      "true");
        ds_put_format(&string, "<group-statistics>%s</group-statistics>",
                      "true");
        ds_put_format(&string, "<queue-statistics>%s</queue-statistics>",
                      "true");
        ds_put_format(&string,
                      "<reassemble-ip-fragments>%s</reassemble-ip-fragments>",
                      "true");
        ds_put_format(&string, "<block-looping-ports>%s</block-looping-ports>",
                      "true");

        ds_put_format(&string, "<reserved-port-types><type>all</type>"
                      "<type>controller</type><type>table</type>"
                      "<type>inport</type><type>any</type><type>normal</type>"
                      "<type>flood</type></reserved-port-types>");

        ds_put_format(&string, "<group-types><type>all</type>"
                      "<type>select</type><type>indirect</type>"
                      "<type>fast-failover</type></group-types>");

        ds_put_format(&string, "<group-capabilities>"
                      "<capability>select-weight</capability>"
                      "<capability>select-liveness</capability>"
                      "<capability>chaining-check</capability>"
                      "</group-capabilities>");

        ds_put_format(&string, "<action-types>");
        ds_put_format(&string, "<type>set-mpls-ttl</type>"
                      "<type>dec-mpls-ttl</type><type>push-vlan</type>"
                      "<type>pop-vlan</type><type>push-mpls</type>");
        ds_put_format(&string, "<type>pop-mpls</type><type>set-queue</type>"
                      "<type>group</type><type>set-nw-ttl</type>"
                      "<type>dec-nw-ttl</type><type>set-field</type>");
        ds_put_format(&string, "</action-types>");

        ds_put_format(&string, "<instruction-types>");
        ds_put_format(&string, "<type>apply-actions</type>"
                      "<type>clear-actions</type><type>write-actions</type>"
                      "<type>write-metadata</type><type>goto-table</type>");
        ds_put_format(&string, "</instruction-types>");
        ds_put_format(&string, "</capabilities>");
        if (row->n_controller > 0) {
            ds_put_format(&string, "<controllers>");
            for (i = 0; i < row->n_controller; ++i) {
                get_controller_state(&string, row->controller[i]);
            }
            ds_put_format(&string, "</controllers>");
        }
        ds_put_format(&string, "</switch>");
    }

    return ds_steal_cstr(&string);
}

void
append_resource_refs(struct ds *string, struct ovsdb_idl_row **h,
                     size_t count, const char *elem)
{
    size_t i;
    if (count > 0) {
        for (i = 0; i < count; ++i) {
            /* TODO translate UUID -> resource-id */
            ds_put_format(string, "<%s>%s</%s>", elem,
                          print_uuid_ro(&h[i]->uuid), elem);
        }
    }
}
static char *
get_bridges_config(void)
{
    const struct ovsrec_bridge *row, *next;
    struct ovsrec_port *port;
    struct ds string;
    size_t i;

    ds_init(&string);
    OVSREC_BRIDGE_FOR_EACH_SAFE(row, next, ofc_global_context->idl) {
        /* char *uuid = print_uuid(&row->header_.uuid); */
        /* ds_put_format(&string, "<resource-id>%s</resource-id>", uuid);
         * free(uuid); */
        ds_put_format(&string, "<switch>");
        ds_put_format(&string, "<id>%s</id>", row->name);
        /* TODO
           "<enabled>%s</enabled>"
           */
        find_and_append_smap_val(&row->other_config, "datapath-id",
                                 "datapath-id", &string);
        if (row->fail_mode != NULL) {
            ds_put_format(&string, "<lost-connection-behavior>%s"
                    "</lost-connection-behavior>", row->fail_mode);
        }
        if (row->n_controller > 0) {
            ds_put_format(&string, "<controllers>");
            for (i = 0; i < row->n_controller; ++i) {
                get_controller_config(&string, row->controller[i]);
            }
            ds_put_format(&string, "</controllers>");
        }

        ds_put_format(&string, "<resources>");
        for (i=0; i<row->n_ports; i++) {
            port = row->ports[i];
            if (port == NULL) {
                continue;
            }
            append_resource_refs(&string,
                                 (struct ovsdb_idl_row **) port->interfaces,
                                 port->n_interfaces, "port");
        }
        append_resource_refs(&string,
                             (struct ovsdb_idl_row **) row->value_flow_tables,
                             row->n_flow_tables, "flow-table");

        if (row->n_ports > 0) {
            for (i = 0; i < row->n_ports; ++i) {
                if (row->ports[i]->qos != NULL) {
                    /* XXX test with some QoS'ed interface */
                    append_resource_refs(&string,
                            (struct ovsdb_idl_row **) row->ports[i]->qos
                            ->value_queues,
                            row->ports[i]->qos->n_queues, "queue");
                }
            }
        }
        /* TODO:
           "<certificate>%s</certificate>"
           */
        ds_put_format(&string, "</resources></switch>");
    }

    return ds_steal_cstr(&string);
}

char *
get_owned_certificates_config()
{
    /* TODO owned certificate "<owned-certificate>"
     * "<resource-id>%s</resource-id>" "<certificate>%s</certificate>"
     * "<private-key>" "<key-type>" "<dsa>" "<DSAKeyValue>" "<P>%s</P>"
     * "<Q>%s</Q>" "<J>%s</J>" "<G>%s</G>" "<Y>%s</Y>" "<Seed>%s</Seed>"
     * "<PgenCounter>%s</PgenCounter>" "</DSAKeyValue>" "</dsa>" "<rsa>"
     * "<RSAKeyValue>" "<Modulus>%s</Modulus>" "<Exponent>%s</Exponent>"
     * "</RSAKeyValue>" "</rsa>" "</key-type>" "</private-key>"
     * "</owned-certificate>" */
    return NULL;
}

char *
get_external_certificates_config()
{
    /* TODO external-certificate "<external-certificate>"
     * "<resource-id>%s</resource-id>" "<certificate>%s</certificate>"
     * "</external-certificate>" */
    return NULL;
}

/* synchronize local copy of OVSDB */
static void
ofconf_update(ofconf_t *p)
{
    int retval, i;
    for (i=0; i<4; i++) {
        ovsdb_idl_run(p->idl);
        if (!ovsdb_idl_is_alive(p->idl)) {
            retval = ovsdb_idl_get_last_error(p->idl);
            nc_verb_error("OVS database connection failed (%s)",
                   ovs_retval_to_string(retval));
        }

        if (p->seqno != ovsdb_idl_get_seqno(p->idl)) {
            p->seqno = ovsdb_idl_get_seqno(p->idl);
            i--;
        }

        if (p->seqno == ovsdb_idl_get_seqno(p->idl)) {
            ovsdb_idl_wait(p->idl);
            poll_timer_wait(100); /* wait for 100ms (at most) */
            poll_block();
        }
    }
}


char *
get_config_data()
{
    const char *config_data_format = "<?xml version=\"1.0\"?><capable-switch xmlns=\"urn:onf:config:yang\">"
        "<id>%s</id><resources>" "%s"    /* port */
        "%s"                    /* queue */
        "%s"                    /* owned-certificate */
        "%s"                    /* external-certificate */
        "%s"                    /* flow-table */
        "</resources>"
        "<logical-switches>%s</logical-switches></capable-switch>";

    struct ds state_data;

#define FOREACH_STR(GEN) \
    GEN(queues) GEN(ports) GEN(flow_tables) GEN(bridges) \
    GEN(owned_certificates) GEN(external_certificates)
#define DEFINITION(name) char *name;

    FOREACH_STR(DEFINITION)
#undef DEFINITION


    if (ofc_global_context == NULL) {
        return NULL;
    }

#define ASSIGMENT(name) name = get_ ## name ## _config(); \
    if (name == NULL) { \
        name = strdup(""); \
    }
    FOREACH_STR(ASSIGMENT)
#undef ASSIGMENT

    ds_init(&state_data);

    ds_put_format(&state_data, config_data_format,"XXX" /* switch/id XXX */,
    ports, queues, flow_tables, owned_certificates, external_certificates,
                  bridges);

#define FREE(name) free(name);
    FOREACH_STR(FREE)
#undef FREE
#undef FOREACH_STR

    return ds_steal_cstr(&state_data);
}

char *
get_state_data()
{
    const char *state_data_format = "<?xml version=\"1.0\"?>"
        "<capable-switch xmlns=\"urn:onf:config:yang\"><config-version>%s</config-version>"
        "<resources>%s%s</resources>"
        "<logical-switches>%s</logical-switches></capable-switch>";

#define FOREACH_STR(GEN) \
    GEN(ports) GEN(flow_tables) GEN(bridges)
#define DEFINITION(name) char *name;
    FOREACH_STR(DEFINITION)
#undef DEFINITION
    struct ds state_data;

    if (ofc_global_context == NULL) {
        return NULL;
    }
    ofconf_update(ofc_global_context);

#define ASSIGMENT(name) name = get_ ## name ## _state(); \
    if (name == NULL) { \
        name = strdup(""); \
    }
    FOREACH_STR(ASSIGMENT)
#undef ASSIGMENT

    ds_init(&state_data);

    ds_put_format(&state_data, state_data_format, "1.2", ports, flow_tables,
                  bridges);

#define FREE(name) free(name);
    FOREACH_STR(FREE)
#undef FREE
#undef FOREACH_STR

    return ds_steal_cstr(&state_data);
}

void
ofconf_init(const char *ovs_db_path)
{
    ofconf_t *p = (ofconf_t *) calloc(1, sizeof (ofconf_t));

    if (p == NULL) {
        /* failed */
        return;
    }
    ofc_global_context = p;

    ovsrec_init();
    p->idl = ovsdb_idl_create(ovs_db_path, &ovsrec_idl_class, true, true);
    p->seqno = ovsdb_idl_get_seqno(p->idl);
    ofconf_update(p);
}

void
ofconf_destroy(void)
{
    if (ofc_global_context != NULL) {
        /* close everything */
        ovsdb_idl_destroy(ofc_global_context->idl);

        free(ofc_global_context);
        ofc_global_context = NULL;
    }
}

/* Notes:
OpenFlow access:
    +--rw capable-switch
        +--rw resources
            +--rw port* [resource-id]
                +--rw configuration
                    no-receive
                    no-forward
                    no-packet-in
                +--ro state
                    +--ro live?
        The no-receive is true if NO_RECV is found.
        The no-forward is true if NO_FWD is found.
        The no-packet-in is true if NO_PACKET_IN is found.
        Use ovs-ofctl(8) to set values:
        # ovs-ofctl mod-port <SWITCH> <PORT> <no-receive|receive>
        # ovs-ofctl mod-port <SWITCH> <PORT> <no-forward|forward>
        # ovs-ofctl mod-port <SWITCH> <PORT> <no-packet-in|packet-in>
        # ovs-ofctl show <SWITCH>
        Note: The value is true if LIVE is found on the line:
        state: ...

OVSDB access:
+--rw capable-switch
    +--rw id - internally
    +--ro config-version? 1.2
    +--rw resources
        +--rw port* [resource-id]
            +--rw resource-id
            +--ro number?           ovsrec_interface->ofport[n_ofport]
            +--rw requested-number? ovsrec_interface->ofport_request[n_ofport_request]
            +--ro name?             ovsrec_interface->name
            +--ro state
                +--ro oper-state?   ovsrec_interface->link_state
                +--ro blocked?      ovsrec_interface->status:stp_state
            +--rw (tunnel-type)?
                +--:(tunnel)
                    +--rw tunnel
                        +--rw (endpoints)
                            +--:(v4-endpoints)
                                +--rw local-endpoint-ipv4-adress?      ovsrec_interface->options:local_ip
                                +--rw remote-endpoint-ipv4-adress?     ovsrec_interface->options:remote_ip
                            +--:(ipgre-tunnel)
                                +--rw ipgre-tunnel
                                +--rw (endpoints)
                                | +--:(v4-endpoints)
                                |    +--rw local-endpoint-ipv4-adress?      ovsrec_interface->options:local_ip
                                |    +--rw remote-endpoint-ipv4-adress?     ovsrec_interface->options:remote_ip
                                +--rw checksum-present?                     ovsrec_interface->options:csum
                                +--rw key-present?
                                +--rw key                                   ovsrec_interface->options:key
                            +--:(vxlan-tunnel)
                                +--rw vxlan-tunnel
                                +--rw (endpoints)
                                | +--:(v4-endpoints)
                                |    +--rw local-endpoint-ipv4-adress?      ovsrec_interface->options:local_ip
                                |    +--rw remote-endpoint-ipv4-adress?     ovsrec_interface->options:remote_ip
                                +--rw vni?                                  ovsrec_interface->options:key
        +--rw queue* [resource-id]
            +--rw resource-id
            +--rw id
            +--rw port?
            +--rw properties
                +--rw min-rate?         ovsrec queue->other_config:min-rate
                +--rw max-rate?         ovsrec queue->other_config:max-rate
                +--rw experimenter-id?
                +--rw experimenter-data?

ioctl access:
+--rw capable-switch
    +--rw id - internally
    +--ro config-version? 1.2
    +--rw resources
        +--rw port* [resource-id]
            +--ro current-rate?
            +--rw configuration
                +--rw admin-state
*/

