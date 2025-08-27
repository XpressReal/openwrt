// SPDX-License-Identifier: GPL-2.0
/*
 * USB port test
 *
 * Copyright (C) 2023 Realtek Semiconductor Corporation
 */

/* #define DEBUG */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/of.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/usb/ch11.h>
#include <linux/usb/hcd.h>

#include "hub.h"

struct usbport_data {
	struct class *class;

	struct list_head ports;
	struct notifier_block nb;

	struct mutex lock;
};

struct usbport_port {
	struct device dev;

	struct usbport_data *data;
	struct usb_device *hub;
	int portnum;
	char *port_name;
	bool runTestMode;
	struct list_head list;

	struct usb_device *attached_usb;
};

static struct usbport_data *g_usbport_data;

/* Note that hdev or one of its children must be locked! */
static struct usb_hub *hdev_to_struct_usb_hub(struct usb_device *hdev)
{
	if (!hdev || !hdev->actconfig || !hdev->maxchild)
		return NULL;
	return usb_get_intfdata(hdev->actconfig->interface[0]);
}

static int get_hub_descriptor_port(struct usb_device *hdev, void *data,
	    int size, int port1)
{
	struct usb_hub *hub = hdev_to_struct_usb_hub(hdev);
	struct usb_device *dev = hub->ports[port1 - 1]->child;

	if (dev) {
		memset(data, 0, size);

		return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
				USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
				(USB_DT_DEVICE << 8), 0, data, size,
				USB_CTRL_GET_TIMEOUT);
	}

	return 0;
}

/*
 * USB 2.0 spec Section 11.24.2.2
 */
static int hub_clear_port_feature(struct usb_device *hdev, int port1, int feature)
{
	return usb_control_msg(hdev, usb_sndctrlpipe(hdev, 0),
			USB_REQ_CLEAR_FEATURE, USB_RT_PORT, feature, port1,
			NULL, 0, 1000);
}

/*
 * USB 2.0 spec Section 11.24.2.13
 */
static int hub_set_port_feature(struct usb_device *hdev, int port1, int feature)
{
	return usb_control_msg(hdev, usb_sndctrlpipe(hdev, 0),
			USB_REQ_SET_FEATURE, USB_RT_PORT, feature, port1,
			NULL, 0, 1000);
}

/* use a short timeout for hub/port status fetches */
#define	USB_STS_TIMEOUT		1000
#define	USB_STS_RETRIES		5

static int get_port_status(struct usb_device *hdev, int port1,
		struct usb_port_status *data)
{
	int i, status = -ETIMEDOUT;

	for (i = 0; i < USB_STS_RETRIES &&
			(status == -ETIMEDOUT || status == -EPIPE); i++) {
		dev_info(&hdev->dev, "get_port_status at port %d ...\n", port1);
		status = usb_control_msg(hdev, usb_rcvctrlpipe(hdev, 0),
			USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_PORT, 0, port1,
			data, sizeof(*data), USB_STS_TIMEOUT);
	}
	return status;
}

enum {
	TEST_RESET = 0,
	TEST_TEST_J,
	TEST_TEST_K,
	TEST_TEST_SE0_NAK,
	TEST_TEST_PACKET,
	TEST_TEST_FORCE_ENABLE,
	TEST_SUSPEND_RESUME,
	TEST_SINGLE_STEP_GET_DEVICE_DESCRIPTOR,
	TEST_PORT_RESET,
	MAX_CTS_TEST_CASE,
};

/***************************************
 * Device attr
 ***************************************/

static ssize_t show_runTestMode(struct device *dev,
	    struct device_attribute *attr, char *buf)
{
	struct usbport_port *port = container_of(dev,
		     struct usbport_port, dev);
	struct usb_device *udev;

	udev = port->hub;

	if (!port->runTestMode) {
		dev_err(dev, "usb device %s not support runTestMode\n",
			    dev_name(&udev->dev));
		return sprintf (buf, "This node is not support runTestMode\n");
	}

	if (udev->descriptor.bDeviceClass != USB_CLASS_HUB)
		return sprintf (buf, "This node is not a HUB\n");

	return sprintf (buf, "%s to runTestMode\n"
		    "echo \"1 %d\" to run port1 HC_RESET command\n"
		    "echo \"1 %d\" to run port1 TEST_J command\n"
		    "echo \"1 %d\" ro run port1 TEST_K command\n"
		    "echo \"1 %d\" to run port1 TEST_SE0_NAK command\n"
		    "echo \"1 %d\" to run port1 TEST_PACKET command\n"
		    "echo \"1 %d\" to run port1 TEST_FORCE_ENABLE command\n"
		    "echo \"1 %d\" to run port1 SUSPEND/RESUME command\n"
		    "echo \"1 %d\" to run port1 "
		    "SINGLE_STEP_GET_DEVICE_DESCRIPTOR command\n"
		    "echo \"1 %d\" to run port1 PORT_RESET command\n",
		    dev_name(&udev->dev),
		    TEST_RESET, TEST_TEST_J, TEST_TEST_K, TEST_TEST_SE0_NAK,
		    TEST_TEST_PACKET,
		    TEST_TEST_FORCE_ENABLE, TEST_SUSPEND_RESUME,
		    TEST_SINGLE_STEP_GET_DEVICE_DESCRIPTOR,
		    TEST_PORT_RESET);

