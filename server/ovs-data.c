
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

#define _GNU_SOURCE
#include <config.h>

#define _GNU_SOURCE

#include <assert.h>
#include <sys/socket.h>
#include <linux/ethtool.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

/* libovs */
#include <dynamic-string.h>
#include <ovsdb-idl-provider.h>
#include <vswitch-idl.h>
#include <dirs.h>
#include <dpif.h>
#include <vconn.h>
#include <socket-util.h>
#include <ofp-util.h>
#include <ofp-msgs.h>
#include <ofp-print.h>
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
    bool added_interface;
    struct vconn *vconn;
} ovsdb_t;
ovsdb_t *ovsdb_handler = NULL;

int ioctlfd = -1;

/* locally stored data */
static xmlChar *cs_id = NULL;   /* /capable-switch/id */

struct u32_str_map {
    uint32_t value;
    const char *str;
};

static const struct u32_str_map rates[] = {
    {ADVERTISED_10baseT_Half, "10Mb-HD"},
    {ADVERTISED_10baseT_Full, "10Mb-FD"},
    {ADVERTISED_100baseT_Half, "100Mb-HD"},
    {ADVERTISED_100baseT_Full, "100Mb-FD"},
    {ADVERTISED_1000baseT_Half, "1Gb-HD"},
    {ADVERTISED_1000baseT_Full, "1Gb-FD"},
    {ADVERTISED_1000baseKX_Full, "1Gb-FD"},
//      { ADVERTISED_2500baseX_Full,     "2500baseX/Full" },
    {ADVERTISED_10000baseT_Full, "10Gb"},
    {ADVERTISED_10000baseKX4_Full, "10Gb"},
    {ADVERTISED_10000baseKR_Full, "10Gb"},
//      { ADVERTISED_20000baseMLD2_Full, "20000baseMLD2/Full" },
//      { ADVERTISED_20000baseKR2_Full,  "20000baseKR2/Full" },
    {ADVERTISED_40000baseKR4_Full, "40Gb"},
    {ADVERTISED_40000baseCR4_Full, "40Gb"},
    {ADVERTISED_40000baseSR4_Full, "40Gb"},
    {ADVERTISED_40000baseLR4_Full, "40Gb"},
};

static const struct u32_str_map medium[] = {
    {ADVERTISED_TP, "copper"},
    {ADVERTISED_FIBRE, "fiber"},
};

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
of_open_vconn_socket(const char *name, struct vconn **vconnp)
{
    int error;
    char *vconn_name;

    if (asprintf(&vconn_name, "unix:%s", name) == -1) {
        return ENOMEM;
    }

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
static bool
of_open_vconn(const char *name, struct vconn **vconnp)
{
    char *dp_name = NULL, *dp_type = NULL, *sock_name = NULL;
    enum ofputil_protocol protocol;
    char *bridge_path = NULL;
    int ofp_version;
    int error;
    bool ret = false;

    if (asprintf(&bridge_path, "%s/%s.mgmt", ovs_rundir(), name) == -1) {
        return false;
    }

    /* changed to called function */
    dp_parse_name(name, &dp_name, &dp_type);

    if (asprintf(&sock_name, "%s/%s.mgmt", ovs_rundir(), dp_name) == -1) {
        nc_verb_error("%s: asprintf() failed", __func__);
        goto cleanup;
    }

    if (strchr(name, ':')) {
        vconn_open(name, OFPUTIL_DEFAULT_VERSIONS, DSCP_DEFAULT, vconnp);
    } else if (!of_open_vconn_socket(name, vconnp)) {
        /* Fall Through. */
    } else if (!of_open_vconn_socket(bridge_path, vconnp)) {
        /* Fall Through. */
    } else if (!of_open_vconn_socket(sock_name, vconnp)) {
        /* Fall Through. */
    } else {
        nc_verb_error("OpenFlow: %s is not a bridge or a socket.", name);
        goto cleanup;
    }

    nc_verb_verbose("OpenFlow: connecting to %s", vconn_get_name(*vconnp));
    error = vconn_connect_block(*vconnp);
    if (error) {
        nc_verb_error("OpenFlow: %s: failed to connect to socket (%s).", name,
                      ovs_strerror(error));
        goto cleanup;
    }

    ofp_version = vconn_get_version(*vconnp);
    protocol = ofputil_protocol_from_ofp_version(ofp_version);
    if (!protocol) {
        nc_verb_error("OpenFlow: %s: unsupported OpenFlow version 0x%02x.",
                      name, ofp_version);
        goto cleanup;
    }

    ret = true;

cleanup:
    free(bridge_path);
    free(dp_name);
    free(dp_type);
    free(sock_name);

    return ret;
}

/* Gets information about interfaces using 'vconnp' connection.  Function
 * returns pointer to OpenFlow buffer (implemented by OVS) that is used
 * to get results. */
static struct ofpbuf *
of_get_ports(struct vconn *vconnp)
{
    struct ofpbuf *request;
    struct ofpbuf *reply;
    int ofp_version;

    if (vconnp == NULL) {
        return NULL;
    }

    /* existence of version was checked in of_open_vconn() */
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
 * bit.  Otherwise, set 'bit' to 1.  Function returns EXIT_SUCCESS on success.
 * Otherwise it returns EXIT_FAILURE and sets *e. */
static int
of_mod_port_cfg_internal(struct vconn *vconnp, const char *port_name,
                         enum ofputil_port_config bit, char value,
                         struct nc_err **e)
{
    enum ofptype type;
    int ofp_version, ret;
    enum ofputil_protocol protocol;
    struct ofputil_phy_port pp;
    struct ofputil_port_mod pm;
    struct ofp_header *oh;
    struct ofpbuf b, *request, *reply = of_get_ports(vconnp);
    struct ofpbuf *mod_reply = NULL;
    char *oferr = NULL;

    if (reply == NULL) {
        *e = nc_err_new(NC_ERR_DATA_MISSING);
        nc_err_set(*e, NC_ERR_PARAM_MSG, "No port found via OpenFlow.");
        return EXIT_FAILURE;
    }

    ofp_version = vconn_get_version(vconnp);

    protocol = ofputil_protocol_from_ofp_version(ofp_version);
    oh = ofpbuf_data(reply);

    /* get the beginning of data */
    ofpbuf_use_const(&b, oh, ntohs(oh->length));
    if (ofptype_pull(&type, &b) || type != OFPTYPE_PORT_DESC_STATS_REPLY) {
        *e = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Unexpected response via OpenFlow.");
        goto error;
    }

    /* find port by name */
    while (!ofputil_pull_phy_port(oh->version, &b, &pp)) {
        /* modify port */
        if (!strncmp(pp.name, port_name, strlen(pp.name) + 1)) {
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
            ret = vconn_transact_noreply(vconnp, request, &mod_reply);
            if (ret) {
                /* got error code */
                *e = nc_err_new(NC_ERR_OP_FAILED);
                nc_err_set(*e, NC_ERR_PARAM_MSG, ovs_strerror(ret));
                goto error;
            } else {
                if (mod_reply) {
                    oferr = ofp_to_string(ofpbuf_data(reply),
                                          ofpbuf_size(reply), 2);

                    *e = nc_err_new(NC_ERR_OP_FAILED);
                    nc_err_set(*e, NC_ERR_PARAM_MSG, oferr);

                    free(oferr);
                    ofpbuf_delete(mod_reply);
                    goto error;
                }
                /* success - reply to port_mod was empty */
                ofpbuf_delete(reply);
                return EXIT_SUCCESS;
            }
        }
    }
    /* Port name was not found. */
    *e = nc_err_new(NC_ERR_OP_FAILED);
    nc_err_set(*e, NC_ERR_PARAM_MSG, "Modification of unknown port.");

error:
    ofpbuf_delete(reply);
    return EXIT_FAILURE;
}

static unsigned int
dev_get_flags(const char *ifname)
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
dev_set_flags(const char *ifname, unsigned int flags)
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

static int
dev_is_system(const char *ifname)
{
    struct ethtool_drvinfo drvinfo;
    struct ifreq ethreq;

    memset(&ethreq, 0, sizeof ethreq);
    memset(&drvinfo, 0, sizeof drvinfo);

    strncpy(ethreq.ifr_name, ifname, sizeof ethreq.ifr_name);
    drvinfo.cmd = ETHTOOL_GDRVINFO;
    ethreq.ifr_data = &drvinfo;

    errno = 0;
    ioctl(ioctlfd, SIOCETHTOOL, &ethreq);
    if (errno ||  !strcmp(drvinfo.driver, "openvswitch")) {
        return 0;
    } else {
        return 1;
    }
}

static struct ethtool_cmd *
dev_get_ethtool(const char *ifname)
{
    static struct ethtool_cmd ecmd;
    struct ifreq ethreq;

    memset(&ethreq, 0, sizeof ethreq);
    memset(&ecmd, 0, sizeof ecmd);

    strncpy(ethreq.ifr_name, ifname, sizeof ethreq.ifr_name);
    ecmd.cmd = ETHTOOL_GSET;
    ethreq.ifr_data = &ecmd;

    ioctl(ioctlfd, SIOCETHTOOL, &ethreq);

    return &ecmd;
}

static int
dev_set_ethtool(const char *ifname, struct ethtool_cmd *ecmd)
{
    struct ifreq ethreq;

    memset(&ethreq, 0, sizeof ethreq);

    strncpy(ethreq.ifr_name, ifname, sizeof ethreq.ifr_name);
    ecmd->cmd = ETHTOOL_SSET;
    ethreq.ifr_data = ecmd;

    return ioctl(ioctlfd, SIOCETHTOOL, &ethreq);
}

static const xmlChar *
find_bridge_with_port(const xmlChar *port_name)
{
    const struct ovsrec_bridge *bridge;
    const struct ovsrec_port *port;
    const struct ovsrec_interface *interface;
    int port_i, inter_i;

    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        for (port_i = 0; port_i < bridge->n_ports; port_i++) {
            port = bridge->ports[port_i];
            for (inter_i = 0; inter_i < port->n_interfaces; inter_i++) {
                interface = port->interfaces[inter_i];
                if (!strncmp
                    (interface->name, (char *) port_name,
                     strlen(interface->name) + 1)) {
                    return BAD_CAST bridge->name;
                }
            }
        }
    }
    return NULL;
}

int
of_mod_port_cfg(const xmlChar *port_name, const xmlChar *node_name,
                const xmlChar *value, struct nc_err **e)
{
    struct vconn *vconnp;
    enum ofputil_port_config bit;
    char val;
    const xmlChar *br_name;

    br_name = find_bridge_with_port(port_name);
    if (!br_name) {
        nc_verb_error("%s: the bridge with the port %s not found", __func__,
                      (char *) port_name);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "The bridge with the port being modified not found.");
        return EXIT_FAILURE;
    }

    if (xmlStrEqual(node_name, BAD_CAST "no-receive")) {
        bit = OFPUTIL_PC_NO_RECV;
    } else if (xmlStrEqual(node_name, BAD_CAST "no-forward")) {
        bit = OFPUTIL_PC_NO_FWD;
    } else if (xmlStrEqual(node_name, BAD_CAST "no-packet-in")) {
        bit = OFPUTIL_PC_NO_PACKET_IN;
    } else if (xmlStrEqual(node_name, BAD_CAST "admin-state")) {
        if (dev_is_system((const char *) port_name)) {
            /* set status if the system interface directly via ioctl() */
            return txn_mod_port_admin_state(port_name, value, e);
        }

        /* it is internal interface, use OpenFlow */
        bit = OFPUTIL_PC_PORT_DOWN;
    } else {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, (char *) node_name);
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Invalid element name.");
        return EXIT_FAILURE;
    }

    /* check value */
    val = -1;
    if (bit == OFPUTIL_PC_PORT_DOWN) {
        /* admin-state */
        if (!value || xmlStrEqual(value, BAD_CAST "up")) {
            /* !value = delete, up is the default value */
            val = 0;
        } else if (xmlStrEqual(value, BAD_CAST "down")) {
            val = 1;
        }
    } else {
        /* no-receive, no-forward, no-packet-in */
        if (!value || xmlStrEqual(value, BAD_CAST "false")) {
            /* !value = delete, false is the default value */
            val = 0;
        } else if (xmlStrEqual(value, BAD_CAST "true")) {
            val = 1;
        }
    }
    if (val == -1) {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, (char *) node_name);
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Invalid element value.");
        return EXIT_FAILURE;
    }

    /* prepare OpenFlow connection to the bridge where the port is used ... */
    if (of_open_vconn((char *) br_name, &vconnp) != true) {
        nc_verb_error("OpenFlow: could not connect to '%s' bridge.", br_name);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "Unable to connect to the bridge via openFlow.");
        return EXIT_FAILURE;
    }

    /* ... and apply change to the port */
    if (of_mod_port_cfg_internal(vconnp, (char *) port_name, bit, val, e)) {
        nc_verb_error("OpenFlow: modification of configuration failed.");
        vconn_close(vconnp);
        return EXIT_FAILURE;
    }
    vconn_close(vconnp);

    return EXIT_SUCCESS;
}

/* Finds interface with 'name' in OpenFlow 'reply' and returns pointer to it.
 * If interface is not found, function returns NULL.  'reply' should be
 * prepared by ofc_of_get_ports().  Note that 'reply' still needs to be
 * freed. */
static struct ofputil_phy_port *
of_get_port_byname(struct ofpbuf *reply, const char *name)
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
         * configuration of interface */
        if (!strncmp(pp.name, name, strlen(pp.name) + 1)) {
            return &pp;
        }
    }
    return NULL;
}


/*
 * Locally stored data
 */

/*
 * Set /capable-switch/id
 *
 * If this value not set, do not provide any access to the configuration data
 */
int
ofc_set_switchid(xmlNodePtr node)
{
    xmlChar *id;

    if (!node) {
        /* delete id */
        xmlFree(cs_id);
        cs_id = NULL;
        return EXIT_SUCCESS;
    }

    if (!node->children || node->children->type != XML_TEXT_NODE) {
        nc_verb_error("%s: invalid id element", __func__);
        return EXIT_FAILURE;
    }

    id = xmlStrdup(node->children->content);
    if (!id) {
        nc_verb_error("%s: invalid id element content", __func__);
        return EXIT_FAILURE;
    }

    xmlFree(cs_id);
    cs_id = id;

    return EXIT_SUCCESS;
}

/*
 * Read current /capable-switch/id
 */
const xmlChar *
ofc_get_switchid(void)
{
    return cs_id;
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
            for (i = 0, j = 1; j < 24; j++) {
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

/* Returns true and sets key of flow-table if found.  Otherwise it
 * returns false.
 */
static bool
find_flowtable_id(const struct ovsrec_flow_table *flowtable, int64_t *key)
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
                *key = row->key_flow_tables[i];
                return true;
            }
        }
    }
    return false;
}

static char *
get_flow_tables_config(void)
{
    const struct ovsrec_flow_table *row;
    const char *resource_id;
    struct ds string;
    int64_t table_id;

    ds_init(&string);
    OVSREC_FLOW_TABLE_FOR_EACH(row, ovsdb_handler->idl) {
        resource_id = smap_get(&row->external_ids, OFC_RESOURCE_ID);
        if (!find_flowtable_id(row, &table_id)) {
            continue;
        }
        ds_put_format(&string, "<flow-table><table-id>%" PRIi64 "</table-id>",
                      table_id);
        if (resource_id != NULL && strcmp(resource_id, "")) {
            ds_put_format(&string, "<resource-id>%s</resource-id>",
                          resource_id);
        }
        if (row->name) {
            ds_put_format(&string, "<name>%s</name></flow-table>", row->name);
        }
    }
    return string.length ? ds_steal_cstr(&string) : NULL;
}

static const struct ovsrec_port *
find_queue_port(const char *rid)
{
    const struct ovsrec_port *port;
    const char *aux;
    int i;

    OVSREC_PORT_FOR_EACH(port, ovsdb_handler->idl) {
        if (!port->qos) {
            continue;
        }

        for (i = 0; i < port->qos->n_queues; i++) {
            aux = smap_get(&port->qos->value_queues[i]->external_ids,
                            OFC_RESOURCE_ID);
            if (aux && !strcmp(aux, rid)) {
                return port;
            }
        }
    }

    return NULL;
}

