
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
#include <vlog.h>
#include <dpif.h>
#include <vconn.h>
#include <socket-util.h>
#include <ofp-util.h>

/* possible needed files in future */
//#include <openvswitch/lib/ofp-actions.h>
//#include <openvswitch/lib/ofp-errors.h>
//#include <openvswitch/lib/ofp-msgs.h>
//#include <openvswitch/lib/ofp-parse.h>
//#include <openvswitch/lib/ofp-print.h>
//#include <openvswitch/lib/ofp-version-opt.h>
//#include <openvswitch/lib/ofpbuf.h>

VLOG_DEFINE_THIS_MODULE(ofexample);

/* Path to directory containing management socket of bridges.  OVS
uses ovs_rundir() for this purpose. */
#define PATH_TO_OFSOCKET	"/var/run/openvswitch"

/* copied from ovs-ofctl */
static int
open_vconn_socket(const char *name, struct vconn **vconnp)
{
    char *vconn_name = xasprintf("unix:%s", name);
    int error;

    error = vconn_open(vconn_name, get_allowed_ofp_versions(), DSCP_DEFAULT,
                       vconnp);
    if (error && error != ENOENT) {
        ovs_fatal(0, "%s: failed to open socket (%s)", name,
                  ovs_strerror(error));
    }
    free(vconn_name);

    return error;
}

/* copied from ovs-ofctl */
static void
run(int retval, const char *message, ...)
{
    if (retval) {
        va_list args;

        va_start(args, message);
        ovs_fatal_valist(retval, message, args);
    }
}

/* partially copied from ovs-ofctl.c */
static enum ofputil_protocol
open_vconn(const char *name, const char *target, struct vconn **vconnp)
{
    const char *suffix = target;
    char *datapath_name, *datapath_type, *socket_name;
    enum ofputil_protocol protocol;
    char *bridge_path;
    int ofp_version;
    int error;

    bridge_path = xasprintf("%s/%s.%s", OFC_OVS_OFSOCKET_DIR, name, suffix);

    /* changed to called function */
    dp_parse_name(name, &datapath_name, &datapath_type);

    socket_name =
        xasprintf("%s/%s.%s", OFC_OVS_OFSOCKET_DIR, datapath_name, suffix);
    free(datapath_name);
    free(datapath_type);

    if (strchr(name, ':')) {
        run(vconn_open(name, get_allowed_ofp_versions(), DSCP_DEFAULT, vconnp),
            "connecting to %s", name);
    } else if (!open_vconn_socket(name, vconnp)) {
        /* Fall Through. */
    } else if (!open_vconn_socket(bridge_path, vconnp)) {
        /* Fall Through. */
    } else if (!open_vconn_socket(socket_name, vconnp)) {
        /* Fall Through. */
    } else {
        ovs_fatal(0, "%s is not a bridge or a socket", name);
    }

    free(bridge_path);
    free(socket_name);

    VLOG_DBG("connecting to %s", vconn_get_name(*vconnp));
    error = vconn_connect_block(*vconnp);
    if (error) {
        ovs_fatal(0, "%s: failed to connect to socket (%s)", name,
                  ovs_strerror(error));
    }

    ofp_version = vconn_get_version(*vconnp);
    protocol = ofputil_protocol_from_ofp_version(ofp_version);
    if (!protocol) {
        ovs_fatal(0, "%s: unsupported OpenFlow version 0x%02x",
                  name, ofp_version);
    }
    return protocol;
}

int
main(int argc, char **argv)
{
    unsigned int seqno;
    char end = 0, change = 1;
    const char *bridge_name = "mbr1";
    char *name, *type;
    struct vconn *vconn;

    /* verbose level */
    vlog_set_levels(NULL, VLF_CONSOLE, VLL_DBG);
    // ovsrec_init();
    open_vconn(bridge_name, "mgmt", &vconn);

    vconn_close(vconn);

    return 0;
}
