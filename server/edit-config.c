
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

#include <ctype.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <libnetconf.h>

#include "data.h"

#define NC_NS_BASE10        "urn:ietf:params:xml:ns:netconf:base:1.0"

#define XPATH_BUFFER 1024

static int edit_replace(xmlDocPtr, xmlNodePtr, int, struct nc_err **);
static int edit_delete(xmlNodePtr, int, struct nc_err **);
static int edit_remove(xmlDocPtr, xmlNodePtr, int, struct nc_err **);
static int edit_create(xmlDocPtr, xmlNodePtr, int, struct nc_err **);
static int edit_merge(xmlDocPtr, xmlNodePtr, int, struct nc_err **);

/*
 * Remove leading and trailing whitespaces from the string.
 *
 * @param[in] in Input string to clear.
 * @return copy of the input string without leading/trailing whitespaces,
 * NULL on memory allocation error. Caller is supposed to free the returned
 * string.
 */
static char *
clrwspace(const char *in)
{
    int i, j = 0, len = strlen(in);
    char *retval = strdup(in);

    if (retval == NULL) {
        nc_verb_error("Memory allocation failed (%s).", __func__);
        return (NULL);
    }

    if (isspace(retval[0])) {
        /* remove leading whitespace characters */
        for (i = 0, j = 0; i < len; i++, j++) {
            while (retval[i] != '\0' && isspace(retval[i])) {
                i++;
            }
            retval[j] = retval[i];
        }
    }

    /* remove trailing whitespace characters */
    while (j >= 0 && isspace(retval[j])) {
        retval[j] = '\0';
        j--;
    }

    return (retval);
}

/*
 * Compare namespaces of the 2 nodes. The function includes XML namespace
 * wildcard mechanism defined by RFC 6241. It means, that if the client does
 * not specify the namespace of the (edit-config) data, the namespaces always
 * match.
 *
 * @param[in] reference XML element node to compare (the one from edit-config)
 * @param[in] node XML element node to compare
 * @return 0 for the case that namespaces match, 1 otherwise.
 */
static int
nscmp(xmlNodePtr reference, xmlNodePtr node)
{
    int in_ns = 1;
    char *s = NULL;

    if (reference->ns != NULL && reference->ns->href != NULL) {

        /* XML namespace wildcard mechanism:
         * 1) no namespace defined and namespace is inherited from message
         *    so it is NETCONF base namespace
         * 2) namespace is empty: xmlns=""
         */
        if (!strcmp((char *) reference->ns->href, NC_NS_BASE10) ||
            strlen(s = clrwspace((char *) (reference->ns->href))) == 0) {
            free(s);
            return 0;
        }
        free(s);

        in_ns = 0;
        if (node->ns != NULL) {
            if (!strcmp((char *) reference->ns->href, (char *) node->ns->href)) {
                in_ns = 1;
            }
        }
    }
    return (in_ns == 1 ? 0 : 1);
}

/**
 * @brief Learn whether the namespace definition is used as namespace in the
 * subtree.
 * @param[in] node Node where to start checking.
 * @param[in] ns Namespace to find.
 * @return 0 if the namespace is not used, 1 if the usage of the ns was found
 */
static int
find_namespace_usage(xmlNodePtr node, xmlNsPtr ns)
{
    xmlNodePtr child;
    xmlAttrPtr prop;

    /* check the element itself */
    if (node->ns == ns) {
        return 1;
    } else {
        /* check attributes of the element */
        for (prop = node->properties; prop != NULL; prop = prop->next) {
            if (prop->ns == ns) {
                return 1;
            }
        }

        /* go recursive into children */
        for (child = node->children; child != NULL; child = child->next) {
            if (child->type == XML_ELEMENT_NODE
                && find_namespace_usage(child, ns) == 1) {
                return 1;
            }
        }
    }

    return 0;
}

/**
 * @brief Remove namespace definition from the node which are no longer used.
 * @param[in] node XML element node where to check for namespace definitions
 */
static void
clrns(xmlNodePtr node)
{
    xmlNsPtr ns, prev = NULL;

    if (node == NULL || node->type != XML_ELEMENT_NODE) {
        return;
    }

    for (ns = node->nsDef; ns != NULL;) {
        if (find_namespace_usage(node, ns) == 0) {
            /* no one use the namespace - remove it */
            if (prev == NULL) {
                node->nsDef = ns->next;
                xmlFreeNs(ns);
                ns = node->nsDef;
            } else {
                prev->next = ns->next;
                xmlFreeNs(ns);
                ns = prev->next;
            }
        } else {
            /* check another namespace definition */
            prev = ns;
            ns = ns->next;
        }
    }
}