static char *
get_queues_config(void)
{
    const struct ovsrec_queue *row;
    const struct ovsrec_port *port;
    struct ds string, aux;
    const char *id, *rid;

    ds_init(&string);
    OVSREC_QUEUE_FOR_EACH(row, ovsdb_handler->idl) {
        rid = smap_get(&row->external_ids, OFC_RESOURCE_ID);
        if (!rid || (!strcmp(rid, ""))) {
            continue;
        }
        ds_put_format(&string, "<queue><resource-id>%s</resource-id>", rid);
        id = smap_get(&row->external_ids, "ofc_id");
        if (id) {
            ds_put_format(&string, "<id>%s</id>", id);
        }

        port = find_queue_port(rid);
        if (port) {
            ds_put_format(&string, "<port>%s</port>", port->name);
        }

        ds_init(&aux);
        find_and_append_smap_val(&row->other_config, "min-rate", "min-rate",
                                 &aux);
        find_and_append_smap_val(&row->other_config, "max-rate", "max-rate",
                                 &aux);
        find_and_append_smap_val(&row->other_config, "experimenter-id",
                                 "experimenter-id", &aux);
        find_and_append_smap_val(&row->other_config, "experimenter-data",
                                 "experimenter-data", &aux);
        if (aux.length) {
            ds_put_format(&string, "<properties>%s</properties></queue>",
                          ds_cstr(&aux));
            ds_destroy(&aux);
        } else {
            ds_put_format(&string, "</queue>");
        }
    }
    return string.length ? ds_steal_cstr(&string) : NULL;
}

static void
dump_port_features(struct ds *s, uint32_t mask)
{
    int i;

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

static char *
get_ports_config(const struct ovsrec_bridge *bridge)
{
    const struct ovsrec_interface *row;
    size_t port_it, ifc;
    struct ds string;
    struct vconn *vconnp;
    const char *bridge_name = bridge->name;
    const char *tunnel_type;
    struct ofpbuf *of_ports = NULL;
    struct ofputil_phy_port *of_port = NULL;
    enum ofputil_port_config c;
    struct ethtool_cmd *ecmd;

    if (of_open_vconn(bridge_name, &vconnp) == true) {
        of_ports = of_get_ports(vconnp);
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
            if (row->n_ofport_request) {
                ds_put_format(&string,
                              "<requested-number>%"PRIu64"</requested-number>",
                              row->ofport_request[0]);
            }

            /* port/configuration/ */
            of_port = of_get_port_byname(of_ports, row->name);
            if (of_port != NULL) {
                c = of_port->config;
                ds_put_format(&string, "<configuration>"
                              "<admin-state>%s</admin-state>"
                              "<no-receive>%s</no-receive>"
                              "<no-forward>%s</no-forward>"
                              "<no-packet-in>%s</no-packet-in>"
                              "</configuration>",
                              (c & OFPUTIL_PC_PORT_DOWN ? "down" : "up"),
                              OFC_PORT_CONF_BIT(c, OFPUTIL_PC_NO_RECV),
                              OFC_PORT_CONF_BIT(c, OFPUTIL_PC_NO_FWD),
                              OFC_PORT_CONF_BIT(c, OFPUTIL_PC_NO_PACKET_IN));
            } else if (row->admin_state) {
                /* port was not found in OpenFlow reply, but admin-state can
                 * be get directly from OVSDB
                 */
                ds_put_format(&string, "<configuration>"
                              "<admin-state>%s</admin-state>"
                              "</configuration>", row->admin_state);
            }

            if (dev_is_system(row->name)) {
                /* get port/features/ via ioctl() */
                ecmd = dev_get_ethtool(row->name);

                ds_put_format(&string, "<features><advertised>");
                dump_port_features(&string, ecmd->advertising);
                ds_put_format(&string, "</advertised></features>");
            }

            tunnel_type = smap_get(&row->external_ids, "tunnel_type");
            if (tunnel_type) {
                ds_put_format(&string, "<%s>", tunnel_type);
                find_and_append_smap_val(&row->options, "local_ip",
                                         "local-endpoint-ipv4-adress",
                                         &string);
                find_and_append_smap_val(&row->options, "remote_ip",
                                         "remote-endpoint-ipv4-adress",
                                         &string);
                if (!strcmp(tunnel_type, "ipgre-tunnel")) {
                    find_and_append_smap_val(&row->options, "csum",
                                             "checksum-present", &string);
                    find_and_append_smap_val(&row->options, "key", "key",
                                             &string);
                } else if (!strcmp(tunnel_type, "vxlan-tunnel")) {
                    find_and_append_smap_val(&row->options, "key", "vni",
                                             &string);
                }
                ds_put_format(&string, "</%s>", tunnel_type);
            }
            ds_put_format(&string, "</port>");
        }
    }

    ofpbuf_delete(of_ports);
    vconn_close(vconnp);

    return string.length ? ds_steal_cstr(&string) : NULL;
}

static char *
get_flow_tables_state(void)
{
    struct ds string;
    const struct ovsrec_flow_table *ft;
    const char *tid;

    ds_init(&string);

    OVSREC_FLOW_TABLE_FOR_EACH(ft, ovsdb_handler->idl) {
        if (ft->n_flow_limit > 0) {
            tid = smap_get(&(ft->external_ids), "table_id");
            if (!tid) {
                continue;
            }
            ds_put_format(&string, "<flow-table><table-id>%s</table-id>"
                          "<max-entries>%ld</max-entries></flow-table>",
                          tid, ft->flow_limit[0]);
        }
    }

    return string.length ? ds_steal_cstr(&string) : NULL;
}

static char *
get_ports_state(const struct ovsrec_bridge *bridge)
{
    const struct ovsrec_interface *row;
    size_t port_it, ifc;
    struct ds string, aux;
    struct vconn *vconnp;
    const char *bridge_name = bridge->name;
    struct ofpbuf *of_ports = NULL;
    struct ofputil_phy_port *of_port = NULL;
    const unsigned char norate = 0xff;

    if (of_open_vconn(bridge_name, &vconnp) == true) {
        of_ports = of_get_ports(vconnp);
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
            if (row->n_ofport > 0) {
                ds_put_format(&string, "<number>%" PRIu64 "</number>",
                              row->ofport[0]);
            }

            ds_init(&aux);
            if (row->link_state) {
                ds_put_format(&aux, "<oper-state>%s</oper-state>",
                              row->link_state);
            }
            find_and_append_smap_val(&row->other_config, "stp_state",
                                     "blocked", &aux);
            of_port = of_get_port_byname(of_ports, row->name);
            if (of_port != NULL) {
                ds_put_format(&aux, "<live>%s</live>",
                              OFC_PORT_CONF_BIT(of_port->state,
                                                OFPUTIL_PS_LIVE));
            }
            if (aux.length) {
                ds_put_format(&string, "<state>%s</state>", aux.string);
                ds_destroy(&aux);
            }

            /* get port/features/ via ioctl() */
            struct ethtool_cmd ecmd_;
            struct ethtool_cmd *ecmd = &ecmd_;

            if (!dev_is_system(row->name)) {
                memset(ecmd, 0, sizeof *ecmd);
            } else {
                ecmd = dev_get_ethtool(row->name);
            }

            ds_put_format(&string, "<features><current>");
            /* rate - get speed and convert it with duplex value to
             * OFPortRateType */
            switch ((ecmd->speed_hi << 16) | ecmd->speed) {
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
                /* do not print duplex suffix */
                ecmd->duplex = DUPLEX_FULL + 1;
                break;
            case 40000:
                ds_put_format(&string, "<rate>40Gb");
                /* do not print duplex suffix */
                ecmd->duplex = DUPLEX_FULL + 1;
                break;
            default:
                ds_put_format(&string, "<rate>other");
                /* do not print duplex suffix */
                ecmd->duplex = norate;
            }
            switch (ecmd->duplex) {
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
                          ecmd->autoneg ? "true" : "false");
            /* medium */
            switch (ecmd->port) {
            case PORT_TP:
                ds_put_format(&string, "<medium>copper</medium>");
                break;
            case PORT_FIBRE:
                ds_put_format(&string, "<medium>fiber</medium>");
                break;
            }

            /* pause is filled with the same value as in advertised */
            if (ADVERTISED_Asym_Pause & ecmd->advertising) {
                ds_put_format(&string, "<pause>asymmetric</pause>");
            } else if (ADVERTISED_Pause & ecmd->advertising) {
                ds_put_format(&string, "<pause>symmetric</pause>");
            } else {
                ds_put_format(&string, "<pause>unsupported</pause>");
            }

            ds_put_format(&string, "</current><supported>");
            dump_port_features(&string, ecmd->supported);
            ds_put_format(&string, "</supported><advertised-peer>");
            dump_port_features(&string, ecmd->lp_advertising);
            ds_put_format(&string, "</advertised-peer></features>");

            if (of_port != NULL && ecmd->duplex == norate) {
                ds_put_format(&string,
                              "<current-rate>%" PRIu32 "</current-rate>"
                              "<max-rate>%" PRIu32 "</max-rate>",
                              of_port->curr_speed, of_port->max_speed);
            }

            ds_put_format(&string, "</port>");
        }
    }

    ofpbuf_delete(of_ports);
    vconn_close(vconnp);

    return string.length ? ds_steal_cstr(&string) : NULL;
}

static void
get_controller_state(struct ds *string, const struct ovsrec_controller *row)
{
    const char *val;

    val = smap_get(&(row->external_ids), "ofconfig-id");
    if (!val) {
        /* skip this record */
        return;
    }

    ds_put_format(string, "<controller><id>%s</id><state>", val);
    if (row->is_connected) {
        ds_put_format(string, "<connection-state>up</connection-state>");
        /* XXX not mapped: ds_put_format(string,
         * "<current-version>%s</current-version>", ); ds_put_format(string,
         * "<supported-versions>%s</supported-versions>", ); */
        /* XXX local-*-in-use - TODO use netstat ds_put_format(string,
         * "<local-ip-address-in-use>%s</local-ip-address-in-use>", );
         * ds_put_format(string, "<local-port-in-use>%s</local-port-in-use>",
         * ); */
    } else {
        ds_put_format(string, "<connection-state>down</connection-state>");
    }
    ds_put_format(string, "</state></controller>");
}

/* parses target t: rewrites delimiters to \0 and sets output pointers */
static void
parse_target_to_addr(char *t, const char **protocol, const char **address,
                     const char **port)
{
    /* XXX write some test for this... */
    char *is_ipv6 = NULL, *delim;
    static const char *tls = "tls";

    if (t == NULL) {
        (*protocol) = NULL;
        (*address) = NULL;
        (*port) = NULL;
        return;
    }

    /* t begins with protocol */
    (*protocol) = t;

    /* address is after delimiter ':' */
    delim = strchr(*protocol, ':');
    is_ipv6 = strchr(delim, '[');
    if (delim != NULL && (delim + 1) != '\0') {
        /* address */
        *delim = '\0';
        *address = delim + 1;

        /* port */
        if (is_ipv6 != NULL) {
            *address += 1;
            delim = strchr(*address, ']');
            if (delim) {
                *delim = '\0';
                delim++;
            }
        } else {
            delim = strchr(*address, ':');
        }
        if (delim && *delim == ':') {
            *delim = '\0';
            *port = delim + 1;
        } else {
            *port = NULL;
        }
    } else {
        *port = NULL;
        *address = NULL;
    }

    /* map protocol value to the of-config values */
    if (!strcmp(*protocol, "ssl")) {
        *protocol = tls;
    }
}

static void
get_controller_config(struct ds *string, const struct ovsrec_controller *row)
{
    const char *protocol, *address, *port;
    const char *id;
    char *target = strdup(row->target);

    parse_target_to_addr(target, &protocol, &address, &port);
    id = smap_get(&(row->external_ids), "ofconfig-id");

    ds_put_format(string, "<controller><id>%s</id>", id);
    if (target) {
        ds_put_format(string, "<ip-address>%s</ip-address>", address);
    }
    if (port) {
        ds_put_format(string, "<port>%s</port>", port);
    }
    if (row->local_ip) {
        ds_put_format(string, "<local-ip-address>%s</local-ip-address>",
                      row->local_ip);
    }
    if (protocol) {
        ds_put_format(string, "<protocol>%s</protocol>", protocol);
    }
    ds_put_format(string, "</controller>");
    free(target);
}

static char *
get_bridges_state(void)
{
    const struct ovsrec_bridge *row;
    struct ds string;
    size_t i;

    ds_init(&string);
    OVSREC_BRIDGE_FOR_EACH(row, ovsdb_handler->idl) {
        /* char *uuid = print_uuid(&row->header_.uuid); */
        /* ds_put_format(&string, "<resource-id>%s</resource-id>", uuid);
         * free(uuid); */
        ds_put_format(&string, "<switch><id>%s</id>", row->name);
        ds_put_format(&string, "<capabilities>"
                      "<max-buffered-packets>256</max-buffered-packets>"
                      "<max-tables>255</max-tables>"
                      "<max-ports>255</max-ports>"
                      "<flow-statistics>true</flow-statistics>"
                      "<table-statistics>true</table-statistics>"
                      "<port-statistics>true</port-statistics>"
                      "<group-statistics>true</group-statistics>"
                      "<queue-statistics>true</queue-statistics>"
                      "<reassemble-ip-fragments>true</reassemble-ip-fragments>"
                      "<block-looping-ports>true</block-looping-ports>");

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

        ds_put_format(&string, "<action-types><type>set-mpls-ttl</type>"
                      "<type>dec-mpls-ttl</type><type>push-vlan</type>"
                      "<type>pop-vlan</type><type>push-mpls</type>"
                      "<type>pop-mpls</type><type>set-queue</type>"
                      "<type>group</type><type>set-nw-ttl</type>"
                      "<type>dec-nw-ttl</type><type>set-field</type>"
                      "</action-types>");

        ds_put_format(&string, "<instruction-types><type>apply-actions</type>"
                      "<type>clear-actions</type><type>write-actions</type>"
                      "<type>write-metadata</type><type>goto-table</type>"
                      "</instruction-types></capabilities>");

        if (row->n_controller > 0) {
            ds_put_format(&string, "<controllers>");
            for (i = 0; i < row->n_controller; ++i) {
                get_controller_state(&string, row->controller[i]);
            }
            ds_put_format(&string, "</controllers>");
        }
        ds_put_format(&string, "</switch>");
    }

    return string.length ? ds_steal_cstr(&string) : NULL;
}

static char *
get_bridges_config(void)
{
    const struct ovsrec_bridge *row;
    const struct ovsrec_open_vswitch *ovs;
    const char *resid, *cert_resid = NULL;
    struct ovsrec_port *port;
    struct ds string, aux;
    size_t i, j;

    /* prepare certificate info, which is global for all bridges */
    ovs = ovsrec_open_vswitch_first(ovsdb_handler->idl);
    if (ovs && ovs->ssl) {
        cert_resid = smap_get(&ovs->ssl->external_ids, OFC_RESID_OWN);
    }

    ds_init(&string);
    OVSREC_BRIDGE_FOR_EACH(row, ovsdb_handler->idl) {
        ds_put_format(&string, "<switch>");
        ds_put_format(&string, "<id>%s</id>", row->name);
        find_and_append_smap_val(&row->other_config, "datapath-id",
                                 "datapath-id", &string);

        /* enabled is not handled: it is too complicated to handle it in
         * combination with the OVSDB's garbage collection. We would have to
         * store almost complete configuration data locally including applying
         * edit-config to it temporarily while the bridge is disabled. */

        if (row->fail_mode) {
            if (!strcmp(row->fail_mode, "standalone")) {
                ds_put_format(&string, "<lost-connection-behavior>"
                              "failStandaloneMode</lost-connection-behavior>");
            } else {
                /* default secure mode */
                ds_put_format(&string, "<lost-connection-behavior>"
                              "failSecureMode</lost-connection-behavior>");
            }
        }
        if (row->n_controller > 0) {
            ds_put_format(&string, "<controllers>");
            for (i = 0; i < row->n_controller; ++i) {
                get_controller_config(&string, row->controller[i]);
            }
            ds_put_format(&string, "</controllers>");
        }

        /* switch/resources/ */
        ds_init(&aux);
        for (i = 0; i < row->n_ports; i++) {
            port = row->ports[i];
            if (port == NULL) {
                continue;
            }
            ds_put_format(&aux, "<port>%s</port>", port->name);
        }

        /* flow-table is linked using table-id */
        for (i = 0; i < row->n_flow_tables; i++) {
            /* OVS uses 64b keys */
            ds_put_format(&aux, "<flow-table>%" PRId64 "</flow-table>",
                          row->key_flow_tables[i]);
        }

        /* queue is linked using resource-id */
        if (row->n_ports > 0) {
            for (i = 0; i < row->n_ports; ++i) {
                if (row->ports[i]->qos != NULL) {
                    for (j = 0; j < row->ports[i]->qos->n_queues; j++) {
                        resid = smap_get(&row->ports[i]->qos->value_queues[j]->external_ids,
                                         OFC_RESOURCE_ID);
                        if (resid != NULL) {
                            ds_put_format(&aux, "<queue>%s</queue>", resid);
                        }
                    }
                }
            }
        }

        if (cert_resid) {
            ds_put_format(&aux, "<certificate>%s</certificate>", cert_resid);
        }

        if (aux.length) {
            ds_put_format(&string, "<resources>%s</resources></switch>",
                          aux.string);
            ds_destroy(&aux);
        } else {
            ds_put_format(&string, "</switch>");
        }
    }

    return string.length ? ds_steal_cstr(&string) : NULL;
}

