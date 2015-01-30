
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
#include <vlog.h>
#include <vswitch-idl.h>
#include <dpif.h>
#include <vconn.h>
#include <socket-util.h>
#include <ofp-util.h>
#include <ofp-msgs.h>

#include "ovs-data.h"

VLOG_DEFINE_THIS_MODULE(ovsdata);

static char *
print_uuid(const struct uuid *uuid)
{
    char str[38];

    snprintf(str, 37, UUID_FMT, UUID_ARGS(uuid));
    str[37] = 0;
    return strdup(str);
}


char *get_ovsdb_flow_tables_state(ofconf_t *ofconf)
{
    const struct ovsrec_flow_table *row, *next;
    struct ds string;
    ds_init(&string);
    OVSREC_FLOW_TABLE_FOR_EACH_SAFE(row, next, ofconf->idl) {
        char *uuid = print_uuid(&row->header_.uuid);
        ds_put_format(&string, "<flow-table><resource-id>%s</resource-id><max-entries>%d</max-entries></flow-table>",
            uuid, (row->n_flow_limit > 0?row->flow_limit[0]:0));
        free(uuid);
    }
    return ds_steal_cstr(&string);
}

char *get_ovsdb_queues_state(ofconf_t *ofconf)
{
    const struct ovsrec_queue *row, *next;
    struct ds string;
    ds_init(&string);
    OVSREC_QUEUE_FOR_EACH_SAFE(row, next, ofconf->idl) {
        char *uuid = print_uuid(&row->header_.uuid);
        ds_put_format(&string, "<queue><resource-id>%s</resource-id><properties>", uuid);
        const struct smap_node *oc, *oc_it;

        SMAP_FOR_EACH_SAFE(oc, oc_it, &row->other_config) {
            if (!strcmp(oc->key, "min-rate")) {
                ds_put_format(&string, "<min-rate>%s</min-rate>", oc->value);
            } else if (!strcmp(oc->key, "max-rate")) {
                ds_put_format(&string, "<max-rate>%s</max-rate>", oc->value);
            }
        }
        ds_put_format(&string, "</properties></queue>");
        free(uuid);
    }
    return ds_steal_cstr(&string);
}

char *get_ovsdb_port_state(ofconf_t *ofconf)
{
    const struct ovsrec_interface *row, *next;
    struct ds string;
    ds_init(&string);
    OVSREC_INTERFACE_FOR_EACH_SAFE(row, next, ofconf->idl) {
        char *uuid = print_uuid(&row->header_.uuid);
        ds_put_format(&string, "<port>");
        ds_put_format(&string, "<resource-id>%s</resource-id>", uuid);
        ds_put_format(&string, "<number>%"PRIu64"</number>", (row->n_ofport>0?row->ofport[0]:0));
        ds_put_format(&string, "<name>%s</name>", row->name);
        ds_put_format(&string, "<state>");
        ds_put_format(&string, "<oper-state>%s</oper-state>", row->link_state);

        const struct smap_node *oc, *oc_it;

        SMAP_FOR_EACH_SAFE(oc, oc_it, &row->other_config) {
            if (!strcmp(oc->key, "stp_state")) {
                ds_put_format(&string, "<blocked>%s</blocked>", oc->value);
            }
        }
        ds_put_format(&string, "</state>");
        ds_put_format(&string, "</port>");
        free(uuid);
    }
    return ds_steal_cstr(&string);
}

