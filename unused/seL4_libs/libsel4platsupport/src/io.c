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

#include <autoconf.h>
#include <sel4platsupport/gen_config.h>

#include <sel4platsupport/io.h>
#ifdef CONFIG_ARCH_ARM
#include <platsupport/clock.h>
#include <platsupport/mux.h>
#endif
#include <utils/util.h>
#include <vspace/page.h>

#include <vspace/vspace.h>
#include <vka/capops.h>

#include <stdint.h>
#include <stdlib.h>

typedef struct io_mapping {
    /* address we returned to the user */
    void *returned_addr;
    /* base address of the mapping with respect to the vspace */
    void *mapped_addr;
    size_t num_pages;
    size_t page_size;
    size_t page_size_bits;
    /* caps for the mappings (s) */
    seL4_CPtr *caps;
    /* allocation cookie for allocation(s) */
    seL4_Word *alloc_cookies;
    struct io_mapping *next, *prev;
} io_mapping_t;

typedef struct sel4platsupport_io_mapper_cookie {
    vspace_t *vspace;
    vka_t *vka;
    io_mapping_t *head;
} sel4platsupport_io_mapper_cookie_t;

static void free_node(io_mapping_t *node)
{
    assert(node);
    if (node->caps) {
        free(node->caps);
    }
    if (node->alloc_cookies) {
        free(node->alloc_cookies);
    }
    free(node);
}

static io_mapping_t *new_node(size_t num_pages)
{
    io_mapping_t *ret = calloc(1, sizeof(io_mapping_t));

    if (!ret) {
        return NULL;
    }

    ret->caps = calloc(num_pages, sizeof(seL4_CPtr));
    if (!ret->caps) {
        free_node(ret);
        return NULL;
    }

    ret->alloc_cookies = calloc(num_pages, sizeof(seL4_Word));
    if (!ret->alloc_cookies) {
        free_node(ret);
        return NULL;
    }

    ret->num_pages = num_pages;
    return ret;
}

static void destroy_node(vka_t *vka, io_mapping_t *mapping)
{
    cspacepath_t path;
    for (size_t i = 0; i < mapping->num_pages; i++) {
        /* free the allocation */
        vka_utspace_free(vka, kobject_get_type(KOBJECT_FRAME, mapping->page_size_bits),
                         mapping->page_size_bits, mapping->alloc_cookies[i]);
        /* free the caps */
        vka_cspace_make_path(vka, mapping->caps[i], &path);
        vka_cnode_delete(&path);
        vka_cspace_free(vka, mapping->caps[i]);
    }
    free_node(mapping);
}

static void insert_node(sel4platsupport_io_mapper_cookie_t *io_mapper, io_mapping_t *node)
{
    node->prev = NULL;
    node->next = io_mapper->head;
    if (io_mapper->head) {
        io_mapper->head->prev = node;
    }
    io_mapper->head = node;
}

static io_mapping_t *find_node(sel4platsupport_io_mapper_cookie_t *io_mapper, void *returned_addr)
{
    io_mapping_t *current;
    for (current = io_mapper->head; current; current = current->next) {
        if (current->returned_addr == returned_addr) {
            return current;
        }
    }
    return NULL;
}

static void remove_node(sel4platsupport_io_mapper_cookie_t *io_mapper, io_mapping_t *node)
{
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        assert(io_mapper->head == node);
        io_mapper->head = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    }
}