static char *
get_owned_certificates_config(void)
{
    struct ds str;
    off_t size;
    char *pem, *pem_start, *pem_end;
    const char *resid;
    const struct ovsrec_ssl *ssl;
    int fd;

    ds_init(&str);

    ssl = ovsrec_ssl_first(ovsdb_handler->idl);
    if (ssl != NULL && ssl->certificate != NULL && ssl->private_key != NULL) {
        resid = smap_get(&ssl->external_ids, OFC_RESID_OWN);
        if (!resid) {
            ds_destroy(&str);
            return NULL;
        }

        ds_put_format(&str, "<owned-certificate><resource-id>%s</resource-id>",
                      resid);

        /* certificate */
        fd = open(ssl->certificate, O_RDONLY);
        if (fd == -1) {
            ds_destroy(&str);
            return NULL;
        }
        size = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);
        pem = malloc(size + 1);
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
        pem = malloc(size + 1);
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
        *(pem_end + 3) = '\0';

        ds_put_format(&str, "<key-type>%s</key-type>", pem_end);
        ds_put_format(&str, "<key-data>%s</key-data>", pem_start);
        free(pem);
        ds_put_format(&str, "</private-key>");

        ds_put_format(&str, "</owned-certificate>");
    }

    return str.length ? ds_steal_cstr(&str) : NULL;
}

static char *
get_external_certificates_config(void)
{
    struct ds str;
    off_t size;
    char *pem, *pem_start, *pem_end;
    const char *resid = NULL;
    const struct ovsrec_ssl *ssl;
    int fd;

    ds_init(&str);

    ssl = ovsrec_ssl_first(ovsdb_handler->idl);
    if (ssl != NULL && ssl->ca_cert != NULL) {
        resid = smap_get(&ssl->external_ids, OFC_RESID_EXT);
        if (!resid) {
            ds_destroy(&str);
            return NULL;
        }

        ds_put_format(&str,
                      "<external-certificate><resource-id>%s</resource-id>",
                      resid);

        /* certificate (ca_cert) */
        fd = open(ssl->ca_cert, O_RDONLY);
        if (fd == -1) {
            ds_destroy(&str);
            return NULL;
        }
        size = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);
        pem = malloc(size + 1);
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

    return str.length ? ds_steal_cstr(&str) : NULL;
}

/* Synchronize local copy of OVSDB.  Returns EXIT_SUCCESS on success.
 * Otherwise returns EXIT_FAILURE. */
static int
ofc_update(ovsdb_t *p)
{
    int retval;

    ovsdb_idl_run(p->idl);
    while (!p->seqno || p->seqno != ovsdb_idl_get_seqno(p->idl)) {
        if (!ovsdb_idl_is_alive(p->idl)) {
            retval = ovsdb_idl_get_last_error(p->idl);
            nc_verb_error("OVS database connection failed (%s)",
                          ovs_retval_to_string(retval));
            return EXIT_FAILURE;
        }
        if (p->seqno != ovsdb_idl_get_seqno(p->idl)) {
            p->seqno = ovsdb_idl_get_seqno(p->idl);
        } else if (p->seqno == ovsdb_idl_get_seqno(p->idl)) {
            ovsdb_idl_wait(p->idl);
            poll_timer_wait(100);       /* wait for 100ms (at most) */
            poll_block();
        }
        ovsdb_idl_run(p->idl);
    }
    return EXIT_SUCCESS;
}

char *
ofc_get_config_data(void)
{
    struct ds data, ports_ds;
    const char *id;
    char *queues;
    char *ports;
    char *flow_tables;
    char *bridges;
    char *owned_cert;
    char *external_cert;
    const struct ovsrec_bridge *bridge;

    if (ovsdb_handler == NULL) {
        return NULL;
    }
    ofc_update(ovsdb_handler);

    id = (const char *) ofc_get_switchid();
    if (!id) {
        /* no id -> no data */
        return strdup("");
    }

    ds_init(&data);
    ds_put_format(&data, "<?xml version=\"1.0\"?>"
                  "<capable-switch xmlns=\"urn:onf:config:yang\">"
                  "<id>%s</id>", id);

    /* /capable-switch/resources */
    ds_init(&ports_ds);
    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        ports = get_ports_config(bridge);
        if (ports == NULL) {
            continue;
        }

        ds_put_format(&ports_ds, "%s", ports);
        free(ports);
    }

    ports = NULL;
    if (ports_ds.length) {
        ports = ds_cstr(&ports_ds);
    }
    queues = get_queues_config();
    owned_cert = get_owned_certificates_config();
    external_cert = get_external_certificates_config();
    flow_tables = get_flow_tables_config();

    if (ports || queues || owned_cert || external_cert || flow_tables) {
        ds_put_format(&data, "<resources>%s%s%s%s%s</resources>",
                      ports ? ports : "",
                      queues ? queues : "",
                      owned_cert ? owned_cert : "",
                      external_cert ? external_cert : "",
                      flow_tables ? flow_tables : "");
    }
    ds_destroy(&ports_ds);
    free(queues);;
    free(owned_cert);
    free(external_cert);
    free(flow_tables);

    /* /capable-switch/logical-switches/ */
    bridges = get_bridges_config();
    if (bridges) {
        ds_put_format(&data, "<logical-switches>%s</logical-switches>",
                      bridges);
    }
    free(bridges);

    /* close the envelope */
    ds_put_format(&data, "</capable-switch>");

    return ds_steal_cstr(&data);
}

char *
ofc_get_state_data(void)
{
    const char *id;
    char *ports;
    char *flow_tables;
    char *bridges;
    const struct ovsrec_bridge *bridge;

    struct ds data;
    struct ds ports_ds;

    if (ovsdb_handler == NULL) {
        return NULL;
    }
    ds_init(&ports_ds);
    ofc_update(ovsdb_handler);

    id = (const char *) ofc_get_switchid();
    if (!id) {
        /* no id -> no data */
        return strdup("");
    }

    ds_init(&data);
    ds_put_format(&data, "<?xml version=\"1.0\"?>"
                  "<capable-switch xmlns=\"urn:onf:config:yang\">"
                  "<config-version>1.2</config-version>");

    /* /capable-switch/resources */
    ds_init(&ports_ds);
    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        ports = get_ports_state(bridge);
        if (ports == NULL) {
            continue;
        }

        ds_put_format(&ports_ds, "%s", ports);
        free(ports);
    }

    ports = NULL;
    if (ports_ds.length) {
        ports = ds_cstr(&ports_ds);
    }
    flow_tables = get_flow_tables_state();

    if (ports || flow_tables) {
        ds_put_format(&data, "<resources>%s%s</resources>",
                      ports ? ports : "",
                      flow_tables ? flow_tables : "");
    }
    ds_destroy(&ports_ds);
    free(flow_tables);

    /* /capable-switch/logical-switches/ */
    bridges = get_bridges_state();
    if (bridges) {
        ds_put_format(&data, "<logical-switches>%s</logical-switches>",
                      bridges);
    }
    free(bridges);

    /* close the envelope */
    ds_put_format(&data, "</capable-switch>");

    return ds_steal_cstr(&data);
}

bool
ofc_init(const char *ovs_db_path)
{
    ovsdb_t *p = calloc(1, sizeof *p);

    if (p == NULL) {
        /* failed */
        return false;
    }
    ovsdb_handler = p;
    p->added_interface = false;

    ovsrec_init();
    p->idl = ovsdb_idl_create(ovs_db_path, &ovsrec_idl_class, true, true);
    p->txn = NULL;
    p->seqno = ovsdb_idl_get_seqno(p->idl);
    nc_verb_verbose("Try to synchronize OVSDB.");
    if (ofc_update(p) == EXIT_FAILURE) {
        ofc_destroy();
        return false;
    }

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
    ovsdb_handler->added_interface = false;
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
                          errmsg =
                          ovsdb_idl_txn_get_error(ovsdb_handler->txn));
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

int
txn_del_all(struct nc_err **UNUSED(e))
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
     * garbage collection */
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

    ofc_set_switchid(NULL);

    return EXIT_SUCCESS;
}

int
txn_del_port(const xmlChar *port_name, struct nc_err **e)
{
    const struct ovsrec_port *port;
    const struct ovsrec_interface *iface;

    if (!port_name) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    OVSREC_PORT_FOR_EACH(port, ovsdb_handler->idl) {
        if (xmlStrEqual(port_name, BAD_CAST port->name)) {
            break;
        }
    }
    OVSREC_INTERFACE_FOR_EACH(iface, ovsdb_handler->idl) {
        if (xmlStrEqual(port_name, BAD_CAST iface->name)) {
            break;
        }
    }

    if (!iface || !port) {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "name");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Port does not exist in OVSDB");
        return EXIT_FAILURE;
    }

    ovsrec_port_delete(port);
    ovsrec_interface_delete(iface);
    return EXIT_SUCCESS;

}

/* /capable-switch/resources/port/requested-number */
int
txn_mod_port_reqnumber(const xmlChar *port_name, const xmlChar* value,
                       struct nc_err **e)
{
    int64_t rn;
    const struct ovsrec_interface *iface;

    if (!port_name) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
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
            rn = strtol((char *) value, NULL, 10);
            ovsrec_interface_set_ofport_request(iface, &rn, 1);
        } else {
            ovsrec_interface_set_ofport_request(iface, NULL, 0);
        }

        return EXIT_SUCCESS;
    } else {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "name");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Port does not exist in OVSDB");
        return EXIT_FAILURE;
    }
}

int
txn_mod_port_admin_state(const xmlChar *port_name, const xmlChar *value,
                         struct nc_err **e)
{
    unsigned int flags;
    int req;

    if (!port_name) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    if (!value) {
        /* delete -> set default value (up) */
        req = 1;
        flags = dev_get_flags((char *) port_name);
        if (flags && IFF_UP) {
            /* already up */
            return EXIT_SUCCESS;
        }
        dev_set_flags((char *) port_name, flags & IFF_UP);
    } else {
        if (xmlStrEqual(value, BAD_CAST "up")) {
            req = 1;
            flags = dev_get_flags((char *) port_name) & IFF_UP;
        } else if (xmlStrEqual(value, BAD_CAST "down")) {
            req = 0;
            flags = dev_get_flags((char *) port_name) & ~IFF_UP;
        } else {
            *e = nc_err_new(NC_ERR_BAD_ELEM);
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "admin-state");
            nc_err_set(*e, NC_ERR_PARAM_MSG, "Invalid value to set.");
            return EXIT_FAILURE;
        }
        dev_set_flags((char *) port_name, flags);
    }

    /* check the result */
    flags = dev_get_flags((char *) port_name);
    if ((req && !(flags && IFF_UP)) || (!req && (flags && IFF_UP))) {
        *e = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "Interface admin state not set to the requested value.");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int
of_post_ports(xmlNodePtr cfg, struct nc_err **error)
{
    xmlXPathContextPtr xpathCtx = NULL;
    xmlXPathObjectPtr xpathObj = NULL;
    const xmlChar *port_name, *value;
    xmlNodePtr port, aux;
    const char *xpathexpr = "//ofc:port/ofc:configuration/..";
    size_t size, i;
    int ret = EXIT_FAILURE;

    if (!cfg || ovsdb_handler->added_interface == false) {
        return EXIT_SUCCESS;
    }

    /* Create xpath evaluation context */
    xpathCtx = xmlXPathNewContext(cfg->doc);
    if (!xpathCtx) {
        nc_verb_error("%s: Unable to create new XPath context", __func__);
        goto cleanup;
    }

    if (xmlXPathRegisterNs(xpathCtx, BAD_CAST "ofc",
                           BAD_CAST "urn:onf:config:yang")) {
        nc_verb_error("%s: Registering a namespace for XPath failed.",
                      __func__);
        goto cleanup;
    }

    /* Evaluate xpath expression */
    xpathObj = xmlXPathEvalExpression(BAD_CAST xpathexpr, xpathCtx);
    if (!xpathObj) {
        nc_verb_error("%s: Unable to evaluate xpath expression \"%s\"",
                      __func__, xpathexpr);
        goto cleanup;
    }

    /* apply changes via OpenFlow */
    size = (xpathObj->nodesetval) ? xpathObj->nodesetval->nodeNr : 0;
    for (i = 0; i < size; i++) {
        if (xpathObj->nodesetval->nodeTab[i]) {
            port = xpathObj->nodesetval->nodeTab[i];
            port_name = get_key(port, "name");
            aux = go2node(port, BAD_CAST "configuration");
            if (!aux) {
                continue;
            }

            /* process port/configuration/. elements of the port */
            for (aux = aux->children; aux; aux = aux->next) {
                value = aux->children ? aux->children->content : NULL;
                if (of_mod_port_cfg(port_name, aux->name, value, error)) {
                    goto cleanup;
                }
            }
        }
    }

    ret = EXIT_SUCCESS;

cleanup:
    /* Cleanup, use bridge_name that was initialized or set */
    xmlXPathFreeObject(xpathObj);
    xmlXPathFreeContext(xpathCtx);
    return ret;
}