xmlNodePtr
go2node(xmlNodePtr parent, xmlChar *name)
{
    xmlNodePtr child;

    if (!parent) {
        return NULL;
    }

    for (child = parent->children; child; child = child->next) {
        if (child->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (xmlStrEqual(child->name, name)) {
            break;
        }
    }

    return child;
}

const xmlChar *
get_key(xmlNodePtr parent, const char *name)
{
    xmlNodePtr node;

    node = go2node(parent, BAD_CAST name);
    if (!node || !node->children || !node->children->content
        || xmlIsBlankNode(node->children)) {
        nc_verb_error("Invalid key of %s", parent->name);
        return NULL;
    }

    return node->children->content;
}

/**
 * @brief Get the default value of the node
 * @param[in] node XML element whose default value we want to get
 * @return Default value of the node, NULL if no default value is defined
 */
static xmlChar *
get_defval(xmlNodePtr node)
{
    if (!node) {
        return NULL;
    }

    if (xmlStrEqual(node->name, BAD_CAST "lost-connection-behavior")) {
        return (xmlChar *) "failSecureMode";
    } else if (xmlStrEqual(node->name, BAD_CAST "port")
               && xmlStrEqual(node->parent->name, BAD_CAST "controller")) {
        return (xmlChar *) "6633";
    } else if (xmlStrEqual(node->name, BAD_CAST "protocol")) {
        return (xmlChar *) "tls";
    } else if (xmlStrEqual(node->name, BAD_CAST "checksum-present")
               || xmlStrEqual(node->name, BAD_CAST "key-present")
               || xmlStrEqual(node->name, BAD_CAST "auto-negotiate")) {
        return (xmlChar *) "true";
    } else if (xmlStrEqual(node->name, BAD_CAST "no-receive")
               || xmlStrEqual(node->name, BAD_CAST "no-forward")
               || xmlStrEqual(node->name, BAD_CAST "no-packet-in")) {
        return (xmlChar *) "false";
    } else if (xmlStrEqual(node->name, BAD_CAST "admin-state")) {
        return (xmlChar *) "up";
    }

    return NULL;
}

/**
 * \brief Get the list of elements with the specified selected edit-config's
 * operation.
 *
 * \param[in] op edit-config's operation type to search for.
 * \param[in] edit XML document covering edit-config's \<config\> element. The
 *                 elements with specified operation will be searched for in
 *                 this document.
 */
static xmlXPathObjectPtr
get_operation_elements(NC_EDIT_OP_TYPE op, xmlDocPtr edit)
{
    xmlXPathContextPtr edit_ctxt = NULL;
    xmlXPathObjectPtr operation_nodes = NULL;
    char xpath[XPATH_BUFFER];
    char *opstring;

    switch (op) {
    case NC_EDIT_OP_MERGE:
        opstring = "merge";
        break;
    case NC_EDIT_OP_REPLACE:
        opstring = "replace";
        break;
    case NC_EDIT_OP_CREATE:
        opstring = "create";
        break;
    case NC_EDIT_OP_DELETE:
        opstring = "delete";
        break;
    case NC_EDIT_OP_REMOVE:
        opstring = "remove";
        break;
    default:
        nc_verb_error("Unsupported edit operation %d (%s)", op, __func__);
        return (NULL);
    }

    /* create xpath evaluation context */
    edit_ctxt = xmlXPathNewContext(edit);
    if (edit_ctxt == NULL) {
        if (edit_ctxt != NULL) {
            xmlXPathFreeContext(edit_ctxt);
        }
        nc_verb_error("Creating the XPath evaluation context failed (%s).",
                      __func__);
        return (NULL);
    }

    if (xmlXPathRegisterNs(edit_ctxt, BAD_CAST "nc", BAD_CAST NC_NS_BASE10)) {
        xmlXPathFreeContext(edit_ctxt);
        nc_verb_error("Registering a namespace for XPath failed (%s).",
                      __func__);
        return (NULL);
    }

    if (snprintf(xpath, XPATH_BUFFER, "//*[@nc:operation='%s']", opstring) <= 0) {
        xmlXPathFreeContext(edit_ctxt);
        nc_verb_error("Preparing the XPath query failed (%s).", __func__);
        return (NULL);
    }
    operation_nodes = xmlXPathEvalExpression(BAD_CAST xpath, edit_ctxt);

    /* clean up */
    xmlXPathFreeContext(edit_ctxt);

    return (operation_nodes);
}

/**
 * \brief Get value of the operation attribute of the \<node\> element.
 * If no such attribute is present, defop parameter is used and returned.
 *
 * \param[in] node XML element to analyze
 * \param[in] defop Default operation to use if no specific operation is present
 * \param[out] err NETCONF error structure to store the error description in
 *
 * \return NC_OP_TYPE_ERROR on error, valid NC_OP_TYPE values otherwise
 */
static NC_EDIT_OP_TYPE
get_operation(xmlNodePtr node, NC_EDIT_DEFOP_TYPE defop, struct nc_err **error)
{
    xmlChar *operation = NULL;
    NC_EDIT_OP_TYPE op;

    /* get specific operation the node */
    operation =
        xmlGetNsProp(node, BAD_CAST "operation", BAD_CAST NC_NS_BASE10);
    if (operation) {
        if (xmlStrEqual(operation, BAD_CAST "merge")) {
            op = NC_EDIT_OP_MERGE;
        } else if (xmlStrEqual(operation, BAD_CAST "replace")) {
            op = NC_EDIT_OP_REPLACE;
        } else if (xmlStrEqual(operation, BAD_CAST "create")) {
            op = NC_EDIT_OP_CREATE;
        } else if (xmlStrEqual(operation, BAD_CAST "delete")) {
            op = NC_EDIT_OP_DELETE;
        } else if (xmlStrEqual(operation, BAD_CAST "remove")) {
            op = NC_EDIT_OP_REMOVE;
        } else {
            *error = nc_err_new(NC_ERR_BAD_ATTR);
            nc_err_set(*error, NC_ERR_PARAM_INFO_BADATTR, "operation");
            op = NC_EDIT_OP_ERROR;
        }
    } else {
        op = (NC_EDIT_OP_TYPE) defop;
    }
    xmlFree(operation);

    return op;
}

/*
 * Tell if the specified node is a key of a list instance
 *
 * @param[in] node XML element to analyze
 * @return 0 as false, 1 as true
 */
static int
is_key(xmlNodePtr node)
{
    if (xmlStrEqual(node->name, BAD_CAST "id")) {
        if (xmlStrEqual(node->parent->name, BAD_CAST "switch")
            || xmlStrEqual(node->parent->name, BAD_CAST "controller")) {
            return 1;
        }
    } else if (xmlStrEqual(node->name, BAD_CAST "table-id")) {
        return 1;
    } else if (xmlStrEqual(node->name, BAD_CAST "name")
               && !xmlStrEqual(node->parent->name, BAD_CAST "flow-table")) {
        return 1;
    } else if (xmlStrEqual(node->name, BAD_CAST "resource-id")
               && !xmlStrEqual(node->parent->name, BAD_CAST "flow-table")) {
        return 1;
    }

    return 0;
}

/**
 * \brief Compare 2 elements and decide if they are equal for NETCONF.
 *
 * Matching does not include attributes and children match (only key children
 * are checked). Furthermore, XML node types and namespaces are also checked.
 *
 * Supported XML node types are XML_TEXT_NODE and XML_ELEMENT_NODE.
 *
 * \param[in] node1 First node to compare (from edit-config data).
 * \param[in] node2 Second node to compare.
 *
 * \return 0 - false, 1 - true (matching elements), -1 - error.
 */
static int
matching_elements(xmlNodePtr node1, xmlNodePtr node2)
{
    xmlNodePtr key1, key2;
    char *aux1, *aux2;
    int ret;

    if (!node1 || !node2) {
        return -1;
    }

    /* compare text nodes */
    if (node1->type == XML_TEXT_NODE && node2->type == XML_TEXT_NODE) {
        aux1 = clrwspace((char *) (node1->content));
        aux2 = clrwspace((char *) (node2->content));

        if (strcmp(aux1, aux2) == 0) {
            ret = 1;
        } else {
            ret = 0;
        }
        free(aux1);
        free(aux2);
        return ret;
    }

    /* check element types - only element nodes are processed */
    if ((node1->type != XML_ELEMENT_NODE) || (node2->type != XML_ELEMENT_NODE)) {
        return 0;
    }
    /* check element names */
    if (xmlStrcmp(node1->name, node2->name) != 0) {
        return 0;
    }

    /* check element namespace */
    if (nscmp(node1, node2) != 0) {
        return 0;
    }

    /*
     * if required, check children text node if exists, this is usually needed
     * for leaf-list's items
     */
    if (xmlStrEqual(node2->name, BAD_CAST "queue")
        || xmlStrEqual(node2->name, BAD_CAST "flow-table")
        || xmlStrEqual(node2->name, BAD_CAST "rate")
        || xmlStrEqual(node2->name, BAD_CAST "medium")
        || (xmlStrEqual(node2->name, BAD_CAST "port")
            && xmlStrEqual(node2->parent->name, BAD_CAST "resources"))) {
        if (node1->children != NULL && node1->children->type == XML_TEXT_NODE
            && node2->children != NULL
            && node2->children->type == XML_TEXT_NODE) {
            /*
             * we do not need to continue to keys checking since compared elements
             * do not contain any children that can serve as a key
             */
            return (matching_elements(node1->children, node2->children));
        }
    }

    /* check keys in lists */
    aux1 = NULL;
    if (xmlStrEqual(node2->name, BAD_CAST "controller")
        || xmlStrEqual(node2->name, BAD_CAST "switch")) {
        aux1 = "id";
    } else if (xmlStrEqual(node2->name, BAD_CAST "port")
               && xmlStrEqual(node2->parent->parent->name,
                           BAD_CAST "capable-switch")) {
        aux1 = "name";
    } else if (xmlStrEqual(node2->name, BAD_CAST "flow-table")
               && xmlStrEqual(node2->parent->parent->name,
                           BAD_CAST "capable-switch")) {
        aux1 = "table-id";
    } else if ((xmlStrEqual(node2->name, BAD_CAST "queue")
                || xmlStrEqual(node2->name, BAD_CAST "owned-certificate")
                || xmlStrEqual(node2->name, BAD_CAST "external-certificate"))
               && xmlStrEqual(node2->parent->parent->name,
                              BAD_CAST "capable-switch")) {
        aux1 = "resource-id";
    } else {
        return 1;
    }

    /* evaluate keys */
    key1 = go2node(node1, BAD_CAST aux1);
    key2 = go2node(node2, BAD_CAST aux1);
    if (!key1 || !key2) {
        return 0;
    } else {
        /* compare key's text nodes, not the key elements itself */
        return matching_elements(key1->children, key2->children);
    }
}

/**
 * \brief Find an equivalent of the given edit node in the orig_doc document.
 *
 * \param[in] orig_doc Original configuration document to edit.
 * \param[in] edit Element from the edit-config's \<config\>. Its equivalent in
 *                 orig_doc should be found.
 * \return Found equivalent element, NULL if no such element exists.
 */
static xmlNodePtr
find_element_equiv(xmlDocPtr orig_doc, xmlNodePtr edit)
{
    xmlNodePtr orig_parent, node;

    if (edit == NULL || orig_doc == NULL) {
        return (NULL);
    }

    /* go recursively to the root */
    if (edit->parent->type != XML_DOCUMENT_NODE) {
        orig_parent = find_element_equiv(orig_doc, edit->parent);
    } else {
        if (orig_doc->children == NULL) {
            orig_parent = NULL;
        } else {
            orig_parent = orig_doc->children->parent;
        }
    }
    if (orig_parent == NULL) {
        return (NULL);
    }

    /* element check */
    node = orig_parent->children;
    while (node != NULL) {
        /* compare edit and node */
        if (matching_elements(edit, node) == 0) {
            /* non matching nodes */
            node = node->next;
            continue;
        } else {
            /* matching nodes found */
            return (node);
        }
    }

    /* no corresponding node found */
    return (NULL);
}

/*
 * Check, that all list instances include thair key necessary to identify them.
 *
 * @param[in] doc XML to check
 * @param[out] e NETCONF error structure to fill in case of error
 *
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int
check_keys(xmlDocPtr doc, struct nc_err **e)
{
    xmlNodePtr l1, l2, l3, root, enode;

    if (!doc || !(root = xmlDocGetRootElement(doc))) {
        return EXIT_SUCCESS;
    }

    for (l1 = root->children; l1; l1 = l1->next) {
        if (l1->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrEqual(l1->name, BAD_CAST "resources")) {
            for (l2 = l1->children; l2; l2 = l2->next) {
                if (l2->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (xmlStrEqual(l2->name, BAD_CAST "port")) {
                    /* port must have name */
                    if (!go2node(l2, BAD_CAST "name")) {
                        enode = l2;
                        goto error;
                    }
                } else if (xmlStrEqual(l2->name, BAD_CAST "queue")
                           || xmlStrEqual(l2->name, BAD_CAST "owned-certificate")
                           || xmlStrEqual(l2->name,
                                          BAD_CAST "external-certificate")) {
                    /* here must be resource-id */
                    if (!go2node(l2, BAD_CAST "resource-id")) {
                        enode = l2;
                        goto error;
                    }
                } else if (xmlStrEqual(l2->name, BAD_CAST "flow-table")) {
                    /* flow-table must have table-id */
                    if (!go2node(l2, BAD_CAST "table-id")) {
                        enode = l2;
                        goto error;
                    }
                }
            }
        } else if (xmlStrEqual(l1->name, BAD_CAST "logical-switches")) {
            for (l2 = l1->children; l2; l2 = l2->next) {
                if (l2->type != XML_ELEMENT_NODE
                    || !xmlStrEqual(l2->name, BAD_CAST "switch")) {
                    continue;
                }

                /* switch must have id */
                if (!go2node(l2, BAD_CAST "id")) {
                    enode = l2;
                    goto error;
                }

                /* check controllers if any */
                l3 = go2node(l2, BAD_CAST "controllers");
                if (!l3) {
                    continue;
                }

                for (l3 = l3->children; l3; l3 = l3->next) {
                    if (l3->type != XML_ELEMENT_NODE
                        || !xmlStrEqual(l3->name, BAD_CAST "controller")) {
                        continue;
                    }

                    /* controller must have id */
                    if (!go2node(l3, BAD_CAST "id")) {
                        enode = l3;
                        goto error;
                    }
                }
            }
        }
    }

    return EXIT_SUCCESS;