char *get_ovsdb_bridges_state(ofconf_t *ofconf)
{
    const struct ovsrec_bridge *row, *next;
    struct ds string;
    ds_init(&string);
    OVSREC_BRIDGE_FOR_EACH_SAFE(row, next, ofconf->idl) {
        /* char *uuid = print_uuid(&row->header_.uuid);
        ds_put_format(&string, "<resource-id>%s</resource-id>", uuid);
        free(uuid); */
        ds_put_format(&string, "<switch><capabilities>");
        ds_put_format(&string, "<max-buffered-packets>%d</max-buffered-packets>", 256);
        ds_put_format(&string, "<max-tables>%s</max-tables>");
        ds_put_format(&string, "<max-ports>%d</max-ports>", 255);
        ds_put_format(&string, "<flow-statistics>%s</flow-statistics>", "true");
        ds_put_format(&string, "<table-statistics>%s</table-statistics>", "true");
        ds_put_format(&string, "<port-statistics>%s</port-statistics>", "true");
        ds_put_format(&string, "<group-statistics>%s</group-statistics>", "true");
        ds_put_format(&string, "<queue-statistics>%s</queue-statistics>", "true");
        ds_put_format(&string, "<reassemble-ip-fragments>%s</reassemble-ip-fragments>", "true");
        ds_put_format(&string, "<block-looping-ports>%s</block-looping-ports>", "true");

        ds_put_format(&string, "<reserved-port-types><type>all</type><type>controller</type><type>table</type><type>inport</type><type>any</type><type>normal</type><type>flood</type></reserved-port-types>");

        ds_put_format(&string, "<group-types><type>all</type><type>select</type><type>indirect</type><type>fast-failover</type></group-types>");

        ds_put_format(&string, "<group-capabilities><capability>select-weight</capability><capability>select-liveness</capability><capability>chaining-check</capability></group-capabilities>");

        ds_put_format(&string, "<action-types>");
        ds_put_format(&string, "<type>set-mpls-ttl</type><type>dec-mpls-ttl</type><type>push-vlan</type><type>pop-vlan</type><type>push-mpls</type>");
        ds_put_format(&string, "<type>pop-mpls</type><type>set-queue</type><type>group</type><type>set-nw-ttl</type><type>dec-nw-ttl</type><type>set-field</type>");
        ds_put_format(&string, "</action-types>");

        ds_put_format(&string, "<instruction-types>");
        ds_put_format(&string, "<type>apply-actions</type><type>clear-actions</type><type>write-actions</type><type>write-metadata</type><type>goto-table</type>");
        ds_put_format(&string, "</instruction-types>");
        ds_put_format(&string, "</capabilities></switch>");
/* TODO
        "<controllers>"
        "<controller>"
        "<state>"
        "<connection-state>%s</connection-state>"
        "<current-version>%s</current-version>"
        "<supported-versions>%s</supported-versions>"
        "<local-ip-address-in-use>%s</local-ip-address-in-use>"
        "<local-port-in-use>%s</local-port-in-use>"
        "</state>"
        "</controller>"
        "</controllers>"
        "</switch>";
*/

    }

    return ds_steal_cstr(&string);
}

const char *state_data_resources_format = "<port>"
        "<number>%s</number>"
        "<name>%s</name>"
        "<current-rate>%s</current-rate>"
        "<max-rate>%s</max-rate>"
        "<state>"
        "<oper-state>%s</oper-state>"
        "<blocked>%s</blocked>"
        "<live>%s</live>"
        "</state>"
        "<features>"
        "<current>"
        "<rate>%s</rate>"
        "<auto-negotiate>%s</auto-negotiate>"
        "<medium>%s</medium>"
        "<pause>%s</pause>"
        "</current>"
        "<supported>"
        "<rate>%s</rate>"
        "<auto-negotiate>%s</auto-negotiate>"
        "<medium>%s</medium>"
        "<pause>%s</pause>"
        "</supported>"
        "<advertised-peer>"
        "<rate>%s</rate>"
        "<auto-negotiate>%s</auto-negotiate>"
        "<medium>%s</medium>"
        "<pause>%s</pause>"
        "</advertised-peer>"
        "</features>"
        "</port>%s";


