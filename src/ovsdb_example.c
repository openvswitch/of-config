/*
 * LICENSE TERMS
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this
 * product may be distributed under the terms of the GNU General Public
 * License (GPL) version 2 or later, in which case the provisions
 * of the GPL apply INSTEAD OF those given above.
 *
 * This software is provided ``as is'', and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even
 * if advised of the possibility of such damage.
 *
 */
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <openvswitch/lib/dynamic-string.h>
#include <openvswitch/lib/ovsdb-idl-provider.h>
#include <openvswitch/lib/vlog.h>
#include <openvswitch/lib/vswitch-idl.h>

#ifndef DEFAULT_OVS_DBPATH
#define DEFAULT_OVS_DBPATH  "unix:/var/run/openvswitch/db.sock"
#endif

char *
print_uuid(const struct uuid *uuid)
{
    char str[38];

    snprintf(str, 37, UUID_FMT, UUID_ARGS(uuid));
    str[37] = 0;
    return strdup(str);
}

int
main(int argc, char **argv)
{
    unsigned int seqno;
    struct ovsdb_idl *idl;
    char end = 0, change = 1;
    static struct ovsdb_idl_txn *status_txn;
    enum ovsdb_idl_txn_status status;
    char *ovs_db_path = DEFAULT_OVS_DBPATH;

    /* dynamic string from OVS, it will be probably needed, contains char * */
    struct ds output;

    ds_init(&output);

    /* verbose level */
    vlog_set_levels(NULL, VLF_CONSOLE, VLL_DBG);
    ovsrec_init();
    idl = ovsdb_idl_create(ovs_db_path, &ovsrec_idl_class, true, true);
    seqno = ovsdb_idl_get_seqno(idl);
    for (;;) {
        ovsdb_idl_run(idl);
        if (!ovsdb_idl_is_alive(idl)) {
            int retval = ovsdb_idl_get_last_error(idl);

            printf("database connection failed (%s)",
                   ovs_retval_to_string(retval));
        }

        if (seqno != ovsdb_idl_get_seqno(idl)) {
            seqno = ovsdb_idl_get_seqno(idl);

            const struct ovsrec_interface *ifc;

            /* for reading */
            OVSREC_INTERFACE_FOR_EACH(ifc, idl) {
                char *u = print_uuid(&ifc->header_.uuid);

                printf("%s %s %s %" PRIi64 " mac: %s mac_in_use: %s\n",
                       u, ifc->name,
                       ifc->admin_state, ifc->mtu ? ifc->mtu[0] : 0, ifc->mac,
                       ifc->mac_in_use);
                free(u);
                int i;

                for (i = 0; i < ifc->n_statistics; i++) {
                    printf("%s ", ifc->key_statistics[i]);
                }
                printf("\n");

                struct smap opt_cl;
                const struct smap_node *oc;
                const struct smap_node *oc_it;

                SMAP_FOR_EACH(oc, &ifc->options) {
                    printf("%s: %s\n", oc->key, oc->value);
                }

                if (!strcmp(ifc->name, "gre0")) {
                    smap_clone(&opt_cl, &ifc->options);
                    smap_remove(&opt_cl, "local_ip");
                    puts("after remove");
                    SMAP_FOR_EACH(oc_it, &opt_cl) {
                        printf("%s: %s\n", oc_it->key, oc_it->value);
                    }
                    smap_add_once(&opt_cl, "local_ip", "1.1.1.2");
                    puts("after remove");
                    SMAP_FOR_EACH(oc_it, &opt_cl) {
                        printf("%s: %s\n", oc_it->key, oc_it->value);
                    }
                    status_txn = ovsdb_idl_txn_create(idl);
                    ovsrec_interface_set_options(ifc, &opt_cl);
                    ovsrec_interface_verify_options(ifc);
                    status = ovsdb_idl_txn_commit(status_txn);
                    ovsdb_idl_txn_destroy(status_txn);
                }

                /* change admin_state of mbr1 */
                if (!strcmp(ifc->name, "wlan0") && change) {
                    change = 0;
                    status_txn = ovsdb_idl_txn_create(idl);
                    // ovsrec_interface_set_admin_state(ifc, "up");
                    // ovsrec_interface_set_link_state(ifc, "up");
                    ifc->mtu[0] = 1234;
                    // ovsrec_interface_set_mac(ifc, "88:53:2E:AF:4E:CA");
                    ovsrec_interface_set_mac_in_use(ifc, "12:34:56:78:9A:BC");
                    status = ovsdb_idl_txn_commit(status_txn);
                    ovsdb_idl_txn_destroy(status_txn);
                }
            }
            const struct ovsrec_port *port;

            OVSREC_PORT_FOR_EACH(port, idl) {
                printf("%s %s %" PRIi64 " (%d)\n", port->name, port->vlan_mode,
                       port->n_tag >= 1 ? port->tag[0] : -1, port->n_tag);
                if (!strcmp(port->name, "tunbr")) {
                    status_txn = ovsdb_idl_txn_create(idl);
                    ovsrec_port_set_vlan_mode(port, "access");

                    if (port->n_tag == 0) {
                        int64_t t[2] = { 101, 102 };
                        ovsrec_port_set_tag(port, t, 2);
                    }
                    ovsrec_port_verify_vlan_mode(port);
                    ovsrec_port_verify_tag(port);
                    status = ovsdb_idl_txn_commit(status_txn);
                    ovsdb_idl_txn_destroy(status_txn);
                }
            }

            /* go to exit after this output */
            // break;
        }

        if (seqno == ovsdb_idl_get_seqno(idl)) {
            ovsdb_idl_wait(idl);
            poll_block();
        }
    }

    ds_destroy(&output);
    ovsdb_idl_destroy(idl);
    return 0;
}