int
txn_add_port(xmlNodePtr node, struct nc_err **e)
{
    xmlNodePtr aux, aux2, advert;
    const xmlChar *xmlval, *port_name;
    struct ovsrec_port *port;
    struct ovsrec_interface *iface;
    struct ethtool_cmd *ecmd;
    int i;
    int tunnel = 0;

    if (!node) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    /* prepare new structures to set content of the port configuration */
    port = ovsrec_port_insert(ovsdb_handler->txn);
    iface = ovsrec_interface_insert(ovsdb_handler->txn);
    ovsrec_port_set_interfaces(port, (struct ovsrec_interface **) &iface, 1);

    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(aux->name, BAD_CAST "name")) {
            xmlval = aux->children ? aux->children->content : NULL;
            if (xmlval) {
                ovsrec_interface_verify_name(iface);
                ovsrec_interface_set_name(iface, (char *) xmlval);
                ovsrec_port_verify_name(port);
                ovsrec_port_set_name(port, (char *) xmlval);
            } else {
                nc_verb_error("%s: port name value is missing.", __func__);
                *e = nc_err_new(NC_ERR_BAD_ELEM);
                nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "name");
                nc_err_set(*e, NC_ERR_PARAM_MSG, "Invalid name of the port.");
                return EXIT_FAILURE;
            }
        } else if (xmlStrEqual(aux->name, BAD_CAST "requested-number")) {
            xmlval = aux->children ? aux->children->content : NULL;
            if (txn_mod_port_reqnumber(BAD_CAST iface->name, xmlval, e)) {
                return EXIT_FAILURE;
            }
        } else if (xmlStrEqual(aux->name, BAD_CAST "configuration")) {
            /* can't be set before commit, it will be set later */
            ovsdb_handler->added_interface = true;
        } else if (xmlStrEqual(aux->name, BAD_CAST "ipgre-tunnel")
                   || xmlStrEqual(aux->name, BAD_CAST "vxlan-tunnel")
                   || xmlStrEqual(aux->name, BAD_CAST "tunnel")) {
            /* check if there another tunnel already set */
            if (tunnel) {
                nc_verb_error("%s: multiple tunnels specified.");
                *e = nc_err_new(NC_ERR_BAD_ELEM);
                nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, (char *)aux->name);
                nc_err_set(*e, NC_ERR_PARAM_MSG,
                           "Multiple branches of the \"tunnel-type\" choice.");
                return EXIT_FAILURE;
            }

            port_name = get_key(aux->parent, "name");
            if (txn_add_port_tunnel(port_name, aux, e)) {
                return EXIT_FAILURE;
            }

            /* set the flag to avoid multiple branches of the choice */
            tunnel = 1;
        } else if (xmlStrEqual(aux->name, BAD_CAST "features")) {
            advert = go2node(aux, BAD_CAST "advertised");
            if (!advert) {
                continue;
            }

            if (!dev_is_system((const char*)port_name)) {
                continue;
            }

            ecmd = dev_get_ethtool(iface->name);
            /* prepare default values */
            ecmd->advertising = ADVERTISED_Autoneg;

            for (aux2 = advert->children; aux2; aux2 = aux2->next) {
                if (aux2->type != XML_ELEMENT_NODE) {
                    continue;
                }
                if (xmlStrEqual(aux2->name, BAD_CAST "rate")) {
                    for (i = 0; i < (sizeof rates) / (sizeof rates[0]); i++) {
                        if (xmlStrEqual(aux2->children->content,
                                        BAD_CAST rates[i].str)) {
                            ecmd->advertising |= rates[i].value;
                            break;
                        }
                    }
                } else if (xmlStrEqual(aux2->name, BAD_CAST "medium")) {
                    for (i = 0; i < (sizeof medium) / (sizeof medium[0]); i++) {
                        if (xmlStrEqual(aux2->children->content,
                                        BAD_CAST medium[i].str)) {
                            ecmd->advertising |= medium[i].value;
                            break;
                        }
                    }
                } else if (xmlStrEqual(aux2->name, BAD_CAST "auto-negotiate")) {
                    if (xmlStrEqual(aux2->children->content, BAD_CAST "false")) {
                        ecmd->advertising &= ~ADVERTISED_Autoneg;
                    }
                } else if (xmlStrEqual(aux2->name, BAD_CAST "pause")) {
                    if (xmlStrEqual(aux2->children->content,
                                    BAD_CAST "symetric")) {
                        ecmd->advertising |= ADVERTISED_Pause;
                    } else if (xmlStrEqual(aux2->children->content,
                                           BAD_CAST "asymetric")) {
                        ecmd->advertising |= ADVERTISED_Asym_Pause;
                    }
                }
            }
            dev_set_ethtool(iface->name, ecmd);
        }
    }

    /* detect interface type */
    if (!tunnel) {
        if (dev_is_system(port->name)) {
            ovsrec_interface_set_type(iface, "system");
        } else {
            ovsrec_interface_set_type(iface, "internal");
        }
    }

    ovsdb_handler->added_interface = true;

    return EXIT_SUCCESS;
}

int
txn_add_port_advert(const xmlChar *port_name, xmlNodePtr node,
                    struct nc_err **e)
{
    int i;
    struct ethtool_cmd *ecmd;

    if (!port_name || !node) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    if (!dev_is_system((const char*)port_name)) {
        *e = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "Unable to set advertised values on this type of interface");
        return EXIT_FAILURE;
    }

    ecmd = dev_get_ethtool((char *) port_name);

    if (xmlStrEqual(node->name, BAD_CAST "rate")) {
        for (i = 0; i < (sizeof rates) / (sizeof rates[0]); i++) {
            if (xmlStrEqual(node->children->content, BAD_CAST rates[i].str)) {
                ecmd->advertising |= rates[i].value;
                break;
            }
        }
    } else if (xmlStrEqual(node->name, BAD_CAST "medium")) {
        for (i = 0; i < (sizeof medium) / (sizeof medium[0]); i++) {
            if (xmlStrEqual(node->children->content, BAD_CAST medium[i].str)) {
                ecmd->advertising |= medium[i].value;
                break;
            }
        }
    } else if (xmlStrEqual(node->name, BAD_CAST "auto-negotiate")) {
        if (xmlStrEqual(node->children->content, BAD_CAST "false")) {
            ecmd->advertising &= ~ADVERTISED_Autoneg;
        } else if (xmlStrEqual(node->children->content, BAD_CAST "true")) {
            ecmd->advertising |= ADVERTISED_Autoneg;
        }
    } else if (xmlStrEqual(node->name, BAD_CAST "pause")) {
        if (xmlStrEqual(node->children->content, BAD_CAST "asymetric")) {
            ecmd->advertising &= ~ADVERTISED_Pause;
            ecmd->advertising |= ADVERTISED_Asym_Pause;
        } else if (xmlStrEqual(node->children->content, BAD_CAST "symetric")) {
            ecmd->advertising &= ~ADVERTISED_Asym_Pause;
            ecmd->advertising |= ADVERTISED_Pause;
        }
    }
    dev_set_ethtool((char*)port_name, ecmd);

    return EXIT_SUCCESS;
}

int
txn_del_port_advert(const xmlChar *port_name, xmlNodePtr node,
                    struct nc_err **e)
{
    int i;
    struct ethtool_cmd *ecmd;

    if (!port_name || !node) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    if (!dev_is_system((const char*)port_name)) {
        *e = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "Unable to set advertised values on this type of interface");
        return EXIT_FAILURE;
    }

    ecmd = dev_get_ethtool((char *) port_name);

    if (xmlStrEqual(node->name, BAD_CAST "rate")) {
        for (i = 0; i < (sizeof rates) / (sizeof rates[0]); i++) {
            if (xmlStrEqual(node->children->content, BAD_CAST rates[i].str)) {
                ecmd->advertising &= ~rates[i].value;
                break;
            }
        }
    } else if (xmlStrEqual(node->name, BAD_CAST "medium")) {
        for (i = 0; i < (sizeof medium) / (sizeof medium[0]); i++) {
            if (xmlStrEqual(node->children->content, BAD_CAST medium[i].str)) {
                ecmd->advertising &= ~medium[i].value;
                break;
            }
        }
    } else if (xmlStrEqual(node->name, BAD_CAST "auto-negotiate")) {
        /* default is true */
        ecmd->advertising |= ADVERTISED_Autoneg;
    } else if (xmlStrEqual(node->name, BAD_CAST "pause")) {
        ecmd->advertising &= ~ADVERTISED_Pause;
        ecmd->advertising &= ~ADVERTISED_Asym_Pause;
    }
    dev_set_ethtool((char*)port_name, ecmd);

    return EXIT_SUCCESS;
}


/* Queue */

int
txn_add_queue(xmlNodePtr node, struct nc_err **e)
{
    xmlNodePtr aux, prop = NULL;
    xmlChar *id_s = NULL;
    xmlChar *rid = NULL, *port_name = NULL;
    struct ovsrec_queue *queue;

    if (!node) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(aux->name, BAD_CAST "resource-id")) {
            rid = aux->children ? aux->children->content : NULL;
        } else if (xmlStrEqual(aux->name, BAD_CAST "id")) {
            id_s = aux->children ? aux->children->content : NULL;
        } else if (xmlStrEqual(aux->name, BAD_CAST "port")) {
            port_name = aux->children ? aux->children->content : NULL;
        } else if (xmlStrEqual(aux->name, BAD_CAST "properties")) {
            prop = aux;
        }
    }

    /* check mandatory items */
    if (!rid || !id_s) {
        /* key/mandatory item is missing */
        nc_verb_error("%s: missing table_id.", __func__);
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "queue");
        if (!rid) {
            nc_err_set(*e, NC_ERR_PARAM_MSG, "The list element misses key.");
        } else {
            nc_err_set(*e, NC_ERR_PARAM_MSG, "Mandatory gueue id is missing.");
        }
        return EXIT_FAILURE;
    }
    nc_verb_verbose("Add queue %s:%s to port.", (char *) rid, (char *) id_s);

    /* create new */
    queue = ovsrec_queue_insert(ovsdb_handler->txn);

    /* resource-id */
    smap_add_once(&queue->external_ids, OFC_RESOURCE_ID, (const char *) rid);
    ovsrec_queue_verify_external_ids(queue);
    ovsrec_queue_set_external_ids(queue, &queue->external_ids);

    if (id_s) {
        if (txn_mod_queue_id(rid, id_s, e)) {
            return EXIT_FAILURE;
        }
    }

    if (port_name) {
        if (txn_add_queue_port(rid, port_name, e)) {
            return EXIT_FAILURE;
        }
    }

    if (prop != NULL) {
        for (aux = prop->children; aux; aux = aux->next) {
            if (txn_mod_queue_options(rid, (char *) aux->name, aux, e)) {
                return EXIT_FAILURE;
            }
        }
    }

    ovsrec_queue_verify_other_config(queue);
    ovsrec_queue_set_other_config(queue, &queue->other_config);

    return EXIT_SUCCESS;
}

/* Finds queue by resource-id and returns pointer to it.  When no queue is
 * found, returns NULL. */
static const struct ovsrec_queue *
find_queue(const xmlChar *resource_id)
{
    const struct ovsrec_queue *queue;
    const char *rid;

    if (!resource_id) {
        return NULL;
    }

    OVSREC_QUEUE_FOR_EACH(queue, ovsdb_handler->idl) {
        rid = smap_get(&queue->external_ids, OFC_RESOURCE_ID);
        if (rid && xmlStrEqual(resource_id, BAD_CAST rid)) {
            break;
        }
    }

    return queue;
}

/* Add reference for queue to port (via qos).
 *
 * queue_id: queue/id (int64)
 */
int
txn_add_queue_port(const xmlChar *rid, const xmlChar *port_name,
                   struct nc_err **e)
{
    const struct ovsrec_queue *queue;
    const struct ovsrec_port *port;
    struct ovsrec_qos *qos;
    struct ovsrec_queue **queues;
    int64_t *queues_keys;
    const char* qid_s;
    int64_t qid;
    int i;

    if (!rid || !port_name) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    /* find queue record */
    queue = find_queue(rid);
    if (!queue) {
        nc_verb_error("Queue to link with port not found");
        *e = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Queue to link with port not found.");
        return EXIT_FAILURE;
    }
    /* check queue's id */
    qid_s = smap_get(&queue->external_ids, "ofc_id");
    if (!qid_s) {
        nc_verb_error("%s: queue without ID found", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "Internal inconsistency of the queue records.");
        return EXIT_FAILURE;
    }

    /* find port record */
    OVSREC_PORT_FOR_EACH(port, ovsdb_handler->idl) {
        if (xmlStrEqual(port_name, BAD_CAST port->name)) {
            break;
        }
    }
    if (!port) {
        nc_verb_error("Port to link with queue not found");
        *e = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Port to link with queue not found.");
        return EXIT_FAILURE;
    }

    /* id */
    errno = 0;
    if (sscanf((char *) qid_s, "%" SCNi64, &qid) != 1) {
        /* parsing error, wrong number */
        nc_verb_error("sscanf() for queue id failed (%s)",
                      errno ? strerror(errno) : "unknown error");
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "id");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Invalid queue's id value.");
        return EXIT_FAILURE;
    }

    qos = port->qos;
    if (qos == NULL) {
        /* create new qos */
        qos = ovsrec_qos_insert(ovsdb_handler->txn);

        ovsrec_qos_verify_type(qos);
        ovsrec_qos_set_type(qos, QOS_TYPE);

        ovsrec_qos_verify_queues(qos);
        ovsrec_qos_set_queues(qos, (int64_t *) &qid,
                              (struct ovsrec_queue **) &queue, 1);

        ovsrec_port_verify_qos(port);
        ovsrec_port_set_qos(port, qos);
    } else {
        /* add into existing qos (that is referred from port) */

        /* enlarge array of pairs */
        queues = malloc(sizeof *qos->value_queues * (qos->n_queues + 1));
        queues_keys = malloc(sizeof *qos->key_queues * (qos->n_queues + 1));
        for (i = 0; i < qos->n_queues; i++) {
            queues[i] = qos->value_queues[i];
            queues_keys[i] = qos->key_queues[i];
        }
        queues[i] = (struct ovsrec_queue *) queue;
        queues_keys[i] = qid;

        ovsrec_qos_verify_queues(qos);
        ovsrec_qos_set_queues(qos, queues_keys, queues, qos->n_queues + 1);

        free(queues);
        free(queues_keys);
    }

    return EXIT_SUCCESS;
}

/*
 * Set Queue's ID
 * Covers creation and change of the current value. deletion is not supported
 * since the ID is mandatory item (and replacing splited into delete + create
 * is covered by subsequent create changing the current value).
 */
int
txn_mod_queue_id(const xmlChar *rid, const xmlChar* qid_s, struct nc_err **e)
{
    const struct ovsrec_queue *queue;
    const struct ovsrec_qos *qos;
    const char *aux;
    int64_t qid;
    int i;

    if (!rid || !qid_s ) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    errno = 0;
    if (sscanf((char *) qid_s, "%" SCNi64, &qid) != 1) {
        /* parsing error, wrong number */
        nc_verb_error("sscanf() for queue id failed (%s)",
                      errno ? strerror(errno) : "unknown error");
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "id");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Invalid queue's id value.");
        return EXIT_FAILURE;
    }

    /* find queue record */
    queue = find_queue(rid);
    if (!queue) {
        nc_verb_error("Queue to link with port not found");
        *e = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Queue to link with port not found.");
        return EXIT_FAILURE;
    }

    /* check if the queue is already used in some port, if so, we have to
     * update the array in QoS table
     */
    OVSREC_QOS_FOR_EACH(qos, ovsdb_handler->idl) {
        for (i = 0; i < qos->n_queues; i++) {
            aux = smap_get(&(qos->value_queues[i]->external_ids),
                           OFC_RESOURCE_ID);
            if (aux && xmlStrEqual(rid, BAD_CAST aux)) {
                /* there is some connection, update the id */
                qos->key_queues[i] = qid;
                ovsrec_qos_verify_queues(qos);
                ovsrec_qos_set_queues(qos, qos->key_queues, qos->value_queues,
                                      qos->n_queues);

                /* stop the loop */
                break;
            }
        }
        if (i < qos->n_queues) {
            /* stop the outer loop, the queue can be used only once */
            break;
        }
    }

    /* store the id into the queue record itself */
    smap_replace((struct smap *) &queue->external_ids, "ofc_id",
                 (const char *) qid_s);
    ovsrec_queue_verify_external_ids(queue);
    ovsrec_queue_set_external_ids(queue, &queue->external_ids);

    return EXIT_SUCCESS;
}

static int
txn_del_qos_queue(const struct ovsrec_qos **qos, int i, struct nc_err **e)
{
    struct ovsrec_queue **queues;
    int64_t *queues_keys;
    int j, k;

    if ((*qos)->n_queues == 1) {
        /* there is only one queue in QoS, remove the whole QoS record */
        ovsrec_qos_delete(*qos);
        *qos = NULL;
    } else {
        /* there are multiple queues in QoS, update the list of queues */
        queues = malloc(sizeof *(*qos)->value_queues * ((*qos)->n_queues - 1));
        queues_keys = malloc(sizeof *(*qos)->key_queues * ((*qos)->n_queues - 1));
        for (j = k = 0; j < (*qos)->n_queues; j++) {
            /* we have i value from interrupted for loop in main part of this
             * function
             */
            if (j == i) {
                continue;
            }
            queues[k] = (*qos)->value_queues[j];
            queues_keys[k] = (*qos)->key_queues[j];
            k++;
        }

        ovsrec_qos_verify_queues(*qos);
        ovsrec_qos_set_queues((*qos), queues_keys, queues, (*qos)->n_queues - 1);

        free(queues);
        free(queues_keys);
    }

    return EXIT_SUCCESS;
}

