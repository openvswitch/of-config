
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

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libxml/tree.h>
#include <libnetconf_xml.h>
#include <libnetconf_ssh.h>

#include "data.h"

#ifdef __GNUC__
#	define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#	define UNUSED(x) UNUSED_ ## x
#endif

/* transAPI version which must be compatible with libnetconf */

/* int transapi_version = 5; */

/* Signal to libnetconf that configuration data were modified by any callback.
 * 0 - data not modified
 * 1 - data have been modified
 */
int ofc_config_modified = 0;

/* Do not modify or set! This variable is set by libnetconf to announce
 * edit-config's error-option. Feel free to use it to distinguish module
 * behavior for different error-option values.
 * Possible values:
 * NC_EDIT_ERROPT_STOP - Following callback after failure are not executed, all
 *     successful callbacks executed till failure point must be applied to the
 *     device.
 * NC_EDIT_ERROPT_CONT - Failed callbacks are skipped, but all callbacks needed
 *     to apply configuration changes are executed
 * NC_EDIT_ERROPT_ROLLBACK - After failure, following callbacks are not
 *     executed, but previous successful callbacks are executed again with
 *     previous configuration data to roll it back.
 */
NC_EDIT_ERROPT_TYPE ofc_erropt = NC_EDIT_ERROPT_NOTSET;

/* Retrieve state data from device and return them as XML document
 *
 */
xmlDocPtr
ofc_status_clb(xmlDocPtr UNUSED(model), xmlDocPtr UNUSED(running),
               struct nc_err **UNUSED(err))
{
    char *state_data;
    xmlDocPtr d;

    state_data = ofc_get_state_data();
    if (state_data != NULL) {
        d = xmlReadMemory(state_data, strlen(state_data), NULL, NULL, 0);
        free(state_data);
        return d;
    }
    return (NULL);
}

/* Mapping between prefixes and namespaces. */
struct ns_pair ofc_namespace_mapping[] = {
    {"of", "urn:onf:config:yang"},
    {NULL, NULL}
};

/*
 * CONFIGURATION callbacks
 */

/* Initialize of-config module
 *
 * When the running is returned untouched, the device must follow the default
 * values defined in the data model.
 */
int
ofc_transapi_init(xmlDocPtr * UNUSED(running))
{
    return EXIT_SUCCESS;
}

/* Free all resources allocated on plugin runtime and prepare plugin for
 * removal.
 */
void
ofc_transapi_close(void)
{
    return;
}

int
callback_top(void **UNUSED(data), XMLDIFF_OP UNUSED(op),
             xmlNodePtr UNUSED(node), struct nc_err **UNUSED(error))
{
    nc_verb_verbose("OF-CONFIG data affected.");

    return (EXIT_SUCCESS);
}

/*
 * Structure transapi_config_callbacks provide mapping between callback and path
 * in configuration datastore.
 */
struct transapi_data_callbacks ofc_clbks = {
    .callbacks_count = 1,
    .data = NULL,
    .callbacks = {
                  {.path = "/of:capable-switch",
                   .func = callback_top},
                  }
};

/*
 * RPC callbacks - in this module we don't have any RPC
 */

/*
 * Structure transapi_rpc_callbacks provide mapping between callbacks and RPC
 * messages.
 */
struct transapi_rpc_callbacks ofc_rpc_clbks = {
    .callbacks_count = 0,
    .callbacks = {}
};

/* overall structure providing content of this module to the libnetconf */
struct transapi ofc_transapi = {
    .init = ofc_transapi_init,
    .close = ofc_transapi_close,
    .get_state = ofc_status_clb,
    .clbks_order = TRANSAPI_CLBCKS_ORDER_DEFAULT,
    .data_clbks = &ofc_clbks,
    .rpc_clbks = &ofc_rpc_clbks,
    .ns_mapping = ofc_namespace_mapping,
    .config_modified = &ofc_config_modified,
    .erropt = &ofc_erropt,
    .file_clbks = NULL,
};
