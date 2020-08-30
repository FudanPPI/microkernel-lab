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
#include <utils/util.h>
#include <sel4/sel4.h>

#include <vka/vka.h>
#include <vka/object.h>

/* Map a page to a virtual address, allocating a page table if necessary.
*
*
* @param vka a vka compliant allocator
* @param pd page directory to map the page into
* @param page capability to the page to map in
* @param vaddr unmapped virtual address to map the page into
* @param rights permissions to map the page with
* @param cacheable 1 if the page should be cached (0 if it is for DMA)
* @param objects array of vka_object_t structure to be populated with paging structures
*                info any one are allocated
* @param num_objects Pointer to both the size of the objects array, and the number of
*                    objects that get allocated
*
* @return error sel4 error code or -1 if allocation failed.
*/
int sel4utils_map_page(vka_t *vka, seL4_CPtr pd, seL4_CPtr frame, void *vaddr,
                       seL4_CapRights_t rights, int cacheable, vka_object_t *objects, int *num_objects);

/** convenient wrapper this if you don't want to track allocated page tables */
static inline int sel4utils_map_page_leaky(vka_t *vka, seL4_CPtr pd, seL4_CPtr frame, void *vaddr,
                                           seL4_CapRights_t rights, int cacheable)
{
    vka_object_t objects[3];
    int num = 3;
    return sel4utils_map_page(vka, pd, frame, vaddr, rights, cacheable, objects, &num);
}

#include <vspace/vspace.h>

/* Duplicate a page cap and map it into a vspace
 *
 * @param vka Allocator for resources
 * @param vspace vspace to map into
 * @param page cptr to duplicate and map
 * @param size_bits size of the page to map
 *
 * @return virtual address of mapping
 */
void *sel4utils_dup_and_map(vka_t *vka, vspace_t *vspace, seL4_CPtr page, size_t size_bits);

/* Unmap a duplicated page cap and free any resources. Is the opposite
 * of sel4utils_dup_and_map
 *
 * @param vka Allocator used to allocated resources
 * @param vspace vspace that frame was mapped into
 * @param mapping virtual address of mapping to remove
 * @param size_bits size of the page to unmap
 *
 * @return none
 */
void sel4utils_unmap_dup(vka_t *vka, vspace_t *vspace, void *mapping, size_t size_bits);

#if defined(CONFIG_IOMMU) || defined(CONFIG_ARM_SMMU)
int sel4utils_map_iospace_page(vka_t *vka, seL4_CPtr iospace, seL4_CPtr frame, seL4_Word vaddr,
                               seL4_CapRights_t rights, int cacheable, seL4_Word size_bits,
                               vka_object_t *pts, int *num_pts);
#endif /* defined(CONFIG_IOMMU) || defined(CONFIG_ARM_SMMU) */

#ifdef CONFIG_VTX
int sel4utils_map_ept_page(vka_t *vka, seL4_CPtr pd, seL4_CPtr frame, seL4_Word vaddr,
                           seL4_CapRights_t rights, int cacheable, seL4_Word size_bits, vka_object_t *pagetable, vka_object_t *pagedir,
                           vka_object_t *pdpt);

#endif /* CONFIG_VTX */

