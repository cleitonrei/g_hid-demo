/*
 * hid.c -- HID Composite driver
 *
 * Based on multi.c
 *
 * Copyright (C) 2010 Fabien Chouteau <fabien.chouteau@barco.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/usb/composite.h>

#include "gadget_chips.h"
#define DRIVER_DESC		"HID Gadget"
#define DRIVER_VERSION		"2010/03/16"

/*-------------------------------------------------------------------------*/

#define HIDG_VENDOR_NUM		0x0525	/* XXX NetChip */
#define HIDG_PRODUCT_NUM	0xa4ac	/* Linux-USB HID gadget */

/*-------------------------------------------------------------------------*/

/*
 * kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */
#include "f_hid.c"


struct hidg_func_node {
	struct list_head node;
	struct hidg_func_descriptor *func;
};

static LIST_HEAD(hidg_func_list);

/*-------------------------------------------------------------------------*/
USB_GADGET_COMPOSITE_OPTIONS();

static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	.bcdUSB =		cpu_to_le16(0x0200),

	/* .bDeviceClass =		USB_CLASS_COMM, */
	/* .bDeviceSubClass =	0, */
	/* .bDeviceProtocol =	0, */
	.bDeviceClass =		USB_CLASS_PER_INTERFACE,
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	/* .bMaxPacketSize0 = f(hardware) */

	/* Vendor and product id can be overridden by module parameters.  */
	.idVendor =		cpu_to_le16(HIDG_VENDOR_NUM),
	.idProduct =		cpu_to_le16(HIDG_PRODUCT_NUM),
	/* .bcdDevice = f(hardware) */
	/* .iManufacturer = DYNAMIC */
	/* .iProduct = DYNAMIC */
	/* NO SERIAL NUMBER */
	.bNumConfigurations =	1,
};

static struct usb_otg_descriptor otg_descriptor = {
	.bLength =		sizeof otg_descriptor,
	.bDescriptorType =	USB_DT_OTG,

	/* REVISIT SRP-only hardware is possible, although
	 * it would not be called "OTG" ...
	 */
	.bmAttributes =		USB_OTG_SRP | USB_OTG_HNP,
};

static const struct usb_descriptor_header *otg_desc[] = {
	(struct usb_descriptor_header *) &otg_descriptor,
	NULL,
};


