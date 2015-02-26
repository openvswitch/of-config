
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
#include <math.h>
#include <stdlib.h>

#include "res-map.h"

#include <ovsdb-idl-provider.h>
#include <libnetconf.h>

/* convert uuid to string, for debug/test purposes only */
static const char *
print_uuid_ro(const struct uuid *uuid)
{
    static char str[38];

    snprintf(str, 37, UUID_FMT, UUID_ARGS(uuid));
    str[37] = 0;
    return str;
}

/*
 * Compare two elements from index array.
 * It is used for qsort() and bsearch() of index arrays index_r, index_u.
 * \return standard values like strcmp() (-1: <, 0: ==, 1 >)
 */
static int
cmp_resourceid_index(const void *p1, const void *p2)
{
    const ofc_tuple_t *r1 = *((const ofc_tuple_t **) p1);
    const ofc_tuple_t *r2 = *((const ofc_tuple_t **) p2);

    return strcmp(r1->resource_id, r2->resource_id);
}

/*
 * Compare two elements from index array.
 * It is used for qsort() and bsearch() of index arrays index_r, index_u.
 * \return standard values like strcmp() (-1: <, 0: ==, 1 >)
 */
static int
cmp_uuid_index(const void *p1, const void *p2)
{
    const ofc_tuple_t *r1 = *((const ofc_tuple_t **) p1);
    const ofc_tuple_t *r2 = *((const ofc_tuple_t **) p2);

    if (uuid_equals(&r1->uuid, &r2->uuid)) {
        return 0;
    } else {
        /* not equal, iterate over all parts of UUID */
        if (r1->uuid.parts[0] > r2->uuid.parts[0]) {
            return 1;
        } else if (r1->uuid.parts[0] < r2->uuid.parts[0]) {
            return -1;
        }
        if (r1->uuid.parts[1] > r2->uuid.parts[1]) {
            return 1;
        } else if (r1->uuid.parts[1] < r2->uuid.parts[1]) {
            return -1;
        }
        if (r1->uuid.parts[2] > r2->uuid.parts[2]) {
            return 1;
        } else if (r1->uuid.parts[2] < r2->uuid.parts[2]) {
            return -1;
        }
        if (r1->uuid.parts[3] > r2->uuid.parts[3]) {
            return 1;
        } else if (r1->uuid.parts[3] < r2->uuid.parts[3]) {
            return -1;
        }
    }
    /* nothing can get here... == was tested at first */
    return 0;
}

ofc_resmap_t *
ofc_resmap_init(size_t init_size)
{
    ofc_resmap_t *t = (ofc_resmap_t *) calloc(1, sizeof *t);

    if (t == NULL) {
        return NULL;
    }

    t->records_length = MAX(256, init_size);
    t->n_records = 0;
    t->records = (ofc_tuple_t *) calloc(t->records_length,
                                        sizeof (ofc_tuple_t));
    t->index_r = (ofc_tuple_t **) calloc(t->records_length,
                                         sizeof (ofc_tuple_t *));
    t->index_u = (ofc_tuple_t **) calloc(t->records_length,
                                         sizeof (ofc_tuple_t *));
    if ((t->records == NULL) || (t->index_r == NULL) || (t->index_u == NULL)) {
        free(t->records);
        free(t->index_r);
        free(t->index_u);
        free(t);
        return NULL;
    }
    return t;
}

/*
 * Run sort on index arrays.
 * \param[in,out] rm    pointer to the resource map structure
 */
static void
ofc_reindex(ofc_resmap_t *rm)
{
    if (rm->n_records > 1) {
        qsort(rm->index_r, rm->n_records, sizeof (ofc_tuple_t **),
              cmp_resourceid_index);
        qsort(rm->index_u, rm->n_records, sizeof (ofc_tuple_t **),
              cmp_uuid_index);
    }
}

bool
ofc_resmap_insert(ofc_resmap_t *rm, const char *resource_id,
                  const struct uuid *uuid, const struct ovsdb_idl_row *h)
{
    size_t new_size;

    if (rm->n_records == rm->records_length) {
        /* not enough space, we need to realloc */
        new_size = rm->records_length + rm->records_length / 2;
        if (realloc(rm->records, new_size * sizeof (ofc_tuple_t)) == NULL) {
            return false;
        }
        if (realloc(rm->index_r, new_size * sizeof (ofc_tuple_t *)) == NULL) {
            return false;
        }
        if (realloc(rm->index_u, new_size * sizeof (ofc_tuple_t *)) == NULL) {
            return false;
        }
        /* successfuly enlarged or exited */
        rm->records_length = new_size;
    }
    /* append new record */
    rm->records[rm->n_records].resource_id = strdup(resource_id);
    rm->records[rm->n_records].uuid = *uuid;
    rm->records[rm->n_records].header = h;
    rm->index_r[rm->n_records] = &rm->records[rm->n_records];
    rm->index_u[rm->n_records] = &rm->records[rm->n_records];
    rm->n_records++;

    /* update index arrays */
    ofc_reindex(rm);

    return true;
}