error:
    *e = nc_err_new(NC_ERR_BAD_ELEM);
    nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM, (char *) enode->name);
    nc_err_set(*e, NC_ERR_PARAM_MSG, "The list element misses key.");
    return EXIT_FAILURE;
}

/**
 * \brief Check edit-config's node operations hierarchy.
 *
 * In case of the removal ("remove" and "delete") operations, the supreme
 * operation (including the default operation) cannot be the creation ("create
 * or "replace") operation.
 *
 * In case of the creation operations, the supreme operation cannot be a removal
 * operation.
 *
 * \param[in] edit XML node from edit-config's \<config\> whose hierarchy
 *                 (supreme operations) will be checked.
 * \param[in] defop Default edit-config's operation for this edit-config call.
 * \param[out] err NETCONF error structure.
 * \return On error, non-zero is returned and err structure is filled. Zero is
 * returned on success.
 */
static int
check_edit_ops_hierarchy(xmlNodePtr edit, NC_EDIT_DEFOP_TYPE defop,
                         struct nc_err **error)
{
    xmlNodePtr parent;
    NC_EDIT_OP_TYPE op, parent_op;

    op = get_operation(edit, NC_EDIT_DEFOP_NOTSET, error);
    if (op == (NC_EDIT_OP_TYPE) NC_EDIT_DEFOP_NOTSET) {
        /* no operation defined for this node */
        return EXIT_SUCCESS;
    } else if (op == NC_EDIT_OP_ERROR) {
        return EXIT_FAILURE;
    } else if (op == NC_EDIT_OP_DELETE || op == NC_EDIT_OP_REMOVE) {
        if (defop == NC_EDIT_DEFOP_REPLACE) {
            if (error != NULL) {
                if (error != NULL) {
                    *error = nc_err_new(NC_ERR_OP_FAILED);
                }
            }
            return EXIT_FAILURE;
        }

        /* check parent elements for operation compatibility */
        parent = edit->parent;
        while (parent->type != XML_DOCUMENT_NODE) {
            parent_op = get_operation(parent, NC_EDIT_DEFOP_NOTSET, error);
            if (parent_op == NC_EDIT_OP_ERROR) {
                return EXIT_FAILURE;
            } else if (parent_op == NC_EDIT_OP_CREATE
                       || parent_op == NC_EDIT_OP_REPLACE) {
                if (error != NULL) {
                    *error = nc_err_new(NC_ERR_OP_FAILED);
                }
                return EXIT_FAILURE;
            }
            parent = parent->parent;
        }
    } else if (op == NC_EDIT_OP_CREATE || op == NC_EDIT_OP_REPLACE) {
        /* check parent elements for operation compatibility */
        parent = edit->parent;
        while (parent->type != XML_DOCUMENT_NODE) {
            parent_op = get_operation(parent, NC_EDIT_DEFOP_NOTSET, error);
            if (parent_op == NC_EDIT_OP_ERROR) {
                return EXIT_FAILURE;
            } else if (parent_op == NC_EDIT_OP_DELETE
                       || parent_op == NC_EDIT_OP_REMOVE) {
                if (error != NULL) {
                    *error = nc_err_new(NC_ERR_OP_FAILED);
                }
                return EXIT_FAILURE;
            }
            parent = parent->parent;
        }
    }

    return EXIT_SUCCESS;
}

/**
 * \brief Check edit-config's operation rules.
 *
 * In case of the "create" operation, if the configuration data exists, the
 * "data-exists" error is generated.
 *
 * In case of the "delete" operation, if the configuration data does not exist,
 * the "data-missing" error is generated.
 *
 * Operation hierarchy check check_edit_ops_hierarchy() is also applied.
 *
 * \param[in] op Operation type to check (only the "delete" and "create"
 * operation types are valid).
 * \param[in] defop Default edit-config's operation for this edit-config call.
 * \param[in] orig Original configuration document to edit.
 * \param[in] edit XML document covering edit-config's \<config\> element
 * supposed to edit the orig configuration data.
 * \param[out] err NETCONF error structure.
 * \return On error, non-zero is returned and an err structure is filled.
 * 0 is returned on success.
 */
