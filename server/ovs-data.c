
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

#include <assert.h>
#include <sys/socket.h>
#include <linux/ethtool.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* libovs */
#include <dynamic-string.h>
#include <ovsdb-idl-provider.h>
#include <vswitch-idl.h>
#include <dpif.h>
#include <vconn.h>
#include <socket-util.h>
#include <ofp-util.h>
#include <ofp-msgs.h>
#include <poll-loop.h>

#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <libnetconf.h>

#include "data.h"

/* Tests bit of OpenFlow port config and returns "true"/"false" */
#define OFC_PORT_CONF_BIT(conf, bit) \
    (conf & bit ? "true" : "false")

typedef struct {
    struct ovsdb_idl *idl;
    struct ovsdb_idl_txn *txn;
    unsigned int seqno;
    struct vconn *vconn;
    ofc_resmap_t *resource_map;
    struct ofc_resmap_certificate *cert_map;
} ovsdb_t;
ovsdb_t *ovsdb_handler = NULL;

int ioctlfd = -1;

struct u32_str_map{
    uint32_t value;
    const char *str;
};

static const char *
print_uuid_ro(const struct uuid *uuid)
{
    static char str[38];

    snprintf(str, 37, UUID_FMT, UUID_ARGS(uuid));
    str[37] = 0;
    return str;
}


/* OpenFlow helpers to get information about interfaces */

/* Prepare vconnp - connection with bridge using OpenFlow.  Bridge is
 * identified by name (e.g. ofc-bridge).  We use default OF versions
 * (OFPUTIL_DEFAULT_VERSIONS).
 *
 * This function was copied from ovs-ofctl and modified.
 *
 * Function returns 0 on success, otherwise errno value.  If successful,
 * stores a pointer to the new connection in '*vconnp', otherwise a null
 * pointer. */
static int
open_vconn_socket(const char *name, struct vconn **vconnp)
{
    int error;
    char *vconn_name = xasprintf("unix:%s", name);

    error = vconn_open(vconn_name, OFPUTIL_DEFAULT_VERSIONS, DSCP_DEFAULT,
                       vconnp);
    if (error && error != ENOENT) {
        nc_verb_error("OpenFlow: %s: failed to open socket (%s)", name,
                      ovs_strerror(error));
    }
    free(vconn_name);

    return error;
}

/* Creates connection via OpenFlow with the bridge identified by 'name'.
 *
 * Function was partially copied from ovs-ofctl and modified.
 *
 * Function returns true on success.  If successful, it stores a pointer
 * to the new connection in '*vconnp', otherwise a null pointer. */
bool
ofc_of_open_vconn(const char *name, struct vconn **vconnp)
{
    char *datapath_name, *datapath_type, *socket_name;
    enum ofputil_protocol protocol;
    char *bridge_path;
    int ofp_version;
    int error;

    bridge_path = xasprintf("%s/%s.mgmt", OFC_OVS_OFSOCKET_DIR, name);

    /* changed to called function */
    dp_parse_name(name, &datapath_name, &datapath_type);

    socket_name = xasprintf("%s/%s.mgmt", OFC_OVS_OFSOCKET_DIR, datapath_name);
    free(datapath_name);
    free(datapath_type);

    if (strchr(name, ':')) {
        vconn_open(name, OFPUTIL_DEFAULT_VERSIONS, DSCP_DEFAULT, vconnp);
    } else if (!open_vconn_socket(name, vconnp)) {
        /* Fall Through. */
    } else if (!open_vconn_socket(bridge_path, vconnp)) {
        /* Fall Through. */
    } else if (!open_vconn_socket(socket_name, vconnp)) {
        /* Fall Through. */
    } else {
        nc_verb_error("OpenFlow: %s is not a bridge or a socket.", name);
        return false;
    }

    free(bridge_path);
    free(socket_name);

    nc_verb_verbose("OpenFlow: connecting to %s", vconn_get_name(*vconnp));
    error = vconn_connect_block(*vconnp);
    if (error) {
        nc_verb_error("OpenFlow: %s: failed to connect to socket (%s).", name,
                      ovs_strerror(error));
        return false;
    }

    ofp_version = vconn_get_version(*vconnp);
    protocol = ofputil_protocol_from_ofp_version(ofp_version);
    if (!protocol) {
        nc_verb_error("OpenFlow: %s: unsupported OpenFlow version 0x%02x.",
                      name, ofp_version);
        return false;
    }

    return true;
}

/* Gets information about interfaces using 'vconnp' connection.  Function
 * returns pointer to OpenFlow buffer (implemented by OVS) that is used
 * to get results. */
struct ofpbuf *
ofc_of_get_ports(struct vconn *vconnp)
{
    struct ofpbuf *request;
    struct ofpbuf *reply;
    int ofp_version;

    if (vconnp == NULL) {
        return NULL;
    }

    /* existence of version was checked in ofc_of_open_vconn() */
    ofp_version = vconn_get_version(vconnp);

    request = ofputil_encode_port_desc_stats_request(ofp_version, OFPP_NONE);
    vconn_transact(vconnp, request, &reply);

    /* updates reply size */
    ofputil_switch_features_has_ports(reply);
    return reply;
}

/* Sets value of configuration bit of 'port_name' interface.  It can be used
 * to set: OFPUTIL_PC_NO_FWD, OFPUTIL_PC_NO_PACKET_IN, OFPUTIL_PC_NO_RECV,
 * OFPUTIL_PC_PORT_DOWN given as 'bit'.  If 'value' is 0, clear configuration
 * bit.  Otherwise, set 'bit' to 1.
 */
static void
ofc_of_mod_port_internal(struct vconn *vconnp, const char *port_name,
                enum ofputil_port_config bit, char value)
{
    enum ofptype type;
    int ofp_version;
    enum ofputil_protocol protocol;
    struct ofputil_phy_port pp;
    struct ofputil_port_mod pm;
    struct ofp_header *oh;
    struct ofpbuf b, *request, *reply = ofc_of_get_ports(vconnp);

    if (reply == NULL) {
        return;
    }

    ofp_version = vconn_get_version(vconnp);

    protocol = ofputil_protocol_from_ofp_version(ofp_version);
    oh = ofpbuf_data(reply);

    /* get the beginning of data */
    ofpbuf_use_const(&b, oh, ntohs(oh->length));
    if (ofptype_pull(&type, &b) || type != OFPTYPE_PORT_DESC_STATS_REPLY) {
        nc_verb_error("OpenFlow: bad reply.");
        return;
    }

    /* find port by name */
    while (!ofputil_pull_phy_port(oh->version, &b, &pp)) {
        /* modify port */
        if (!strncmp(pp.name, port_name, strlen(pp.name)+1)) {
            /* prepare port for transaction */
            pm.port_no = pp.port_no;
            pm.config = 0;
            pm.mask = 0;
            pm.advertise = 0;
            memcpy(pm.hw_addr, pp.hw_addr, ETH_ADDR_LEN);
            pm.mask = bit;
            /* set value to selected bit: */
            pm.config = (value != 0 ? bit : 0);

            request = ofputil_encode_port_mod(&pm, protocol);
            ofpmsg_update_length(request);
            vconn_transact_noreply(vconnp, request, &reply);
            break;
        }
    }
    ofpbuf_delete(reply);
}

void
ofc_of_mod_port(const xmlChar *bridge_name, const xmlChar *port_name, const xmlChar *bit_xchar, const xmlChar *value)
{
    struct vconn *vconnp;
    enum ofputil_port_config bit;
    struct ofpbuf *of_ports = NULL;
    char val;
    if (xmlStrEqual(value, BAD_CAST "false")) {
        val = 0;
    } else if (xmlStrEqual(value, BAD_CAST "true")) {
        val = 1;
    } else if (xmlStrEqual(value, BAD_CAST "down")) {
        val = 1; /* inverse logic for admin-state */
    } else if (xmlStrEqual(value, BAD_CAST "up")) {
        val = 0; /* inverse logic for admin-state */
    } else {
        return;
    }
    if (xmlStrEqual(bit_xchar, BAD_CAST "no-receive")) {
        bit = OFPUTIL_PC_NO_RECV;
    } else if (xmlStrEqual(bit_xchar, BAD_CAST "no-forward")) {
        bit = OFPUTIL_PC_NO_FWD;
    } else if (xmlStrEqual(bit_xchar, BAD_CAST "no-packet-in")) {
        bit = OFPUTIL_PC_NO_PACKET_IN;
    } else if (xmlStrEqual(bit_xchar, BAD_CAST "admin-state")) {
        bit = OFPUTIL_PC_PORT_DOWN;
    } else {
        nc_verb_error("Bad element");
        return;
    }

    if (ofc_of_open_vconn((char *) bridge_name, &vconnp) == true) {
        of_ports = ofc_of_get_ports(vconnp);
    } else {
        nc_verb_error("OpenFlow: could not connect to '%s' bridge.",
                      bridge_name);
    }
    ofc_of_mod_port_internal(vconnp, (char *) port_name, bit, val);
    if (of_ports != NULL) {
        ofpbuf_delete(of_ports);
    }
    if (vconnp != NULL) {
        vconn_close(vconnp);
    }
}

/* Finds interface with 'name' in OpenFlow 'reply' and returns pointer to it.
 * If interface is not found, function returns NULL.  'reply' should be
 * prepared by ofc_of_get_ports().  Note that 'reply' still needs to be
 * freed. */
struct ofputil_phy_port *
ofc_of_getport_byname(struct ofpbuf *reply, const char *name)
{
    struct ofpbuf b;
    static struct ofputil_phy_port pp;
    struct ofp_header *oh;
    enum ofptype type;

    if (reply == NULL) {
        return NULL;
    }

    /* get the beginning of data */
    oh = ofpbuf_data(reply);
    ofpbuf_use_const(&b, oh, ntohs(oh->length));
    if (ofptype_pull(&type, &b) || type != OFPTYPE_PORT_DESC_STATS_REPLY) {
        nc_verb_error("OpenFlow: bad reply.");
        return NULL;
    }

    while (!ofputil_pull_phy_port(oh->version, &b, &pp)) {
        /* this is the point where we have information about state and
           configuration of interface */
        if (!strncmp(pp.name, name, strlen(pp.name)+1)) {
            return &pp;
        }
    }
    return NULL;
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
    int i, j;
    char dpid[24];

    const char *value = smap_get(s, key);
    if (value != NULL) {
        /* exception for datapath-id format conversion */
        if (!strcmp(elem, "datapath-id")) {
            for(i = 0, j = 1; j < 24; j++) {
                if (!(j % 3)) {
                    dpid[j - 1] = ':';
                } else {
                    dpid[j - 1] = value[i++];
                }
            }
            dpid[j - 1] = '\0';
            value = dpid;
        }

        ds_put_format(string, "<%s>%s</%s>", elem, value, elem);
    }
}

/*
 * Look up resource-id by uuid in the rm map, generate new if missing.
 * \param[in,out] rm    pointer to the rm map
 * \param[in] uuid      find existing record by uuid
 * \return Non-empty string with found or generated resource-id.
 * Empty string "" when no resource-id found and insertion of the
 * new one failed.  Resource-id is generated by converting UUID into string.
 */
