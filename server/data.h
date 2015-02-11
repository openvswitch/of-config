
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
 * ovs-data.c
 */

bool ofc_init(const char *ovs_db_path);

char *ofc_get_state_data(void);

char *ofc_get_config_data(void);

void ofc_destroy(void);

/*
 * Start a new transaction. There can be only a single active transaction at
 * a time.
 */
void txn_init(void);

/*
 * Set port parameters
 */
int txn_set_port(xmlNodePtr p, struct nc_err **e);

/*
 * Set bridge parameters
 */
int txn_set_bridge(xmlNodePtr p, struct nc_err **e);

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

/* get stored /capable=switch/id value */
const xmlChar *ofc_get_switchid(void);

#endif /* data.h */