int
check_edit_ops(NC_EDIT_OP_TYPE op, NC_EDIT_DEFOP_TYPE defop, xmlDocPtr orig,
               xmlDocPtr edit, struct nc_err **error)
{
    xmlXPathObjectPtr operation_nodes = NULL;
    xmlNodePtr node_to_process = NULL, n;
    xmlChar *defval = NULL, *value = NULL;
    int i, r;

    operation_nodes = get_operation_elements(op, edit);
    if (operation_nodes == NULL) {
        *error = nc_err_new(NC_ERR_OP_FAILED);
        return EXIT_FAILURE;
    }

    if (xmlXPathNodeSetIsEmpty(operation_nodes->nodesetval)) {
        xmlXPathFreeObject(operation_nodes);
        return EXIT_SUCCESS;
    }

    *error = NULL;
    for (i = 0; i < operation_nodes->nodesetval->nodeNr; i++) {
        node_to_process = operation_nodes->nodesetval->nodeTab[i];

        r = check_edit_ops_hierarchy(node_to_process, defop, error);
        if (r != EXIT_SUCCESS) {
            xmlXPathFreeObject(operation_nodes);
            return EXIT_FAILURE;
        }

        /* TODO namespace handlings */
        n = find_element_equiv(orig, node_to_process);
        if (op == NC_EDIT_OP_DELETE && n == NULL) {
            if (ncdflt_get_basic_mode() == NCWD_MODE_ALL) {
                /* A valid 'delete' operation attribute for a data node that
                 * contains its schema default value MUST succeed, even though
                 * the data node is immediately replaced by the server with
                 * the default value. */
                defval = get_defval(node_to_process);
                if (defval == NULL) {
                    /* no default value for this node */
                    *error = nc_err_new(NC_ERR_DATA_MISSING);
                    break;
                }
                value = xmlNodeGetContent(node_to_process);
                if (value == NULL) {
                    *error = nc_err_new(NC_ERR_DATA_MISSING);
                    break;
                }
                if (xmlStrcmp(defval, value) != 0) {
                    /* node do not contain default value */
                    *error = nc_err_new(NC_ERR_DATA_MISSING);
                    break;
                } else {
                    /* remove delete operation - it is valid but there is no
                     * reason to really perform it */
                    xmlUnlinkNode(node_to_process);
                    xmlFreeNode(node_to_process);
                }
                xmlFree(defval);
                defval = NULL;
                xmlFree(value);
                value = NULL;
            } else {
                *error = nc_err_new(NC_ERR_DATA_MISSING);
                break;
            }
        } else if (op == NC_EDIT_OP_CREATE && n != NULL) {
            if (ncdflt_get_basic_mode() == NCWD_MODE_TRIM) {
                /* A valid 'create' operation attribute for a data node that
                 * has a schema default value defined MUST succeed. */
                defval = get_defval(node_to_process);
                if (defval == NULL) {
                    /* no default value for this node */
                    *error = nc_err_new(NC_ERR_DATA_EXISTS);
                    break;
                }
                value = xmlNodeGetContent(node_to_process);
                if (value == NULL) {
                    *error = nc_err_new(NC_ERR_DATA_EXISTS);
                    break;
                }
                if (xmlStrcmp(defval, value) != 0) {
                    /* node do not contain default value */
                    *error = nc_err_new(NC_ERR_DATA_MISSING);
                    break;
                } else {
                    /* remove old node in configuration to allow recreate it
                     * by the new one with the default value */
                    xmlUnlinkNode(n);
                    xmlFreeNode(n);
                }
                xmlFree(defval);
                defval = NULL;
                xmlFree(value);
                value = NULL;

            } else {
                *error = nc_err_new(NC_ERR_DATA_EXISTS);
                break;
            }
        }
    }
    xmlXPathFreeObject(operation_nodes);
    if (defval != NULL) {
        xmlFree(defval);
    }
    if (value != NULL) {
        xmlFree(value);
    }

    if (*error != NULL) {
        return (EXIT_FAILURE);
    } else {
        return EXIT_SUCCESS;
    }
}