static bool
ofc_resmap_remove(ofc_resmap_t *rm, ofc_tuple_t *del)
{
    ofc_tuple_t **index_r_p;
    ofc_tuple_t **index_u_p;
    ofc_tuple_t *tuple = NULL;
    ofc_tuple_t key, *key_p = &key;
    size_t i;

    for (i = 0; i < rm->n_records; i++) {
        tuple = &rm->records[i];
        if (uuid_equals(&del->uuid, &tuple->uuid)) {
            break;
        }
    }
    if (!tuple || !uuid_equals(&del->uuid, &tuple->uuid)) {
        /* record was not found in records */
        return false;
    }
    key.resource_id = tuple->resource_id;
    key.uuid = tuple->uuid;
    index_r_p = (ofc_tuple_t **) bsearch(&key_p, rm->index_r, rm->n_records,
                                         sizeof (ofc_tuple_t **),
                                         cmp_resourceid_index);
    index_u_p =
        (ofc_tuple_t **) bsearch(&key_p, rm->index_u, rm->n_records,
                                 sizeof (ofc_tuple_t **), cmp_uuid_index);
    free(tuple->resource_id);
    (*tuple) = rm->records[rm->n_records - 1];
    tuple->resource_id = rm->records[rm->n_records - 1].resource_id;
    (*index_r_p) = rm->index_r[rm->n_records - 1];
    (*index_u_p) = rm->index_u[rm->n_records - 1];
    rm->n_records--;
    ofc_reindex(rm);

    return true;
}

bool
ofc_resmap_remove_r(ofc_resmap_t *rm, const char *resource_id)
{
    ofc_tuple_t *tuple;

    if (rm->n_records == 0) {
        /* empty */
        return false;
    }

    tuple = ofc_resmap_find_r(rm, resource_id);
    if (tuple == NULL) {
        /* not found in the index array */
        return false;
    }
    return ofc_resmap_remove(rm, tuple);
}

bool
ofc_resmap_remove_u(ofc_resmap_t *rm, const struct uuid *uuid)
{
    ofc_tuple_t *tuple;

    if (rm->n_records == 0) {
        /* empty */
        return false;
    }

    tuple = ofc_resmap_find_u(rm, uuid);
    if (tuple == NULL) {
        /* not found in the index array */
        return false;
    }
    return ofc_resmap_remove(rm, tuple);
}

ofc_tuple_t *
ofc_resmap_find_r(ofc_resmap_t *rm, const char *resource_id)
{
    const void *result;
    ofc_tuple_t key, *key_p = &key;

    key.resource_id = (char *) resource_id;
    result = bsearch(&key_p, rm->index_r, rm->n_records,
                     sizeof (ofc_tuple_t **), cmp_resourceid_index);
    if (result == NULL) {
        /* not found */
        return NULL;
    }
    return *((ofc_tuple_t **) result);
}

ofc_tuple_t *
ofc_resmap_find_u(ofc_resmap_t *rm, const struct uuid *uuid)
{
    const void *result;
    ofc_tuple_t key, *key_p = &key;

    key.uuid = *uuid;
    result = bsearch(&key_p, rm->index_u, rm->n_records,
                     sizeof (ofc_tuple_t **), cmp_uuid_index);
    if (result == NULL) {
        /* not found */
        return NULL;
    }
    return *((ofc_tuple_t **) result);
}

void
ofc_resmap_destroy(ofc_resmap_t **resmap)
{
    int i;
    ofc_resmap_t *t;

    if (resmap == NULL) {
        return;
    }
    t = *resmap;
    for (i = 0; i < t->n_records; i++) {
        /* clean up stored copies of resource_id strings */
        free(t->records[i].resource_id);
    }
    free(t->records);
    free(t->index_r);
    free(t->index_u);
    free(t);
    (*resmap) = NULL;
}

void
ofc_resmap_update_uuids(ofc_resmap_t *rm)
{
    size_t i;
    const struct ovsdb_idl_row *h;
    bool changed = false;

    for (i = 0; i < rm->n_records; i++) {
        if (rm->records[i].header != NULL) {
            h = rm->records[i].header;
            rm->records[i].uuid = h->uuid;
            rm->records[i].header = NULL;
            nc_verb_verbose("Updated UUID %s for %s resource-id.",
                            print_uuid_ro(&rm->records[i].uuid),
                            rm->records[i].resource_id);
            changed = true;
        }
    }
    if (changed) {
        ofc_reindex(rm);
    }
}

#ifdef TEST_RESOURCE_MAP

/*
 * Print content of the map sort by the uuid index.
 * \param[in] rm    pointer to the resource map structure
 */
