
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

#ifndef OFC_VERBOSITY
#define OFC_VERBOSITY   0
#endif

#include <stdbool.h>
#include "res-map.h"

/*
 * ofconfig-datastore.c
 */

/*
 * Get the child node with the specified name
 */
xmlNodePtr go2node(xmlNodePtr parent, xmlChar *name);

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

void ofc_of_mod_port(const xmlChar *bridge_name, const xmlChar *port_name, const xmlChar *bit_xchar, const xmlChar *value);

void ofc_destroy(void);

/*
 * Start a new transaction. There can be only a single active transaction at
 * a time.
 */
void txn_init(void);

/*
 * Delete complete OVSDB content
 */
void txn_del_all(void);

/* new functions */
void txn_del_bridge_port(const xmlChar *br_name, const xmlChar *port_name);
void txn_add_bridge_port(const xmlChar *br_name, const xmlChar *port_name);
void txn_del_bridge(const xmlChar *br_name);
void txn_add_bridge(xmlNodePtr node);
void txn_mod_bridge_datapath(const xmlChar *br_name, const xmlChar* value);

void txn_del_port(const xmlChar *port_name);
void txn_add_port(xmlNodePtr node);
void txn_mod_port_reqnumber(const xmlChar *port_name, const xmlChar* value);
void txn_mod_port_admin_state(const xmlChar *port_name, const xmlChar* value);

void txn_mod_port_add_tunnel(const xmlChar *port_name, xmlNodePtr tunnel_node);
void txn_del_port_tunnel(const xmlChar *port_name, xmlNodePtr tunnel_node);

/*
 * Set port parameters
 */
int txn_set_port(xmlNodePtr p, NC_EDIT_OP_TYPE op, struct nc_err **e);

/*
 * Set bridge parameters
 */
int txn_set_bridge(xmlNodePtr p, NC_EDIT_OP_TYPE op,  struct nc_err **e);

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

xmlChar *ofc_find_bridge_for_port_iterative(xmlChar *port_name);

#endif /* data.h */