static const char *
find_resid_generate(ofc_resmap_t *rm, const struct uuid *uuid)
{
    bool result;
    const char *resource_id = NULL;
    ofc_tuple_t *found = ofc_resmap_find_u(rm, uuid);
    if (found == NULL) {
        /* generate new resource-id (UUID string) */
        resource_id = print_uuid_ro(uuid);
        /* insert new record */
        result = ofc_resmap_insert(rm, resource_id, uuid, NULL);
        if (result == true) {
            return resource_id;
        } else {
            return "";
        }
    } else {
        return found->resource_id;
    }
}


static char *
get_flow_tables_state(void)
{
    const struct ovsrec_flow_table *row, *next;
    struct ds string;
    const char *resource_id;

    ds_init(&string);
    OVSREC_FLOW_TABLE_FOR_EACH_SAFE(row, next, ovsdb_handler->idl) {
        resource_id = find_resid_generate(ovsdb_handler->resource_map,
                                          &row->header_.uuid);
        ds_put_format(&string, "<flow-table><resource-id>%s</resource-id>"
                      "<max-entries>%"PRId64"</max-entries></flow-table>",
                      resource_id,
                      (row->n_flow_limit > 0 ? row->flow_limit[0] : 0));
    }
    return ds_steal_cstr(&string);
}

static bool
find_flowtable_id(const struct ovsrec_flow_table *flowtable, int64_t *id)
{
    const struct ovsrec_bridge *row;
    size_t i;
    int cmp;
    OVSREC_BRIDGE_FOR_EACH(row, ovsdb_handler->idl) {
        for (i = 0; i < row->n_flow_tables; i++) {
            cmp = uuid_equals(&flowtable->header_.uuid,
                              &row->value_flow_tables[i]->header_.uuid);
            if (cmp) {
                /* found */
                *id = row->key_flow_tables[i];
                return true;
            }
        }
    }
    return false;
}

static char *
get_flow_tables_config(void)
{
    const struct ovsrec_flow_table *row, *next;
    const char *resource_id;
    struct ds string;
    int64_t id;

    ds_init(&string);
    OVSREC_FLOW_TABLE_FOR_EACH_SAFE(row, next, ovsdb_handler->idl) {
        resource_id = find_resid_generate(ovsdb_handler->resource_map,
                                          &row->header_.uuid);
        ds_put_format(&string, "<flow-table><resource-id>%s</resource-id>",
                      resource_id);
        if (find_flowtable_id(row, &id)) {
            ds_put_format(&string, "<table-id>%"PRIi64"</table-id>", id);
        }
        ds_put_format(&string, "<name>%s</name>", row->name);
        ds_put_format(&string, "</flow-table>");
    }
    return ds_steal_cstr(&string);
}

static bool
find_queue_id(const struct ovsrec_queue *queue, int64_t *id)
{
    const struct ovsrec_qos *row;
    size_t i;
    int cmp;
    OVSREC_QOS_FOR_EACH(row, ovsdb_handler->idl) {
        for (i = 0; i < row->n_queues; i++) {
            cmp = uuid_equals(&queue->header_.uuid,
                              &row->value_queues[i]->header_.uuid);
            if (cmp) {
                /* found */
                *id = row->key_queues[i];
                return true;
            }
        }
    }
    return false;
}

static bool
find_queue_port(const struct ovsrec_queue *queue, const char **port_name)
{
    const struct ovsrec_port *row;
    const struct ovsrec_qos *qos;
    size_t q;
    int cmp;
    OVSREC_PORT_FOR_EACH(row, ovsdb_handler->idl) {
        qos = row->qos;
        if (qos == NULL) {
            continue;
        }
        for (q = 0; q < qos->n_queues; q++) {
            /* compare with resource-id */
            cmp = uuid_equals(&qos->value_queues[q]->header_.uuid,
                              &queue->header_.uuid);
            if (cmp) {
                *port_name = row->name;
                return true;
            }
        }
    }
    *port_name = NULL;
    return false;
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
    const char *resource_id, *port_name;
    int64_t id;

    ds_init(&string);
    OVSREC_QUEUE_FOR_EACH_SAFE(row, next, ovsdb_handler->idl) {
        resource_id = find_resid_generate(ovsdb_handler->resource_map,
                                          &row->header_.uuid);

        ds_put_format(&string,
                      "<queue><resource-id>%s</resource-id>",
                      resource_id);
        if (find_queue_id(row, &id)) {
            ds_put_format(&string, "<id>%"PRIi64"</id>", id);
        }
        find_queue_port(row, &port_name);
        if (port_name != NULL) {
            ds_put_format(&string, "<port>%s</port>", port_name);
        }
        ds_put_format(&string, "<properties>");
        find_and_append_smap_val(&row->other_config, "min-rate", "min-rate",
                                 &string);
        find_and_append_smap_val(&row->other_config, "max-rate", "max-rate",
                                 &string);
        ds_put_format(&string, "</properties></queue>");
    }
    return ds_steal_cstr(&string);
}

static void
dump_port_features(struct ds *s, uint32_t mask)
{
    int i;
    static const struct u32_str_map rates[] = {
        { ADVERTISED_10baseT_Half,       "10Mb-HD" },
        { ADVERTISED_10baseT_Full,       "10Mb-FD" },
        { ADVERTISED_100baseT_Half,      "100Mb-HD" },
        { ADVERTISED_100baseT_Full,      "100Mb-FD" },
        { ADVERTISED_1000baseT_Half,     "1Gb-HD" },
        { ADVERTISED_1000baseT_Full,     "1Gb-FD" },
        { ADVERTISED_1000baseKX_Full,    "1Gb-FD" },
//      { ADVERTISED_2500baseX_Full,     "2500baseX/Full" },
        { ADVERTISED_10000baseT_Full,    "10Gb" },
        { ADVERTISED_10000baseKX4_Full,  "10Gb" },
        { ADVERTISED_10000baseKR_Full,   "10Gb" },
//      { ADVERTISED_20000baseMLD2_Full, "20000baseMLD2/Full" },
//      { ADVERTISED_20000baseKR2_Full,  "20000baseKR2/Full" },
        { ADVERTISED_40000baseKR4_Full,  "40Gb" },
        { ADVERTISED_40000baseCR4_Full,  "40Gb" },
        { ADVERTISED_40000baseSR4_Full,  "40Gb" },
        { ADVERTISED_40000baseLR4_Full,  "40Gb" },
    };
    static const struct u32_str_map medium[] = {
        { ADVERTISED_TP,    "copper" },
        { ADVERTISED_FIBRE, "fiber" },
    };

    assert(s);

    /* dump rate elements */
    for (i = 0; i < (sizeof rates) / (sizeof rates[0]); i++) {
        if (rates[i].value & mask) {
            ds_put_format(s, "<rate>%s</rate>", rates[i].str);
        }
    }

    /* dump auto-negotiate element */
    ds_put_format(s, "<auto-negotiate>%s</auto-negotiate>",
                  ADVERTISED_Autoneg & mask ? "true" : "false");

    /* dump medium elements */
    for (i = 0; i < (sizeof medium) / (sizeof medium[0]); i++) {
        if (medium[i].value & mask) {
            ds_put_format(s, "<medium>%s</medium>", medium[i].str);
        }
    }

    /* dump pause element */
    if (ADVERTISED_Asym_Pause & mask) {
        ds_put_format(s, "<pause>asymmetric</pause>");
    } else if (ADVERTISED_Pause & mask) {
        ds_put_format(s, "<pause>symmetric</pause>");
    } else {
        ds_put_format(s, "<pause>unsupported</pause>");
    }
}

static unsigned int
dev_get_flags(const char* ifname)
{
    struct ifreq ethreq;
    strncpy(ethreq.ifr_name, ifname, sizeof ethreq.ifr_name);

    if (ioctl(ioctlfd, SIOCGIFFLAGS, &ethreq)) {
        nc_verb_error("ioctl %d on \"%s\" failed (%s)", SIOCGIFFLAGS, ifname,
                      strerror(errno));
        return 0;
    }

    return ethreq.ifr_flags;
}