int
txn_del_queue_port(const xmlChar *rid, struct nc_err **e)
{
    const struct ovsrec_port *port;
    const char *aux;
    int i, ret;

    if (!rid) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    OVSREC_PORT_FOR_EACH(port, ovsdb_handler->idl) {
        if (!port->qos) {
            continue;
        }

        for (i = 0; i < port->qos->n_queues; i++) {
            aux = smap_get(&(port->qos->value_queues[i]->external_ids),
                           OFC_RESOURCE_ID);
            if (aux && xmlStrEqual(rid, BAD_CAST aux)) {
                /* we found the connection, remove it and update port/qos */
                ret = txn_del_qos_queue((const struct ovsrec_qos **)&port->qos,
                                        i, e);
                if (!port->qos) {
                    ovsrec_port_verify_qos(port);
                    ovsrec_port_set_qos(port, NULL);
                }
                return ret;
            }
        }
    }

    /* no connection between the queue and some port (its qos) found */
    nc_verb_error("%s: requested queue link not found in any port.", __func__);
    *e = nc_err_new(NC_ERR_OP_FAILED);
    return EXIT_FAILURE;
}

int
txn_mod_queue_options(const xmlChar *rid, const char *option,
                      xmlNodePtr edit, struct nc_err **e)
{
    const struct ovsrec_queue *queue;
    char *value;

    if (!rid || !option) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    /* find queue record */
    queue = find_queue(rid);
    if (!queue) {
        nc_verb_error("Queue to set properties not found");
        *e = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Queue to set properties not found");
        return EXIT_FAILURE;
    }

    if (edit) {
        /* add */
        value = edit->children ? (char *) edit->children->content : NULL;
        if (!value) {
            nc_verb_error("Missing value for %s queue property", option);
            *e = nc_err_new(NC_ERR_OP_FAILED);
            nc_err_set(*e, NC_ERR_PARAM_MSG,
                       "Missing value of the queue property");
            return EXIT_FAILURE;
        }
        nc_verb_verbose("set option %s with value %s to queue", option, value);

        smap_replace((struct smap *) &queue->other_config, option, value);
        ovsrec_queue_verify_other_config(queue);
        ovsrec_queue_set_other_config(queue, &queue->other_config);

        return EXIT_SUCCESS;
    } else {
        /* delete */
        nc_verb_verbose("delete option %s from queue", option);
        smap_remove((struct smap *) &queue->other_config, option);
        ovsrec_queue_verify_other_config(queue);
        ovsrec_queue_set_other_config(queue, &queue->other_config);

        return EXIT_SUCCESS;
    }
}

/* end of Queue */

int
txn_add_flow_table(xmlNodePtr node, struct nc_err **e)
{
    xmlNodePtr aux;
    xmlChar *tid_s = NULL, *name = NULL, *rid = NULL;
    int64_t tid;
    struct ovsrec_flow_table *ft;

    if (!node) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(aux->name, BAD_CAST "table-id")) {
            tid_s = aux->children ? aux->children->content : NULL;
            if (sscanf((const char *) tid_s, "%" SCNi64, &tid) != 1) {
                /* parsing error, wrong number */
                *e = nc_err_new(NC_ERR_BAD_ELEM);
                nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "table_id");
                nc_err_set(*e, NC_ERR_PARAM_MSG, "Invalid value.");
                return EXIT_FAILURE;
            }
        } else if (xmlStrEqual(aux->name, BAD_CAST "resource-id")) {
            rid = aux->children ? aux->children->content : NULL;
        } else if (xmlStrEqual(aux->name, BAD_CAST "name")) {
            name = aux->children ? aux->children->content : NULL;
        }
    }

    /* check mandatory items */
    if (!tid_s) {
        /* key is missing */
        nc_verb_error("%s: missing table_id.", __func__);
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "flow-table");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "The list element misses key.");
        return EXIT_FAILURE;
    }

    /* create flow-table record */
    ft = ovsrec_flow_table_insert(ovsdb_handler->txn);
    smap_replace(&ft->external_ids, "table_id", (const char *) tid_s);
    if (rid) {
        smap_replace(&ft->external_ids, OFC_RESOURCE_ID, (char *) rid);
    }
    ovsrec_flow_table_verify_external_ids(ft);
    ovsrec_flow_table_set_external_ids(ft, &ft->external_ids);
    if (name) {
        ovsrec_flow_table_verify_name(ft);
        ovsrec_flow_table_set_name(ft, (char *) name);
    }

    return EXIT_SUCCESS;
}

/* Finds flow-table by resource-id and returns pointer to it.  When no queue is
 * found, returns NULL. */
static const struct ovsrec_flow_table *
find_flowtable(const xmlChar *table_id)
{
    const struct ovsrec_flow_table *ft;
    const char *tid_s;

    if (!table_id) {
        return NULL;
    }

    OVSREC_FLOW_TABLE_FOR_EACH(ft, ovsdb_handler->idl) {
        tid_s = smap_get(&(ft->external_ids), "table_id");
        if (tid_s && xmlStrEqual(table_id, BAD_CAST tid_s)) {
            return ft;
        }
    }

    return NULL;
}

int
txn_mod_flowtable_name(const xmlChar *table_id, xmlNodePtr node,
                       struct nc_err **e)
{
    xmlChar *value;
    const struct ovsrec_flow_table *ft;

    if (!table_id) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    ft = find_flowtable(table_id);
    if (!ft) {
        nc_verb_error("Flow-table %s was not found, name is not set.",
                      (const char *) table_id);
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "table-id");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Flow table does not exist in OVSDB");
        return EXIT_FAILURE;
    }

    if (node != NULL) {
        /* add */
        value = xmlNodeGetContent(node);
        ovsrec_flow_table_verify_name(ft);
        ovsrec_flow_table_set_name(ft, (const char *) value);
        xmlFree(value);
    } else {
        /* delete */
        ovsrec_flow_table_verify_name(ft);
        ovsrec_flow_table_set_name(ft, "");
    }

    return EXIT_SUCCESS;
}

int
txn_mod_flowtable_resid(const xmlChar *table_id, xmlNodePtr node,
                        struct nc_err **e)
{
    xmlChar *value;
    const struct ovsrec_flow_table *ft;

    if (!table_id) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    ft = find_flowtable(table_id);
    if (ft == NULL) {
        nc_verb_error("Flow-table %s was not found, name is not set.",
                      (const char *) table_id);
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "table-id");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Flow table does not exist in OVSDB");
        return EXIT_FAILURE;
    }

    if (node != NULL) {
        /* add */
        value = node->children ? node->children->content : NULL;
        nc_verb_verbose("Set resource-id (%s) to flow-table (%s).",
                        (const char *) value, (const char *) table_id);
        smap_replace((struct smap *) &ft->external_ids, OFC_RESOURCE_ID,
                     (char *) value);
        ovsrec_flow_table_verify_external_ids(ft);
        ovsrec_flow_table_set_external_ids(ft, &ft->external_ids);
    } else {
        /* delete */
        nc_verb_verbose("Remove flow-table (%s).", (const char *) table_id);
        smap_remove((struct smap *) &ft->external_ids, OFC_RESOURCE_ID);
        ovsrec_flow_table_verify_external_ids(ft);
        ovsrec_flow_table_set_external_ids(ft, &ft->external_ids);
    }

    return EXIT_SUCCESS;
}

int
txn_del_port_tunnel(const xmlChar *port_name, xmlNodePtr tunnel_node,
                    struct nc_err **e)
{
    const struct ovsrec_interface *ifc = NULL;

    nc_verb_verbose("Removing tunnel (%s:%d)", __FILE__, __LINE__);

    OVSREC_INTERFACE_FOR_EACH(ifc, ovsdb_handler->idl) {
        if (xmlStrEqual(port_name, BAD_CAST ifc->name)) {
            break;
        }
    }
    if (!ifc) {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "name");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Port not found in OVSDB.");
        return EXIT_FAILURE;
    }

    ovsrec_interface_verify_options(ifc);
    ovsrec_interface_verify_type(ifc);
    smap_remove((struct smap *) &ifc->options, "local_ip");
    smap_remove((struct smap *) &ifc->options, "remote_ip");
    smap_remove((struct smap *) &ifc->external_ids, "tunnel_type");
    ovsrec_interface_set_options(ifc, &ifc->options);
    ovsrec_interface_set_external_ids(ifc, &ifc->external_ids);

    /* detect interface type */
    if (dev_is_system((const char *) port_name)) {
        ovsrec_interface_set_type(ifc, "system");
    } else {
        ovsrec_interface_set_type(ifc, "internal");
    }

    return EXIT_SUCCESS;
}

int
txn_del_flow_table(const xmlChar *table_id, struct nc_err **e)
{
    const struct ovsrec_flow_table *ft;
    const char *tid_s;

    if (!table_id) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    OVSREC_FLOW_TABLE_FOR_EACH(ft, ovsdb_handler->idl) {
        tid_s = smap_get(&ft->external_ids, "table_id");
        if (tid_s && xmlStrEqual(table_id, BAD_CAST tid_s)) {
            ovsrec_flow_table_delete(ft);
            return EXIT_SUCCESS;
        }
    }

    *e = nc_err_new(NC_ERR_BAD_ELEM);
    nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "table_id");
    nc_err_set(*e, NC_ERR_PARAM_MSG, "Flow-table does not exist in OVSDB");
    return EXIT_FAILURE;
}

/* Remove port reference from the Bridge table */
int
txn_del_bridge_flowtable(const xmlChar *br_name, const xmlChar *table_id,
                         struct nc_err **e)
{
    const struct ovsrec_bridge *bridge;
    const char *tid_s;
    struct ovsrec_flow_table **fts;
    int64_t *fts_keys;
    size_t i, j, n;

    if (!table_id || !br_name) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        if (xmlStrEqual(br_name, BAD_CAST bridge->name)) {
            break;
        }
    }
    if (!bridge) {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "id");
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "Logical-switch does not exist in OVSDB");
        return EXIT_FAILURE;
    }
    if (!bridge->n_flow_tables) {
        /* be silent here - OVSDB IDL do some optimization during the first
         * call to ovsrec_bridge_set_flow_tables() and updates it according
         * to previously removed flow_tables in /capable-switch/resources/
         * The check if the caller requests removing the non-existing
         * element is done earlier.
         */
        return EXIT_SUCCESS;
    }

    n = bridge->n_flow_tables - 1;
    if (!n) {
        /* special case - the last flow-table in the bridge */
        tid_s = smap_get(&(bridge->value_flow_tables[0]->external_ids),
                        "table_id");
        if (!tid_s) {
            nc_verb_error("%s: flow-table with no table-id found in OVSDB");
            *e = nc_err_new(NC_ERR_OP_FAILED);
            nc_err_set(*e, NC_ERR_PARAM_MSG,
                            "Flow-table with no table-id found in OVSDB");
            return EXIT_FAILURE;
        }

        if (!xmlStrEqual(table_id, BAD_CAST tid_s)) {
            /* be silent again for the same reason as earlier in this
             * function
             */
            return EXIT_SUCCESS;
        }

        ovsrec_bridge_verify_flow_tables(bridge);
        ovsrec_bridge_set_flow_tables(bridge, NULL, NULL, 0);
        return EXIT_SUCCESS;
    }

    /* there are still some flow-tables in the bridge, rearrange the array
     * of them
     */
    fts = malloc(n * sizeof *bridge->value_flow_tables);
    fts_keys = malloc(n * sizeof *bridge->key_flow_tables);
    for (i = j = 0; i < bridge->n_flow_tables; i++) {
        tid_s = smap_get(&(bridge->value_flow_tables[i]->external_ids),
                         "table_id");
        if (!tid_s) {
            nc_verb_error("%s: flow-table with no table-id found in OVSDB");
            *e = nc_err_new(NC_ERR_OP_FAILED);
            nc_err_set(*e, NC_ERR_PARAM_MSG,
                       "Flow-table with no table-id found in OVSDB");
            free(fts);
            free(fts_keys);
            return EXIT_FAILURE;
        }

        if (!xmlStrEqual(table_id, BAD_CAST tid_s)) {
            if (j == n) {
                /* be silent again for the same reason as earlier in this
                 * function
                 */
                free(fts);
                free(fts_keys);
                return EXIT_SUCCESS;
            }
            fts[j] = bridge->value_flow_tables[i];
            fts_keys[j] = bridge->key_flow_tables[i];
            j++;
        }
    }

    ovsrec_bridge_verify_flow_tables(bridge);
    ovsrec_bridge_set_flow_tables(bridge, fts_keys, fts, n);
    free(fts);
    free(fts_keys);

    return EXIT_SUCCESS;
}

/* Remove port reference from the Bridge table */
int
txn_del_bridge_port(const xmlChar *br_name, const xmlChar *port_name,
                    struct nc_err **e)
{
    const struct ovsrec_bridge *bridge;
    struct ovsrec_port **ports;
    size_t i, j;

    if (!port_name || !br_name) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        if (xmlStrEqual(br_name, BAD_CAST bridge->name)) {
            break;
        }
    }
    if (!bridge) {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "id");
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "Logical-switch does not exist in OVSDB");
        return EXIT_FAILURE;
    }

    if (!bridge->n_ports) {
        /* be silent here - OVSDB IDL do some optimization during the first
         * call to ovsrec_bridge_set_ports() and updates it according
         * to previously removed ports in /capable-switch/resources/
         * The check if the caller requests removing the non-existing
         * element is done earlier.
         */
        return EXIT_SUCCESS;
    }

    if (bridge->n_ports == 1) {
        /* special case - the last port in the bridge */
        if (!xmlStrEqual(port_name, BAD_CAST bridge->ports[0]->name)) {
            /* be silent again for the same reason as earlier in this
             * function
             */
            return EXIT_SUCCESS;
        }

        ovsrec_bridge_verify_ports(bridge);
        ovsrec_bridge_set_ports(bridge, NULL, 0);
        return EXIT_SUCCESS;
    }

    ports = malloc(sizeof *bridge->ports * (bridge->n_ports - 1));
    for (i = j = 0; j < bridge->n_ports; j++) {
        if (!xmlStrEqual(port_name, BAD_CAST bridge->ports[j]->name)) {
            if (i == bridge->n_ports - 1) {
                /* be silent again for the same reason as earlier in this
                 * function
                 */
                free(ports);
                return EXIT_SUCCESS;
            }
            ports[i] = bridge->ports[j];
            i++;
        }
    }
    ovsrec_bridge_verify_ports(bridge);
    ovsrec_bridge_set_ports(bridge, ports, bridge->n_ports - 1);
    free(ports);

    return EXIT_SUCCESS;
}

/* Finds queue by resource-id and returns pointer to it.  When no queue is
 * found, returns NULL.
 * type specifies what external id is caller looking for, i.e. if it search
 * for the owned or external certificate
 */
static const struct ovsrec_ssl *
find_ssl(const char *type, const xmlChar *resource_id, struct nc_err **e)
{
    const struct ovsrec_ssl *ssl;
    const char *rid;

    if (!resource_id) {
        return NULL;
    }

    OVSREC_SSL_FOR_EACH(ssl, ovsdb_handler->idl) {
        rid = smap_get(&ssl->external_ids, type);
        if (xmlStrEqual(resource_id, BAD_CAST rid)) {
            return ssl;
        }
    }

    nc_verb_error("%s: could not find the SSL table.", __func__);
    *e = nc_err_new(NC_ERR_BAD_ELEM);
    nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "resource_id");
    if (strcmp(type, OFC_RESID_OWN)) {
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "Could not find owned-certificate with the specified resource-id");
    } else {
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "Could not find external-certificate with the specified resource-id");
    }
    return NULL;
}

