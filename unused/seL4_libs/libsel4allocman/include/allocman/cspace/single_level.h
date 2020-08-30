/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

#pragma once

#include <autoconf.h>
#include <stdlib.h>
#include <sel4/types.h>
#include <allocman/cspace/cspace.h>

struct cspace_single_level_config {
    /* A cptr to the cnode that we are managing slots in */
    seL4_CPtr cnode;
    /* Size in bits of the cnode */
    size_t cnode_size_bits;
    /* Guard depth added to this cspace. */
    size_t cnode_guard_bits;
    /* First valid slot (as an index) */
    size_t first_slot;
    /* Last valid slot + 1 (as an index) */
    size_t end_slot;
};

typedef struct cspace_single_level {
    struct cspace_single_level_config config;
    size_t *bitmap;
    size_t bitmap_length;
    size_t last_entry;
} cspace_single_level_t;

int cspace_single_level_create(struct allocman *alloc, cspace_single_level_t *cspace, struct cspace_single_level_config config);

/* Frees any allocated resources back to the given allocation manager */
void cspace_single_level_destroy(struct allocman *alloc, cspace_single_level_t *cspace);

int _cspace_single_level_alloc(struct allocman *alloc, void *_cspace, cspacepath_t *slot);
int _cspace_single_level_alloc_at(struct allocman *alloc, void *_cspace, seL4_CPtr slot);
void _cspace_single_level_free(struct allocman *alloc, void *_cspace, const cspacepath_t *slot);

static inline cspacepath_t _cspace_single_level_make_path(void *_cspace, seL4_CPtr slot)
{
    cspace_single_level_t *cspace = (cspace_single_level_t*) _cspace;
    return (cspacepath_t) {
        .root = cspace->config.cnode,
        .capPtr = slot,
        .capDepth = cspace->config.cnode_size_bits + cspace->config.cnode_guard_bits,
        .dest = 0,
        .destDepth = 0,
        .offset = slot,
        .window = 1
    };
}

static inline cspace_interface_t cspace_single_level_make_interface(cspace_single_level_t *cspace) {
    return (cspace_interface_t) {
        .alloc = _cspace_single_level_alloc,
        .free = _cspace_single_level_free,
        .make_path = _cspace_single_level_make_path,
        /* We do not want to handle recursion, as it shouldn't happen */
        .properties = ALLOCMAN_DEFAULT_PROPERTIES,
        .cspace = cspace
    };
}