/* string IDs are assigned dynamically */
static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "",
	[USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
	[USB_GADGET_SERIAL_IDX].s = "",
	{  } /* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};



/****************************** Configurations ******************************/

static int __init do_config(struct usb_configuration *c)
{
	struct hidg_func_node *e;
	int func = 0, status = 0;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	list_for_each_entry(e, &hidg_func_list, node) {
		status = hidg_bind_config(c, e->func, func++);
		if (status)
			break;
	}

	return status;
}

static struct usb_configuration config_driver = {
	.label			= "HID Gadget",
	.bConfigurationValue	= 1,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
};

/****************************** Gadget Bind ******************************/

static int __init hid_bind(struct usb_composite_dev *cdev)
{
	struct usb_gadget *gadget = cdev->gadget;
	struct list_head *tmp;
	int status, funcs = 0;

	list_for_each(tmp, &hidg_func_list)
		funcs++;

	if (!funcs)
		return -ENODEV;

	/* set up HID */
	status = ghid_setup(cdev->gadget, funcs);
	if (status < 0)
		return status;

	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */

	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		return status;
	device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;

	/* register our configuration */
	status = usb_add_config(cdev, &config_driver, do_config);
	if (status < 0)
		return status;

	usb_composite_overwrite_options(cdev, &coverwrite);
	dev_info(&gadget->dev, DRIVER_DESC ", version: " DRIVER_VERSION "\n");

	return 0;
}

static int __exit hid_unbind(struct usb_composite_dev *cdev)
{
	ghid_cleanup();
	return 0;
}

static int __init hidg_plat_driver_probe(struct platform_device *pdev)
{
	struct hidg_func_descriptor *func = dev_get_platdata(&pdev->dev);
	struct hidg_func_node *entry;

	if (!func) {
		dev_err(&pdev->dev, "Platform data missing\n");
		return -ENODEV;
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->func = func;
	list_add_tail(&entry->node, &hidg_func_list);

	return 0;
}

static int hidg_plat_driver_remove(struct platform_device *pdev)
{
	struct hidg_func_node *e, *n;

	list_for_each_entry_safe(e, n, &hidg_func_list, node) {
		list_del(&e->node);
		kfree(e);
	}

	return 0;
}


/****************************** Some noise ******************************/


static __refdata struct usb_composite_driver hidg_driver = {
	.name		= "g_hid",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.max_speed	= USB_SPEED_HIGH,
	.bind		= hid_bind,
	.unbind		= __exit_p(hid_unbind),
};

static struct platform_driver hidg_plat_driver = {
	.remove		= hidg_plat_driver_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "hidg",
	},
};

/* This is a quick and dirty hack to demonstrate the gadget hid driver.
   Normally the platform_device descriptor would be part of the board
   support for the device, e.g. in arch/arm/mach-omap2/devices.c,
   but for convenience I am putting it in to the g_hid module to make
   easier for testing.

   The structures my_hid_data and my_hid are straight from 
   Documentation/usb/gadget_hid.txt   
 */

/* hid descriptor for a keyboard */
static struct hidg_func_descriptor my_hid_data = {
        .subclass               = 0, /* No subclass */
        .protocol               = 1, /* Keyboard */
        .report_length          = 8,
        .report_desc_length     = 63,
        .report_desc            = {
                0x05, 0x01,     /* USAGE_PAGE (Generic Desktop)           */
                0x09, 0x06,     /* USAGE (Keyboard)                       */
                0xa1, 0x01,     /* COLLECTION (Application)               */
                0x05, 0x07,     /*   USAGE_PAGE (Keyboard)                */
                0x19, 0xe0,     /*   USAGE_MINIMUM (Keyboard LeftControl) */
                0x29, 0xe7,     /*   USAGE_MAXIMUM (Keyboard Right GUI)   */
                0x15, 0x00,     /*   LOGICAL_MINIMUM (0)                  */
                0x25, 0x01,     /*   LOGICAL_MAXIMUM (1)                  */
                0x75, 0x01,     /*   REPORT_SIZE (1)                      */
                0x95, 0x08,     /*   REPORT_COUNT (8)                     */
                0x81, 0x02,     /*   INPUT (Data,Var,Abs)                 */
                0x95, 0x01,     /*   REPORT_COUNT (1)                     */
                0x75, 0x08,     /*   REPORT_SIZE (8)                      */
                0x81, 0x03,     /*   INPUT (Cnst,Var,Abs)                 */
                0x95, 0x05,     /*   REPORT_COUNT (5)                     */
                0x75, 0x01,     /*   REPORT_SIZE (1)                      */
                0x05, 0x08,     /*   USAGE_PAGE (LEDs)                    */
                0x19, 0x01,     /*   USAGE_MINIMUM (Num Lock)             */
                0x29, 0x05,     /*   USAGE_MAXIMUM (Kana)                 */
                0x91, 0x02,     /*   OUTPUT (Data,Var,Abs)                */
                0x95, 0x01,     /*   REPORT_COUNT (1)                     */
                0x75, 0x03,     /*   REPORT_SIZE (3)                      */
                0x91, 0x03,     /*   OUTPUT (Cnst,Var,Abs)                */
                0x95, 0x06,     /*   REPORT_COUNT (6)                     */
                0x75, 0x08,     /*   REPORT_SIZE (8)                      */
                0x15, 0x00,     /*   LOGICAL_MINIMUM (0)                  */
                0x25, 0x65,     /*   LOGICAL_MAXIMUM (101)                */
                0x05, 0x07,     /*   USAGE_PAGE (Keyboard)                */
                0x19, 0x00,     /*   USAGE_MINIMUM (Reserved)             */
                0x29, 0x65,     /*   USAGE_MAXIMUM (Keyboard Application) */
                0x81, 0x00,     /*   INPUT (Data,Ary,Abs)                 */
                0xc0            /* END_COLLECTION                         */
        }
};

static struct platform_device my_hid = {
        .name                   = "hidg",
        .id                     = 0,
        .num_resources          = 0,
        .resource               = 0,
        .dev.platform_data      = &my_hid_data,
};

int __init init_my_hid(void)
{
	pr_info("g_hid: loading keyboard HID descriptor\n");
	return platform_device_register(&my_hid);
}

/* End hack */

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Fabien Chouteau, Peter Korsgaard");
MODULE_LICENSE("GPL");

static int __init hidg_init(void)
{
	int status;

	init_my_hid();

	status = platform_driver_probe(&hidg_plat_driver,
				hidg_plat_driver_probe);
	if (status < 0) {
		pr_info("platform_driver_probe failed: %d\n", status);
		platform_device_unregister(&my_hid);
		return status;
	}

	status = usb_composite_probe(&hidg_driver);
	if (status < 0) {
		pr_info("usb_composite_probe failed: %d\n", status);
		platform_driver_unregister(&hidg_plat_driver);
		platform_device_unregister(&my_hid);
	}

	return status;
}
module_init(hidg_init);

static void __exit hidg_cleanup(void)
{
	platform_driver_unregister(&hidg_plat_driver);
	usb_composite_unregister(&hidg_driver);
}
module_exit(hidg_cleanup);