static int
dev_set_flags(const char* ifname, unsigned int flags)
{
    struct ifreq ethreq;
    strncpy(ethreq.ifr_name, ifname, sizeof ethreq.ifr_name);

    ethreq.ifr_flags = flags;
    if (ioctl(ioctlfd, SIOCSIFFLAGS, &ethreq)) {
        nc_verb_error("ioctl %d on \"%s\" failed (%s)", SIOCSIFFLAGS, ifname,
                      strerror(errno));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


static char *
get_ports_config(const struct ovsrec_bridge *bridge)
{
    const struct ovsrec_interface *row;
    size_t port_it, ifc;
    struct ds string;
    struct vconn *vconnp;
    const char *bridge_name = bridge->name;
    struct ofpbuf *of_ports = NULL;
    struct ofputil_phy_port *of_port = NULL;
    enum ofputil_port_config c;
    if (ofc_of_open_vconn(bridge_name, &vconnp) == true) {
        of_ports = ofc_of_get_ports(vconnp);
    } else {
        nc_verb_error("OpenFlow: could not connect to '%s' bridge.",
                      bridge_name);
    }
    ds_init(&string);

    /* iterate over all interfaces of all ports */
    for (port_it = 0; port_it < bridge->n_ports; port_it++) {
        for (ifc = 0; ifc < bridge->ports[port_it]->n_interfaces; ifc++) {
            row = bridge->ports[port_it]->interfaces[ifc];

            ds_put_format(&string, "<port>");
            ds_put_format(&string, "<name>%s</name>", row->name);
            ds_put_format(&string, "<requested-number>%" PRIu64 "</requested-number>",
                          (row->n_ofport_request > 0 ? row->ofport_request[0] : 0));
            ds_put_format(&string, "<configuration>");

            /* get interface status */
            of_port = ofc_of_getport_byname(of_ports, row->name);
            if (of_port != NULL) {
                c = of_port->config;
                ds_put_format(&string, "<admin-state>%s</admin-state>"
                              "<no-receive>%s</no-receive>"
                              "<no-forward>%s</no-forward>"
                              "<no-packet-in>%s</no-packet-in>",
                              (c & OFPUTIL_PC_PORT_DOWN ? "down" : "up"),
                              OFC_PORT_CONF_BIT(c, OFPUTIL_PC_NO_RECV),
                              OFC_PORT_CONF_BIT(c, OFPUTIL_PC_NO_FWD),
                              OFC_PORT_CONF_BIT(c, OFPUTIL_PC_NO_PACKET_IN));
            } else {
                /* port was not found in OpenFlow reply, but we have ethtool */
                ds_put_format(&string, "<admin-state>%s</admin-state>",
                              (dev_get_flags(row->name) & IFF_UP) ? "up" : "down");
            }

            ds_put_format(&string, "</configuration>");

            /* get interface features via ioctl() */
            struct ifreq ethreq;
            struct ethtool_cmd ecmd;
            memset(&ethreq, 0, sizeof ethreq);
            strncpy(ethreq.ifr_name, row->name, sizeof ethreq.ifr_name);
            memset(&ecmd, 0, sizeof ecmd);
            ecmd.cmd = ETHTOOL_GSET;
            ethreq.ifr_data = &ecmd;
            ioctl(ioctlfd, SIOCETHTOOL, &ethreq);
            ds_put_format(&string, "<features><advertised>");
            dump_port_features(&string, ecmd.advertising);
            ds_put_format(&string, "</advertised></features>");

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
        }
    }
    if (of_ports != NULL) {
        ofpbuf_delete(of_ports);
    }
    if (vconnp != NULL) {
        vconn_close(vconnp);
    }
    return ds_steal_cstr(&string);
}

static char *
get_ports_state(const struct ovsrec_bridge *bridge)
{
    const struct ovsrec_interface *row;
    size_t port_it, ifc;
    struct ds string;
    struct vconn *vconnp;
    const char *bridge_name = bridge->name;
    struct ofpbuf *of_ports = NULL;
    struct ofputil_phy_port *of_port = NULL;

    if (ofc_of_open_vconn(bridge_name, &vconnp) == true) {
        of_ports = ofc_of_get_ports(vconnp);
    } else {
        nc_verb_error("OpenFlow: could not connect to '%s' bridge.",
                      bridge_name);
    }
    ds_init(&string);

    /* iterate over all interfaces of all ports */
    for (port_it = 0; port_it < bridge->n_ports; port_it++) {
        for (ifc = 0; ifc < bridge->ports[port_it]->n_interfaces; ifc++) {
            row = bridge->ports[port_it]->interfaces[ifc];

            /* get interface status via ioctl() */
            struct ifreq ethreq;
            struct ethtool_cmd ecmd;
            memset(&ethreq, 0, sizeof ethreq);
            memset(&ecmd, 0, sizeof ecmd);
            strncpy(ethreq.ifr_name, row->name, sizeof ethreq.ifr_name);
            ecmd.cmd = ETHTOOL_GSET;
            ethreq.ifr_data = &ecmd;
            ioctl(ioctlfd, SIOCETHTOOL, &ethreq);

            ds_put_format(&string, "<port>");
            ds_put_format(&string, "<name>%s</name>", row->name);
            ds_put_format(&string, "<number>%" PRIu64 "</number>",
                    (row->n_ofport > 0 ? row->ofport[0] : 0));
            of_port = ofc_of_getport_byname(of_ports, row->name);
            if (of_port != NULL) {
                ds_put_format(&string, "<current-rate>%"PRIu32"</current-rate>"
                        "<max-rate>%"PRIu32"</max-rate>",
                        of_port->curr_speed, of_port->max_speed);
            }
            ds_put_format(&string, "<state>");
            ds_put_format(&string, "<oper-state>%s</oper-state>",
                    (row->link_state != NULL ? row->link_state : "down"));

            find_and_append_smap_val(&row->other_config, "stp_state", "blocked",
                    &string);
            if (of_port != NULL) {
                ds_put_format(&string, "<live>%s</live>",
                        OFC_PORT_CONF_BIT(of_port->state, OFPUTIL_PS_LIVE));
            }

            ds_put_format(&string, "</state>");

            ds_put_format(&string, "<features><current>");
            /* rate
             * - get speed and convert it with duplex value to OFPortRateType
             */
            switch ((ecmd.speed_hi << 16) | ecmd.speed) {
                case 10:
                    ds_put_format(&string, "<rate>10Mb");
                    break;
                case 100:
                    ds_put_format(&string, "<rate>100Mb");
                    break;
                case 1000:
                    ds_put_format(&string, "<rate>1Gb");
                    break;
                case 10000:
                    ds_put_format(&string, "<rate>10Gb");
                    ecmd.duplex = DUPLEX_FULL + 1; /* do not print duplex suffix */
                    break;
                case 40000:
                    ds_put_format(&string, "<rate>40Gb");
                    ecmd.duplex = DUPLEX_FULL + 1; /* do not print duplex suffix */
                    break;
                default:
                    ds_put_format(&string, "<rate>");
                    ecmd.duplex = DUPLEX_FULL + 1; /* do not print duplex suffix */
            }
            switch (ecmd.duplex) {
                case DUPLEX_HALF:
                    ds_put_format(&string, "-HD</rate>");
                    break;
                case DUPLEX_FULL:
                    ds_put_format(&string, "-FD</rate>");
                    break;
                default:
                    ds_put_format(&string, "</rate>");
                    break;
            }

            /* auto-negotiation */
            ds_put_format(&string, "<auto-negotiate>%s</auto-negotiate>",
                    ecmd.autoneg ? "true" : "false");
            /* medium */
            switch(ecmd.port) {
                case PORT_TP:
                    ds_put_format(&string, "<medium>copper</medium>");
                    break;
                case PORT_FIBRE:
                    ds_put_format(&string, "<medium>fiber</medium>");
                    break;
            }

            /* pause is filled with the same value as in advertised */
            if (ADVERTISED_Asym_Pause & ecmd.advertising) {
                ds_put_format(&string, "<pause>asymmetric</pause>");
            } else if (ADVERTISED_Pause & ecmd.advertising) {
                ds_put_format(&string, "<pause>symmetric</pause>");
            } else {
                ds_put_format(&string, "<pause>unsupported</pause>");
            }

            ds_put_format(&string, "</current><supported>");
            dump_port_features(&string, ecmd.supported);
            ds_put_format(&string, "</supported><advertised-peer>");
            dump_port_features(&string, ecmd.lp_advertising);
            ds_put_format(&string, "</advertised-peer></features>");

            ds_put_format(&string, "</port>");
        }
    }
    if (of_ports != NULL) {
        ofpbuf_delete(of_ports);
    }
    if (vconnp != NULL) {
        vconn_close(vconnp);
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
    const char *resource_id;

    parse_target_to_addr(target, &protocol, &address, &port);
    resource_id = find_resid_generate(ovsdb_handler->resource_map,
                                      &row->header_.uuid);

    ds_put_format(string, "<controller>");
    ds_put_format(string, "<id>%s</id>", resource_id);
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
    OVSREC_BRIDGE_FOR_EACH_SAFE(row, next, ovsdb_handler->idl) {
        /* char *uuid = print_uuid(&row->header_.uuid); */
        /* ds_put_format(&string, "<resource-id>%s</resource-id>", uuid);
         * free(uuid); */
        ds_put_format(&string, "<switch>");
        ds_put_format(&string, "<id>%s</id>", row->name);
        ds_put_format(&string, "<capabilities><max-buffered-packets>%d"
                      "</max-buffered-packets>", 256);
        ds_put_format(&string, "<max-tables>%d</max-tables>", 255);
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

static void
append_resource_refs(struct ds *string, struct ovsdb_idl_row **h,
                     size_t count, const char *elem)
{
    size_t i;
    const char *resource_id;
    if (count > 0) {
        for (i = 0; i < count; ++i) {
            resource_id = find_resid_generate(ovsdb_handler->resource_map,
                                              &h[i]->uuid);
            ds_put_format(string, "<%s>%s</%s>", elem, resource_id, elem);
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
    OVSREC_BRIDGE_FOR_EACH_SAFE(row, next, ovsdb_handler->idl) {
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
            ds_put_format(&string, "<port>%s</port>", port->name);
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

static char *
get_owned_certificates_config(void)
{
    struct ds str;
    off_t size;
    char* pem, *pem_start, *pem_end;
    const struct ovsrec_ssl *ssl;
    int fd;

    ds_init(&str);

    ssl = ovsrec_ssl_first(ovsdb_handler->idl);
    if (ssl != NULL && ssl->certificate != NULL && ssl->private_key != NULL) {
        /* missing resource-id */
        if (ovsdb_handler->cert_map->owned_resid == NULL) {
            ovsdb_handler->cert_map->owned_resid = strdup(print_uuid_ro(&ssl->header_.uuid));
        }
        if (uuid_equals(&ovsdb_handler->cert_map->uuid, &ssl->header_.uuid)) {
            ds_destroy(&str);
            return NULL;
        }

        ds_put_format(&str, "<owned-certificate>");
        ds_put_format(&str, "<resource-id>%s</resource-id>", ovsdb_handler->cert_map->owned_resid);

        /* certificate */
        fd = open(ssl->certificate, O_RDONLY);
        if (fd == -1) {
            ds_destroy(&str);
            return NULL;
        }
        size = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);
        pem = malloc(size+1);
        if (pem == NULL) {
            ds_destroy(&str);
            close(fd);
            return NULL;
        }
        if (read(fd, pem, size) < size) {
            ds_destroy(&str);
            close(fd);
            free(pem);
            return NULL;
        }
        close(fd);
        pem[size] = '\0';

        pem_start = strstr(pem, "-----BEGIN CERTIFICATE-----\n");
        pem_end = strstr(pem, "\n-----END CERTIFICATE-----");
        if (pem_start == NULL || pem_end == NULL) {
            ds_destroy(&str);
            free(pem);
            return NULL;
        }
        pem_start += 28;
        *pem_end = '\0';

        ds_put_format(&str, "<certificate>%s</certificate>", pem_start);
        free(pem);

        /* private-key */
        ds_put_format(&str, "<private-key>");

        fd = open(ssl->private_key, O_RDONLY);
        if (fd == -1) {
            ds_destroy(&str);
            return NULL;
        }
        size = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);
        pem = malloc(size+1);
        if (pem == NULL) {
            ds_destroy(&str);
            close(fd);
            return NULL;
        }
        if (read(fd, pem, size) < size) {
            ds_destroy(&str);
            close(fd);
            free(pem);
            return NULL;
        }
        close(fd);
        pem[size] = '\0';

        pem_start = strstr(pem, "-----BEGIN RSA PRIVATE KEY-----\n");
        pem_end = strstr(pem, "\n-----END RSA PRIVATE KEY-----");
        if (pem_start == NULL) {
            pem_start = strstr(pem, "-----BEGIN DSA PRIVATE KEY-----\n");
            pem_end = strstr(pem, "\n-----END DSA PRIVATE KEY-----");
        }
        if (pem_start == NULL || pem_end == NULL) {
            ds_destroy(&str);
            free(pem);
            return NULL;
        }
        pem_start += 32;
        *pem_end = '\0';
        pem_end += 10;
        *(pem_end+3) = '\0';

        ds_put_format(&str, "<key-type>%s</key-type>", pem_end);
        ds_put_format(&str, "<key-data>%s</key-data>", pem_start);
        free(pem);
        ds_put_format(&str, "</private-key>");

        ds_put_format(&str, "</owned-certificate>");
    }

    return ds_steal_cstr(&str);
}

static char *
get_external_certificates_config(void)
{
    struct ds str;
    off_t size;
    char* pem, *pem_start, *pem_end;
    const struct ovsrec_ssl *ssl;
    int fd;

    ds_init(&str);

    ssl = ovsrec_ssl_first(ovsdb_handler->idl);
    if (ssl != NULL &&
            ssl->ca_cert != NULL) {
        /* missing resource-id */
        if (ovsdb_handler->cert_map->external_resid == NULL) {
            ovsdb_handler->cert_map->external_resid = strdup(print_uuid_ro(&ssl->header_.uuid));
        }
        if (uuid_equals(&ovsdb_handler->cert_map->uuid, &ssl->header_.uuid)) {
            ds_destroy(&str);
            return NULL;
        }

        ds_put_format(&str, "<external-certificate>");
        ds_put_format(&str, "<resource-id>%s</resource-id>", ovsdb_handler->cert_map->external_resid);

        /* certificate (ca_cert) */
        fd = open(ssl->ca_cert, O_RDONLY);
        if (fd == -1) {
            ds_destroy(&str);
            return NULL;
        }
        size = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);
        pem = malloc(size+1);
        if (pem == NULL) {
            ds_destroy(&str);
            close(fd);
            return NULL;
        }
        if (read(fd, pem, size) < size) {
            ds_destroy(&str);
            close(fd);
            free(pem);
            return NULL;
        }
        close(fd);
        pem[size] = '\0';

        pem_start = strstr(pem, "-----BEGIN CERTIFICATE-----\n");
        pem_end = strstr(pem, "\n-----END CERTIFICATE-----");
        if (pem_start == NULL || pem_end == NULL) {
            ds_destroy(&str);
            free(pem);
            return NULL;
        }
        pem_start += 28;
        *pem_end = '\0';

        ds_put_format(&str, "<certificate>%s</certificate>", pem_start);
        free(pem);

        ds_put_format(&str, "</external-certificate>");
    }

    return ds_steal_cstr(&str);
}

/* synchronize local copy of OVSDB */
static void
ofconf_update(ovsdb_t *p)
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
ofc_get_config_data(void)
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

    const char *id;
    char *queues;
    char *ports;
    char *flow_tables;
    char *bridges;
    char *owned_certificates;
    char *external_certificates;
    const struct ovsrec_bridge *bridge;
    struct ds ports_ds;

    if (ovsdb_handler == NULL) {
        return NULL;
    }

    ds_init(&ports_ds);

    id = (const char*)ofc_get_switchid();
    if (!id) {
        /* no id -> no data */
        return strdup("");
    }

    if (ovsdb_handler == NULL) {
        return NULL;
    }
    ofconf_update(ovsdb_handler);
    queues = get_queues_config();
    if (queues == (NULL)) {
        queues = strdup("");
    }
    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        ports = get_ports_config(bridge);
        if (ports == NULL) {
            strdup("");
        }
        ds_put_format(&ports_ds, "%s", ports);
        free(ports);
    }
    flow_tables = get_flow_tables_config();
    if (flow_tables == (NULL)) {
        flow_tables = strdup("");
    } bridges = get_bridges_config();
    if (bridges == (NULL)) {
        bridges = strdup("");
    } owned_certificates = get_owned_certificates_config();
    if (owned_certificates == (NULL)) {
        owned_certificates = strdup("");
    } external_certificates = get_external_certificates_config();
    if (external_certificates == (NULL)) {
        external_certificates = strdup("");
    }

    ds_init(&state_data);

    ds_put_format(&state_data, config_data_format, id, ds_cstr(&ports_ds), queues,
                  flow_tables, owned_certificates, external_certificates,
                  bridges);

    free(queues);
    ds_destroy(&ports_ds);
    free(flow_tables);
    free(bridges);
    free(owned_certificates);
    free(external_certificates);

    return ds_steal_cstr(&state_data);
}

char *
ofc_get_state_data(void)
{
    const char *state_data_format = "<?xml version=\"1.0\"?>"
        "<capable-switch xmlns=\"urn:onf:config:yang\">"
        "<config-version>%s</config-version>"
        "<resources>%s%s</resources>"
        "<logical-switches>%s</logical-switches></capable-switch>";

    char *ports;
    char *flow_tables;
    char *bridges;
    const struct ovsrec_bridge *bridge;

    struct ds state_data;
    struct ds ports_ds;

    if (ovsdb_handler == NULL) {
        return NULL;
    }
    ds_init(&ports_ds);
    ofconf_update(ovsdb_handler);

    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        ports = get_ports_state(bridge);
        if (ports == NULL) {
            strdup("");
        }
        ds_put_format(&ports_ds, "%s", ports);
        free(ports);
    }
    flow_tables = get_flow_tables_state();
    if (flow_tables == (NULL)) {
        flow_tables = strdup("");
    }
    bridges = get_bridges_state();
    if (bridges == (NULL)) {
        bridges = strdup("");
    }

    ds_init(&state_data);

    ds_put_format(&state_data, state_data_format, "1.2", ds_cstr(&ports_ds),
                  flow_tables, bridges);

    free(flow_tables);
    free(bridges);
    ds_destroy(&ports_ds);

    return ds_steal_cstr(&state_data);
}

bool
ofc_init(const char *ovs_db_path)
{
    ovsdb_t *p = calloc(1, sizeof *p);

    if (p == NULL) {
        /* failed */
        return false;
    }
    /* create new resource-id map of 1024 elements, it will grow when needed */
    p->resource_map = ofc_resmap_init(1024);
    if (p->resource_map == NULL) {
        free(p);
        return false;
    }

    p->cert_map = calloc(1, sizeof(struct ofc_resmap_certificate));
    if (p->cert_map == NULL) {
        free(p);
        return false;
    }

    ovsrec_init();
    p->idl = ovsdb_idl_create(ovs_db_path, &ovsrec_idl_class, true, true);
    p->txn = NULL;
    p->seqno = ovsdb_idl_get_seqno(p->idl);
    ofconf_update(p);
    ovsdb_handler = p;

    /* prepare descriptor to perform ioctl() */
    ioctlfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);

    return true;
}

void
ofc_destroy(void)
{
    if (ovsdb_handler != NULL) {
        /* close everything */
        ovsdb_idl_destroy(ovsdb_handler->idl);

        ofc_resmap_destroy(&ovsdb_handler->resource_map);
        free(ovsdb_handler->cert_map->owned_resid);
        free(ovsdb_handler->cert_map->external_resid);
        free(ovsdb_handler->cert_map);

        free(ovsdb_handler);
        ovsdb_handler = NULL;
    }

    if (ioctlfd != -1) {
        close(ioctlfd);
        ioctlfd = -1;
    }
}

/*
 * Start a new transaction on 'ovsdb_handler'. There can be only a single
 * active transaction at a time.
 */
void
txn_init(void)
{
    ovsdb_handler->txn = ovsdb_idl_txn_create(ovsdb_handler->idl);
}

/*
 * Abort the transaction being prepared.
 */
void
txn_abort(void)
{
    /* cleanup */
    if (ovsdb_handler && ovsdb_handler->txn) {
        ovsdb_idl_txn_destroy(ovsdb_handler->txn);
        ovsdb_handler->txn = NULL;
    }
}

/*
 * Finish the current transaction on 'ovsdb_handler'.
 */
int
txn_commit(struct nc_err **e)
{
    const char *errmsg;
    int ret = EXIT_SUCCESS;
    enum ovsdb_idl_txn_status status;

    status = ovsdb_idl_txn_commit_block(ovsdb_handler->txn);

    switch (status) {
    case TXN_SUCCESS:
        ofc_resmap_update_uuids(ovsdb_handler->resource_map);
        nc_verb_verbose("OVSDB transaction successful");
        break;
    case TXN_UNCHANGED:
        nc_verb_verbose("OVSDB unchanged");
        break;
    default:
        /* error */
        ret = EXIT_FAILURE;
        *e = nc_err_new(NC_ERR_OP_FAILED);

        switch (status) {
        case TXN_UNCOMMITTED:
        case TXN_INCOMPLETE:
            nc_verb_error("Invalid OVSDB transaction");
            nc_err_set(*e, NC_ERR_PARAM_MSG, "Invalid OVSDB transaction");
            break;
        case TXN_ABORTED:
            /* Should not happen--we never call ovsdb_idl_txn_abort(). */
            nc_verb_error("OVSDB transaction aborted");
            nc_err_set(*e, NC_ERR_PARAM_MSG, "OVSDB transaction aborted");
            break;
        case TXN_TRY_AGAIN:
        case TXN_ERROR:
            nc_verb_error("OVSDB transaction failed (%s)",
                          errmsg = ovsdb_idl_txn_get_error(ovsdb_handler->txn));
            nc_err_set(*e, NC_ERR_PARAM_MSG, errmsg);
            break;
        case TXN_NOT_LOCKED:
            /* Should not happen--we never call ovsdb_idl_set_lock(). */
            nc_verb_error("OVSDB not locked");
            nc_err_set(*e, NC_ERR_PARAM_MSG, "OVSDB not locked");
            break;
        default:
            nc_verb_error("Unknown OVSDB result (%d)", status);
            nc_err_set(*e, NC_ERR_PARAM_MSG, "Unknown OVSDB result");
        }
    }

    /* cleanup */
    txn_abort();

    return ret;
}

void
txn_del_all(void)
{
    const struct ovsrec_open_vswitch *ovs;
    const struct ovsrec_flow_sample_collector_set *fscs;
    const struct ovsrec_qos *qos;
    const struct ovsrec_queue *queue;
    const struct ovsrec_port *port;
    const struct ovsrec_ssl *ssl;
    nc_verb_verbose("Delete all (%s:%d)", __FILE__, __LINE__);

    /* remove all settings - we need only to remove two base tables
     * Open_vSwitch and Flow_Sample_Collector_set, the rest will be done by
     * garbage collection
     */
    OVSREC_OPEN_VSWITCH_FOR_EACH(ovs, ovsdb_handler->idl) {
        ovsrec_open_vswitch_set_bridges(ovs, NULL, 0);
        ovsrec_open_vswitch_set_ssl(ovs, NULL);
        ovsrec_open_vswitch_delete(ovs);
    }

    OVSREC_FLOW_SAMPLE_COLLECTOR_SET_FOR_EACH(fscs, ovsdb_handler->idl) {
        ovsrec_flow_sample_collector_set_delete(fscs);
    }

    /* sometimes, Queue is not removed by OVSDB... commit and delete rest */
    txn_commit(NULL);
    txn_init();

    OVSREC_QOS_FOR_EACH(qos, ovsdb_handler->idl) {
        ovsrec_qos_set_queues(qos, NULL, NULL, 0);
        ovsrec_qos_delete(qos);
    }

    OVSREC_QUEUE_FOR_EACH(queue, ovsdb_handler->idl) {
        ovsrec_queue_delete(queue);
    }

    OVSREC_PORT_FOR_EACH(port, ovsdb_handler->idl) {
        ovsrec_port_set_qos(port, NULL);
        ovsrec_port_delete(port);
    }

    OVSREC_SSL_FOR_EACH(ssl, ovsdb_handler->idl) {
        ovsrec_ssl_delete(ssl);
    }
}

void
txn_del_port(const xmlChar *port_name)
{
    const struct ovsrec_port *port;

    if (!port_name) {
        return;
    }

    OVSREC_PORT_FOR_EACH(port, ovsdb_handler->idl) {
        if (xmlStrEqual(port_name, BAD_CAST port->name)) {
            ovsrec_port_delete(port);
            break;
        }
    }
}

/* /capable-switch/resources/port/requested-number */
void
txn_mod_port_reqnumber(const xmlChar *port_name, const xmlChar* value)
{
    int64_t rn;
    const struct ovsrec_interface *iface;

    if (!port_name) {
        return;
    }

    /* get correct interface table row */
    OVSREC_INTERFACE_FOR_EACH(iface, ovsdb_handler->idl) {
        if (xmlStrEqual(port_name, BAD_CAST iface->name)) {
            break;
        }
    }

    if (iface) {
        ovsrec_interface_verify_ofport_request(iface);
        if (value) {
            rn = strtol((char*)value, NULL, 10);
            ovsrec_interface_set_ofport_request(iface, &rn, 1);
        } else {
            ovsrec_interface_set_ofport_request(iface, NULL, 0);
        }
    }
}

void
txn_mod_port_admin_state(const xmlChar *port_name, const xmlChar* value)
{
    unsigned int flags;


    if (!value) {
        /* delete -> set default value (up) */
        flags = dev_get_flags((char*) port_name) & IFF_UP;
        dev_set_flags((char*) port_name, flags);
    } else {
        if (xmlStrEqual(value, BAD_CAST "up")) {
            flags = dev_get_flags((char*) port_name) & IFF_UP;
        } else if (xmlStrEqual(value, BAD_CAST "down")) {
            flags = dev_get_flags((char*) port_name) & ~IFF_UP;
        } else {
            /*
            *e = nc_err_new(NC_ERR_INVALID_VALUE);
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "admin-state");
            return EXIT_FAILURE;
            */
            return;
        }
        dev_set_flags((char*) port_name, flags);
    }
}

void
txn_add_port(xmlNodePtr node)
{
    xmlNodePtr aux, leaf;
    xmlChar *xmlval, *port_name, *bridge_name;
    struct ovsrec_port *port;
    struct ovsrec_interface *iface;

    if (!node) {
        return;
    }

    /* prepare new structures to set content of the port configuration */
    port = ovsrec_port_insert(ovsdb_handler->txn);
    iface = ovsrec_interface_insert(ovsdb_handler->txn);
    ovsrec_port_set_interfaces(port, (struct ovsrec_interface**)&iface, 1);

    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(aux->name, BAD_CAST "name")) {
            xmlval = xmlNodeGetContent(aux);
            ovsrec_interface_verify_name(iface);
            ovsrec_interface_set_name(iface, (char*) xmlval);
            ovsrec_port_verify_name(port);
            ovsrec_port_set_name(port, (char*) xmlval);
            xmlFree(xmlval);
        } else if (xmlStrEqual(aux->name, BAD_CAST "requested-number")) {
            xmlval = xmlNodeGetContent(aux);
            txn_mod_port_reqnumber(BAD_CAST iface->name, xmlval);
            xmlFree(xmlval);
        } else if (xmlStrEqual(aux->name, BAD_CAST "configuration")) {
            for (leaf = aux->children; leaf; leaf = leaf->next) {
                if (xmlStrEqual(leaf->name, BAD_CAST "no-receive")
                           || xmlStrEqual(leaf->name, BAD_CAST "no-packet-in")
                           || xmlStrEqual(leaf->name, BAD_CAST "no-forward")
                           || xmlStrEqual(leaf->name, BAD_CAST "admin-state")) {
                    nc_verb_verbose("txn_add_port: no-receive, no-forward, no-packet-in");
                    port_name = xmlNodeGetContent(go2node(leaf->parent->parent, BAD_CAST "name"));
                    bridge_name = ofc_find_bridge_for_port_iterative(port_name);
                    if (bridge_name != NULL) {
                        ofc_of_mod_port(bridge_name, port_name, leaf->name, xmlNodeGetContent(leaf));
                    }
                    xmlFree(port_name);
                } else {
                    nc_verb_error("unexpected element '%s'", leaf->name);
                }
            }
        } else if (xmlStrEqual(aux->name, BAD_CAST "ipgre-tunnel")
                   || xmlStrEqual(aux->name, BAD_CAST "vxlan-tunnel")
                   || xmlStrEqual(aux->name, BAD_CAST "tunnel")) {
            port_name = xmlNodeGetContent(go2node(aux->parent, BAD_CAST "name"));
            txn_mod_port_add_tunnel(port_name, aux);
            xmlFree(port_name);
        }
        /* TODO features */
    }
}