static void
ofc_resmap_print_u(ofc_resmap_t *rm)
{
    size_t i;

    for (i = 0; i < rm->n_records; i++) {
        printf("%s - %s\n", rm->index_u[i]->resource_id,
               print_uuid_ro(&rm->index_u[i]->uuid));
    }
}

/*
 * Print content of the map sort by the records array.
 * \param[in] rm    pointer to the resource map structure
 */
void
ofc_resmap_print(ofc_resmap_t * rm)
{
    size_t i;

    for (i = 0; i < rm->n_records; i++) {
        printf("%s - %s\n", rm->records[i].resource_id,
               print_uuid_ro(&rm->records[i].uuid));
    }
}

/*
 * Print content of the map sort by the resource_id index.
 * \param[in] rm    pointer to the resource map structure
 */
static void
ofc_resmap_print_r(ofc_resmap_t * rm)
{
    size_t i;

    for (i = 0; i < rm->n_records; i++) {
        printf("%s - %s\n", rm->index_r[i]->resource_id,
               print_uuid_ro(&rm->index_r[i]->uuid));
    }
}

/* test of the data structure: inserts TEST_NUM_ELEMS records, removes half
 * of them from the beginning and half of them from the end, the rest is printed.
 */
#define TEST_NUM_ELEMS  50

int
main(int argc, char **argv)
{
    struct uuid u;
    char *r;
    ofc_tuple_t *found;
    size_t i, bi;
    uint64_t errors = 0;
    ofc_resmap_t *rm = ofc_resmap_init(0);

    puts("insert");
    for (i = 0; i <= TEST_NUM_ELEMS; ++i) {
        uuid_generate(&u);
        ofc_resmap_insert(rm, print_uuid_ro(&u), &u, NULL);
    }
    puts("sorted by resource-id");
    ofc_resmap_print_r(rm);
    puts("sorted by uuid");
    ofc_resmap_print_u(rm);

    puts("find resource-id");
    for (i = 0; i < rm->n_records; ++i) {
        r = rm->records[i].resource_id;
        found = ofc_resmap_find_r(rm, r);
        if (found == NULL) {
            printf("Not found %s\n", r);
            errors++;
        } else {
            if (strcmp(r, print_uuid_ro(&found->uuid))) {
                printf("DNM %s - %s\n", r, print_uuid_ro(&found->uuid));
                errors++;
            }
        }
    }
    if (errors == 0) {
        puts("ok");
    } else {
        printf("%" PRIu64 "\n", errors);
        errors = 0;
    }
    puts("find uuid");
    for (i = 0; i < rm->n_records; ++i) {
        u = rm->records[i].uuid;
        found = ofc_resmap_find_u(rm, &u);
        if (found == NULL) {
            printf("Not found %s\n", print_uuid_ro(&u));
            errors++;
        } else {
            if (!uuid_equals(&u, &found->uuid)) {
                printf("DNM %s - %s\n", print_uuid_ro(&u),
                       print_uuid_ro(&found->uuid));
                errors++;
            }
        }
    }
    if (errors == 0) {
        puts("ok");
    } else {
        printf("%" PRIu64 "\n", errors);
        errors = 0;
    }
    puts("remove resource-id backwards");
    bi = rm->n_records / 2;
    if (bi > 0) {
        for (i = bi; i >= 0; i--) {
            r = strdup(rm->records[0].resource_id);
            if (!ofc_resmap_remove_r(rm, r)) {
                printf("not removed %s\n", r);
                printf("%s\n", rm->records[0].resource_id);
                errors++;
            }
            found = ofc_resmap_find_r(rm, r);
            if (found != NULL) {
                printf("not removed, still found %s\n", r);
                errors++;
            } else {
                puts("removed");
            }
            free(r);
            if (i == 0) {
                break;
            }
        }
    }
    if (errors == 0) {
        puts("ok");
        printf("%" PRIu64 "\n", bi);
    } else {
        printf("%" PRIu64 "\n", errors);
        errors = 0;
    }
    puts("remove resource-id");
    bi = rm->n_records;
    for (i = 0; i < bi; i++) {
        r = strdup(rm->records[0].resource_id);
        if (!ofc_resmap_remove_r(rm, r)) {
            printf("not removed %s\n", r);
            printf("%s\n", rm->records[0].resource_id);
            errors++;
        }
        found = ofc_resmap_find_r(rm, r);
        if (found != NULL) {
            printf("not removed, still found %s\n", r);
            errors++;
        } else {
            puts("removed");
        }
        free(r);
    }
    if (errors == 0) {
        puts("ok");
        printf("%" PRIu64 "\n", bi);
    } else {
        printf("%" PRIu64 "\n", errors);
        errors = 0;
    }
    puts("records left in map");
    printf("%" PRIu64 "\n", rm->n_records);
    ofc_resmap_print(rm);

    ofc_resmap_destroy(&rm);
    return 0;
}
#endif