/*
 * Recursive follow-up of the compact_edit_operations()
 *
 * @param[in] node XML element to process (recursively)
 * @param[in] supreme_op The operation type somehow (explicitelly or as a
 * default operation) inherited from the parent node.
 *
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
static int
compact_edit_operations_r(xmlNodePtr node, NC_EDIT_OP_TYPE supreme_op)
{
    NC_EDIT_OP_TYPE op;
    xmlNodePtr children;
    int ret;

    op = get_operation(node, NC_EDIT_DEFOP_NOTSET, NULL);
    switch ((int) op) {
    case NC_EDIT_OP_ERROR:
        return EXIT_FAILURE;
        break;
    case 0:
        /* no operation defined -> go recursively, but use supreme operation,
         * it may be the default operation and in such a case remove it */
        op = supreme_op;
        break;
    default:
        /* any operation specified */
        if (op == supreme_op) {
            /* operation duplicity -> remove subordinate duplicated operation */
            /* remove operation attribute */
            xmlRemoveProp(xmlHasNsProp(node, BAD_CAST "operation",
                                       BAD_CAST NC_NS_BASE10));
            clrns(node);
        }
        break;
    }

    /* go recursive */
    for (children = node->children; children != NULL;
         children = children->next) {
        ret = compact_edit_operations_r(children, op);
        if (ret == EXIT_FAILURE) {
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

/*
 * Keep as a few explicit operations in edit-config as possible. It avoids a
 * duplication of the same operation in a subtree.
 *
 * @param[in] edit_doc XML doc to process
 * @param[in] def_op The default operation type to inherit in case no operation
 * is explicitely specified.
 *
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int
compact_edit_operations(xmlDocPtr edit_doc, NC_EDIT_DEFOP_TYPE defop)
{
    xmlNodePtr root;
    int r;

    if (edit_doc == NULL) {
        return EXIT_FAILURE;
    }

    /* to start recursive check, use defop as root's supreme operation */
    for (root = edit_doc->children; root != NULL; root = root->next) {
        if (root->type != XML_ELEMENT_NODE) {
            continue;
        }

        r = compact_edit_operations_r(root, (NC_EDIT_OP_TYPE) defop);
        if (r != EXIT_SUCCESS) {
            return (EXIT_FAILURE);
        }
    }
    return EXIT_SUCCESS;
}

/**
 * \brief Perform all the edit-config's operations specified in the edit_doc.
 *
 * \param[in] orig_doc Original configuration document to edit.
 * \param[in] edit_doc XML document covering edit-config's \<config\> element
 *                     supposed to edit orig_doc configuration data.
 * \param[in] defop Default edit-config's operation for this edit-config call.
 * \param[out] err NETCONF error structure.
 *
 * \return On error, non-zero is returned and err structure is filled. Zero is
 *         returned on success.
 */
int
edit_operations(xmlDocPtr orig_doc, xmlDocPtr edit_doc,
                NC_EDIT_DEFOP_TYPE defop, int running, struct nc_err **error)
{
    xmlXPathObjectPtr nodes;
    int i;
    xmlNodePtr orig_node, edit_node;

    *error = NULL;

    /* default replace */
    if (defop == NC_EDIT_DEFOP_REPLACE) {
        /* replace whole document */
        for (edit_node = edit_doc->children; edit_node != NULL;
             edit_node = edit_doc->children) {
            edit_replace(orig_doc, edit_node, running, error);
        }
    }

    /* delete operations */
    nodes = get_operation_elements(NC_EDIT_OP_DELETE, edit_doc);
    if (nodes != NULL) {
        if (!xmlXPathNodeSetIsEmpty(nodes->nodesetval)) {
            /* something to delete */
            for (i = 0; i < nodes->nodesetval->nodeNr; i++) {
                edit_node = nodes->nodesetval->nodeTab[i];
                orig_node = find_element_equiv(orig_doc, edit_node);
                if (orig_node == NULL) {
                    xmlXPathFreeObject(nodes);
                    if (error != NULL) {
                        *error = nc_err_new(NC_ERR_DATA_MISSING);
                    }
                    goto error;
                }
                for (; orig_node != NULL;
                     orig_node = find_element_equiv(orig_doc, edit_node)) {
                    /* remove the edit node's equivalent from the original
                     * document */
                    edit_delete(orig_node, running, error);
                }
                /* remove the node from the edit document */
                edit_delete(edit_node, 0, error);
                nodes->nodesetval->nodeTab[i] = NULL;
            }
        }
        xmlXPathFreeObject(nodes);
    }

    /* remove operations */
    nodes = get_operation_elements(NC_EDIT_OP_REMOVE, edit_doc);
    if (nodes != NULL) {
        if (!xmlXPathNodeSetIsEmpty(nodes->nodesetval)) {
            /* something to remove */
            for (i = 0; i < nodes->nodesetval->nodeNr; i++) {
                if (edit_remove(orig_doc, nodes->nodesetval->nodeTab[i],
                                running, error) != EXIT_SUCCESS) {
                    xmlXPathFreeObject(nodes);
                    goto error;
                }
                nodes->nodesetval->nodeTab[i] = NULL;
            }
        }
        xmlXPathFreeObject(nodes);
    }

    /* replace operations */
    nodes = get_operation_elements(NC_EDIT_OP_REPLACE, edit_doc);
    if (nodes != NULL) {
        if (!xmlXPathNodeSetIsEmpty(nodes->nodesetval)) {
            /* something to replace */
            for (i = 0; i < nodes->nodesetval->nodeNr; i++) {
                if (edit_replace(orig_doc, nodes->nodesetval->nodeTab[i],
                                 running, error) != EXIT_SUCCESS) {
                    xmlXPathFreeObject(nodes);
                    goto error;
                }
                nodes->nodesetval->nodeTab[i] = NULL;
            }
        }
        xmlXPathFreeObject(nodes);
    }

    /* create operations */
    nodes = get_operation_elements(NC_EDIT_OP_CREATE, edit_doc);
    if (nodes != NULL) {
        if (!xmlXPathNodeSetIsEmpty(nodes->nodesetval)) {
            /* something to create */
            for (i = 0; i < nodes->nodesetval->nodeNr; i++) {
                if (edit_create(orig_doc, nodes->nodesetval->nodeTab[i],
                                running, error) != EXIT_SUCCESS) {
                    xmlXPathFreeObject(nodes);
                    goto error;
                }
                nodes->nodesetval->nodeTab[i] = NULL;
            }
        }
        xmlXPathFreeObject(nodes);
    }

    /* merge operations */
    nodes = get_operation_elements(NC_EDIT_OP_MERGE, edit_doc);
    if (nodes != NULL) {
        if (!xmlXPathNodeSetIsEmpty(nodes->nodesetval)) {
            /* something to create */
            for (i = 0; i < nodes->nodesetval->nodeNr; i++) {
                if (edit_merge(orig_doc, nodes->nodesetval->nodeTab[i],
                               running, error) != EXIT_SUCCESS) {
                    xmlXPathFreeObject(nodes);
                    goto error;
                }
                nodes->nodesetval->nodeTab[i] = NULL;
            }
        }
        xmlXPathFreeObject(nodes);
    }

    /* default merge */
    if (defop == NC_EDIT_DEFOP_MERGE || defop == NC_EDIT_DEFOP_NOTSET) {
        /* replace whole document */
        if (edit_doc->children != NULL) {
            for (edit_node = edit_doc->children; edit_node != NULL;
                 edit_node = edit_doc->children) {
                if (edit_merge(orig_doc, edit_doc->children,
                               running, error) != EXIT_SUCCESS) {
                    goto error;
                }
            }
        }
    }

    return EXIT_SUCCESS;

error:

    if (*error == NULL) {
        *error = nc_err_new(NC_ERR_OP_FAILED);
    }

    return EXIT_FAILURE;
}

/**
 * \brief Perform edit-config's "delete" operation on the selected node.
 *
 * @param[in] node XML node from the configuration data to delete.
 * @param[in] running Flag for applying changes to the OVSDB
 * @return Zero on success, non-zero otherwise.
 */
static int
edit_delete(xmlNodePtr node, int running, struct nc_err **e)
{
    xmlNodePtr child;
    const xmlChar *bridge_name;
    const xmlChar *key, *aux;
    int ret = EXIT_SUCCESS;

    if (!node) {
        return EXIT_SUCCESS;
    }

    nc_verb_verbose("Deleting the node %s%s", (char *) node->name,
                    (running ? " Running" : ""));
    if (running) {
        if (node->parent->type == XML_DOCUMENT_NODE) {  /* capable-switch node
                                                         */
            /* removing root */
            return txn_del_all(e);
        }
        if (xmlStrEqual(node->parent->name, BAD_CAST "capable-switch")) {
            if (xmlStrEqual(node->name, BAD_CAST "id")) {
                ofc_set_switchid(NULL);
            } else {            /* resources, logical-switches */
                while (node->children) {
                    if ((ret = edit_delete(node->children, running, e))) {
                        /* failure */
                        break;
                    }
                }
            }
        } else if (xmlStrEqual(node->parent->name, BAD_CAST "resources")) {
            if (xmlStrEqual(node->parent->parent->name,
                            BAD_CAST "capable-switch")) {
                if (xmlStrEqual(node->name, BAD_CAST "port")) {
                    key = get_key(node, "name");
                    ret = txn_del_port(key, e);
                } else if (xmlStrEqual(node->name, BAD_CAST "queue")) {
                    key = get_key(node, "resource-id");
                    ret = txn_del_queue(key, e);
                } else if (xmlStrEqual(node->name,
                                       BAD_CAST "owned-certificate")) {
                    ret = txn_del_owned_certificate(node, e);
                } else if (xmlStrEqual(node->name,
                                       BAD_CAST "external-certificate")) {
                    ret = txn_del_external_certificate(node, e);
                } else if (xmlStrEqual(node->name, BAD_CAST "flow-table")) {
                    key = get_key(node, "table-id");
                    ret = txn_del_flow_table(key, e);
                } else {
                    nc_verb_warning("%s: unknown element %s (parent: %s)",
                                    __func__, (const char *) node->name,
                                    (const char *) node->parent->name);
                }
            } else {            /* logical-switch */
                /* get bridge name */
                key = get_key(node->parent->parent, "id");

                /* get leaf-ref value */
                aux = node->children ? node->children->content : NULL;
                if (!aux) {
                    *e = nc_err_new(NC_ERR_BAD_ELEM);
                    nc_err_set(*e, NC_ERR_PARAM_MSG,
                               "invalid resources leafref");
                    nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM,
                               (char *) node->name);
                    ret = EXIT_FAILURE;
                } else if (xmlStrEqual(node->name, BAD_CAST "port")) {
                    ret = txn_del_bridge_port(key, aux, e);
                } else if (xmlStrEqual(node->name, BAD_CAST "flow-table")) {
                    ret = txn_del_bridge_flowtable(key, aux, e);
                }
                /* certificate is ignored on purpose! Once defined, it is
                 * automatically referenced and used in every bridge. */
                /* queue is ignored on purpose! It is linked with the port,
                 * reference here is only informative
                 */
            }
        } else if (xmlStrEqual(node->name, BAD_CAST "private-key")) {
            while (node->children) {
                ret = edit_delete(node->children, running, e);
                if (ret) {
                    break;
                }
            }
        } else if (xmlStrEqual(node->parent->name, BAD_CAST "private-key")) {
            key = get_key(node->parent->parent, "resource-id");

            if (xmlStrEqual(node->name, BAD_CAST "key-type")) {
                ret = txn_mod_own_cert_key_type(key, NULL, e);
            } else if (xmlStrEqual(node->name, BAD_CAST "key-data")) {
                ret = txn_mod_own_cert_key_data(key, NULL, e);
            } else {
                nc_verb_warning("%s: unknown element %s (parent: %s)",
                                __func__, (const char *) node->name,
                                (const char *) node->parent->name);
            }
        } else
            if (xmlStrEqual(node->parent->name, BAD_CAST "owned-certificate"))
        {
            key = get_key(node->parent, "resource-id");
            ret = txn_mod_own_cert_certificate(key, NULL, e);
        } else
            if (xmlStrEqual
                (node->parent->name, BAD_CAST "external-certificate")) {
            key = get_key(node->parent, "resource-id");
            ret = txn_mod_ext_cert_certificate(key, NULL, e);
        } else if (xmlStrEqual(node->name, BAD_CAST "switch")) {
            /* remove bridge */
            key = get_key(node, "id");
            ret = txn_del_bridge(key, e);
        } else if (xmlStrEqual(node->parent->name, BAD_CAST "switch")) {
            key = get_key(node->parent, "id");

            /* key (id) cannot be deleted */
            if (xmlStrEqual(node->name, BAD_CAST "datapath-id")) {
                ret = txn_mod_bridge_datapath(key, NULL, e);
            } else if (xmlStrEqual(node->name, BAD_CAST "controllers")) {
                while (node->children) {        /* controller */
                    if ((ret = edit_delete(node->children, running, e))) {
                        break;
                    }
                }
            } else
                if (xmlStrEqual
                    (node->name, BAD_CAST "lost-connection-behavior")) {
                ret = txn_mod_bridge_failmode(key, NULL, e);
            }
            /* enabled is not handled: it is too complicated to handle it in
             * combination with the OVSDB's garbage collection. */
        } else if (xmlStrEqual(node->parent->name, BAD_CAST "queue")) {
            if (xmlStrEqual(node->name, BAD_CAST "port")) {
                key = get_key(node->parent, "resource-id");
                ret = txn_del_queue_port(key, e);
            } else if (xmlStrEqual(node->name, BAD_CAST "properties")) {
                while (node->children) {
                    if ((ret = edit_delete(node->children, running, e))) {
                        break;
                    }
                }
            }
            /* id is ignored on purpose. It is mandatory value and delete as
             * part of replace is not needed since the subsequent create
             * allows to replace the current value directly */
        } else if (xmlStrEqual(node->parent->name, BAD_CAST "properties")) {
            key = get_key(node->parent->parent, "resource-id");
            ret = txn_mod_queue_options(key, (char *) node->name, NULL, e);
        } else if (xmlStrEqual(node->parent->name, BAD_CAST "flow-table")) {
            /* key 'table-id' cannot be deleted */
            key = get_key(node->parent, "table-id");
            if (xmlStrEqual(node->name, BAD_CAST "name")) {
                ret = txn_mod_flowtable_name(key, NULL, e);
            } else if (xmlStrEqual(node->name, BAD_CAST "resource-id")) {
                ret = txn_mod_flowtable_resid(key, NULL, e);
            }
        } else if (xmlStrEqual(node->name, BAD_CAST "controller")) {
            key = get_key(node, "id");  /* controller id */
            aux = get_key(node->parent->parent, "id");  /* bridge id */
            ret = txn_del_contr(key, aux, e);
        } else if (xmlStrEqual(node->parent->name, BAD_CAST "controller")) {
            key = get_key(node->parent, "id");
            /* key 'id' cannot be deleted */
            if (xmlStrEqual(node->name, BAD_CAST "local-ip-address")) {
                ret = txn_mod_contr_lip(key, NULL, e);
            } else if (xmlStrEqual(node->name, BAD_CAST "ip-address")
                       || xmlStrEqual(node->name, BAD_CAST "port")
                       || xmlStrEqual(node->name, BAD_CAST "protocol")) {
                ret = txn_mod_contr_target(key, node->name, NULL, e);
            }
        } else if (xmlStrEqual(node->name, BAD_CAST "requested-number")) {
            key = get_key(node->parent, "name");
            ret = txn_mod_port_reqnumber(key, NULL, e);
        } else if (xmlStrEqual(node->name, BAD_CAST "ipgre-tunnel")
                   || xmlStrEqual(node->name, BAD_CAST "vxlan-tunnel")
                   || xmlStrEqual(node->name, BAD_CAST "tunnel")) {
            key = get_key(node->parent, "name");
            ret = txn_del_port_tunnel(key, node, e);
        } else if (xmlStrEqual(node->name, BAD_CAST "no-receive")
                   || xmlStrEqual(node->name, BAD_CAST "no-forward")
                   || xmlStrEqual(node->name, BAD_CAST "no-packet-in")
                   || xmlStrEqual(node->name, BAD_CAST "admin-state")) {
            key = get_key(node->parent->parent, "name");
            bridge_name = ofc_find_bridge_with_port(key);

            /* delete -> set to default */
            ret =
                ofc_of_mod_port(bridge_name, key, node->name, BAD_CAST "", e);
        } else if (xmlStrEqual(node->name, BAD_CAST "features")) {
            ret =
                edit_delete(go2node(node, BAD_CAST "advertised"), running, e);
        } else if (xmlStrEqual(node->name, BAD_CAST "advertised")) {
            key = get_key(node->parent->parent, "name");
            for (child = node->children; child; child = child->next) {
                if ((ret = txn_del_port_advert(key, child, e))) {
                    break;
                }
            }
        } else if (xmlStrEqual(node->parent->name, BAD_CAST "advertised")) {
            key = get_key(node->parent->parent->parent, "name");
            ret = txn_del_port_advert(key, node, e);
        } else
            if (xmlStrEqual(node->name, BAD_CAST "local-endpoint-ipv4-adress")
                || xmlStrEqual(node->name,
                               BAD_CAST "remote-endpoint-ipv4-adress")
                || xmlStrEqual(node->name, BAD_CAST "key")
                || xmlStrEqual(node->name, BAD_CAST "vni")) {
            key = get_key(node->parent->parent, "name");
            ret = txn_mod_port_tunnel_opt(key, node, NULL, e);
        } else {
            nc_verb_warning("%s: Unknown element %s (parent: %s)", __func__,
                            (const char *) node->name,
                            (const char *) node->parent->name);
        }
    }

    if (!ret) {
        xmlUnlinkNode(node);
        xmlFreeNode(node);
    }

    return ret;
}

