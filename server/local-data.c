
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
#include <stdlib.h>

#include <libxml/tree.h>

#include <libnetconf.h>

/* locally stored data */
static xmlChar *cs_id = NULL;   /* /capable-switch/id */

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

const xmlChar *
ofc_get_switchid(void)
{
    return cs_id;
}