void
txn_add_queue(xmlNodePtr node)
{
    xmlNodePtr aux, prop;
    xmlChar *port_name = NULL, *id = NULL, *min_rate = NULL, *max_rate = NULL;
    xmlChar *resource_id = NULL;
    struct ovsrec_qos *qos;
    struct ovsrec_queue *queue;
    const struct ovsrec_queue *queue_resid;
    const struct ovsrec_port *port;

    if (!node) {
        return;
    }

    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(aux->name, BAD_CAST "resource-id")) {
            resource_id = xmlNodeGetContent(aux);
        } else if (xmlStrEqual(aux->name, BAD_CAST "id")) {
            id = xmlNodeGetContent(aux);
        } else if (xmlStrEqual(aux->name, BAD_CAST "port")) {
            port_name = xmlNodeGetContent(aux);
        } else if (xmlStrEqual(aux->name, BAD_CAST "properties")) {
            for (prop = aux->children; prop; prop = prop->next) {
                if (xmlStrEqual(prop->name, BAD_CAST "min-rate")) {
                    min_rate = xmlNodeGetContent(prop);
                } else if (xmlStrEqual(prop->name, BAD_CAST "max-rate")) {
                    max_rate = xmlNodeGetContent(prop);
                }
            }
        }
    }
    nc_verb_verbose("Add queue %s to %s.", BAD_CAST resource_id, BAD_CAST port_name);

    /* TODO check if exists */

    /* create new */
    qos = ovsrec_qos_insert(ovsdb_handler->txn);
    queue = ovsrec_queue_insert(ovsdb_handler->txn);
    queue_resid = ovsrec_queue_first(ovsdb_handler->idl);

    ofc_resmap_insert(ovsdb_handler->resource_map, (const char *) resource_id,
                      &queue_resid->header_.uuid,
                      &queue_resid->header_);
    int64_t key;
    if (sscanf((char *) id, "%"SCNi64, &key) != 1) {
        /* parsing error, wrong number */
    }
    ovsrec_qos_verify_queues(qos);
    ovsrec_qos_set_queues(qos, (int64_t *) &key, (struct ovsrec_queue **) &queue, 1);
    OVSREC_PORT_FOR_EACH(port, ovsdb_handler->idl) {
        if (xmlStrEqual(port_name, BAD_CAST port->name)) {
            ovsrec_port_verify_qos(port);
            ovsrec_port_set_qos(port, qos);
            break;
        }
    }
    if (max_rate != NULL) {
        smap_add_once(&queue->other_config, "max-rate", (char *) max_rate);
        xmlFree(max_rate);
    }
    if (min_rate != NULL) {
        smap_add_once(&queue->other_config, "min-rate", (char *) min_rate);
        xmlFree(min_rate);
    }
    ovsrec_queue_verify_other_config(queue);
    ovsrec_queue_set_other_config(queue, &queue->other_config);
}