static int
write_cert(const char *cert, const char *file, struct nc_err **e)
{
    int fd;
    int ret;

    fd = creat(file, 0644);
    if (fd == -1) {
        nc_verb_error("%s: creating the certificate file (%s) failed (%s).",
                      __func__, file, strerror(errno));
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }
    write(fd, "-----BEGIN CERTIFICATE-----\n", 28);
    write(fd, cert, strlen(cert));
    ret = write(fd, "\n-----END CERTIFICATE-----", 26);
    close(fd);
    if (ret < 26) {
        /* if some of the previous writes failed, this one will too */
        nc_verb_error("%s: writing the certificate file (%s) failed.",
                      __func__, file);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int
txn_add_owned_certificate(xmlNodePtr node, struct nc_err **e)
{
    const struct ovsrec_open_vswitch *ovs;
    const struct ovsrec_ssl *ssl;
    xmlNodePtr aux, leaf;
    int fd, ret;
    const xmlChar *new_resid = NULL, *cert = NULL;
    const char *key_type = NULL, *key_data = NULL;
    const char *resid = NULL;

    if (!node) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    /* prepare new structures to set content of the SSL configuration */
    ssl = ovsrec_ssl_first(ovsdb_handler->idl);
    if (ssl) {
        /* there is already some ssl record, compare its resource-id */
        resid = smap_get(&ssl->external_ids, OFC_RESID_OWN);
        new_resid = get_key(node, "resource-id");

        if (!new_resid) {
            nc_verb_error("%s: missing resource-id key of the owned-certificate",
                          __func__);
            *e = nc_err_new(NC_ERR_MISSING_ELEM);
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "resource-id");
            return EXIT_FAILURE;
        }

        if (resid && !xmlStrEqual(new_resid, BAD_CAST resid)) {
            nc_verb_error("%s: adding another owned-certificate",
                          __func__);
            *e = nc_err_new(NC_ERR_OP_FAILED);
            nc_err_set(*e, NC_ERR_PARAM_MSG,
                       "OVS allows to use only a single owned certificate");
            return EXIT_FAILURE;
        }
    } else {
        ssl = ovsrec_ssl_insert(ovsdb_handler->txn);
    }

    /* get data from the data being added */
    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!new_resid && xmlStrEqual(aux->name, BAD_CAST "resource-id")) {
            new_resid = aux->children ? aux->children->content : NULL;
        } else if (xmlStrEqual(aux->name, BAD_CAST "certificate")) {
            cert = aux->children ? aux->children->content : NULL;
        } else if (xmlStrEqual(aux->name, BAD_CAST "private-key")) {
            for (leaf = aux->children; leaf; leaf = leaf->next) {
                if (xmlStrEqual(leaf->name, BAD_CAST "key-type")) {
                    key_type = leaf->children ?
                                    (char *) leaf->children->content : NULL;
                } else if (xmlStrEqual(leaf->name, BAD_CAST "key-data")) {
                    key_data = leaf->children ?
                                    (char *) leaf->children->content : NULL;
                }
            }
        }
    }

    /* check mandatory values */
    if (!new_resid || !cert || !key_type || !key_data) {
        *e = nc_err_new(NC_ERR_MISSING_ELEM);
        if (!new_resid) {
            nc_verb_error("%s: missing mandatory node \"resource-id\"",
                          __func__);
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "resource-id");
        } else if (!cert) {
            nc_verb_error("%s: missing mandatory node \"certificate\"",
                          __func__);
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "certificate");
        } else if (!key_type) {
            nc_verb_error("%s: missing mandatory node \"key-type\"", __func__);
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "key-type");
        } else if (!key_data) {
            nc_verb_error("%s: missing mandatory node \"key-data\"", __func__);
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "key-data");
        }
        return EXIT_FAILURE;
    }

    /* store the data */
    /* certificate */
    if (write_cert((char *) cert, OFC_DATADIR "/cert.pem", e)) {
        return (EXIT_FAILURE);
    }
    ovsrec_ssl_verify_certificate(ssl);
    ovsrec_ssl_set_certificate(ssl, OFC_DATADIR "/cert.pem");

    /* private-key */
    fd = creat(OFC_DATADIR "/key.pem", 0600);
    if (fd == -1) {
        nc_verb_error("%s: creating the private key file failed (%s).",
                      __func__, strerror(errno));
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }
    write(fd, "-----BEGIN ", 11);
    write(fd, key_type, strlen(key_type));
    write(fd, " PRIVATE KEY-----\n", 18);
    write(fd, key_data, strlen(key_data));
    write(fd, "\n-----END ", 10);
    write(fd, key_type, strlen(key_type));
    ret = write(fd, " PRIVATE KEY-----", 17);
    close(fd);
    if (ret < 17) {
        /* if some of the previous writes failed, this one will too */
        nc_verb_error("%s: writing the private key failed.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }
    ovsrec_ssl_verify_private_key(ssl);
    ovsrec_ssl_set_private_key(ssl, OFC_DATADIR "/key.pem");

    /* resource-id */
    smap_replace((struct smap *) &ssl->external_ids, OFC_RESID_OWN,
                 (const char*) new_resid);
    ovsrec_ssl_verify_external_ids(ssl);
    ovsrec_ssl_set_external_ids(ssl, &ssl->external_ids);

    /* Get the Open_vSwitch table for linking the SSL structure into and thus
     * force all the bridges to use it.
     */
    ovs = ovsrec_open_vswitch_first(ovsdb_handler->idl);
    if (!ovs) {
        ovs = ovsrec_open_vswitch_insert(ovsdb_handler->txn);
    }
    ovsrec_open_vswitch_verify_ssl(ovs);
    ovsrec_open_vswitch_set_ssl(ovs, ssl);

    return EXIT_SUCCESS;
}

int
txn_add_external_certificate(xmlNodePtr node, struct nc_err **e)
{
    const struct ovsrec_open_vswitch *ovs;
    const struct ovsrec_ssl *ssl;
    const char *resid = NULL;
    xmlNodePtr aux;
    const xmlChar *new_resid = NULL, *cert = NULL;

    if (!node) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    /* prepare new structures to set content of the SSL configuration */
    ssl = ovsrec_ssl_first(ovsdb_handler->idl);
    if (ssl) {
        /* there is already some ssl record, compare its resource-id */
        resid = smap_get(&ssl->external_ids, OFC_RESID_EXT);
        new_resid = get_key(node, "resource-id");

        if (!new_resid) {
            nc_verb_error("%s: missing resource-id key of the external-certificate",
                          __func__);
            *e = nc_err_new(NC_ERR_MISSING_ELEM);
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "resource-id");
            return EXIT_FAILURE;
        }

        if (resid && !xmlStrEqual(new_resid, BAD_CAST resid)) {
            nc_verb_error("%s: adding another external-certificate",
                          __func__);
            *e = nc_err_new(NC_ERR_OP_FAILED);
            nc_err_set(*e, NC_ERR_PARAM_MSG,
                       "OVS allows to use only a single external certificate");
            return EXIT_FAILURE;
        }
    } else {
        ssl = ovsrec_ssl_insert(ovsdb_handler->txn);
    }

    /* get data from the data being added */
    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!new_resid && xmlStrEqual(aux->name, BAD_CAST "resource-id")) {
            new_resid = aux->children ? aux->children->content : NULL;
        } else if (xmlStrEqual(aux->name, BAD_CAST "certificate")) {
            cert = aux->children ? aux->children->content : NULL;
        }
    }

    /* check mandatory values */
    if (!new_resid || !cert) {
        *e = nc_err_new(NC_ERR_MISSING_ELEM);
        if (!new_resid) {
            nc_verb_error("%s: missing mandatory node \"resource-id\"",
                          __func__);
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "resource-id");
        } else if (!cert) {
            nc_verb_error("%s: missing mandatory node \"certificate\"",
                          __func__);
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "certificate");
        }
        return EXIT_FAILURE;
    }

    /* store the data */
    /* certificate */
    if (write_cert((char *) cert, OFC_DATADIR "/ca_cert.pem", e)) {
        return (EXIT_FAILURE);
    }
    ovsrec_ssl_verify_ca_cert(ssl);
    ovsrec_ssl_set_ca_cert(ssl, OFC_DATADIR "/ca_cert.pem");

    /* resource-id */
    smap_replace((struct smap *) &ssl->external_ids, OFC_RESID_EXT,
                 (const char*) new_resid);
    ovsrec_ssl_verify_external_ids(ssl);
    ovsrec_ssl_set_external_ids(ssl, &ssl->external_ids);

    /* Get the Open_vSwitch table for linking the SSL structure into and thus
     * force all the bridges to use it.
     */
    ovs = ovsrec_open_vswitch_first(ovsdb_handler->idl);
    if (!ovs) {
        ovs = ovsrec_open_vswitch_insert(ovsdb_handler->txn);
    }
    ovsrec_open_vswitch_verify_ssl(ovs);
    ovsrec_open_vswitch_set_ssl(ovs, ssl);

    return EXIT_SUCCESS;
}

int
txn_mod_own_cert_certificate(const xmlChar * resid, xmlNodePtr node,
                             struct nc_err **e)
{
    const struct ovsrec_ssl *ssl;
    xmlChar *cert;

    if (!resid) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    ssl = find_ssl(OFC_RESID_OWN, resid, e);
    if (!ssl) {
        return EXIT_FAILURE;
    }

    if (!node) {
        /* remove */
        unlink(OFC_DATADIR "/cert.pem");
        ovsrec_ssl_set_ca_cert(ssl, "");
    } else {
        /* add */
        cert = node->children ? node->children->content : NULL;
        if (!cert) {
            nc_verb_error("%s: certificate element is empty.", __func__);
            *e = nc_err_new(NC_ERR_BAD_ELEM);
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "certificate");
            nc_err_set(*e, NC_ERR_PARAM_MSG,
                       "No data in owned-certificate");
            return EXIT_FAILURE;
        }

        if (write_cert((char *) cert, OFC_DATADIR "/cert.pem", e)) {
            return (EXIT_FAILURE);
        }
        ovsrec_ssl_verify_certificate(ssl);
        ovsrec_ssl_set_certificate(ssl, OFC_DATADIR "/cert.pem");
    }

    return EXIT_SUCCESS;
}

static int
parse_pem(const char *file, char **pem, const char **pem_start,
          const char **pem_end, struct nc_err **e)
{
    int fd;
    size_t size;

    fd = open(file, O_RDWR);
    if (fd == -1) {
        nc_verb_error("%s: could not open the private key file (%s).",
                      __func__, strerror(errno));
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return -1;
    }
    size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    *pem = malloc(size + 1);
    if (*pem == NULL) {
        nc_verb_error("%s: malloc() failed.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        close(fd);
        return -1;
    }
    if (read(fd, *pem, size) < size) {
        nc_verb_error("%s: reading the private key failed.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        free(*pem);
        close(fd);
        return -1;
    }
    (*pem)[size] = '\0';

    *pem_start = strstr(*pem, "-----BEGIN RSA PRIVATE KEY-----\n");
    *pem_end = strstr(*pem, "\n-----END RSA PRIVATE KEY-----");
    if (*pem_start == NULL) {
        *pem_start = strstr(*pem, "-----BEGIN DSA PRIVATE KEY-----\n");
        *pem_end = strstr(*pem, "\n-----END DSA PRIVATE KEY-----");
    }
    if (*pem_start == NULL || *pem_end == NULL) {
        nc_verb_error("%s: private key is invalid.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        free(*pem);
        close(fd);
        return -1;
    }

    return fd;
}

int
txn_mod_own_cert_key_type(const xmlChar *res_id, xmlNodePtr node,
                          struct nc_err **e)
{
    const struct ovsrec_ssl *ssl;
    xmlChar *type;
    char *pem;
    const char *pem_start, *pem_end;
    int fd;

    if (!res_id) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    if (!node) {
        /* key-type was removed, we don't care much, it will be added soon */
        return EXIT_SUCCESS;
    }

    /* change the type of the stored key, data are not touched */
    type = node->children ? node->children->content : NULL;
    if (!type) {
        nc_verb_error("%s: key-type element is empty.", __func__);
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "key-type");
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "No data in owned-certificate's private-key type");
    }

    ssl = find_ssl(OFC_RESID_OWN, res_id, e);
    if (!ssl) {
        return EXIT_FAILURE;
    }

    fd = parse_pem(ssl->private_key, &pem, &pem_start, &pem_end, e);
    if (fd == -1) {
        return EXIT_FAILURE;
    }

    /* get pointer to the key type string */
    pem_start += 11;
    pem_end += 10;

    /* and move the file descriptor cursor to the type */
    lseek(fd, pem_start - pem, SEEK_SET);

    /* and rewrite the value */
    if (write(fd, type, xmlStrlen(type)) < xmlStrlen(type)) {
        nc_verb_error("%s: private key is invalid.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        free(pem);
        close(fd);
        return EXIT_FAILURE;
    }

    /* and the same for the end marker */
    lseek(fd, pem_end - pem, SEEK_SET);
    if (write(fd, type, xmlStrlen(type)) < xmlStrlen(type)) {
        nc_verb_error("%s: private key is invalid.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        free(pem);
        close(fd);
        return EXIT_FAILURE;
    }

    free(pem);
    close(fd);

    return EXIT_SUCCESS;
}

int
txn_mod_own_cert_key_data(const xmlChar * res_id, xmlNodePtr node,
                          struct nc_err **e)
{
    const struct ovsrec_ssl *ssl;
    xmlChar *data;
    char *pem;
    const char *pem_start, *pem_end;
    int fd;

    if (!res_id) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    if (!node) {
        /* key-data were removed, we don't care much, they will be added soon
         */
        return EXIT_SUCCESS;
    }

    /* change the data of the key, type is taken from previous content */
    data = node->children ? node->children->content : NULL;
    if (!data) {
        nc_verb_error("%s: key-data element is empty.", __func__);
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "key-data");
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "No data in owned-certificate's private-key data");
    }

    ssl = find_ssl(OFC_RESID_OWN, res_id, e);
    if (!ssl) {
        return EXIT_FAILURE;
    }

    fd = parse_pem(ssl->private_key, &pem, &pem_start, &pem_end, e);
    if (fd == -1) {
        return EXIT_FAILURE;
    }

    /* rewrite the complete content of the file */
    if (ftruncate(fd, 0) == -1) {
        nc_verb_error("%s: failed to truncate the private key (%s).", __func__,
                      strerror(errno));
        *e = nc_err_new(NC_ERR_OP_FAILED);
        free(pem);
        close(fd);
        return EXIT_FAILURE;
    }
    lseek(fd, 0, SEEK_SET);

    write(fd, pem_start, 32);
    write(fd, data, xmlStrlen(data));
    if (write(fd, pem_end, 30) < 30) {
        nc_verb_error("%s: writing the private key failed.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        free(pem);
        close(fd);
        return EXIT_FAILURE;
    }

    free(pem);
    close(fd);

    return EXIT_SUCCESS;
}

int
txn_mod_ext_cert_certificate(const xmlChar *resid, xmlNodePtr node,
                             struct nc_err **e)
{
    const struct ovsrec_ssl *ssl;
    xmlChar *cert;

    if (!resid) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    ssl = find_ssl(OFC_RESID_EXT, resid, e);
    if (!ssl) {
        return EXIT_FAILURE;
    }

    if (!node) {
        /* remove */
        unlink(OFC_DATADIR "/ca_cert.pem");
        ovsrec_ssl_set_ca_cert(ssl, "");
    } else {
        /* add */
        cert = node->children ? node->children->content : NULL;
        if (!cert) {
            nc_verb_error("%s: could not find the SSL table.", __func__);
            *e = nc_err_new(NC_ERR_BAD_ELEM);
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "certificate");
            nc_err_set(*e, NC_ERR_PARAM_MSG,
                       "No data in external-certificate");
            return EXIT_FAILURE;
        }

        if (write_cert((char *) cert, OFC_DATADIR "/ca_cert.pem", e)) {
            return (EXIT_FAILURE);
        }
        ovsrec_ssl_verify_ca_cert(ssl);
        ovsrec_ssl_set_ca_cert(ssl, OFC_DATADIR "/ca_cert.pem");
    }

    return EXIT_SUCCESS;
}

int
txn_del_owned_certificate(xmlNodePtr node, struct nc_err **e)
{
    const struct ovsrec_open_vswitch *ovs;
    const struct ovsrec_ssl *ssl;
    const xmlChar *resid;

    if (!node) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    resid = get_key(node, "resource-id");
    if (!resid) {
        nc_verb_error("%s: missing resource-id element.", __func__);
        *e = nc_err_new(NC_ERR_MISSING_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "resource_id");
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "owned-certificate has no resource-id specified");
        return EXIT_FAILURE;
    }

    ssl = find_ssl(OFC_RESID_OWN, resid, e);
    if (!ssl) {
        return EXIT_FAILURE;
    }

    /* remove the SSL table if ext-cert was removed as well */
    if (smap_get(&ssl->external_ids, OFC_RESID_EXT) == NULL) {
        ovs = ovsrec_open_vswitch_first(ovsdb_handler->idl);
        if (!ovs) {
            nc_verb_error("%s: could not find the Open vSwitch table.",
                          __func__);
            *e = nc_err_new(NC_ERR_OP_FAILED);
            nc_err_set(*e, NC_ERR_PARAM_MSG, "OVSDB content is corrupted");
            return EXIT_FAILURE;
        }
        ovsrec_open_vswitch_verify_ssl(ovs);
        ovsrec_open_vswitch_set_ssl(ovs, NULL);
        ovsrec_ssl_delete(ssl);
    } else {
        /* remove certificate */
        unlink(OFC_DATADIR "/cert.pem");
        ovsrec_ssl_verify_certificate(ssl);
        ovsrec_ssl_set_certificate(ssl, "");

        /* remove private key */
        unlink(OFC_DATADIR "/key.pem");
        ovsrec_ssl_verify_private_key(ssl);
        ovsrec_ssl_set_private_key(ssl, "");

        /* remove the owned-certificate resource-id */
        smap_remove((struct smap *) &ssl->external_ids, OFC_RESID_OWN);
        ovsrec_ssl_verify_external_ids(ssl);
        ovsrec_ssl_set_external_ids(ssl, &ssl->external_ids);
    }

    return EXIT_SUCCESS;
}