	return 0;
}

static ssize_t
set_runTestMode (struct device *dev, struct device_attribute *attr,
	    const char *buf, size_t count)
{
	struct usbport_port *port = container_of(dev,
		     struct usbport_port, dev);
	struct usb_device *udev;
	int value, test_case;
	unsigned int port1 = 1;
	struct usb_port_status port_status;

	udev = port->hub;

	if ((value = sscanf (buf, "%u", &port1)) != 1)
		return -EINVAL;

	buf += 2;

	if ((value = sscanf (buf, "%u", &test_case)) != 1)
		return -EINVAL;

	if (!port->runTestMode) {
		dev_err(dev, "usb device %s not support runTestMode\n",
			    dev_name(&udev->dev));
		return value;
	}

	if (udev->descriptor.bDeviceClass != USB_CLASS_HUB)
		return value;

	switch (test_case) {
	case TEST_RESET:
		dev_info(dev, "run HC_RESET (%d) to port %d ...\n", test_case, port1);
		if (udev->bus != NULL) {
			struct usb_hcd *hcd = bus_to_hcd(udev->bus);
			int ret = 0;
			if (hcd != NULL && hcd->driver != NULL && hcd->driver->reset)
				ret = hcd->driver->reset(hcd);
			if (ret)
				dev_err(dev, "run HC_RESET fail ...\n");
		}

	break;
	case TEST_TEST_J:
	case TEST_TEST_K:
	case TEST_TEST_SE0_NAK:
	case TEST_TEST_PACKET:
	case TEST_TEST_FORCE_ENABLE:
		usb_disable_autosuspend(udev);
		dev_info(dev, "run USB_PORT_FEAT_TEST mode %d to port %d ...\n",
			    test_case, port1);
		hub_set_port_feature(udev,(test_case << 8) | port1, USB_PORT_FEAT_TEST);

	break;
	case TEST_SUSPEND_RESUME:
		dev_info(dev, "run TEST_SUSPEND_RESUME to the port %d of the hub ...\n",
			    port1);
		msleep(15000);
		dev_info(dev, "set USB_PORT_FEAT_SUSPEND to the port %d of the hub ...\n",
			    port1);
		hub_set_port_feature(udev, port1, USB_PORT_FEAT_SUSPEND);
		dev_info(dev, "set OK !!!\n");
		msleep(15000);
		dev_info(dev, "clear USB_PORT_FEAT_SUSPEND to the port %d of the hub ...\n",
			    port1);
		hub_clear_port_feature(udev, port1, USB_PORT_FEAT_SUSPEND);
		dev_info(dev, "clear OK !!!\n");

		dev_info(dev, "get_port_status port %d of the hub ...\n", port1);
		msleep(USB_RESUME_TIMEOUT);
		get_port_status(udev, port1, &port_status);
	break;
	case TEST_SINGLE_STEP_GET_DEVICE_DESCRIPTOR:
	{
		int i, size = 0x12;
		unsigned char		*data;

		dev_info(dev, "run SINGLE_STEP_GET_DEVICE_DESCRIPTOR to the port %d of the hub ...\n",
			    port1);
		data = (unsigned char*)kmalloc(size, GFP_KERNEL);
		if (!data)
			return -ENOMEM;
		memset (data, 0, size);
		get_hub_descriptor_port(udev, data, size, port1);

		dev_info(dev, "get device descriptor\n");
		for( i = 0; i < size; i++)
		{
			pr_info(" %.2x", data[i]);
			if((i % 15) == 0 && (i != 0))
				printk("\n<1>");
		}
		pr_info("\n");

		kfree(data);
	}
	break;
	case TEST_PORT_RESET:
		dev_info(dev, "run PORT_RESET (%d) to port %d ...\n",
			    test_case, port1);
		hub_clear_port_feature(udev, port1, USB_PORT_FEAT_POWER);
		msleep(1000);
		hub_set_port_feature(udev, port1, USB_PORT_FEAT_POWER);

	break;
	default:
		dev_err(dev, "error test_case %d !!!\n", test_case);
	break;
	}

	return (value < 0) ? value : count;
}
static DEVICE_ATTR(runTestMode, S_IRUGO | S_IWUSR,
		show_runTestMode, set_runTestMode);