xmlChar *
ofc_find_bridge_for_flowtable(xmlNodePtr root, xmlChar *flowtable)
{
    xmlXPathContextPtr xpathCtx;
    xmlXPathObjectPtr xpathObj;
    xmlDocPtr doc;
    char *xpathexpr = NULL;
    xmlChar *bridge_name = NULL;
    int size;
    if (root == NULL) {
        return NULL;
    }
    doc = root->doc;
    xpathexpr = xasprintf("//ofc:switch/ofc:resources/ofc:flow-table['%s']/../../ofc:id[1]",
                          (const char *) flowtable);
    /* Create xpath evaluation context */
    xpathCtx = xmlXPathNewContext(doc);
    if(xpathCtx == NULL) {
        nc_verb_error("Unable to create new XPath context");
        goto cleanup;
    }

    if (xmlXPathRegisterNs(xpathCtx, BAD_CAST "ofc", BAD_CAST "urn:onf:config:yang")) {
        nc_verb_error("Registering a namespace for XPath failed (%s).", __func__);
        goto cleanup;
    }

    /* Evaluate xpath expression */
    xpathObj = xmlXPathEvalExpression(BAD_CAST xpathexpr, xpathCtx);
    if(xpathObj == NULL) {
        nc_verb_error("Unable to evaluate xpath expression \"%s\"", xpathexpr);
        goto cleanup;
    }

    /* Print results */
    size = (xpathObj->nodesetval) ? xpathObj->nodesetval->nodeNr : 0;
    if (size == 1) {
        if (xpathObj->nodesetval->nodeTab[0]
            && xpathObj->nodesetval->nodeTab[0]->children
            && xpathObj->nodesetval->nodeTab[0]->children->content) {
            bridge_name = xmlNodeGetContent(xpathObj->nodesetval->nodeTab[0]);
        }
    }

cleanup:
    /* Cleanup, use bridge_name that was initialized or set */
    xmlXPathFreeObject(xpathObj);
    xmlXPathFreeContext(xpathCtx);
    return bridge_name;
}

void
txn_add_flow_table(xmlNodePtr node)
{
    xmlNodePtr aux;
    xmlChar *resource_id = NULL;
    xmlChar *name = NULL;
    xmlChar *table_id_txt, *bridge_name;
    int64_t table_id;
    struct ovsrec_flow_table *flowtable;
    const struct ovsrec_flow_table *flowtable_resid;
    const struct ovsrec_bridge *bridge;

    if (!node) {
        return;
    }

    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(aux->name, BAD_CAST "resource-id")) {
            resource_id = xmlNodeGetContent(aux);
        } else if (xmlStrEqual(aux->name, BAD_CAST "table-id")) {
            table_id_txt = xmlNodeGetContent(aux);
            if (sscanf((const char *) table_id_txt, "%"SCNi64, &table_id) != 1) {
                /* parsing error, wrong number */
            }
            xmlFree(table_id_txt);
        } else if (xmlStrEqual(aux->name, BAD_CAST "name")) {
            name = xmlNodeGetContent(aux);
        }
    }
    nc_verb_verbose("Add flow table %s with %s name.", BAD_CAST resource_id, BAD_CAST name);

    flowtable = ovsrec_flow_table_insert(ovsdb_handler->txn);
    flowtable_resid = ovsrec_flow_table_first(ovsdb_handler->idl);

    ofc_resmap_insert(ovsdb_handler->resource_map, (const char *) resource_id,
                      &flowtable_resid->header_.uuid,
                      &flowtable_resid->header_);
    ovsrec_flow_table_set_name(flowtable, (const char *) name);
    xmlFree(name);

    /* TODO check if exists */

    bridge_name = ofc_find_bridge_for_flowtable(node, resource_id);
    if (bridge_name == NULL) {
        xmlFree(resource_id);
        return;
    }
    xmlFree(resource_id);
    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        if (xmlStrEqual(bridge_name, BAD_CAST bridge->name)) {
            /* TODO replace with append */
            ovsrec_bridge_set_flow_tables(bridge, (const int64_t *) &table_id,
                    (struct ovsrec_flow_table **) &flowtable, 1);
        }
    }
}