/**
 * \brief Perform edit-config's "remove" operation on the selected node.
 *
 * \param[in] orig_doc Original configuration document to edit.
 * \param[in] edit_node Node from the edit-config's \<config\> element with
 * the specified "remove" operation.
 * @param[in] running Flag for applying changes to the OVSDB
 *
 * \return Zero on success, non-zero otherwise.
 */
static int
edit_remove(xmlDocPtr orig_doc, xmlNodePtr edit_node, int running,
            struct nc_err **error)
{
    xmlNodePtr old;

    old = find_element_equiv(orig_doc, edit_node);

    /* remove the node from the edit document */
    edit_delete(edit_node, 0, error);

    if (old) {
        /* remove the edit node's equivalent from the original document */
        edit_delete(old, running, error);
    }
    return (EXIT_SUCCESS);
}

/**
 * \brief Perform edit-config's "replace" operation on the selected node.
 *
 * \param[in] orig_doc Original configuration document to edit.
 * \param[in] edit_node Node from the edit-config's \<config\> element with
 * the specified "replace" operation.
 * @param[in] running Flag for applying changes to the OVSDB
 *
 * \return Zero on success, non-zero otherwise.
 */
static int
edit_replace(xmlDocPtr orig_doc, xmlNodePtr edit_node, int running,
             struct nc_err **error)
{
    xmlNodePtr old;

    if (orig_doc == NULL) {
        return (EXIT_FAILURE);
    }

