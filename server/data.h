
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

#ifndef DATA_H
#define DATA_H 1

#include <libxml/tree.h>

#include <libnetconf.h>

#ifndef OFC_VERBOSITY
#define OFC_VERBOSITY   0
#endif

#include <stdbool.h>

#ifdef __GNUC__
#   define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#   define UNUSED(x) UNUSED_ ## x
#endif

/*
 * OF-CONFIG uses resource-id to identify some configuration data.  It is
 * stored into OVSDB for every row that is mapped to OF-CONFIG.  Value of
 * resource-id is store in 'external_ids' string maps with key
 * 'OFC_RESOURCE_ID'. */
#define OFC_RESOURCE_ID "ofc_resource_id"

/*
 * Certificates (external and owned) are mapped to same UUID.
 * There is no need to allocate resmap for certificates.
 */
#include <uuid.h>
struct ofc_resmap_certificate {
    struct uuid uuid;       /* UUID of SSL row */
    char *external_resid;   /* resource-id of external-certificate */
    char *owned_resid;      /* resource-id of owned-certificate */
};

/*
 * ofconfig-datastore.c
 */

/*
 * Get the child node with the specified name
 */
xmlNodePtr go2node(xmlNodePtr parent, xmlChar *name);

const xmlChar *get_key(xmlNodePtr parent, const char *name);

/*
 * Get the operation value from the node, if not present, it tries to get it
 * from parents. If no operation set, it returns defop
 */
NC_EDIT_OP_TYPE edit_op_get(xmlNodePtr node, NC_EDIT_DEFOP_TYPE defop,
                            struct nc_err **e);

/*
 * ovs-data.c
 */

bool ofc_init(const char *ovs_db_path);

char *ofc_get_state_data(void);

char *ofc_get_config_data(void);

int ofc_of_mod_port(const xmlChar *bridge_name, const xmlChar *port_name, const xmlChar *bit_xchar, const xmlChar *value, struct nc_err **e);

void ofc_destroy(void);

/*
 * Start a new transaction. There can be only a single active transaction at
 * a time.
 */
void txn_init(void);

/*
 * Delete complete OVSDB content
 */
int txn_del_all(struct nc_err **e);

/* new functions */
int txn_del_bridge_port(const xmlChar *br_name, const xmlChar *port_name, struct nc_err **e);
int txn_add_bridge_port(const xmlChar *br_name, const xmlChar *port_name, struct nc_err **e);
int txn_del_bridge(const xmlChar *br_name, struct nc_err **e);
int txn_add_bridge(xmlNodePtr node, struct nc_err **e);
int txn_mod_bridge_datapath(const xmlChar *br_name, const xmlChar* value, struct nc_err **e);
int txn_mod_bridge_failmode(const xmlChar *br_name, const xmlChar* value, struct nc_err **e);

int txn_del_contr(const xmlChar *contr_id, const xmlChar *br_name, struct nc_err **e);
int txn_add_contr(xmlNodePtr node, const xmlChar *br_name, struct nc_err **e);
int txn_mod_contr_lip(const xmlChar *contr_id, const xmlChar* value, struct nc_err **e);
int txn_mod_contr_target(const xmlChar *contr_id, const xmlChar *name, const xmlChar *value, struct nc_err **e);

int txn_del_port(const xmlChar *port_name, struct nc_err **e);
int txn_add_port(xmlNodePtr node, struct nc_err **e);
int txn_add_port_advert(const xmlChar *port_name, xmlNodePtr node, struct nc_err **e);
int txn_del_port_advert(const xmlChar *port_name, xmlNodePtr node, struct nc_err **e);
int txn_mod_port_reqnumber(const xmlChar *port_name, const xmlChar* value, struct nc_err **e);
int txn_mod_port_admin_state(const xmlChar *port_name, const xmlChar* value, struct nc_err **e);
int txn_mod_port_configuration(xmlNodePtr cfg, struct nc_err **error);
int txn_mod_port_tunnel_opt(const xmlChar *port_name, xmlNodePtr node, const xmlChar *value, struct nc_err **e);

int txn_mod_port_add_tunnel(const xmlChar *port_name, xmlNodePtr tunnel_node, struct nc_err **e);
int txn_del_port_tunnel(const xmlChar *port_name, xmlNodePtr tunnel_node, struct nc_err **e);

int txn_add_queue(xmlNodePtr node, struct nc_err **e);
int txn_del_queue(const xmlNodePtr node, struct nc_err **e);
int txn_add_queue_port(const xmlChar *resource_id, xmlNodePtr edit, struct nc_err **e);
int txn_add_queue_id(const xmlChar *resource_id, xmlNodePtr edit, struct nc_err **e);
int txn_del_queue_port(const xmlChar *resource_id, xmlNodePtr edit, struct nc_err **e);
int txn_del_queue_id(const xmlChar *resource_id, xmlNodePtr edit, struct nc_err **e);

/* if edit is not NULL, add max-rate / min-rate / experimenter-id / experimenter-data into other_options.
 * if edit is null, delete. */
int txn_mod_queue_options(const xmlChar *resource_id, const char *option, xmlNodePtr edit, struct nc_err **e);

int txn_add_flow_table(xmlNodePtr node, struct nc_err **e);
int txn_del_flow_table(xmlNodePtr node, struct nc_err **e);
int txn_mod_flowtable_name(const xmlChar *table_id, xmlNodePtr node, struct nc_err **e);
int txn_mod_flowtable_resid(const xmlChar *table_id, xmlNodePtr node, struct nc_err **e);

int txn_add_flow_table(xmlNodePtr node, struct nc_err **e);
int txn_del_flow_table(xmlNodePtr node, struct nc_err **e);

int txn_add_owned_certificate(xmlNodePtr node, struct nc_err **e);
int txn_del_owned_certificate(xmlNodePtr node, struct nc_err **e);
int txn_add_external_certificate(xmlNodePtr node, struct nc_err **e);
int txn_del_external_certificate(xmlNodePtr node, struct nc_err **e);

/*
 * Abort the transaction being prepared.
 */
void txn_abort(void);

/*
 * Finish the current transaction.
 */
int txn_commit(struct nc_err **e);

/*
 * local-data.c
 */

/* store /capable-switch/id value
 * node - /capable-switch/id element node. If NULL, the function deletes id
 */
int ofc_set_switchid(xmlNodePtr node);

/* get stored /capable-switch/id value */
const xmlChar *ofc_get_switchid(void);

xmlChar *ofc_find_bridge_for_port(xmlNodePtr root, xmlChar *port_name);

const xmlChar *ofc_find_bridge_with_port(const xmlChar *port_name);

#endif /* data.h */