static ssize_t attached_usb_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct usbport_port *port = container_of(dev,
		     struct usbport_port, dev);

	if (port->attached_usb) {
		struct usb_device *usb = port->attached_usb;

		return sprintf(buf, "%s\n", dev_name(&usb->dev)) + 1;
	}
	return sprintf(buf, "no attached usb\n") + 1;
}

static DEVICE_ATTR_RO(attached_usb);

static struct attribute *ports_attrs[] = {
	&dev_attr_runTestMode.attr,
	&dev_attr_attached_usb.attr,
	NULL,
};

static const struct attribute_group ports_group = {
	.attrs = ports_attrs,
};

static const struct attribute_group *ports_groups[] = {
	&ports_group,
	NULL,
};

static void ports_release(struct device *dev)
{
	/* nothing */
}

static const struct device_type port_dev_type = {
	.name = "usbports",
	.groups = ports_groups,
	.release = ports_release,
};

/***************************************
 * Adding & removing ports
 ***************************************/

/**
 * usbport_port_run_test_mode_check - Check if port can runTestMode
 */
static bool usbport_port_run_test_mode_check(
	    struct usbport_data *usbport_data,
	    struct usb_device *usb_dev, int port)
{
	if (!usb_dev->parent) {
		pr_info("%s: %s is roothub, set default to test\n",
			    __func__, dev_name(&usb_dev->dev));
		return true;
	}

	return false;
}

static int usbport_add_port(struct usbport_data *usbport_data,
				 struct usb_device *usb_dev,
				 const char *hub_name, int portnum)
{
	struct usbport_port *port;
	struct device *port_dev;
	size_t len;
	int err;

	dev_dbg(&usb_dev->dev, "%s: add port for hub %s portnum=%d\n",
		    __func__, hub_name, portnum);

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port) {
		err = -ENOMEM;
		goto err_out;
	}

	port->data = usbport_data;
	port->hub = usb_dev;
	port->portnum = portnum;
	port->runTestMode = usbport_port_run_test_mode_check(usbport_data,
		    usb_dev, portnum);

	len = strlen(hub_name) + 8;
	port->port_name = kzalloc(len, GFP_KERNEL);
	if (!port->port_name) {
		err = -ENOMEM;
		goto err_free_port;
	}
	snprintf(port->port_name, len, "%s-port%d", hub_name, portnum);

	port_dev = &port->dev;
	port_dev->class = usbport_data->class;
	port_dev->type = &port_dev_type;
	dev_set_name(port_dev, "%s", port->port_name);
	err = device_register(port_dev);
	if (err) {
		put_device(&port->dev);
		goto err_free_port_name;
	}

	list_add_tail(&port->list, &usbport_data->ports);

	return 0;

err_free_port_name:
	kfree(port->port_name);
err_free_port:
	kfree(port);
err_out:
	return err;
}

static int usbport_add_usb_dev_ports(struct usb_device *usb_dev,
					  void *data)
{
	struct usbport_data *usbport_data = data;
	int i;

	dev_dbg(&usb_dev->dev, "%s: check %s if it is hub\n",
		    __func__, dev_name(&usb_dev->dev));

	for (i = 1; i <= usb_dev->maxchild; i++)
		usbport_add_port(usbport_data, usb_dev,
				      dev_name(&usb_dev->dev), i);

	return 0;
}

static void usbport_remove_port(struct usbport_data *usbport_data,
				     struct usbport_port *port)
{
	struct device *port_dev;

	if (!port) {
		pr_err("%s: ERROR: remove port is NULL\n", __func__);
		return;
	}

	dev_dbg(&port->dev, "%s: remove port %s\n", __func__, port->port_name);
	port_dev = &port->dev;
	list_del(&port->list);
	device_unregister(port_dev);
	kfree(port->port_name);
	kfree(port);
}

static void usbport_remove_usb_dev_ports(struct usbport_data *usbport_data,
					      struct usb_device *usb_dev)
{
	struct usbport_port *port, *tmp;

	dev_dbg(&usb_dev->dev, "%s: remove usb device %s\n",
		    __func__, dev_name(&usb_dev->dev));
	list_for_each_entry_safe(port, tmp, &usbport_data->ports, list) {
		if (port->hub == usb_dev)
			usbport_remove_port(usbport_data, port);
	}
}

static void usbport_attach_usb_dev_to_ports(struct usb_device *usb_dev,
					  void *data)
{
	struct usbport_data *usbport_data = data;
	struct usbport_port *port;

	if (!usb_dev->parent) {
		dev_info(&usb_dev->dev, "%s: usb_dev=%s no parent!\n",
			     __func__, dev_name(&usb_dev->dev));
		return;
	}

	list_for_each_entry(port, &usbport_data->ports, list) {
		if (usb_dev->parent == port->hub &&
		    usb_dev->portnum == port->portnum) {
			dev_dbg(&usb_dev->dev, "%s: %s attach to %s\n",
				    __func__,
				    dev_name(&usb_dev->dev), port->port_name);
			port->attached_usb = usb_dev;
			return;
		}
	}

	dev_dbg(&usb_dev->dev, "%s: no port to attach %s\n",
		    __func__, dev_name(&usb_dev->dev));
	return;
}