int
txn_del_external_certificate(xmlNodePtr node, struct nc_err **e)
{
    const struct ovsrec_open_vswitch *ovs;
    const struct ovsrec_ssl *ssl;
    const xmlChar *resid;

    if (!node) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    resid = get_key(node, "resource-id");
    if (!resid) {
        nc_verb_error("%s: missing resource-id element.", __func__);
        *e = nc_err_new(NC_ERR_MISSING_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "resource_id");
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "external-certificate has no resource-id specified");
        return EXIT_FAILURE;
    }

    ssl = find_ssl(OFC_RESID_EXT, resid, e);
    if (!ssl) {
        return EXIT_FAILURE;
    }

    /* remove the SSL table if own-cert was removed as well */
    if (smap_get(&ssl->external_ids, OFC_RESID_OWN) == NULL) {
        ovs = ovsrec_open_vswitch_first(ovsdb_handler->idl);
        if (!ovs) {
            nc_verb_error("%s: could not find the Open vSwitch table.",
                          __func__);
            *e = nc_err_new(NC_ERR_OP_FAILED);
            nc_err_set(*e, NC_ERR_PARAM_MSG, "OVSDB content is corrupted");
            return EXIT_FAILURE;
        }
        ovsrec_open_vswitch_verify_ssl(ovs);
        ovsrec_open_vswitch_set_ssl(ovs, NULL);
        ovsrec_ssl_delete(ssl);
    } else {
        /* remove certificate */
        smap_remove((struct smap *) &ssl->external_ids, OFC_RESID_EXT);
        unlink(OFC_DATADIR "/ca_cert.pem");
        ovsrec_ssl_verify_ca_cert(ssl);
        ovsrec_ssl_set_ca_cert(ssl, "");

        /* remove the external-certificate resource-id */
        smap_remove((struct smap *) &ssl->external_ids, OFC_RESID_EXT);
        ovsrec_ssl_verify_external_ids(ssl);
        ovsrec_ssl_set_external_ids(ssl, &ssl->external_ids);
    }

    return EXIT_SUCCESS;
}

int
txn_add_bridge_port(const xmlChar *br_name, const xmlChar *port_name,
                    struct nc_err **e)
{
    const struct ovsrec_bridge *bridge;
    const struct ovsrec_port *port;
    struct ovsrec_port **ports;
    size_t i;

    if (!port_name || !br_name) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
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
        nc_verb_error("%s: %s not found", __func__, port ? "bridge" : "port");
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        if (!port) {
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "port");
            nc_err_set(*e, NC_ERR_PARAM_MSG, "Invalid port leafref");
        } else {
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "id");
            nc_err_set(*e, NC_ERR_PARAM_MSG,
                       "Logical switch does not exist in OVSDB");
        }
        return EXIT_FAILURE;
    }
    nc_verb_verbose("Add port %s to %s bridge resource list.",
                    BAD_CAST port_name, BAD_CAST br_name);

    ports = malloc(sizeof *bridge->ports * (bridge->n_ports + 1));
    for (i = 0; i < bridge->n_ports; i++) {
        ports[i] = bridge->ports[i];
    }
    ports[i] = (struct ovsrec_port *) port;

    ovsrec_bridge_verify_ports(bridge);
    ovsrec_bridge_set_ports(bridge, ports, bridge->n_ports + 1);
    free(ports);

    return EXIT_SUCCESS;
}

static int
txn_bridge_insert_flowtable(const struct ovsrec_bridge *bridge,
                            struct ovsrec_flow_table *ft, struct nc_err **e)
{
    int64_t *fts_keys;
    struct ovsrec_flow_table **fts;
    const char *tid_s;
    int64_t tid = 0;
    size_t i;

    if (!bridge || !ft) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    tid_s = smap_get(&ft->external_ids, "table_id");
    if (tid_s) {
        sscanf((const char *) tid_s, "%" SCNi64, &tid);
    } else {
        nc_verb_error("%s: invalid flow table with missing table_id");
        *e = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "Invalid flow table record, internal error");
        return EXIT_FAILURE;
    }

    fts = malloc(sizeof *bridge->value_flow_tables *
                 (bridge->n_flow_tables + 1));
    fts_keys = malloc(sizeof *bridge->key_flow_tables *
                      (bridge->n_flow_tables + 1));
    for (i = 0; i < bridge->n_flow_tables; i++) {
        fts[i] = bridge->value_flow_tables[i];
        fts_keys[i] = bridge->key_flow_tables[i];
    }
    fts[i] = (struct ovsrec_flow_table *) ft;
    fts_keys[i] = tid;

    ovsrec_bridge_verify_flow_tables(bridge);
    ovsrec_bridge_set_flow_tables(bridge, fts_keys, fts,
                                  bridge->n_flow_tables + 1);

    free(fts);
    free(fts_keys);

    return EXIT_SUCCESS;
}

int
txn_add_bridge_flowtable(const xmlChar *br_name, const xmlChar *table_id,
                         struct nc_err **e)
{
    const struct ovsrec_bridge *bridge;
    const struct ovsrec_flow_table *ft;
    const char *tid_s;
    int64_t tid = 0;
    int64_t *fts_keys;
    struct ovsrec_flow_table **fts;
    int i;

    if (!table_id || !br_name) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        if (xmlStrEqual(br_name, BAD_CAST bridge->name)) {
            break;
        }
    }
    OVSREC_FLOW_TABLE_FOR_EACH(ft, ovsdb_handler->idl) {
        tid_s = smap_get(&ft->external_ids, "table_id");
        if (tid_s && xmlStrEqual(table_id, BAD_CAST tid_s)) {
            sscanf((const char *) tid_s, "%" SCNi64, &tid);
            break;
        }
    }

    if (!ft || !bridge) {
        nc_verb_error("%s: %s not found", __func__,
                      ft ? "bridge" : "flow-table");
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        if (!ft) {
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "flow-table");
            nc_err_set(*e, NC_ERR_PARAM_MSG, "Invalid flow-table leafref");
        } else {
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "id");
            nc_err_set(*e, NC_ERR_PARAM_MSG,
                       "Logical switch does not exist in OVSDB");
        }
        return EXIT_FAILURE;
    }

    fts = malloc(sizeof *bridge->value_flow_tables *
                 (bridge->n_flow_tables + 1));
    fts_keys = malloc(sizeof *bridge->key_flow_tables *
                      (bridge->n_flow_tables + 1));
    for (i = 0; i < bridge->n_flow_tables; i++) {
        fts[i] = bridge->value_flow_tables[i];
        fts_keys[i] = bridge->key_flow_tables[i];
    }
    fts[i] = (struct ovsrec_flow_table *) ft;
    fts_keys[i] = tid;

    ovsrec_bridge_verify_flow_tables(bridge);
    ovsrec_bridge_set_flow_tables(bridge, fts_keys, fts,
                                  bridge->n_flow_tables + 1);

    free(fts);
    free(fts_keys);

    return EXIT_SUCCESS;
}

int
txn_del_bridge(const xmlChar *br_name, struct nc_err **e)
{
    struct ovsrec_bridge **bridges;
    const struct ovsrec_bridge *bridge = NULL;
    const struct ovsrec_open_vswitch *ovs;
    size_t i, j;

    if (!br_name) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    /* remove bridge reference from Open_vSwitch table */
    ovs = ovsrec_open_vswitch_first(ovsdb_handler->idl);
    if (!ovs) {
        ovs = ovsrec_open_vswitch_insert(ovsdb_handler->txn);
    }

    bridges = malloc(sizeof *ovs->bridges * (ovs->n_bridges - 1));
    for (i = j = 0; j < ovs->n_bridges; j++) {
        if (!xmlStrEqual(br_name, BAD_CAST ovs->bridges[j]->name)) {
            if (i == ovs->n_bridges - 1) {
                *e = nc_err_new(NC_ERR_BAD_ELEM);
                nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "name");
                nc_err_set(*e, NC_ERR_PARAM_MSG, "switch to remove not found");
                free(bridges);
                return EXIT_FAILURE;
            }
            bridges[i] = ovs->bridges[j];
            i++;
        } else {
            bridge = ovs->bridges[j];
        }
    }

    if (bridge) {
        ovsrec_open_vswitch_verify_bridges(ovs);
        ovsrec_open_vswitch_set_bridges(ovs, bridges, ovs->n_bridges - 1);

        /* remove bridge itself */
        ovsrec_bridge_delete(bridge);
    }
    free(bridges);

    return EXIT_SUCCESS;
}

int
txn_del_queue(const xmlChar *rid, struct nc_err **e)
{
    const struct ovsrec_port *port = NULL;
    const struct ovsrec_qos *qos;
    const struct ovsrec_queue *queue;
    int i;
    const char *rid2;

    if (!rid) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    /* remove links to the queue from port/QoS */
    port = find_queue_port((const char *)rid);
    if (port) {
        /* queue is used by some port */
        if (txn_del_queue_port(rid, e)) {
            return EXIT_FAILURE;
        }
    } else {
        /* the queue has no reference to port, so we have to check references
         * to queues in qos records manually
         */
        OVSREC_QOS_FOR_EACH(qos, ovsdb_handler->idl) {
            if (qos->n_queues == 0) {
                continue;
            }

            for (i = 0; i < qos->n_queues; i++) {
                rid2 = smap_get(&qos->value_queues[i]->external_ids,
                                OFC_RESOURCE_ID);
                if (rid2 && xmlStrEqual(rid, BAD_CAST rid2)) {
                    if (txn_del_qos_queue(&qos, i, e)) {
                        return EXIT_FAILURE;
                    }
                    goto remove_queue;
                }
            }
        }
    }

remove_queue:
    /* Remove the queue itself */
    queue = find_queue(rid);
    if (!queue) {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "resource-id");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Queue does not exist in OVSDB");
        return EXIT_FAILURE;
    }
    ovsrec_queue_delete(queue);

    return EXIT_SUCCESS;
}

/* /capable-switch/logical-switches/switch/controllers/controller/local-ip-address */
int
txn_mod_contr_lip(const xmlChar *contr_id, const xmlChar* value,
                  struct nc_err **e)
{
    const struct ovsrec_controller *contr;
    const char *aux;

    if (!contr_id) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    OVSREC_CONTROLLER_FOR_EACH(contr, ovsdb_handler->idl) {
        aux = smap_get(&(contr->external_ids), "ofconfig-id");
        if (aux && xmlStrEqual(contr_id, BAD_CAST aux)) {
            break;
        }
    }
    if (!contr) {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "id");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Controller does not exist in OVSDB");
        return EXIT_FAILURE;
    }

    ovsrec_controller_set_local_ip(contr, (char*)value);

    return EXIT_SUCCESS;
}

/* /capable-switch/logical-switches/switch/controllers/controller */
int
txn_add_contr(xmlNodePtr node, const xmlChar *br_name, struct nc_err **e)
{
    struct ovsrec_controller *contr;
    struct ovsrec_controller **contrs;
    const struct ovsrec_bridge *br;
    xmlNodePtr aux;
    const char *proto = "ssl", *ip = NULL, *port = NULL;
    char *target = NULL;
    int i;

    if (!br_name || !node) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    contr = ovsrec_controller_insert(ovsdb_handler->txn);
    ovsrec_controller_set_connection_mode(contr, "in-band");

    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(aux->name, BAD_CAST "id")) {
            smap_replace(&(contr->external_ids), "ofconfig-id",
                         (char *) aux->children->content);
            ovsrec_controller_set_external_ids(contr, &(contr->external_ids));
        } else if (xmlStrEqual(aux->name, BAD_CAST "ip-address")) {
            ip = (const char *) aux->children->content;
        } else if (xmlStrEqual(aux->name, BAD_CAST "port")) {
            port = (const char *) aux->children->content;
        } else if (xmlStrEqual(aux->name, BAD_CAST "protocol")) {
            if (xmlStrEqual(aux->children->content, BAD_CAST "tcp")) {
                proto = "tcp";
            }
        } else if (xmlStrEqual(aux->name, BAD_CAST "local-ip-address")) {
            ovsrec_controller_verify_local_ip(contr);
            ovsrec_controller_set_local_ip(contr,
                                           (char *) aux->children->content);
        }
    }

    if (!ip) {
        *e = nc_err_new(NC_ERR_MISSING_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "ip-address");
        return EXIT_FAILURE;
    }

    asprintf(&target, "%s:%s%s%s", proto, ip, port ? ":" : "",
             port ? port : "");
    ovsrec_controller_verify_target(contr);
    ovsrec_controller_set_target(contr, target);
    free(target);

    /* add controller into the bridge */
    OVSREC_BRIDGE_FOR_EACH(br, ovsdb_handler->idl) {
        if (xmlStrEqual(br_name, BAD_CAST br->name)) {
            break;
        }
    }
    if (!br) {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "id");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Bridge does not exist in OVSDB");
        return EXIT_FAILURE;
    }

    contrs = malloc(sizeof *br->controller * (br->n_controller + 1));
    for (i = 0; i < br->n_controller; i++) {
        contrs[i] = br->controller[i];
    }
    contrs[i] = contr;

    ovsrec_bridge_set_controller(br, contrs, br->n_controller + 1);
    free(contrs);

    return EXIT_SUCCESS;
}

/* /capable-switch/logical-switches/switch/controllers/controller */
int
txn_del_contr(const xmlChar *contr_id, const xmlChar *br_name,
              struct nc_err **e)
{
    const struct ovsrec_bridge *br;
    const struct ovsrec_controller *contr;
    struct ovsrec_controller **contrs;
    const char *aux;
    int i, j;

    if (!contr_id || !br_name) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    OVSREC_CONTROLLER_FOR_EACH(contr, ovsdb_handler->idl) {
        aux = smap_get(&(contr->external_ids), "ofconfig-id");
        if (aux && xmlStrEqual(contr_id, BAD_CAST aux)) {
            break;
        }
    }
    if (!contr) {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "id");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Controller does not exist in OVSDB");
        return EXIT_FAILURE;
    }

    /* remove controller from the bridge */
    OVSREC_BRIDGE_FOR_EACH(br, ovsdb_handler->idl) {
        if (xmlStrEqual(br_name, BAD_CAST br->name)) {
            break;
        }
    }
    if (!br) {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "id");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Bridge does not exist in OVSDB");
        return EXIT_FAILURE;
    }

    switch (br->n_controller) {
    case 0:
missing_contr_error:
        *e = nc_err_new(NC_ERR_OP_FAILED);
        nc_err_set(*e, NC_ERR_PARAM_MSG,
                   "Controller to delete is not connected with the specified bridge");
        return EXIT_FAILURE;
        break;
    case 1:
        contrs = NULL;
        break;
    default:
        contrs = malloc(sizeof *br->controller * (br->n_controller - 1));
        for (i = j = 0; i < br->n_controller; i++) {
            if (br->controller[i] != contr) {
                if (j == br->n_controller - 1) {
                    goto missing_contr_error;
                }
                contrs[j] = br->controller[i];
                j++;
            }
        }
    }
    ovsrec_bridge_set_controller(br, contrs, br->n_controller - 1);

    /* remove the controller itself */
    ovsrec_controller_delete(contr);

    return EXIT_SUCCESS;
}

