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

#include "plat_internal.h"
#include <sel4/sel4.h>
#include <platsupport/chardev.h>
#include <sel4platsupport/gen_config.h>
#include <stddef.h>
#include <vka/object.h>

static ssize_t debug_write(ps_chardevice_t *device UNUSED, const void *vdata, size_t count,
                           chardev_callback_t cb UNUSED, void *token UNUSED)
{
#ifdef CONFIG_PRINTING
    size_t sent = 0;
    const char *data = (const char *)vdata;
    for (sent = 0; sent < count; sent++) {
        seL4_DebugPutChar(*data++);
    }
#else
    /* /dev/null */
    (void)vdata;
#endif
    return count;
}

static struct ps_chardevice console_device = {
    .write = &debug_write
};
static struct ps_chardevice *console = &console_device;

void register_console(struct ps_chardevice *user_console)
{
    console = user_console;
}

int __plat_serial_init(ps_io_ops_t *io_ops)
{
    struct ps_chardevice temp_device;
    if (ps_cdev_init(PS_SERIAL_DEFAULT, io_ops, &temp_device)) {
        /* Apply the changes */
#if defined(CONFIG_LIB_SEL4_PLAT_SUPPORT_USE_SEL4_DEBUG_PUTCHAR)
        temp_device.write = &debug_write;
#endif
        console_device = temp_device;
        console = &console_device;
        return 0;
    } else {
        return -1;
    }
}

void __plat_putchar(int c)
{
    if (console) {
        ps_cdev_putchar(console, c);
    }
}

int __plat_getchar(void)
{
    if (console) {
        return ps_cdev_getchar(console);
    } else {
        return EOF;
    }
}