const char *state_data_logical_switches_format = "<switch>"
        "<capabilities>"
        "<max-buffered-packets>%s</max-buffered-packets>"
        "<max-tables>%s</max-tables>"
        "<max-ports>%s</max-ports>"
        "<flow-statistics>%s</flow-statistics>"
        "<table-statistics>%s</table-statistics>"
        "<port-statistics>%s</port-statistics>"
        "<group-statistics>%s</group-statistics>"
        "<queue-statistics>%s</queue-statistics>"
        "<reassemble-ip-fragments>%s</reassemble-ip-fragments>"
        "<block-looping-ports>%s</block-looping-ports>"
        "<reserved-port-types>"
        "<type>%s</type>"
        "</reserved-port-types>"
        "<group-types>"
        "<type>%s</type>"
        "</group-types>"
        "<group-capabilities>"
        "<capability>%s</capability>"
        "</group-capabilities>"
        "<action-types>"
        "<type>%s</type>"
        "</action-types>"
        "<instruction-types>"
        "<type>%s</type>"
        "</instruction-types>"
        "</capabilities>"
        "<controllers>"
        "<controller>"
        "<state>"
        "<connection-state>%s</connection-state>"
        "<current-version>%s</current-version>"
        "<supported-versions>%s</supported-versions>"
        "<local-ip-address-in-use>%s</local-ip-address-in-use>"
        "<local-port-in-use>%s</local-port-in-use>"
        "</state>"
        "</controller>"
        "</controllers>"
        "</switch>";

const char *state_data_format = "<?xml version=\"1.0\"?>"
    "<capable-switch>"
        "<config-version>%s</config-version>"
        "<resources>%s</resources>"
        "<logical-switches>%s</logical-switches>"
    "</capable-switch>";

char *get_state_data(ofconf_t *ofc)
{
    struct ds state_data, resources, logical_switches;

    char *queues = get_ovsdb_queues_state(ofc);
    char *ports = get_ovsdb_port_state(ofc);
    char *flow_tables = get_ovsdb_flow_tables_state(ofc);
    char *bridges = get_ovsdb_bridges_state(ofc);

    ds_init(&state_data);
    ds_init(&resources);
    ds_init(&logical_switches);

    ds_put_format(&resources, "%s%s", ports, flow_tables);
    ds_put_format(&logical_switches, "%s", bridges);
    ds_put_format(&state_data, state_data_format, "1.2", ds_cstr_ro(&resources), ds_cstr_ro(&logical_switches));

    free(ports);
    free(flow_tables);
    free(bridges);

    ds_destroy(&resources);
    ds_destroy(&logical_switches);
    return ds_steal_cstr(&state_data);
}


ofconf_t *ofconf_init(const char *ovs_db_path)
{
    ofconf_t *p = (ofconf_t *) calloc(1, sizeof(ofconf_t));
    if (p == NULL) {
        /* failed */
        return NULL;
    }
    /* verbose level */
    //vlog_set_levels(NULL, VLF_CONSOLE, VLL_DBG);

    ovsrec_init();
    p->idl = ovsdb_idl_create(ovs_db_path, &ovsrec_idl_class, true, true);
    p->seqno = ovsdb_idl_get_seqno(p->idl);
    for (;;) {
        ovsdb_idl_run(p->idl);
        if (!ovsdb_idl_is_alive(p->idl)) {
            int retval = ovsdb_idl_get_last_error(p->idl);

            printf("database connection failed (%s)",
                   ovs_retval_to_string(retval));
        }

        if (p->seqno != ovsdb_idl_get_seqno(p->idl)) {
            p->seqno = ovsdb_idl_get_seqno(p->idl);
            /* go to exit after this output */
            break;
        }

        if (p->seqno == ovsdb_idl_get_seqno(p->idl)) {
            ovsdb_idl_wait(p->idl);
            poll_block();
        }
    }
    return p;
}

void ofconf_destroy(ofconf_t **ofconf)
{
    ofconf_t *p = NULL;
    if ((ofconf != NULL) && (*ofconf != NULL)) {
        p = (ofconf_t *) (*ofconf);
        /* close everything */
        ovsdb_idl_destroy(p->idl);

        free(*ofconf);
        (*ofconf) = NULL;
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