/* /capable-switch/logical-switches/switch/controllers/controller/--
 * -- ip-address, port, protocol
 */
int
txn_mod_contr_target(const xmlChar *contr_id, const xmlChar *name,
                     const xmlChar *value, struct nc_err **e)
{
    const struct ovsrec_controller *contr;
    char *aux;
    const char *p;

    if (!contr_id || !name) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    OVSREC_CONTROLLER_FOR_EACH(contr, ovsdb_handler->idl) {
        p = smap_get(&(contr->external_ids), "ofconfig-id");
        if (p && xmlStrEqual(contr_id, BAD_CAST p)) {
            break;
        }
    }
    if (!contr) {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "id");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Controller does not exist in OVSDB");
        return EXIT_FAILURE;
    }

    ovsrec_controller_verify_target(contr);
    if (!contr->target) {
        /* prepare empty template */
        ovsrec_controller_set_target(contr, "ssl:");
    }

    if (xmlStrEqual(name, BAD_CAST "port")) {
        /* hide the port from the current value in controller structure */
        aux = index(contr->target, ':');        /* protocol_:_ip */
        aux = index(aux + 1, ':');  /* ip_:_port */
        if (aux != NULL) {
            *aux = '\0';
        }
        if (value) {
            asprintf(&aux, "%s:%s", contr->target, value);
            ovsrec_controller_set_target(contr, aux);
            free(aux);
        } /* else delete - keep the value with hidden port */
    } else if (xmlStrEqual(name, BAD_CAST "protocol")) {
        if (value && xmlStrEqual(value, BAD_CAST "tcp")) {
            contr->target[0] = 't';
            contr->target[1] = 'c';
            contr->target[2] = 'p';
        } else {
            /* covers also delete, when we use the default value - tls (ssl) */
            contr->target[0] = 's';
            contr->target[1] = 's';
            contr->target[2] = 'l';
        }
    } else if (xmlStrEqual(name, BAD_CAST "ip-address")) {
        if (value) {
            p = index(contr->target, ':');      /* protocol_:_ip */
            p = index(p + 1, ':');  /* ip_:_port */
            if (p) {
                asprintf(&aux, "xxx:%s:%s", value, p + 1);
            } else {
                asprintf(&aux, "xxx:%s", value);
            }
            /* copy protocol from the current value */
            memcpy(aux, contr->target, 3);
            ovsrec_controller_set_target(contr, aux);
            free(aux);
        } else {
            /* ip-address is mandatory, so it cannot be deleted. However,
             * when replacing the value, we do delete + create. So, we do
             * nothing, since this delete must be followed by create
             * implemented by this function. */
        }
    }

    return EXIT_SUCCESS;
}

/* /capable-switch/logical-switches/switch/datapath-id */
int
txn_mod_bridge_datapath(const xmlChar *br_name, const xmlChar* value,
                        struct nc_err **e)
{
    int i, j;
    static char dp[17];
    const struct ovsrec_bridge *bridge;

    if (!br_name) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        if (xmlStrEqual(br_name, BAD_CAST bridge->name)) {
            break;
        }
    }
    if (!bridge) {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "id");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Bridge does not exist in OVSDB");
        return EXIT_FAILURE;
    }

    ovsrec_bridge_verify_other_config(bridge);
    if (value) {
        /* set */
        if (xmlStrlen(value) != 23) {
            nc_verb_error("Invalid datapath (%s)", value);

            *e = nc_err_new(NC_ERR_BAD_ELEM);
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "datapath-id");
            nc_err_set(*e, NC_ERR_PARAM_MSG, "Invalid value to set");
            return EXIT_FAILURE;
        }

        for (i = j = 0; i < 17; i++, j++) {
            while (j < xmlStrlen(value) && value[j] == ':') {
                j++;
            }
            dp[i] = value[j];
        }

        smap_replace((struct smap *) &bridge->other_config, "datapath-id", dp);
    } else {
        /* delete */
        smap_remove((struct smap *) &bridge->other_config, "datapath-id");
    }

    ovsrec_bridge_verify_other_config(bridge);
    ovsrec_bridge_set_other_config(bridge, &bridge->other_config);

    return EXIT_SUCCESS;
}

/* /capable-switch/logical-switches/switch/lost-connection-behavior */
int
txn_mod_bridge_failmode(const xmlChar *br_name, const xmlChar* value,
                        struct nc_err **e)
{
    const struct ovsrec_bridge *bridge;

    if (!br_name) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        if (xmlStrEqual(br_name, BAD_CAST bridge->name)) {
            break;
        }
    }
    if (!bridge) {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "id");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Bridge does not exist in OVSDB");
        return EXIT_FAILURE;
    }

    if (value && xmlStrEqual(value, BAD_CAST "failStandaloneMode")) {
        ovsrec_bridge_set_fail_mode(bridge, "standalone");
    } else {
        /* default mode */
        ovsrec_bridge_set_fail_mode(bridge, "secure");
    }

    return EXIT_SUCCESS;
}

/* Insert bridge reference into the Open_vSwitch table */
static int
txn_ovs_insert_bridge(const struct ovsrec_open_vswitch *ovs,
                      struct ovsrec_bridge *bridge, struct nc_err **e)
{
    assert(ovs);
    assert(bridge);

    struct ovsrec_bridge **bridges;
    size_t i;

    bridges = malloc(sizeof *ovs->bridges * (ovs->n_bridges + 1));
    for (i = 0; i < ovs->n_bridges; i++) {
        bridges[i] = ovs->bridges[i];
    }
    bridges[ovs->n_bridges] = bridge;
    ovsrec_open_vswitch_verify_bridges(ovs);
    ovsrec_open_vswitch_set_bridges(ovs, bridges, ovs->n_bridges + 1);
    free(bridges);

    return EXIT_SUCCESS;
}

/* Insert port reference into the Bridge table */
static int
txn_bridge_insert_port(const struct ovsrec_bridge *bridge,
                       struct ovsrec_port *port, struct nc_err **e)
{
    struct ovsrec_port **ports;
    size_t i;

    if (!bridge || !port) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    ports = malloc(sizeof *bridge->ports * (bridge->n_ports + 1));
    for (i = 0; i < bridge->n_ports; i++) {
        ports[i] = bridge->ports[i];
    }
    ports[bridge->n_ports] = port;
    ovsrec_bridge_verify_ports(bridge);
    ovsrec_bridge_set_ports(bridge, ports, bridge->n_ports + 1);
    free(ports);

    return EXIT_SUCCESS;
}

int
txn_add_bridge(xmlNodePtr node, struct nc_err **e)
{
    const struct ovsrec_open_vswitch *ovs;
    const struct ovsrec_bridge *bridge;
    const struct ovsrec_port *port = NULL;
    struct ovsrec_flow_table *ft = NULL;
    xmlNodePtr aux, leaf;
    xmlChar *xmlval, *bridge_id = NULL;
    int failmode_flag = 0;

    if (!node) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }
    /* find id */
    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(aux->name, BAD_CAST "id")) {
            bridge_id = aux->children ? aux->children->content : NULL;
            break;
        }
    }

    /* check for existing bridge id */
    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        if (xmlStrEqual(bridge_id, BAD_CAST bridge->name)) {
            /* bridge already exists */
            *e = nc_err_new(NC_ERR_BAD_ELEM);
            nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "id");
            nc_err_set(*e, NC_ERR_PARAM_MSG, "Bridge already exists in OVSDB");
            return EXIT_FAILURE;
        }
    }

    /* get the Open_vSwitch table for bridge links manipulation */
    ovs = ovsrec_open_vswitch_first(ovsdb_handler->idl);
    if (!ovs) {
        ovs = ovsrec_open_vswitch_insert(ovsdb_handler->txn);
    }

    /* create new bridge and add it into Open_vSwitch table */
    bridge = ovsrec_bridge_insert(ovsdb_handler->txn);
    if (txn_ovs_insert_bridge(ovs, (struct ovsrec_bridge *) bridge, e)) {
        return EXIT_FAILURE;
    }

    ovsrec_bridge_verify_name(bridge);
    ovsrec_bridge_set_name(bridge, (char *) bridge_id);

    for (aux = node->children; aux; aux = aux->next) {
        if (aux->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(aux->name, BAD_CAST "datapath-id")) {
            xmlval = aux->children ? aux->children->content : NULL;
            if (txn_mod_bridge_datapath(BAD_CAST bridge->name, xmlval, e)) {
                return EXIT_FAILURE;
            }
        } else if (xmlStrEqual(aux->name, BAD_CAST "resources")) {
            for (leaf = aux->children; leaf; leaf = leaf->next) {
                xmlval = leaf->children ? leaf->children->content : NULL;
                if (xmlStrEqual(leaf->name, BAD_CAST "port")) {
                    if (xmlval) {
                        OVSREC_PORT_FOR_EACH(port, ovsdb_handler->idl) {
                            if (xmlStrEqual(xmlval, BAD_CAST port->name)) {
                                break;
                            }
                        }
                    }
                    if (!port) {
                        *e = nc_err_new(NC_ERR_BAD_ELEM);
                        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "port");
                        nc_err_set(*e, NC_ERR_PARAM_MSG,
                                   "Invalid port leafref in switch.");
                        return EXIT_FAILURE;
                    }
                    if (txn_bridge_insert_port(bridge,
                                               (struct ovsrec_port *) port,
                                               e)) {
                        return EXIT_FAILURE;
                    }
                    port = NULL;
                } else if (xmlStrEqual(leaf->name, BAD_CAST "flow-table")) {
                    ft = (struct ovsrec_flow_table *) find_flowtable(xmlval);
                    if (!ft) {
                        *e = nc_err_new(NC_ERR_BAD_ELEM);
                        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM,
                                   "flow-table");
                        nc_err_set(*e, NC_ERR_PARAM_MSG,
                                   "Invalid flow-table leafref in switch.");
                        return EXIT_FAILURE;
                    }
                    if (txn_bridge_insert_flowtable(bridge, ft, e)) {
                        return EXIT_FAILURE;
                    }
                    ft = NULL;
                }
                /* queue and certificate is already set, there is no explicit
                 * link to it from the bridge in OVSDB */
            }
        } else if (xmlStrEqual(aux->name, BAD_CAST "controllers")) {
            for (leaf = aux->children; leaf; leaf = leaf->next) {
                if (txn_add_contr(leaf, BAD_CAST bridge->name, e)) {
                    return EXIT_FAILURE;
                }
            }
        } else if (xmlStrEqual(aux->name,
                               BAD_CAST "lost-connection-behavior")) {
            if (txn_mod_bridge_failmode
                (BAD_CAST bridge->name, aux->children->content, e)) {
                return EXIT_FAILURE;
            }
            failmode_flag = 1;
        }
        /* enabled is not handled: it is too complicated to handle it in
         * combination with the OVSDB's garbage collection. */
    }

    if (!failmode_flag) {
        /* set default value for fail_mode */
        if (txn_mod_bridge_failmode(BAD_CAST bridge->name, NULL, e)) {
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

int
txn_add_port_tunnel(const xmlChar *port_name, xmlNodePtr tunnel_node,
                    struct nc_err **e)
{
    const struct ovsrec_interface *ifc;
    xmlNodePtr iter;
    char *option, *value;

    nc_verb_verbose("Adding tunnel (%s:%d)", __FILE__, __LINE__);

    if (!port_name || !tunnel_node) {
        nc_verb_error("%s: invalid input parameters.", __func__);
        *e = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    OVSREC_INTERFACE_FOR_EACH(ifc, ovsdb_handler->idl) {
        if (!strcmp(ifc->name, (char *) port_name)) {
            break;
        }
    }
    if (!ifc) {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, "name");
        nc_err_set(*e, NC_ERR_PARAM_MSG, "Port does not exist in OVSDB");
        return EXIT_FAILURE;
    }

    for (iter = tunnel_node->children; iter; iter = iter->next) {
        if (iter->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(iter->name, BAD_CAST "local-endpoint-ipv4-adress")) {
            option = "local_ip";
        } else if (xmlStrEqual(iter->name,
                               BAD_CAST "remote-endpoint-ipv4-adress")) {
            option = "remote_ip";
        } else if (xmlStrEqual(iter->name, BAD_CAST "key")
                || xmlStrEqual(iter->name, BAD_CAST "vni")) {
            option = "key";
        } else {
            /* unknown element */
            continue;
        }
        value = (char *) (iter->children ? iter->children->content : NULL);
        smap_add_once((struct smap *)&ifc->options, option, (char *) value);
    }
    ovsrec_interface_verify_type(ifc);
    if (xmlStrEqual(tunnel_node->name, BAD_CAST "vxlan-tunnel")) {
        ovsrec_interface_set_type(ifc, "vxlan");
    } else {
        /* tunnel or ipgre-tunnel */
        ovsrec_interface_set_type(ifc, "gre");
    }
    ovsrec_interface_verify_options(ifc);
    ovsrec_interface_set_options(ifc, &ifc->options);

    /* store tunnel-type for future get-config to interpret it correctly */
    smap_replace((struct smap *)&ifc->external_ids, "tunnel_type",
                 (char *) tunnel_node->name);
    ovsrec_interface_verify_external_ids(ifc);
    ovsrec_interface_set_external_ids(ifc, &ifc->external_ids);

    return EXIT_SUCCESS;
}

int
txn_mod_port_tunnel_opt(const xmlChar *port_name, const xmlChar *node_name,
                        const xmlChar *value, struct nc_err **e)
{
    const struct ovsrec_interface *ifc = NULL;
    const char *opt_key = NULL;

    if (!node_name) {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        return EXIT_FAILURE;
    }

    if (xmlStrEqual(node_name, BAD_CAST "local-endpoint-ipv4-adress")) {
        opt_key = "local_ip";
    } else if (xmlStrEqual(node_name, BAD_CAST "remote-endpoint-ipv4-adress")) {
        opt_key = "remote_ip";
    } else if (xmlStrEqual(node_name, BAD_CAST "key")
            || xmlStrEqual(node_name, BAD_CAST "vni")) {
        opt_key = "key";
    } else if (xmlStrEqual(node_name, BAD_CAST "checksum-present")) {
        opt_key = "csum";
    } else {
        *e = nc_err_new(NC_ERR_BAD_ELEM);
        nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, (char *) node_name);
        return EXIT_FAILURE;
    }

    OVSREC_INTERFACE_FOR_EACH(ifc, ovsdb_handler->idl) {
        if (xmlStrEqual(port_name, BAD_CAST ifc->name)) {
            break;
        }
    }
    if (!ifc) {
        *e = nc_err_new(NC_ERR_DATA_MISSING);
        return EXIT_FAILURE;
    }

    if (value) {
        /* replace existing */
        smap_replace((struct smap *) &ifc->options, opt_key,
                     (const char *) value);
    } else {
        /* delete */
        smap_remove((struct smap *) &ifc->options, opt_key);
    }
    ovsrec_interface_verify_options(ifc);
    ovsrec_interface_set_options(ifc, &ifc->options);

    return EXIT_SUCCESS;
}

/*
 * 0 - ok, reference to the bridge's port found
 * 1 - error (invalid parameter or missing object in OVSDB)
 * 2 - no link between the queue and the bridge's ports
 */
int
ofc_check_bridge_queue(const xmlChar *br_name, const xmlChar *queue_rid)
{
    const struct ovsrec_bridge *bridge;
    const struct ovsrec_port *port;
    const struct ovsrec_queue *queue;
    size_t i;

    if (!br_name || !queue_rid) {
        return 1;
    }

    /* check that the queue exists */
    queue = find_queue(queue_rid);
    if (!queue) {
        /* there is no such a queue */
        return 1;
    }

    /* get queue's port */
    port = find_queue_port((const char *)queue_rid);
    if (!port) {
        /* the queue has no port */
        return 2;
    }

    /* get bridge structure */

    OVSREC_BRIDGE_FOR_EACH(bridge, ovsdb_handler->idl) {
        if (xmlStrEqual(br_name, BAD_CAST bridge->name)) {
            break;
        }
    }
    if (!bridge) {
        return 1;
    }

    for (i = 0; i < bridge->n_ports; i++) {
        if (!strcmp(bridge->ports[i]->name, port->name)) {
            return 0;
        }
    }

    /* the queue's port is not found inside the bridge's ports */
    return 2;
}