void
txn_del_port_tunnel(const xmlChar *port_name, xmlNodePtr tunnel_node)
{
    const struct ovsrec_interface *ifc, *next, *found = NULL;
    nc_verb_verbose("Removing tunnel (%s:%d)", __FILE__, __LINE__);

    OVSREC_INTERFACE_FOR_EACH_SAFE(ifc, next, ovsdb_handler->idl) {
        if (!strncmp(ifc->name, (char *) port_name, strlen(ifc->name)+1)) {
            found = ifc;
            break;
        }
    }
    if (found == NULL) {
        /* not found */
        return;
    }

    ovsrec_interface_verify_options(found);
    ovsrec_interface_verify_type(found);
    smap_remove((struct smap *) &found->options, "local_ip");
    smap_remove((struct smap *) &found->options, "remote_ip");
    ovsrec_interface_set_options(found, &found->options);
    ovsrec_interface_set_type(found, "");
}

/* Remove port reference from the Bridge table */
void
txn_del_bridge_port(const xmlChar *br_name, const xmlChar *port_name)
{
    const struct ovsrec_bridge *bridge;
    struct ovsrec_port **ports;
    size_t i, j;

    if (!port_name || !br_name) {
        return;
    }

    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        if (xmlStrEqual(br_name, BAD_CAST bridge->name)) {
            break;
        }
    }
    if (!bridge) {
        return;
    }

    ports = malloc(sizeof *bridge->ports * (bridge->n_ports - 1));
    for (i = j = 0; j < bridge->n_ports; j++) {
        if (!xmlStrEqual(port_name, BAD_CAST bridge->ports[j]->name)) {
            ports[i] = bridge->ports[j];
            i++;
        }
    }
    ovsrec_bridge_verify_ports(bridge);
    ovsrec_bridge_set_ports(bridge, ports, bridge->n_ports - 1);
    free(ports);
}

void
txn_add_owned_certificate(xmlNodePtr node)
{
    const struct ovsrec_open_vswitch *ovs;
    const struct ovsrec_ssl *ssl;
    xmlNodePtr aux, leaf;
    xmlChar *xmlval;
    int fd, mod;
    char *key_type, *key_data;

    if (!node) {
        return;
    }

    /* prepare new structures to set content of the SSL configuration */
    ssl = ovsrec_ssl_first(ovsdb_handler->idl);
    if (!ssl) {
        mod = 0;
        ssl = ovsrec_ssl_insert(ovsdb_handler->txn);
    } else {
        mod = 1;
        if (memcmp(&ovsdb_handler->cert_map->uuid, &ssl->header_.uuid, sizeof(struct uuid))) {
            /* TODO error, this SSL table UUID does not match the saved one */
        }
    }

    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(aux->name, BAD_CAST "resource-id")) {
            xmlval = xmlNodeGetContent(aux);
            if (mod) {
                if (ovsdb_handler->cert_map->owned_resid != NULL &&
                    strcmp(ovsdb_handler->cert_map->owned_resid, (char*) xmlval)) {
                    xmlFree(xmlval);
                    /* TODO error, second owned-certificate item in the list, not allowed */
                }
            } else {
                if (ovsdb_handler->cert_map->owned_resid != NULL) {
                    /* TODO error, resid filled without any SSL table */
                }
                memcpy(&ovsdb_handler->cert_map->uuid, &ssl->header_.uuid, sizeof(struct uuid));
            }
            ovsdb_handler->cert_map->owned_resid = (char*) xmlval;
            xmlval = NULL;
        } else if (xmlStrEqual(aux->name, BAD_CAST "certificate")) {
            /* TODO error check */
            fd = creat(OFC_DATADIR "/cert.pem", 0644);
            write(fd, "-----BEGIN CERTIFICATE-----\n", 28);
            xmlval = xmlNodeGetContent(aux);
            write(fd, (char*) xmlval, xmlStrlen(xmlval));
            xmlFree(xmlval);
            write(fd, "\n-----END CERTIFICATE-----", 26);
            close(fd);
            ovsrec_ssl_verify_certificate(ssl);
            ovsrec_ssl_set_certificate(ssl, OFC_DATADIR "/cert.pem");
        } else if (xmlStrEqual(aux->name, BAD_CAST "private-key")) {
            for (leaf = aux->children; leaf; leaf = leaf->next) {
                if (xmlStrEqual(leaf->name, BAD_CAST "key-type")) {
                    key_type = (char*) xmlNodeGetContent(leaf);
                } else if (xmlStrEqual(leaf->name, BAD_CAST "key-data")) {
                    key_data = (char*) xmlNodeGetContent(leaf);
                }
            }

            /* TODO error check */
            fd = creat(OFC_DATADIR "/key.pem", 0600);
            write(fd, "-----BEGIN ", 11);
            write(fd, key_type, strlen(key_type));
            write(fd, " PRIVATE KEY-----\n", 18);
            write(fd, key_data, strlen(key_data));
            write(fd, "\n-----END ", 10);
            write(fd, key_type, strlen(key_type));
            write(fd, " PRIVATE KEY-----", 17);
            close(fd);
            free(key_type);
            free(key_data);
            ovsrec_ssl_verify_private_key(ssl);
            ovsrec_ssl_set_private_key(ssl, OFC_DATADIR "/key.pem");
        }
    }

    /* get the Open_vSwitch table for linking the SSL structure into */
    if (!mod) {
        ovs = ovsrec_open_vswitch_first(ovsdb_handler->idl);
        if (!ovs) {
            ovs = ovsrec_open_vswitch_insert(ovsdb_handler->txn);
        }
        ovsrec_open_vswitch_verify_ssl(ovs);
        ovsrec_open_vswitch_set_ssl(ovs, ssl);
    }
}

void
txn_add_external_certificate(xmlNodePtr node)
{
    const struct ovsrec_open_vswitch *ovs;
    const struct ovsrec_ssl *ssl;
    xmlNodePtr aux;
    xmlChar *xmlval;
    int fd, mod;

    if (!node) {
        return;
    }

    /* prepare new structures to set content of the SSL configuration */
    ssl = ovsrec_ssl_first(ovsdb_handler->idl);
    if (!ssl) {
        mod = 0;
        ssl = ovsrec_ssl_insert(ovsdb_handler->txn);
    } else {
        mod = 1;
        if (memcmp(&ovsdb_handler->cert_map->uuid, &ssl->header_.uuid, sizeof(struct uuid))) {
            /* TODO error, this SSL table UUID does not match the saved one */
        }
    }

    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(aux->name, BAD_CAST "resource-id")) {
            xmlval = xmlNodeGetContent(aux);
            if (mod) {
                if (ovsdb_handler->cert_map->external_resid != NULL &&
                        strcmp(ovsdb_handler->cert_map->external_resid, (char*) xmlval)) {
                    xmlFree(xmlval);
                    /* TODO error, second external-certificate item in the list, not allowed */
                }
            } else {
                if (ovsdb_handler->cert_map->external_resid != NULL) {
                    /* TODO error, resid filled without any SSL table */
                }
                memcpy(&ovsdb_handler->cert_map->uuid, &ssl->header_.uuid, sizeof(struct uuid));
            }
            ovsdb_handler->cert_map->external_resid = (char*) xmlval;
            xmlval = NULL;
        } else if (xmlStrEqual(aux->name, BAD_CAST "certificate")) {
            /* TODO error check */
            fd = creat(OFC_DATADIR "/ca_cert.pem", 0644);
            write(fd, "-----BEGIN CERTIFICATE-----\n", 28);
            xmlval = xmlNodeGetContent(aux);
            write(fd, (char*) xmlval, xmlStrlen(xmlval));
            xmlFree(xmlval);
            write(fd, "\n-----END CERTIFICATE-----", 26);
            close(fd);
            ovsrec_ssl_verify_ca_cert(ssl);
            ovsrec_ssl_set_ca_cert(ssl, OFC_DATADIR "/ca_cert.pem");
        }
    }

    /* get the Open_vSwitch table for linking the SSL structure into */
    if (!mod) {
        ovs = ovsrec_open_vswitch_first(ovsdb_handler->idl);
        if (!ovs) {
            ovs = ovsrec_open_vswitch_insert(ovsdb_handler->txn);
        }
        ovsrec_open_vswitch_verify_ssl(ovs);
        ovsrec_open_vswitch_set_ssl(ovs, ssl);
    }
}

void
txn_del_owned_certificate(xmlNodePtr node)
{
    const struct ovsrec_open_vswitch *ovs;
    const struct ovsrec_ssl *ssl;
    xmlNodePtr aux;
    xmlChar *xmlval;

    if (!node) {
        return;
    }

    ssl = ovsrec_ssl_first(ovsdb_handler->idl);
    if (!ssl) {
        /* TODO error, cannot delete, no SSL table */
    }
    if (memcmp(&ovsdb_handler->cert_map->uuid, &ssl->header_.uuid, sizeof(struct uuid))) {
        /* TODO error, this SSL table UUID does not match the saved one */
    }

    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(aux->name, BAD_CAST "resource-id")) {
            xmlval = xmlNodeGetContent(aux);
            if (ovsdb_handler->cert_map->owned_resid == NULL ||
                    strcmp(ovsdb_handler->cert_map->owned_resid, (char*) xmlval)) {
                xmlFree(xmlval);
                /* TODO error, unknown saved resid, should not happen normally */
            }
            xmlFree(xmlval);

            free(ovsdb_handler->cert_map->owned_resid);
            ovsdb_handler->cert_map->owned_resid = NULL;
        } else if (xmlStrEqual(aux->name, BAD_CAST "certificate")) {
            /* TODO error check */
            unlink(OFC_DATADIR "/cert.pem");
            ovsrec_ssl_set_certificate(ssl, "");
        } else if (xmlStrEqual(aux->name, BAD_CAST "private-key")) {
            /* TODO error check */
            unlink(OFC_DATADIR "/key.pem");
            ovsrec_ssl_set_private_key(ssl, "");
        }
    }

    /* get the Open_vSwitch table for linking the SSL structure into */
    ovs = ovsrec_open_vswitch_first(ovsdb_handler->idl);
    if (!ovs) {
        /* TODO error, no vswitch structure to delete from, how come? */
    }
    if (ovsdb_handler->cert_map->external_resid == NULL && ssl->ca_cert == NULL) {
        ovsrec_open_vswitch_set_ssl(ovs, NULL);
        ovsrec_ssl_delete(ssl);
    } else {
        ovsrec_open_vswitch_set_ssl(ovs, ssl);
    }
}

