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
#include <sel4utils/gen_config.h>

#ifdef CONFIG_PAE_PAGING
#define VSPACE_MAP_PAGING_OBJECTS 3
#else
#define VSPACE_MAP_PAGING_OBJECTS 2
#endif

#define VSPACE_LEVEL_BITS 10
#define VSPACE_NUM_LEVELS 2