static void usbport_detach_usb_dev_from_ports(struct usb_device *usb_dev,
					  void *data)
{
	struct usbport_data *usbport_data = data;
	struct usbport_port *port;

	if (!usb_dev->parent) {
		dev_dbg(&usb_dev->dev, "%s: usb_dev=%s no parent!\n",
			     __func__, dev_name(&usb_dev->dev));
		return;
	}

	list_for_each_entry(port, &usbport_data->ports, list) {
		if (usb_dev->parent == port->hub &&
		    usb_dev->portnum == port->portnum) {
			dev_dbg(&usb_dev->dev, "%s: %s detach from %s\n",
				    __func__,
				    dev_name(&usb_dev->dev), port->port_name);
			port->attached_usb = NULL;
			return;
		}
	}

	dev_dbg(&usb_dev->dev, "%s: no port to detach %s\n",
		    __func__, dev_name(&usb_dev->dev));
	return;
}

static int usbport_scan_usb_dev_ports(struct usb_device *usb_dev,
					  void *data)
{
	struct usbport_data *usbport_data = data;

	dev_dbg(&usb_dev->dev, "%s: start scan %s\n",
		    __func__, dev_name(&usb_dev->dev));

	mutex_lock(&usbport_data->lock);
	usbport_add_usb_dev_ports(usb_dev, data);
	usbport_attach_usb_dev_to_ports(usb_dev, data);
	mutex_unlock(&usbport_data->lock);

	return 0;
}

/***************************************
 * Init, exit, etc.
 ***************************************/

static int usbport_notify(struct notifier_block *nb, unsigned long action,
			       void *data)
{
	struct usbport_data *usbport_data =
		container_of(nb, struct usbport_data, nb);
	struct usb_device *usb_dev = data;

	if (!usb_dev) {
		pr_err("%s: ERROR: No usb_dev!!\n", __func__);
		return -ENODEV;
	}

	switch (action) {
	case USB_DEVICE_ADD:
		mutex_lock(&usbport_data->lock);
		usbport_add_usb_dev_ports(usb_dev, usbport_data);
		mutex_unlock(&usbport_data->lock);
		return NOTIFY_OK;
	case USB_DEVICE_REMOVE:
		mutex_lock(&usbport_data->lock);
		usbport_detach_usb_dev_from_ports(usb_dev,
			    usbport_data);
		usbport_remove_usb_dev_ports(usbport_data, usb_dev);
		mutex_unlock(&usbport_data->lock);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static int __init usbport_test_init(void)
{
	struct usbport_data *usbport_data;
	struct class *usbport_class = NULL;
	int err = 0;

	usbport_data = kzalloc(sizeof(*usbport_data), GFP_KERNEL);
	if (!usbport_data) {
		err = -ENOMEM;
		goto err_out;
	}

	usbport_class = class_create("usbport_test");
	if (!usbport_class) {
		err = -ENOMEM;
		goto err_free;
	}

	usbport_data->class = usbport_class;

	mutex_init(&usbport_data->lock);

	/* List of ports */
	INIT_LIST_HEAD(&usbport_data->ports);
	usb_for_each_dev(usbport_data, usbport_scan_usb_dev_ports);

	/* Notifications */
	usbport_data->nb.notifier_call = usbport_notify;
	usb_register_notify(&usbport_data->nb);

	g_usbport_data = usbport_data;
	return 0;

err_free:
	kfree(usbport_data);
err_out:
	return err;
}
module_init(usbport_test_init);

static void __exit usbport_test_exit(void)
{
	struct usbport_data *usbport_data;
	struct class *usbport_class = NULL;
	struct usbport_port *port, *tmp;

	usbport_data = g_usbport_data;
	if (!usbport_data)
		return;

	usb_unregister_notify(&usbport_data->nb);

	mutex_lock(&usbport_data->lock);
	list_for_each_entry_safe(port, tmp, &usbport_data->ports, list) {
		dev_dbg(&port->dev, "%s: remove port %s\n",
		    __func__, port->port_name);
		usbport_remove_port(usbport_data, port);
	}
	mutex_unlock(&usbport_data->lock);

	usbport_class = usbport_data->class;
	class_destroy(usbport_class);
	kfree(usbport_data);
	g_usbport_data = NULL;
}
module_exit(usbport_test_exit);

MODULE_AUTHOR("Stanley Chang <stanley_chang@realtek.com>");
MODULE_DESCRIPTION("USB port test");
MODULE_LICENSE("GPL v2");