static void *sel4platsupport_map_paddr_with_page_size(sel4platsupport_io_mapper_cookie_t *io_mapper, uintptr_t paddr,
                                                      size_t size, size_t page_size_bits, bool cached)
{

    vka_t *vka = io_mapper->vka;
    vspace_t *vspace = io_mapper->vspace;

    /* search at start of page */
    int page_size = BIT(page_size_bits);
    uintptr_t start = ROUND_DOWN(paddr, page_size);
    uintptr_t offset = paddr - start;
    size += offset;

    io_mapping_t *mapping = new_node(BYTES_TO_SIZE_BITS_PAGES(size, page_size_bits));
    assert(mapping->num_pages << page_size_bits >= size);
    if (!mapping) {
        ZF_LOGE("Failed to allocate node for %zu pages", mapping->num_pages);
        return NULL;
    }
    mapping->page_size_bits = page_size_bits;

    seL4_Word type = kobject_get_type(KOBJECT_FRAME, mapping->page_size_bits);
    /* allocate all of the physical frame caps */
    for (unsigned int i = 0; i < mapping->num_pages; i++) {
        /* allocate a cslot */
        int error = vka_cspace_alloc(vka, &mapping->caps[i]);
        if (error) {
            ZF_LOGE("cspace alloc failed");
            assert(error == 0);
            /* we don't clean up as everything has gone to hell */
            return NULL;
        }

        /* create a path */
        cspacepath_t path;
        vka_cspace_make_path(vka, mapping->caps[i], &path);

        /* allocate the frame */
        error = vka_utspace_alloc_at(vka, &path, type, page_size_bits, start + (i * page_size),
                                     &mapping->alloc_cookies[i]);
        if (error) {
            /* free this slot, and then do general cleanup of the rest of the slots.
             * this avoids a needless seL4_CNode_Delete of this slot, as there is no
             * cap in it */
            vka_cspace_free(vka, mapping->caps[i]);
            mapping->num_pages = i;
            goto error;
        }
    }

    /* Now map the frames in */
    mapping->mapped_addr = vspace_map_pages(vspace, mapping->caps, mapping->alloc_cookies, seL4_AllRights,
                                            mapping->num_pages,
                                            mapping->page_size_bits, cached);
    if (mapping->mapped_addr != NULL) {
        /* fill out and insert node */
        mapping->returned_addr = mapping->mapped_addr + offset;
        insert_node(io_mapper, mapping);
        return mapping->returned_addr;
    }
error:
    destroy_node(vka, mapping);
    return NULL;
}

static void *sel4platsupport_map_paddr(void *cookie, uintptr_t paddr, size_t size, int cached,
                                       UNUSED ps_mem_flags_t flags)
{
    if (!cookie) {
        ZF_LOGE("cookie is NULL");
        return NULL;
    }

    sel4platsupport_io_mapper_cookie_t *io_mapper = (sel4platsupport_io_mapper_cookie_t *)cookie;
    int frame_size_index = 0;
    /* find the largest reasonable frame size */
    while (frame_size_index + 1 < SEL4_NUM_PAGE_SIZES) {
        if (size >> sel4_page_sizes[frame_size_index + 1] == 0) {
            break;
        }
        frame_size_index++;
    }

    /* try mapping in this and all smaller frame sizes until something works */
    for (int i = frame_size_index; i >= 0; i--) {
        void *result = sel4platsupport_map_paddr_with_page_size(io_mapper, paddr, size, sel4_page_sizes[i], cached);
        if (result) {
            return result;
        }
    }

    /* shit out of luck */
    ZF_LOGE("Failed to find a way to map address %p", (void *)paddr);
    return NULL;
}

static void sel4platsupport_unmap_vaddr(void *cookie, void *vaddr, UNUSED size_t size)
{
    if (!cookie) {
        ZF_LOGE("cookie is NULL");
    }

    sel4platsupport_io_mapper_cookie_t *io_mapper = cookie;

    vspace_t *vspace = io_mapper->vspace;
    vka_t *vka = io_mapper->vka;
    io_mapping_t *mapping = find_node(io_mapper, vaddr);

    if (!mapping) {
        ZF_LOGF("Tried to unmap vaddr %p, which was never mapped in", vaddr);
        return;
    }

    /* unmap the pages */
    vspace_unmap_pages(vspace, mapping->mapped_addr, mapping->num_pages, mapping->page_size_bits,
                       VSPACE_PRESERVE);

    /* clean up the node */
    remove_node(io_mapper, mapping);
    destroy_node(vka, mapping);
}

int sel4platsupport_new_io_mapper(vspace_t *vspace, vka_t *vka, ps_io_mapper_t *io_mapper)
{
    sel4platsupport_io_mapper_cookie_t *cookie = calloc(1, sizeof(sel4platsupport_io_mapper_cookie_t));
    if (!cookie) {
        ZF_LOGE("Failed to allocate %zu bytes", sizeof(sel4platsupport_io_mapper_cookie_t));
        return -1;
    }

    cookie->vspace = vspace;
    cookie->vka = vka;
    io_mapper->cookie = cookie;
    io_mapper->io_map_fn = sel4platsupport_map_paddr;
    io_mapper->io_unmap_fn = sel4platsupport_unmap_vaddr;

    return 0;
}