void
txn_del_external_certificate(xmlNodePtr node)
{
    const struct ovsrec_open_vswitch *ovs;
    const struct ovsrec_ssl *ssl;
    xmlNodePtr aux;
    xmlChar *xmlval;

    if (!node) {
        return;
    }

    ssl = ovsrec_ssl_first(ovsdb_handler->idl);
    if (!ssl) {
        /* TODO error, cannot delete, no SSL table */
    }
    if (memcmp(&ovsdb_handler->cert_map->uuid, &ssl->header_.uuid, sizeof(struct uuid))) {
        /* TODO error, this SSL table UUID does not match the saved one */
    }

    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(aux->name, BAD_CAST "resource-id")) {
            xmlval = xmlNodeGetContent(aux);
            if (ovsdb_handler->cert_map->external_resid == NULL ||
                    strcmp(ovsdb_handler->cert_map->external_resid, (char*) xmlval)) {
                xmlFree(xmlval);
                /* TODO error, unknown saved resid, should not happen normally */
            }
            xmlFree(xmlval);

            free(ovsdb_handler->cert_map->external_resid);
            ovsdb_handler->cert_map->external_resid = NULL;
        } else if (xmlStrEqual(aux->name, BAD_CAST "certificate")) {
            /* TODO error check */
            unlink(OFC_DATADIR "/ca_cert.pem");
            ovsrec_ssl_set_ca_cert(ssl, "");
        }
    }

    /* get the Open_vSwitch table for linking the SSL structure into */
    ovs = ovsrec_open_vswitch_first(ovsdb_handler->idl);
    if (!ovs) {
        /* TODO error, no vswitch structure to delete from, how come? */
    }
    if (ovsdb_handler->cert_map->owned_resid == NULL &&
            ssl->certificate == NULL && ssl->private_key == NULL) {
        ovsrec_open_vswitch_set_ssl(ovs, NULL);
        ovsrec_ssl_delete(ssl);
    } else {
        ovsrec_open_vswitch_set_ssl(ovs, ssl);
    }
}

void
txn_add_bridge_port(const xmlChar *br_name, const xmlChar *port_name)
{
    const struct ovsrec_bridge *bridge;
    const struct ovsrec_port *port;
    struct ovsrec_port **ports;
    size_t i;

    if (!port_name || !br_name) {
        return;
    }

    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        if (xmlStrEqual(br_name, BAD_CAST bridge->name)) {
            break;
        }
    }
    OVSREC_PORT_FOR_EACH(port, ovsdb_handler->idl) {
        if (xmlStrEqual(port_name, BAD_CAST port->name)) {
            break;
        }
    }
    if (!port || !bridge) {
        nc_verb_warning("%s: %s not found", __func__, port ? "bridge" : "port");
        return;
    }
    nc_verb_verbose("Add port %s to %s bridge resource list.",
                    BAD_CAST port_name, BAD_CAST br_name);

    ports = malloc(sizeof *bridge->ports * (bridge->n_ports + 1));
    for (i = 0; i < bridge->n_ports; i++) {
        ports[i] = bridge->ports[i];
    }
    ports[i] = (struct ovsrec_port *)port;

    ovsrec_bridge_verify_ports(bridge);
    ovsrec_bridge_set_ports(bridge, ports, bridge->n_ports + 1);
    free(ports);
}

void
txn_add_bridge_queue(const xmlChar *br_name, const xmlChar *resource_id)
{
    const struct ovsrec_bridge *bridge;
    struct ovsrec_qos *qos;
    ofc_tuple_t *resid = NULL;
    size_t i, q;
    int cmp;

    if (!resource_id || !br_name) {
        return;
    }

    nc_verb_verbose("Add queue %s to %s bridge resource list.",
                    BAD_CAST resource_id, BAD_CAST br_name);
    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        if (xmlStrEqual(br_name, BAD_CAST bridge->name)) {
            break;
        }
    }
    if (bridge == NULL) {
        /* not found */
        return;
    }

    resid = ofc_resmap_find_r(ovsdb_handler->resource_map, (const char *) resource_id);
    if (resid == NULL) {
        /* not found */
        return;
    }
    for (i = 0; i < bridge->n_ports; i++) {
        qos = bridge->ports[i]->qos;
        if (qos == NULL) {
            continue;
        }
        for (q = 0; q < qos->n_queues; q++) {
            /* compare with resource-id */
            cmp = uuid_equals(&qos->value_queues[q]->header_.uuid,
                              &resid->uuid);
            if (cmp) {
                ovsrec_port_verify_qos(bridge->ports[i]);
                ovsrec_port_set_qos(bridge->ports[i], NULL);
                //ovsrec_qos_delete(qos);
                return;
            }
        }
    }
}
void
txn_del_bridge_queue(const xmlChar *br_name, const xmlChar *resource_id)
{
    const struct ovsrec_bridge *bridge;
    struct ovsrec_qos *qos;
    ofc_tuple_t *resid = NULL;
    size_t i, q;
    int cmp;

    if (!resource_id || !br_name) {
        return;
    }

    nc_verb_verbose("Delete queue %s from %s bridge resource list.",
                    BAD_CAST resource_id, BAD_CAST br_name);
    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        if (xmlStrEqual(br_name, BAD_CAST bridge->name)) {
            break;
        }
    }
    if (bridge == NULL) {
        /* not found */
        return;
    }

    resid = ofc_resmap_find_r(ovsdb_handler->resource_map, (const char *) resource_id);
    if (resid == NULL) {
        /* not found */
        return;
    }
    for (i = 0; i < bridge->n_ports; i++) {
        qos = bridge->ports[i]->qos;
        if (qos == NULL) {
            continue;
        }
        for (q = 0; q < qos->n_queues; q++) {
            /* compare with resource-id */
            cmp = uuid_equals(&qos->value_queues[q]->header_.uuid,
                              &resid->uuid);
            if (cmp) {
                //ovsrec_port_verify_qos(bridge->ports[i]);
                //ovsrec_port_set_qos(bridge->ports[i], NULL);
                //for (q = 0; q < qos->n_queues; q++) {
                //    ovsrec_queue_delete(qos->value_queues);
                //}
                //ovsrec_qos_set_queues(qos, NULL, NULL, 0);
                //ovsrec_qos_delete(qos);
                return;
            }
        }
    }
}

void
txn_add_bridge_certificate(const xmlChar *br_name, const xmlChar *resource_id)
{
    const struct ovsrec_bridge *bridge;
    const struct ovsrec_ssl *ssl;

    if (!resource_id || !br_name) {
        return;
    }

    nc_verb_verbose("Add certificate %s to %s bridge resource list.",
                    BAD_CAST resource_id, BAD_CAST br_name);
    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        if (xmlStrEqual(br_name, BAD_CAST bridge->name)) {
            break;
        }
    }
    if (bridge == NULL) {
        /* not found */
        return;
    }

    if (ovsdb_handler->cert_map->owned_resid == NULL || strcmp(ovsdb_handler->cert_map->owned_resid, (const char *) resource_id)) {
        /* not found */
        return;
    }
    ssl = ovsrec_ssl_first(ovsdb_handler->idl);
    if (ssl == NULL || memcmp(&ssl->header_.uuid, &ovsdb_handler->cert_map->uuid, sizeof(struct uuid))) {
        /* something wrong */
        return;
    }

    /* TODO make the bridge use the ssl somehow */
    //ovsrec_bridge_set_ssl(bridge, ssl);
}

void
txn_del_bridge_certificate(const xmlChar *br_name, const xmlChar *resource_id)
{
    const struct ovsrec_bridge *bridge;
    //struct ovsrec_ssl *ssl;

    if (!resource_id || !br_name) {
        return;
    }

    nc_verb_verbose("Delete certificate %s from %s bridge resource list.",
                    BAD_CAST resource_id, BAD_CAST br_name);
    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        if (xmlStrEqual(br_name, BAD_CAST bridge->name)) {
            break;
        }
    }
    if (bridge == NULL) {
        /* not found */
        return;
    }

    /* The owned-certificate node could have been already removed,
     * before getting here, so there is nothing for me to check. */
    /*if (ovsdb_handler->cert_map->owned_resid == NULL || strcmp(ovsdb_handler->cert_map->owned_resid, (const char *) resource_id)) {
        * not found *
        return;
    }
    ssl = ovsrec_ssl_first(ovsdb_handler->idl);
    if (ssl == NULL) { || memcmp(&ssl->header_.uuid, &ovsdb_handler->cert_map->uuid, sizeof(struct uuid))) {
        * something wrong *
        return;
    }*/

    /* TODO make the bridge use the ssl somehow */
    //ovsrec_bridge_set_ssl(bridge, NULL);
}


void
txn_del_bridge_flow_table(const xmlChar *br_name, const xmlChar *resource_id)
{
    if (!resource_id || !br_name) {
        return;
    }
    nc_verb_verbose("Remove flow-table %s from %s bridge resource list.",
                    BAD_CAST resource_id, BAD_CAST br_name);
}

void
txn_del_bridge(const xmlChar *br_name)
{
    struct ovsrec_bridge **bridges;
    const struct ovsrec_bridge *bridge;
    const struct ovsrec_open_vswitch *ovs;
    size_t i, j;

    if (!br_name) {
        return;
    }

    /* remove bridge reference from Open_vSwitch table */
    ovs = ovsrec_open_vswitch_first(ovsdb_handler->idl);
    if (!ovs) {
        ovs = ovsrec_open_vswitch_insert(ovsdb_handler->txn);
    }

    bridges = malloc(sizeof *ovs->bridges * (ovs->n_bridges - 1));
    for (i = j = 0; j < ovs->n_bridges; j++) {
        if (!xmlStrEqual(br_name, BAD_CAST ovs->bridges[j]->name)) {
            bridges[i] = ovs->bridges[j];
            i++;
        } else {
            bridge = ovs->bridges[j];
        }
    }
    ovsrec_open_vswitch_verify_bridges(ovs);
    ovsrec_open_vswitch_set_bridges(ovs, bridges, ovs->n_bridges - 1);
    free(bridges);

    /* remove bridge itself */
    ovsrec_bridge_delete(bridge);
}

