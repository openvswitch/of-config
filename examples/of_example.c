
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dpif.h>
#include <vconn.h>
#include <socket-util.h>
#include <ofp-util.h>
#include <ofp-msgs.h>

/* nc_verb_error() */
#include <libnetconf.h>

/* copied from ovs-ofctl */
static int
open_vconn_socket(const char *name, struct vconn **vconnp)
{
    int error;
    char *vconn_name = xasprintf("unix:%s", name);

    error = vconn_open(vconn_name, OFPUTIL_DEFAULT_VERSIONS, DSCP_DEFAULT,
                       vconnp);
    if (error && error != ENOENT) {
        nc_verb_error("%s: failed to open socket (%s)", name,
                      ovs_strerror(error));
    }
    free(vconn_name);

    return error;
}

/* Create connect via OpenFlow with the name Bridge.
 partially copied from ovs-ofctl.c
 \return true on success
 */
bool
ofc_of_open_vconn(const char *name, struct vconn ** vconnp)
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
        // printf("connecting to %s\n", name);
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

struct ofpbuf *
ofc_of_get_ports(struct vconn *vconnp)
{
    struct ofpbuf *request;
    struct ofpbuf *reply;
    int ofp_version;

    /* existence of version was checked in openvconn */
    ofp_version = vconn_get_version(vconnp);

    request = ofputil_encode_port_desc_stats_request(ofp_version, OFPP_NONE);
    vconn_transact(vconnp, request, &reply);

    /* updates reply size */
    ofputil_switch_features_has_ports(reply);
    return reply;
}

void
ofc_of_mod_port(struct vconn *vconnp, const char *port_name,
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

    /* modify port */
    ofpbuf_use_const(&b, oh, ntohs(oh->length));
    if (ofptype_pull(&type, &b) || type != OFPTYPE_PORT_DESC_STATS_REPLY) {
        nc_verb_error("OpenFlow: bad reply.");
        return;
    }
    while (!ofputil_pull_phy_port(oh->version, &b, &pp)) {
        if (!strncmp(pp.name, port_name, strlen(pp.name))) {

            pm.port_no = pp.port_no;
            pm.config = 0;
            pm.mask = 0;
            pm.advertise = 0;
            memcpy(pm.hw_addr, pp.hw_addr, ETH_ADDR_LEN);
            pm.mask = bit;
            // invert current bit:
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
ofc_of_getports(struct vconn *vconnp)
{
    struct ofpbuf b;
    struct ofpbuf *reply;
    struct ofputil_phy_port pp;
    enum ofptype type;
    struct ofp_header *oh;

    reply = ofc_of_get_ports(vconnp);
    if (reply == NULL) {
        return;
    }

    oh = ofpbuf_data(reply);

    ofpbuf_use_const(&b, oh, ntohs(oh->length));
    if (ofptype_pull(&type, &b) || type != OFPTYPE_PORT_DESC_STATS_REPLY) {
        nc_verb_error("OpenFlow: bad reply.");
        return;
    }

    /* iteration over ports in reply */
    while (!ofputil_pull_phy_port(oh->version, &b, &pp)) {
        /* this is the point where we have information about state and
           configuration of interface */
        printf("%s: cur_speed: %" PRId32 " max_speed: %" PRId32 " "
               "admin_state: %s no-receive: %s no-forward: %s "
               "no-packet-in: %s link_state: %s blocked: %s live: %s\n",
               pp.name, pp.curr_speed, pp.max_speed,
               (pp.config & OFPUTIL_PC_PORT_DOWN ? "DOWN" : "UP"),
               (pp.config & OFPUTIL_PC_NO_RECV ? "Y" : "N"),
               (pp.config & OFPUTIL_PC_NO_FWD ? "Y" : "N"),
               (pp.config & OFPUTIL_PC_NO_PACKET_IN ? "Y" : "N"),
               (pp.state & OFPUTIL_PS_LINK_DOWN ? "DOWN" : "UP"),
               (pp.state & OFPUTIL_PS_BLOCKED ? "Y" : "N"),
               (pp.state & OFPUTIL_PS_LIVE ? "Y" : "N"));

        // other possibilities:
        //   OFPUTIL_PS_STP_LISTEN
        //   OFPUTIL_PS_STP_LEARN
        //   OFPUTIL_PS_STP_FORWARD
        //   OFPUTIL_PS_STP_BLOCK
        //   OFPUTIL_PS_STP_MASK
    }

    ofpbuf_delete(reply);
    return;
}

int
main(int argc, char **argv)
{
    const char *bridge_name = "ofc-bridge";
    struct vconn *vconn;

    if (ofc_of_open_vconn(bridge_name, &vconn) == false) {
        return 1;
    }

    ofc_of_getports(vconn);

    ofc_of_mod_port(vconn, "ofc-bridge", OFPUTIL_PC_NO_FWD, 1);
    ofc_of_mod_port(vconn, "ofc-bridge", OFPUTIL_PC_NO_RECV, 1);

    ofc_of_getports(vconn);

    ofc_of_mod_port(vconn, "ofc-bridge", OFPUTIL_PC_PORT_DOWN, 0);

    ofc_of_getports(vconn);

    vconn_close(vconn);

    return 0;
}

