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

/**
 * @brief USB HID Mouse driver
 * @see USB HID spec Appendix B
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <usb/drivers/mouse.h>
#include "../services.h"
#include "hid.h"

struct usb_mouse_device {
	struct usb_dev *udev;
	struct usb_hid_device *hid;
	struct endpoint *ep_int;
	struct xact int_xact;
	struct mouse_event event;
};

static ssize_t mouse_read(ps_chardevice_t* d, void* vdata, size_t bytes,
		chardev_callback_t cb, void* token)
{
	int size;
	struct usb_mouse_device *mouse;
	mouse = (struct usb_mouse_device*)d->vaddr;

	size = sizeof(struct mouse_event);
	if (size > bytes) {
		return 0;
	}

	memcpy((uint8_t*)vdata, &mouse->event, size);

	return size;
}

static int mouse_irq_handler(void* token, enum usb_xact_status stat, int bytes_remaining)
{
	struct usb_mouse_device *mouse;
	uint8_t *data;

	mouse = (struct usb_mouse_device*)token;
	data = xact_get_vaddr(&mouse->int_xact);

	if (stat != XACTSTAT_SUCCESS) {
		ZF_LOGD("Received unsuccessful IRQ\n");
	}

	mouse->event.button = data[0];
	mouse->event.x = data[1];
	mouse->event.y = data[2];

	usbdev_schedule_xact(mouse->udev, mouse->ep_int, &mouse->int_xact, 1,
			&mouse_irq_handler, mouse);

	return 0;
}

int usb_mouse_driver_bind(struct usb_dev *usb_dev, struct ps_chardevice *cdev)
{
	struct usb_mouse_device *mouse;
	int err;

	mouse = (struct usb_mouse_device*)usb_malloc(sizeof(struct usb_mouse_device));
	if (!mouse) {
		ZF_LOGF("Out of memory\n");
	}

	usb_dev->dev_data = (struct udev_priv*)mouse;
	mouse->udev = usb_dev;

	mouse->hid = usb_hid_alloc(usb_dev);

	if (mouse->hid->protocol != 2) {
		ZF_LOGF("Not a mouse: %d\n", mouse->hid->protocol);
	}

	cdev->vaddr = mouse;
	cdev->read = mouse_read;

	/* Find endpoint */
	mouse->ep_int = usb_dev->ep[mouse->hid->iface];
	if (mouse->ep_int == NULL || mouse->ep_int->type != EP_INTERRUPT) {
		ZF_LOGF("Endpoint not found\n");
	}

	ZF_LOGD("Configuring mouse\n");

	err = usb_hid_set_idle(mouse->hid, 0);
	if (err < 0) {
		ZF_LOGF("Mouse initialisation error\n");
	}

	/* Initialise IRQs */
	if (mouse->ep_int->dir == EP_DIR_IN) {
		mouse->int_xact.type = PID_IN;
	} else {
		mouse->int_xact.type = PID_OUT;
	}

	mouse->int_xact.len = mouse->ep_int->max_pkt;

	err = usb_alloc_xact(usb_dev->dman, &mouse->int_xact, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
	}

	usbdev_schedule_xact(usb_dev, mouse->ep_int, &mouse->int_xact, 1,
			&mouse_irq_handler, mouse);

	ZF_LOGD("Successfully initialised\n");

	return 0;
}