void
txn_del_queue(const xmlChar *resource_id)
{
    ofc_tuple_t *found;
    const struct ovsrec_queue *queue;
    size_t i, n_queues;
    const struct ovsrec_qos *row;
    bool cmp, changed = false;

    if (!resource_id) {
        return;
    }
    nc_verb_verbose("Delete queue %s.", BAD_CAST resource_id);
    found = ofc_resmap_find_r(ovsdb_handler->resource_map, (const char *) resource_id);
    if (found == NULL) {
        nc_verb_error("Queue was not found.");
        return;
    }
    queue = ovsrec_queue_get_for_uuid(ovsdb_handler->idl, &found->uuid);

    /* remove queue reference from qos table */
    OVSREC_QOS_FOR_EACH(row, ovsdb_handler->idl) {
        for (i = 0; i < row->n_queues; i++) {
            cmp = uuid_equals(&queue->header_.uuid,
                                    &row->value_queues[i]->header_.uuid);
            if (cmp) {
                /* found */
                row->value_queues[i] = row->value_queues[row->n_queues];
                row->key_queues[i] = row->key_queues[row->n_queues];
                n_queues = row->n_queues - 1;
                ovsrec_qos_set_queues(row, row->key_queues, row->value_queues, n_queues);
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        /* remove queue itself */
        ovsrec_queue_delete(queue);

        ofc_resmap_remove_r(ovsdb_handler->resource_map, (const char *) resource_id);
    }
}

/* /capable-switch/logical-switches/switch/datapath-id */
void
txn_mod_bridge_datapath(const xmlChar *br_name, const xmlChar* value)
{
    int i, j;
    static char dp[17];
    struct smap othcfg;
    const struct ovsrec_bridge *bridge;

    if (!br_name) {
        return;
    }

    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        if (xmlStrEqual(br_name, BAD_CAST bridge->name)) {
            break;
        }
    }
    if (!bridge) {
        return;
    }

    smap_clone(&othcfg, &bridge->other_config);
    ovsrec_bridge_verify_other_config(bridge);

    if (value) {
        /* set */
        if (xmlStrlen(value) != 23) {
            nc_verb_error("Invalid datapath (%s)", value);
            smap_destroy(&othcfg);
            return;
        }

        for (i = j = 0; i < 17; i++, j++) {
            while (j < xmlStrlen(value) && value[j] == ':') {
                j++;
            }
            dp[i] = value[j];
        }

        smap_replace(&othcfg, "datapath-id", dp);
    } else {
        /* delete */
        smap_remove(&othcfg, "datapath-id");
    }

    ovsrec_bridge_verify_other_config(bridge);
    ovsrec_bridge_set_other_config(bridge, &othcfg);
    smap_destroy(&othcfg);
}

/* Insert bridge reference into the Open_vSwitch table */
static void
txn_ovs_insert_bridge(const struct ovsrec_open_vswitch *ovs,
                      struct ovsrec_bridge *bridge)
{
    assert(ovs);
    assert(bridge);

    struct ovsrec_bridge **bridges;
    size_t i;
    nc_verb_verbose("Add bridge %s %s", bridge->name, print_uuid_ro(&bridge->header_.uuid));

    bridges = malloc(sizeof *ovs->bridges * (ovs->n_bridges + 1));
    for (i = 0; i < ovs->n_bridges; i++) {
        bridges[i] = ovs->bridges[i];
    }
    bridges[ovs->n_bridges] = bridge;
    ovsrec_open_vswitch_verify_bridges(ovs);
    ovsrec_open_vswitch_set_bridges(ovs, bridges, ovs->n_bridges + 1);
    free(bridges);
}

/* Insert port reference into the Bridge table */
static void
txn_bridge_insert_port(const struct ovsrec_bridge *bridge,
                       struct ovsrec_port *port)
{
    struct ovsrec_port **ports;
    size_t i;

    if (!bridge || !port) {
        return;
    }
    nc_verb_verbose("Add port %s %s", port->name, print_uuid_ro(&port->header_.uuid));

    ports = malloc(sizeof *bridge->ports * (bridge->n_ports + 1));
    for (i = 0; i < bridge->n_ports; i++) {
        ports[i] = bridge->ports[i];
    }
    ports[bridge->n_ports] = port;
    ovsrec_bridge_verify_ports(bridge);
    ovsrec_bridge_set_ports(bridge, ports, bridge->n_ports + 1);
    free(ports);
}

void
txn_add_bridge(xmlNodePtr node)
{
    const struct ovsrec_open_vswitch *ovs;
    const struct ovsrec_bridge *bridge, *next;
    const struct ovsrec_port *port;
    xmlNodePtr aux, leaf;
    xmlChar *xmlval, *bridge_id = NULL;

    if (!node) {
        return;
    }
    /* find id */
    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(aux->name, BAD_CAST "id")) {
            bridge_id = xmlNodeGetContent(aux);
            break;
        }
    }

    /* check for existing bridge id */
    OVSREC_BRIDGE_FOR_EACH_SAFE(bridge, next, ovsdb_handler->idl) {
        if (xmlStrEqual(bridge_id, BAD_CAST bridge->name)) {
            /* existing bridge, exit */
            xmlFree(bridge_id);
            return;
        }
    }

    /* get the Open_vSwitch table for bridge links manipulation */
    ovs = ovsrec_open_vswitch_first(ovsdb_handler->idl);
    if (!ovs) {
        ovs = ovsrec_open_vswitch_insert(ovsdb_handler->txn);
    }

    /* create new bridge and add it into Open_vSwitch table */
    bridge = ovsrec_bridge_insert(ovsdb_handler->txn);
    txn_ovs_insert_bridge(ovs, (struct ovsrec_bridge*)bridge);

    ovsrec_bridge_verify_name(bridge);
    ovsrec_bridge_set_name(bridge, (char *) bridge_id);
    xmlFree(bridge_id);

    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(aux->name, BAD_CAST "datapath-id")) {
            xmlval = xmlNodeGetContent(aux);
            txn_mod_bridge_datapath(BAD_CAST bridge->name, xmlval);
            xmlFree(xmlval);
        } else if (xmlStrEqual(aux->name, BAD_CAST "resources")) {
            for (leaf = aux->children; leaf; leaf = leaf->next) {
                if (xmlStrEqual(leaf->name, BAD_CAST "port")) {
                    xmlval = xmlNodeGetContent(leaf);
                    OVSREC_PORT_FOR_EACH(port, ovsdb_handler->idl) {
                        if (xmlStrEqual(xmlval, BAD_CAST port->name)) {
                            break;
                        }
                    }
                    txn_bridge_insert_port(bridge, (struct ovsrec_port *)port);
                    xmlFree(xmlval);
                }
                ///* TODO no-receive, no-forward, no-packet-in */
                //nc_verb_verbose("no-receive, no-forward, no-packet-in");
                //port_name = xmlNodeGetContent(go2node(leaf->parent->parent, BAD_CAST "name"));
                //bridge_name = ofc_find_bridge_for_port(orig_doc, port_name);
                //if (bridge_name != NULL) {
                //    ofc_of_mod_port(bridge_name, port_name, leaf->name, xmlNodeGetContent(leaf));
                //}
            }
        }
        /* TODO controllers, enabled, lost-connection-behavior */
    }
}

xmlChar *
ofc_find_bridge_for_port_iterative(xmlChar *port_name)
{
    const struct ovsrec_bridge *bridge, *next;
    const struct ovsrec_port *port;
    const struct ovsrec_interface *interface;
    int port_i, inter_i;

    OVSREC_BRIDGE_FOR_EACH_SAFE(bridge, next, ovsdb_handler->idl) {
        for (port_i = 0; port_i < bridge->n_ports; port_i++) {
            port = bridge->ports[port_i];
            for (inter_i = 0; inter_i < port->n_interfaces; inter_i++) {
                interface = port->interfaces[inter_i];
                if (!strncmp(interface->name, (char *) port_name, strlen(interface->name)+1)) {
                    return BAD_CAST bridge->name;
                }
            }
        }
    }
    return NULL;
}

xmlChar *
ofc_find_bridge_for_port(xmlNodePtr root, xmlChar *port_name)
{
    xmlXPathContextPtr xpathCtx;
    xmlXPathObjectPtr xpathObj;
    xmlDocPtr doc;
    char *xpathexpr = NULL;
    xmlChar *bridge_name = NULL;
    int size;
    if (root == NULL) {
        return NULL;
    }
    doc = root->doc;
    xpathexpr = xasprintf("//ofc:switch/ofc:resources/ofc:port['%s']/../../ofc:id[1]",
                          (char *) port_name);
    /* Create xpath evaluation context */
    xpathCtx = xmlXPathNewContext(doc);
    if(xpathCtx == NULL) {
        nc_verb_error("Unable to create new XPath context");
        goto cleanup;
    }

    if (xmlXPathRegisterNs(xpathCtx, BAD_CAST "ofc", BAD_CAST "urn:onf:config:yang")) {
        nc_verb_error("Registering a namespace for XPath failed (%s).", __func__);
        goto cleanup;
    }

    /* Evaluate xpath expression */
    xpathObj = xmlXPathEvalExpression(BAD_CAST xpathexpr, xpathCtx);
    if(xpathObj == NULL) {
        nc_verb_error("Unable to evaluate xpath expression \"%s\"", xpathexpr);
        goto cleanup;
    }

    /* Print results */
    size = (xpathObj->nodesetval) ? xpathObj->nodesetval->nodeNr : 0;
    if (size == 1) {
        if (xpathObj->nodesetval->nodeTab[0]
            && xpathObj->nodesetval->nodeTab[0]->children
            && xpathObj->nodesetval->nodeTab[0]->children->content) {
            bridge_name = xmlNodeGetContent(xpathObj->nodesetval->nodeTab[0]);
        }
    }

cleanup:
    /* Cleanup, use bridge_name that was initialized or set */
    xmlXPathFreeObject(xpathObj);
    xmlXPathFreeContext(xpathCtx);
    return bridge_name;
}

void
txn_mod_port_add_tunnel(const xmlChar *port_name, xmlNodePtr tunnel_node)
{
    xmlNodePtr iter;
    char *option, *value;
    struct smap opt_cl;
    const struct ovsrec_interface *ifc, *next, *found = NULL;
    nc_verb_verbose("Adding tunnel (%s:%d)", __FILE__, __LINE__);

    OVSREC_INTERFACE_FOR_EACH_SAFE(ifc, next, ovsdb_handler->idl) {
        if (!strncmp(ifc->name, (char *) port_name, strlen(ifc->name)+1)) {
            found = ifc;
            break;
        }
    }
    if (found == NULL) {
        /* not found */
        return;
    }

    smap_clone(&opt_cl, &ifc->options);

    for (iter = tunnel_node->children; iter; iter = iter->next) {
        if (iter->type == XML_ELEMENT_NODE) {
            if (xmlStrEqual(iter->name, BAD_CAST "local-endpoint-ipv4-adress")) {
                option = "local_ip";
                value = (char *) xmlNodeGetContent(iter);

            } else if (xmlStrEqual(iter->name, BAD_CAST "remote-endpoint-ipv4-adress")) {
                option = "remote_ip";
                value = (char *) xmlNodeGetContent(iter);
            }
            smap_add_once(&opt_cl, option, (char *) value);
        }
    }
    ovsrec_interface_verify_type(ifc);
    if (xmlStrEqual(tunnel_node->name, BAD_CAST "ipgre-tunnel")) {
        ovsrec_interface_set_type(ifc, "gre");
    } else if (xmlStrEqual(tunnel_node->name, BAD_CAST "vxlan-tunnel")) {
        ovsrec_interface_set_type(ifc, "vxlan");
    } else {
        ovsrec_interface_set_type(ifc, "gre64");
        /* or we hesitate about geneve and lisp */
    }
    ovsrec_interface_verify_options(ifc);
    ovsrec_interface_set_options(ifc, &opt_cl);
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