int sel4platsupport_new_malloc_ops(ps_malloc_ops_t *ops)
{
    ps_new_stdlib_malloc_ops(ops);
    return 0;
}

static char *sel4platsupport_io_fdt_get(void *cookie)
{
    return cookie != NULL ? (char *) cookie : NULL;
}

int sel4platsupport_new_fdt_ops(ps_io_fdt_t *io_fdt, simple_t *simple, ps_malloc_ops_t *malloc_ops)
{
    if (!io_fdt || !simple || !malloc_ops) {
        ZF_LOGE("arguments are NULL");
        return -1;
    }

    ssize_t block_size = simple_get_extended_bootinfo_length(simple, SEL4_BOOTINFO_HEADER_FDT);
    if (block_size > 0) {
        int error = ps_calloc(malloc_ops, 1, block_size, &io_fdt->cookie);
        if (error) {
            ZF_LOGE("Failed to allocate %zu bytes for the FDT", block_size);
            return -1;
        }

        /* Copy the FDT from the extended bootinfo */
        ssize_t copied_size = simple_get_extended_bootinfo(simple, SEL4_BOOTINFO_HEADER_FDT,
                                                           io_fdt->cookie, block_size);
        if (copied_size != block_size) {
            ZF_LOGE("Failed to copy the FDT");
            ZF_LOGF_IF(ps_free(malloc_ops, block_size, io_fdt->cookie),
                       "Failed to clean-up after a failed operation!");
            return -1;
        }

        /* Cut off the bootinfo header from the start of the buffer */
        ssize_t fdt_size = block_size - sizeof(seL4_BootInfoHeader);
        void *fdt_start = io_fdt->cookie + sizeof(seL4_BootInfoHeader);
        memmove(io_fdt->cookie, fdt_start, fdt_size);

        /* Trim off the extra bytes at the end of the FDT */
        void *fdt_end = io_fdt->cookie + fdt_size;
        memset(fdt_end, 0, sizeof(seL4_BootInfoHeader));
    } else {
        /* No FDT is available so just set the cookie to NULL */
        io_fdt->cookie = NULL;
    }

    /* Set the function pointer inside the io_fdt interface */
    io_fdt->get_fn = sel4platsupport_io_fdt_get;

    return 0;
}

int sel4platsupport_new_io_ops(vspace_t *vspace, vka_t *vka, simple_t *simple, ps_io_ops_t *io_ops)
{
    memset(io_ops, 0, sizeof(ps_io_ops_t));

    int error = 0;

    /* Initialise the interfaces which do not require memory allocation/need to be initialised first */
    error = sel4platsupport_new_malloc_ops(&io_ops->malloc_ops);
    if (error) {
        return error;
    }

    /* Now allocate the IO-specific interfaces (the ones that can be found in this file) */
    error = sel4platsupport_new_io_mapper(vspace, vka, &io_ops->io_mapper);
    if (error) {
        return error;
    }

    error = sel4platsupport_new_fdt_ops(&io_ops->io_fdt, simple, &io_ops->malloc_ops);
    if (error) {
        free(io_ops->io_mapper.cookie);
        io_ops->io_mapper.cookie = NULL;
        return error;
    }

    error = sel4platsupport_new_irq_ops(&io_ops->irq_ops, vka, simple, DEFAULT_IRQ_INTERFACE_CONFIG,
                                        &io_ops->malloc_ops);
    if (error) {
        free(io_ops->io_mapper.cookie);
        io_ops->io_mapper.cookie = NULL;
        ssize_t fdt_size = simple_get_extended_bootinfo_length(simple, SEL4_BOOTINFO_HEADER_FDT);
        if (fdt_size > 0) {
            /* The FDT is available on this platform and we actually copied it, so we free it */
            ps_free(&io_ops->malloc_ops, fdt_size, &io_ops->io_fdt.cookie);
        }
        return error;
    }

    return 0;
}
