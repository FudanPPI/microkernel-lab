/*
 * Copyright 2018, Data61
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

#if CONFIG_PT_LEVELS == 2
#define VSPACE_LEVEL_BITS 10
#elif CONFIG_PT_LEVELS == 3
#define VSPACE_LEVEL_BITS 9
#elif CONFIG_PT_LEVELS == 4
#define VSPACE_LEVEL_BITS 9
#else
#error "Unsupported PT level"
#endif

#define VSPACE_MAP_PAGING_OBJECTS 5

#define VSPACE_NUM_LEVELS CONFIG_PT_LEVELS