    if (edit_node == NULL) {
        /* replace by empty data */
        if (orig_doc->children) {
            return (edit_delete(orig_doc->children, running, error));
        } else {
            /* initial cleanup */
            return txn_del_all(error);
        }
    }

    old = find_element_equiv(orig_doc, edit_node);
    if (old == NULL) {
        /* node to be replaced doesn't exist, so create new configuration data
         */
        return edit_create(orig_doc, edit_node, running, error);
    } else {
        /*
         * replace old configuration data with the new data
         * Do this removing the old node and creating a new one to cover actual
         * "moving" of the instance of the list/leaf-list using YANG's insert
         * attribute
         */
        if (edit_delete(old, running, error)) {
            return EXIT_FAILURE;
        }
        return edit_create(orig_doc, edit_node, running, error);
    }
}

/**
 * \brief Recursive follow-up of the edit_create()
 *
 * The recursion is needed only in case the data are not applied to OVSDB, but
 * only to the XML tree.
 *
 * \param[in] orig_doc Original configuration document to edit.
 * \param[in] edit_node Node from the edit-config's \<config\> element with
 * the specified "replace" operation.
 *
 * \return Zero on success, non-zero otherwise.
 */
static xmlNodePtr
edit_create_r(xmlDocPtr orig_doc, xmlNodePtr edit, struct nc_err **error)
{
    xmlNodePtr retval = NULL;
    xmlNodePtr parent = NULL;
    xmlNsPtr ns_aux;

    retval = find_element_equiv(orig_doc, edit);
    if (retval == NULL) {

        if (edit->parent->type == XML_DOCUMENT_NODE) {
            /* original document is empty */
            nc_verb_verbose("Creating the node %s", (char *) edit->name);
            retval = xmlCopyNode(edit, 0);
            if (edit->ns) {
                ns_aux = xmlNewNs(retval, edit->ns->href, NULL);
                xmlSetNs(retval, ns_aux);
            }
            xmlDocSetRootElement(orig_doc, retval);
            return (retval);
        }

        parent = edit_create_r(orig_doc, edit->parent, error);
        if (parent == NULL) {
            return (NULL);
        }
        retval = xmlAddChild(parent, xmlCopyNode(edit, 0));
        if (edit->ns && parent->ns
            && xmlStrcmp(edit->ns->href, parent->ns->href) == 0) {
            xmlSetNs(retval, parent->ns);
        } else if (edit->ns) {
            ns_aux = xmlNewNs(retval, edit->ns->href, NULL);
            xmlSetNs(retval, ns_aux);
        }
    }
    return retval;
}

/**
 * \brief Perform edit-config's "create" operation on the selected node.
 *
 * \param[in] orig_doc Original configuration document to edit.
 * \param[in] edit Node from the edit-config's \<config\> element with
 * the specified "create" operation.
 * @param[in] running Flag for applying changes to the OVSDB
 *
 * \return Zero on success, non-zero otherwise.
 */
static int
edit_create(xmlDocPtr orig_doc, xmlNodePtr edit, int running,
            struct nc_err **e)
{
    xmlNodePtr parent, child;
    const xmlChar *bridge_name, *key, *aux;
    int ret = EXIT_SUCCESS;

    /* remove operation attribute */
    xmlRemoveProp(xmlHasNsProp(edit, BAD_CAST "operation",
                               BAD_CAST NC_NS_BASE10));
    clrns(edit);

    nc_verb_verbose("Creating the node %s", (char *) edit->name);
    if (running) {
        /* OVS */
        if (edit->parent->type == XML_DOCUMENT_NODE) {
            /* set it all */
            while (edit->children) {
                ret = edit_create(orig_doc, edit->children, running, e);
                if (ret) {
                    break;
                }
            }
            return ret;
        }
        if (xmlStrEqual(edit->parent->name, BAD_CAST "capable-switch")) {
            if (xmlStrEqual(edit->name, BAD_CAST "id")) {
                ofc_set_switchid(edit);
            } else {            /* resources and local-switches */
                /* nothing to do on this level, continue with creating
                 * children */
                while (edit->children) {
                    ret = edit_create(orig_doc, edit->children, running, e);
                    if (ret) {
                        break;
                    }
                }
            }
        } else if (xmlStrEqual(edit->parent->name, BAD_CAST "resources")) {
            if (xmlStrEqual
                (edit->parent->parent->name, BAD_CAST "capable-switch")) {
                if (xmlStrEqual(edit->name, BAD_CAST "port")) {
                    ret = txn_add_port(edit, e);
                } else if (xmlStrEqual(edit->name, BAD_CAST "queue")) {
                    ret = txn_add_queue(edit, e);
                } else
                    if (xmlStrEqual(edit->name, BAD_CAST "owned-certificate"))
                {
                    ret = txn_add_owned_certificate(edit, e);
                } else
                    if (xmlStrEqual
                        (edit->name, BAD_CAST "external-certificate")) {
                    ret = txn_add_external_certificate(edit, e);
                } else if (xmlStrEqual(edit->name, BAD_CAST "flow-table")) {
                    ret = txn_add_flow_table(edit, e);
                } else {
                    nc_verb_warning("%s: unknown element %s (parent: %s)",
                                    __func__, (const char *) edit->name,
                                    (const char *) edit->parent->name);
                }
            } else {            /* logical-switch */
                /* get bridge name */
                key = get_key(edit->parent->parent, "id");

                /* get leaf-ref value */
                aux = edit->children ? edit->children->content : NULL;
                if (!aux) {
                    *e = nc_err_new(NC_ERR_BAD_ELEM);
                    nc_err_set(*e, NC_ERR_PARAM_MSG,
                               "invalid resources leafref");
                    nc_err_set(*e, NC_ERR_PARAM_INFO_BADELEM,
                               (char *) edit->name);
                    ret = EXIT_FAILURE;
                } else if (xmlStrEqual(edit->name, BAD_CAST "port")) {
                    ret = txn_add_bridge_port(key, aux, e);
                } else if (xmlStrEqual(edit->name, BAD_CAST "flow-table")) {
                    ret = txn_add_bridge_flowtable(key, aux, e);
                }
                /* certificate is ignored on purpose! Once defined, it is
                 * automatically referenced and used in every bridge. */
                /* queue is ignored on purpose! It is linked with the port,
                 * reference here is only informative
                 */
            }
        } else if (xmlStrEqual(edit->name, BAD_CAST "private-key")) {
            while (edit->children) {
                ret = edit_create(orig_doc, edit->children, running, e);
                if (ret) {
                    break;
                }
            }
        } else if (xmlStrEqual(edit->parent->name, BAD_CAST "private-key")) {
            key = get_key(edit->parent->parent, "resource-id");

            if (xmlStrEqual(edit->name, BAD_CAST "key-type")) {
                ret = txn_mod_own_cert_key_type(key, edit, e);
            } else if (xmlStrEqual(edit->name, BAD_CAST "key-data")) {
                ret = txn_mod_own_cert_key_data(key, edit, e);
            } else {
                nc_verb_warning("%s: unknown element %s (parent: %s)",
                                __func__, (const char *) edit->name,
                                (const char *) edit->parent->name);
            }
        } else
            if (xmlStrEqual(edit->parent->name, BAD_CAST "owned-certificate"))
        {
            key = get_key(edit->parent, "resource-id");

            ret = txn_mod_own_cert_certificate(key, edit, e);
        } else
            if (xmlStrEqual
                (edit->parent->name, BAD_CAST "external-certificate")) {
            key = get_key(edit->parent, "resource-id");

            ret = txn_mod_ext_cert_certificate(key, edit, e);
        } else if (xmlStrEqual(edit->name, BAD_CAST "switch")) {
            /* create bridge */
            ret = txn_add_bridge(edit, e);
        } else if (xmlStrEqual(edit->parent->name, BAD_CAST "switch")) {
            key = get_key(edit->parent, "id");

            /* key (id) cannot be added separately */
            if (xmlStrEqual(edit->name, BAD_CAST "datapath-id")) {
                aux = edit->children ? edit->children->content : NULL;
                ret = txn_mod_bridge_datapath(key, aux, e);
            } else if (xmlStrEqual(edit->name, BAD_CAST "controllers")) {
                while (edit->children) {        /* controller */
                    ret = edit_create(orig_doc, edit->children, running, e);
                    if (ret) {
                        break;
                    }
                }
            } else
                if (xmlStrEqual
                    (edit->name, BAD_CAST "lost-connection-behavior")) {
                aux = edit->children ? edit->children->content : NULL;
                ret = txn_mod_bridge_failmode(key, aux, e);
            }
            /* enabled is not handled: it is too complicated to handle it in
             * combination with the OVSDB's garbage collection. */
        } else if (xmlStrEqual(edit->parent->name, BAD_CAST "queue")) {
            if (xmlStrEqual(edit->name, BAD_CAST "id")) {
                key = get_key(edit->parent, "resource-id");
                aux = edit->children ? edit->children->content : NULL;
                ret = txn_mod_queue_id(key, aux, e);
            } else if (xmlStrEqual(edit->name, BAD_CAST "port")) {
                key = get_key(edit->parent, "resource-id");
                aux = edit->children ? edit->children->content : NULL;
                ret = txn_add_queue_port(key, aux, e);
            } else if (xmlStrEqual(edit->name, BAD_CAST "properties")) {
                while (edit->children) {
                    ret = edit_create(orig_doc, edit->children, running, e);
                    if (ret) {
                        break;
                    }
                }
            }
        } else if (xmlStrEqual(edit->parent->name, BAD_CAST "properties")) {
            key = get_key(edit->parent->parent, "resource-id");
            ret = txn_mod_queue_options(key, (char *) edit->name, edit, e);
        } else if (xmlStrEqual(edit->parent->name, BAD_CAST "flow-table")) {
            /* key 'table-id' */
            key = get_key(edit->parent, "table-id");
            if (xmlStrEqual(edit->name, BAD_CAST "name")) {
                ret = txn_mod_flowtable_name(key, edit, e);
            } else if (xmlStrEqual(edit->name, BAD_CAST "resource-id")) {
                ret = txn_mod_flowtable_resid(key, edit, e);
            }
        } else if (xmlStrEqual(edit->name, BAD_CAST "controller")) {
            key = get_key(edit->parent->parent, "id");
            ret = txn_add_contr(edit, key, e);
        } else if (xmlStrEqual(edit->parent->name, BAD_CAST "controller")) {
            key = get_key(edit->parent, "id");
            /* key 'id' cannot be deleted */
            if (xmlStrEqual(edit->name, BAD_CAST "local-ip-address")) {
                ret = txn_mod_contr_lip(key, NULL, e);
            } else if (xmlStrEqual(edit->name, BAD_CAST "ip-address")
                       || xmlStrEqual(edit->name, BAD_CAST "port")
                       || xmlStrEqual(edit->name, BAD_CAST "protocol")) {
                aux = edit->children ? edit->children->content : NULL;
                ret = txn_mod_contr_target(key, edit->name, aux, e);
            }
        } else if (xmlStrEqual(edit->name, BAD_CAST "requested-number")) {
            key = get_key(edit->parent, "name");
            aux = edit->children ? edit->children->content : NULL;
            ret = txn_mod_port_reqnumber(key, aux, e);
        } else if (xmlStrEqual(edit->name, BAD_CAST "no-receive")
                   || xmlStrEqual(edit->name, BAD_CAST "no-forward")
                   || xmlStrEqual(edit->name, BAD_CAST "no-packet-in")
                   || xmlStrEqual(edit->name, BAD_CAST "admin-state")) {

            nc_verb_verbose("Modify port configuration (%s:%d)", __FILE__,
                            __LINE__);
            key = get_key(edit->parent->parent, "name");
            aux = edit->children ? edit->children->content : NULL;
            bridge_name = ofc_find_bridge_with_port(key);

            ret = ofc_of_mod_port(bridge_name, key, edit->name, aux, e);
        } else if (xmlStrEqual(edit->name, BAD_CAST "features")) {
            ret = edit_create(orig_doc, go2node(edit, BAD_CAST "advertised"),
                              running, e);
        } else if (xmlStrEqual(edit->name, BAD_CAST "advertised")) {
            key = get_key(edit->parent->parent, "name");
            for (child = edit->children; child; child = child->next) {
                if ((ret = txn_add_port_advert(key, child, e))) {
                    break;
                }
            }
        } else if (xmlStrEqual(edit->parent->name, BAD_CAST "advertised")) {
            key = get_key(edit->parent->parent->parent, "name");
            ret = txn_add_port_advert(key, edit, e);
        } else
            if (xmlStrEqual(edit->name, BAD_CAST "local-endpoint-ipv4-adress")
                || xmlStrEqual(edit->name,
                               BAD_CAST "remote-endpoint-ipv4-adress")
                || xmlStrEqual(edit->name, BAD_CAST "key")
                || xmlStrEqual(edit->name, BAD_CAST "vni")) {
            key = get_key(edit->parent->parent, "name");
            if (edit->children && edit->children->content) {
                ret =
                    txn_mod_port_tunnel_opt(key, edit, edit->children->content,
                                            e);
            }
        } else {
            nc_verb_warning("%s: unknown element %s (parent: %s)", __func__,
                            (const char *) edit->name,
                            (const char *) edit->parent->name);
        }
    } else {
        /* XML */
        if (edit->parent->type != XML_DOCUMENT_NODE) {
            parent = edit_create_r(orig_doc, edit->parent, e);
            if (!parent) {
                return EXIT_FAILURE;
            }
        } else {
            /* we are in the root */
            parent = (xmlNodePtr) (orig_doc->doc);
        }

        if (parent->type == XML_DOCUMENT_NODE) {
            xmlDocSetRootElement(parent->doc, xmlCopyNode(edit, 1));
        } else {
            if (xmlAddChild(parent, xmlCopyNode(edit, 1)) == NULL) {
                nc_verb_error("Creating new node (%s) failed", edit->name);
                return (EXIT_FAILURE);
            }
        }
    }

    /* remove the node from the edit document */
    if (!ret) {
        edit_delete(edit, 0, NULL);
    }

    return ret;
}

/**
 * \brief Perform edit-config's "merge" operation on the selected node.
 *
 * \param[in] orig_doc Original configuration document to edit.
 * \param[in] edit_node Node from the edit-config's \<config\> element where
 * to apply merge
 * @param[in] running Flag for applying changes to the OVSDB
 *
 * \return Zero on success, non-zero otherwise.
 */
static int
edit_merge(xmlDocPtr orig_doc, xmlNodePtr edit_node, int running,
           struct nc_err **error)
{
    xmlNodePtr orig_node;
    xmlNodePtr aux, child;

    orig_node = find_element_equiv(orig_doc, edit_node);
    if (orig_node == NULL) {
        return edit_create(orig_doc, edit_node, running, error);
    }

    if (is_key(edit_node)) {
        /* skip key elements from merging */
        return EXIT_SUCCESS;
    }

    child = edit_node->children;
    if (child && child->type == XML_TEXT_NODE) {
        /* we are in the leaf -> replace the previous value leaf-lists are
         * coverede in find_element_equiv() - if edit_node is a new instance
         * of the leaf-list, orig_node would be NULL */
        return edit_replace(orig_doc, edit_node, running, error);
    } else {
        /* we can go recursive */
        while (child) {
            if (is_key(child)) {
                /* skip keys */
                child = child->next;
            } else {
                aux = child->next;
                edit_merge(orig_doc, child, running, error);
                child = aux;
            }
        }
    }

    /* remove the node from the edit document */
    edit_delete(edit_node, 0, NULL);

    return EXIT_SUCCESS;
}
