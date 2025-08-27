// SPDX-License-Identifier: GPL-2.0+
/*
 * f_usbperf.c - USB peripheral performance testing configuration driver
 *
 * Copyright (C) 2003-2008 David Brownell
 * Copyright (C) 2008 by Nokia Corporation
 * Copyright (C) 2023 Realtek Semiconductor Corporation
 */

/* #define VERBOSE_DEBUG */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/usb/composite.h>
#include <linux/err.h>

#include "u_f.h"

#define BULK_BUFLEN	4096
#define QLEN		32
#define ISOC_INTERVAL	4
#define ISOC_MAXPACKET	1024
#define SS_BULK_QLEN	1
#define SS_ISO_QLEN	8

struct f_usbperf_opts {
	struct usb_function_instance func_inst;
	unsigned int pattern;
	unsigned int isoc_interval;
	unsigned int isoc_maxpacket;
	unsigned int isoc_mult;
	unsigned int isoc_maxburst;
	unsigned int bulk_buflen;
	unsigned int bulk_qlen;
	unsigned int iso_qlen;
	unsigned int bulk_maxburst;

	/*
	 * Read/write access to configfs attributes is handled by configfs.
	 *
	 * This is to protect the data from concurrent access by read/write
	 * and create symlink/remove symlink.
	 */
	struct mutex			lock;
	int				refcnt;
};

/*
 * PERFORMANCE TESTING FUNCTION ... a primary testing vehicle for USB peripheral
 * controller drivers.
 *
 * This just sinks bulk packets OUT to the peripheral and sources them IN
 * to the host, optionally with specific data patterns for integrity tests.
 * As such it supports basic functionality and load tests.
 *
 * In terms of control messaging, this supports all the standard requests
 * plus two that support control-OUT tests.  If the optional "autoresume"
 * mode is enabled, it provides good functional coverage for the "USBCV"
 * test harness from USB-IF.
 */
struct f_usbperf {
	struct usb_function	function;

	struct usb_ep		*in_ep;
	struct usb_ep		*out_ep;
	struct usb_ep		*iso_in_ep;
	struct usb_ep		*iso_out_ep;
	int			cur_alt;

	unsigned int pattern;
	unsigned int isoc_interval;
	unsigned int isoc_maxpacket;
	unsigned int isoc_mult;
	unsigned int isoc_maxburst;
	unsigned int buflen;
	unsigned int bulk_qlen;
	unsigned int iso_qlen;
	unsigned int bulk_maxburst;
};

static inline struct f_usbperf *func_to_usbperf(struct usb_function *f)
{
	return container_of(f, struct f_usbperf, function);
}

/*-------------------------------------------------------------------------*/

static struct usb_interface_descriptor usbperf_intf_alt0 = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bAlternateSetting =	0,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	/* .iInterface		= DYNAMIC */
};

static struct usb_interface_descriptor usbperf_intf_alt1 = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bAlternateSetting =	1,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	/* .iInterface		= DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor fs_in_ep_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_out_ep_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_iso_in_ep_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1023),
	.bInterval =		4,
};

static struct usb_endpoint_descriptor fs_iso_out_ep_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1023),
	.bInterval =		4,
};

static struct usb_descriptor_header *fs_usbperf_descs[] = {
	(struct usb_descriptor_header *)&usbperf_intf_alt0,
	(struct usb_descriptor_header *)&fs_out_ep_desc,
	(struct usb_descriptor_header *)&fs_in_ep_desc,
	(struct usb_descriptor_header *)&usbperf_intf_alt1,
#define FS_ALT_IFC_1_OFFSET	3
	(struct usb_descriptor_header *)&fs_iso_out_ep_desc,
	(struct usb_descriptor_header *)&fs_iso_in_ep_desc,
	NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor hs_in_ep_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_out_ep_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_iso_in_ep_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1024),
	.bInterval =		4,
};

static struct usb_endpoint_descriptor hs_iso_out_ep_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1024),
	.bInterval =		4,
};

static struct usb_descriptor_header *hs_usbperf_descs[] = {
	(struct usb_descriptor_header *)&usbperf_intf_alt0,
	(struct usb_descriptor_header *)&hs_in_ep_desc,
	(struct usb_descriptor_header *)&hs_out_ep_desc,
	(struct usb_descriptor_header *)&usbperf_intf_alt1,
#define HS_ALT_IFC_1_OFFSET	3
	(struct usb_descriptor_header *)&hs_iso_in_ep_desc,
	(struct usb_descriptor_header *)&hs_iso_out_ep_desc,
	NULL,
};

/* super speed support: */

static struct usb_endpoint_descriptor ss_in_ep_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_in_ep_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	0,
};

static struct usb_endpoint_descriptor ss_out_ep_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_out_ep_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	0,
};

static struct usb_endpoint_descriptor ss_iso_in_ep_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1024),
	.bInterval =		4,
};

static struct usb_ss_ep_comp_descriptor ss_iso_in_ep_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	cpu_to_le16(1024),
};

static struct usb_endpoint_descriptor ss_iso_out_ep_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bmAttributes =		USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	cpu_to_le16(1024),
	.bInterval =		4,
};

static struct usb_ss_ep_comp_descriptor ss_iso_out_ep_comp_desc = {
	.bLength =		USB_DT_SS_EP_COMP_SIZE,
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.bMaxBurst =		0,
	.bmAttributes =		0,
	.wBytesPerInterval =	cpu_to_le16(1024),
};

static struct usb_descriptor_header *ss_usbperf_descs[] = {
	(struct usb_descriptor_header *)&usbperf_intf_alt0,
	(struct usb_descriptor_header *)&ss_in_ep_desc,
	(struct usb_descriptor_header *)&ss_in_ep_comp_desc,
	(struct usb_descriptor_header *)&ss_out_ep_desc,
	(struct usb_descriptor_header *)&ss_out_ep_comp_desc,
	(struct usb_descriptor_header *)&usbperf_intf_alt1,
#define SS_ALT_IFC_1_OFFSET	5
	(struct usb_descriptor_header *)&ss_iso_in_ep_desc,
	(struct usb_descriptor_header *)&ss_iso_in_ep_comp_desc,
	(struct usb_descriptor_header *)&ss_iso_out_ep_desc,
	(struct usb_descriptor_header *)&ss_iso_out_ep_comp_desc,
	NULL,
};

/* function-specific strings: */

static struct usb_string strings_usbperf[] = {
	[0].s = "performance testing data",
	{  }			/* end of list */
};

static struct usb_gadget_strings stringtab_usbperf = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_usbperf,
};

static struct usb_gadget_strings *usbperf_strings[] = {
	&stringtab_usbperf,
	NULL,
};

/*-------------------------------------------------------------------------*/

static void disable_ep(struct usb_composite_dev *cdev, struct usb_ep *ep)
{
	int			value;

	value = usb_ep_disable(ep);
	if (value < 0)
		DBG(cdev, "disable %s --> %d\n", ep->name, value);
}

static void __disable_endpoints(struct usb_composite_dev *cdev,
				struct usb_ep *in, struct usb_ep *out,
				struct usb_ep *iso_in, struct usb_ep *iso_out)
{
	disable_ep(cdev, in);
	disable_ep(cdev, out);
	if (iso_in)
		disable_ep(cdev, iso_in);
	if (iso_out)
		disable_ep(cdev, iso_out);
}

static int
usbperf_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_usbperf	*usbperf = func_to_usbperf(f);
	int	id;
	int ret;

	/* allocate interface ID(s) */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	usbperf_intf_alt0.bInterfaceNumber = id;
	usbperf_intf_alt1.bInterfaceNumber = id;

	/* allocate bulk endpoints */
	usbperf->in_ep = usb_ep_autoconfig(cdev->gadget, &fs_in_ep_desc);
	if (!usbperf->in_ep) {
autoconf_fail:
		ERROR(cdev, "%s: can't autoconfigure on %s\n",
		      f->name, cdev->gadget->name);
		return -ENODEV;
	}

	usbperf->out_ep = usb_ep_autoconfig(cdev->gadget, &fs_out_ep_desc);
	if (!usbperf->out_ep)
		goto autoconf_fail;

	/* sanity check the isoc module parameters */
	if (usbperf->isoc_interval < 1)
		usbperf->isoc_interval = 1;
	if (usbperf->isoc_interval > 16)
		usbperf->isoc_interval = 16;
	if (usbperf->isoc_mult > 2)
		usbperf->isoc_mult = 2;
	if (usbperf->isoc_maxburst > 15)
		usbperf->isoc_maxburst = 15;

	/* fill in the FS isoc descriptors from the module parameters */
	fs_iso_in_ep_desc.wMaxPacketSize = usbperf->isoc_maxpacket > 1023 ?
						1023 : usbperf->isoc_maxpacket;
	fs_iso_in_ep_desc.bInterval = usbperf->isoc_interval;
	fs_iso_out_ep_desc.wMaxPacketSize = usbperf->isoc_maxpacket > 1023 ?
						1023 : usbperf->isoc_maxpacket;
	fs_iso_out_ep_desc.bInterval = usbperf->isoc_interval;

	/* allocate iso endpoints */
	usbperf->iso_in_ep = usb_ep_autoconfig(cdev->gadget, &fs_iso_in_ep_desc);
	if (!usbperf->iso_in_ep)
		goto no_iso;

	usbperf->iso_out_ep = usb_ep_autoconfig(cdev->gadget, &fs_iso_out_ep_desc);
	if (!usbperf->iso_out_ep) {
		usb_ep_autoconfig_release(usbperf->iso_in_ep);
		usbperf->iso_in_ep = NULL;
no_iso:
		/*
		 * We still want to work even if the UDC doesn't have isoc
		 * endpoints, so null out the alt interface that contains
		 * them and continue.
		 */
		fs_usbperf_descs[FS_ALT_IFC_1_OFFSET] = NULL;
		hs_usbperf_descs[HS_ALT_IFC_1_OFFSET] = NULL;
		ss_usbperf_descs[SS_ALT_IFC_1_OFFSET] = NULL;
	}

	if (usbperf->isoc_maxpacket > 1024)
		usbperf->isoc_maxpacket = 1024;

	/* support high speed hardware */
	hs_in_ep_desc.bEndpointAddress = fs_in_ep_desc.bEndpointAddress;
	hs_out_ep_desc.bEndpointAddress = fs_out_ep_desc.bEndpointAddress;

	/*
	 * Fill in the HS isoc descriptors from the module parameters.
	 * We assume that the user knows what they are doing and won't
	 * give parameters that their UDC doesn't support.
	 */
	hs_iso_in_ep_desc.wMaxPacketSize = usbperf->isoc_maxpacket;
	hs_iso_in_ep_desc.wMaxPacketSize |= usbperf->isoc_mult << 11;
	hs_iso_in_ep_desc.bInterval = usbperf->isoc_interval;
	hs_iso_in_ep_desc.bEndpointAddress =
		fs_iso_in_ep_desc.bEndpointAddress;

	hs_iso_out_ep_desc.wMaxPacketSize = usbperf->isoc_maxpacket;
	hs_iso_out_ep_desc.wMaxPacketSize |= usbperf->isoc_mult << 11;
	hs_iso_out_ep_desc.bInterval = usbperf->isoc_interval;
	hs_iso_out_ep_desc.bEndpointAddress = fs_iso_out_ep_desc.bEndpointAddress;

	/* support super speed hardware */
	ss_in_ep_desc.bEndpointAddress =
		fs_in_ep_desc.bEndpointAddress;
	ss_out_ep_desc.bEndpointAddress =
		fs_out_ep_desc.bEndpointAddress;

	/*
	 * Fill in the SS isoc descriptors from the module parameters.
	 * We assume that the user knows what they are doing and won't
	 * give parameters that their UDC doesn't support.
	 */
	ss_iso_in_ep_desc.wMaxPacketSize = usbperf->isoc_maxpacket;
	ss_iso_in_ep_desc.bInterval = usbperf->isoc_interval;
	ss_iso_in_ep_comp_desc.bmAttributes = usbperf->isoc_mult;
	ss_iso_in_ep_comp_desc.bMaxBurst = usbperf->isoc_maxburst;
	ss_iso_in_ep_comp_desc.wBytesPerInterval = usbperf->isoc_maxpacket *
		(usbperf->isoc_mult + 1) * (usbperf->isoc_maxburst + 1);
	ss_iso_in_ep_desc.bEndpointAddress =
		fs_iso_in_ep_desc.bEndpointAddress;

	ss_iso_out_ep_desc.wMaxPacketSize = usbperf->isoc_maxpacket;
	ss_iso_out_ep_desc.bInterval = usbperf->isoc_interval;
	ss_iso_out_ep_comp_desc.bmAttributes = usbperf->isoc_mult;
	ss_iso_out_ep_comp_desc.bMaxBurst = usbperf->isoc_maxburst;
	ss_iso_out_ep_comp_desc.wBytesPerInterval = usbperf->isoc_maxpacket *
		(usbperf->isoc_mult + 1) * (usbperf->isoc_maxburst + 1);
	ss_iso_out_ep_desc.bEndpointAddress = fs_iso_out_ep_desc.bEndpointAddress;

	if (usbperf->bulk_maxburst > 15)
		usbperf->bulk_maxburst = 15;

	ss_in_ep_comp_desc.bMaxBurst = usbperf->bulk_maxburst;
	ss_out_ep_comp_desc.bMaxBurst = usbperf->bulk_maxburst;

	ret = usb_assign_descriptors(f, fs_usbperf_descs,
				     hs_usbperf_descs, ss_usbperf_descs,
				     ss_usbperf_descs);
	if (ret)
		return ret;

	DBG(cdev, "%s speed %s: IN/%s, OUT/%s, ISO-IN/%s, ISO-OUT/%s\n",
	    (gadget_is_superspeed(c->cdev->gadget) ? "super" :
	     (gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full")),
			f->name, usbperf->in_ep->name, usbperf->out_ep->name,
			usbperf->iso_in_ep ? usbperf->iso_in_ep->name : "<none>",
			usbperf->iso_out_ep ? usbperf->iso_out_ep->name : "<none>");
	return 0;
}

static void
usbperf_free_func(struct usb_function *f)
{
	struct f_usbperf_opts *opts;

	opts = container_of(f->fi, struct f_usbperf_opts, func_inst);

	mutex_lock(&opts->lock);
	opts->refcnt--;
	mutex_unlock(&opts->lock);

	usb_free_all_descriptors(f);
	kfree(func_to_usbperf(f));
}

/* optionally require specific source/sink data patterns  */
static int check_read_data(struct f_usbperf *usbperf, struct usb_request *req)
{
	unsigned int		i;
	u8			*buf = req->buf;
	struct usb_composite_dev *cdev = usbperf->function.config->cdev;
	int max_packet_size = le16_to_cpu(usbperf->out_ep->desc->wMaxPacketSize);

	if (usbperf->pattern == 2)
		return 0;

	for (i = 0; i < req->actual; i++, buf++) {
		switch (usbperf->pattern) {
		/* all-zeroes has no synchronization issues */
		case 0:
			if (*buf == 0)
				continue;
			break;

		/* "mod63" stays in sync with short-terminated transfers,
		 * OR otherwise when host and gadget agree on how large
		 * each usb transfer request should be.  Resync is done
		 * with set_interface or set_config.  (We *WANT* it to
		 * get quickly out of sync if controllers or their drivers
		 * stutter for any reason, including buffer duplication...)
		 */
		case 1:
			if (*buf == (u8)((i % max_packet_size) % 63))
				continue;
			break;
		}
		ERROR(cdev, "bad OUT byte, buf[%d] = %d\n", i, *buf);
		usb_ep_set_halt(usbperf->out_ep);
		return -EINVAL;
	}
	return 0;
}

static void reinit_write_data(struct usb_ep *ep, struct usb_request *req)
{
	unsigned int	i;
	u8		*buf = req->buf;
	int max_packet_size = le16_to_cpu(ep->desc->wMaxPacketSize);
	struct f_usbperf *usbperf = ep->driver_data;

	switch (usbperf->pattern) {
	case 0:
		memset(req->buf, 0, req->length);
		break;
	case 1:
		for  (i = 0; i < req->length; i++)
			*buf++ = (u8)((i % max_packet_size) % 63);
		break;
	case 2:
		break;
	}
}

static void usbperf_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct usb_composite_dev	*cdev;
	struct f_usbperf		*usbperf = ep->driver_data;
	int				status = req->status;

	/* driver_data will be null if ep has been disabled */
	if (!usbperf)
		return;

	cdev = usbperf->function.config->cdev;

	switch (status) {
	case 0:				/* normal completion? */
		if (ep == usbperf->out_ep) {
			check_read_data(usbperf, req);
			if (usbperf->pattern != 2)
				memset(req->buf, 0x55, req->length);
		}
		break;

	/* this endpoint is normally active while we're configured */
	case -ECONNABORTED:		/* hardware forced ep reset */
	case -ECONNRESET:		/* request dequeued */
	case -ESHUTDOWN:		/* disconnect from host */
		VDBG(cdev, "%s gone (%d), %d/%d\n", ep->name, status,
		     req->actual, req->length);
		if (ep == usbperf->out_ep)
			check_read_data(usbperf, req);
		free_ep_req(ep, req);
		return;

	case -EOVERFLOW:		/* buffer overrun on read means that
					 * we didn't provide a big enough
					 * buffer.
					 */
	default:
		DBG(cdev, "%s complete --> %d, %d/%d\n", ep->name,
		    status, req->actual, req->length);

	case -EREMOTEIO:		/* short read */
		break;
	}

	status = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (status) {
		ERROR(cdev, "kill %s:  resubmit %d bytes --> %d\n",
		      ep->name, req->length, status);
		usb_ep_set_halt(ep);
		/* FIXME recover later ... somehow */
	}
}

static int usbperf_start_ep(struct f_usbperf *usbperf, bool is_in,
			    bool is_iso, int speed)
{
	struct usb_ep		*ep;
	struct usb_request	*req;
	int			i, size, qlen, status = 0;

	if (is_iso) {
		switch (speed) {
		case USB_SPEED_SUPER_PLUS:
		case USB_SPEED_SUPER:
			size = usbperf->isoc_maxpacket *
					(usbperf->isoc_mult + 1) *
					(usbperf->isoc_maxburst + 1);
			break;
		case USB_SPEED_HIGH:
			size = usbperf->isoc_maxpacket * (usbperf->isoc_mult + 1);
			break;
		default:
			size = usbperf->isoc_maxpacket > 1023 ?
					1023 : usbperf->isoc_maxpacket;
			break;
		}
		ep = is_in ? usbperf->iso_in_ep : usbperf->iso_out_ep;
		qlen = usbperf->iso_qlen;
	} else {
		ep = is_in ? usbperf->in_ep : usbperf->out_ep;
		qlen = usbperf->bulk_qlen;
		size = usbperf->buflen;
	}

	for (i = 0; i < qlen; i++) {
		req = alloc_ep_req(ep, size);
		if (!req)
			return -ENOMEM;

		req->complete = usbperf_complete;
		if (is_in)
			reinit_write_data(ep, req);
		else if (usbperf->pattern != 2)
			memset(req->buf, 0x55, req->length);

		status = usb_ep_queue(ep, req, GFP_ATOMIC);
		if (status) {
			struct usb_composite_dev	*cdev;

			cdev = usbperf->function.config->cdev;
			ERROR(cdev, "start %s%s %s --> %d\n",
			      is_iso ? "ISO-" : "", is_in ? "IN" : "OUT",
			      ep->name, status);
			free_ep_req(ep, req);
			return status;
		}
	}

	return status;
}

static void disable_usbperf(struct f_usbperf *usbperf)
{
	struct usb_composite_dev	*cdev;

	cdev = usbperf->function.config->cdev;
	__disable_endpoints(cdev, usbperf->in_ep, usbperf->out_ep, usbperf->iso_in_ep,
			    usbperf->iso_out_ep);
	VDBG(cdev, "%s disabled\n", usbperf->function.name);
}

static int
enable_usbperf(struct usb_composite_dev *cdev, struct f_usbperf *usbperf,
	       int alt)
{
	int					result = 0;
	int					speed = cdev->gadget->speed;
	struct usb_ep				*ep;

	/* one bulk endpoint writes (sources) zeroes IN (to the host) */
	ep = usbperf->in_ep;
	result = config_ep_by_speed(cdev->gadget, &usbperf->function, ep);
	if (result)
		return result;
	result = usb_ep_enable(ep);
	if (result < 0)
		return result;
	ep->driver_data = usbperf;

	result = usbperf_start_ep(usbperf, true, false, speed);
	if (result < 0) {
fail:
		ep = usbperf->in_ep;
		usb_ep_disable(ep);
		return result;
	}

	/* one bulk endpoint reads (sinks) anything OUT (from the host) */
	ep = usbperf->out_ep;
	result = config_ep_by_speed(cdev->gadget, &usbperf->function, ep);
	if (result)
		goto fail;
	result = usb_ep_enable(ep);
	if (result < 0)
		goto fail;
	ep->driver_data = usbperf;

	result = usbperf_start_ep(usbperf, false, false, speed);
	if (result < 0) {
fail2:
		ep = usbperf->out_ep;
		usb_ep_disable(ep);
		goto fail;
	}

	if (alt == 0)
		goto out;

	/* one iso endpoint writes (sources) zeroes IN (to the host) */
	ep = usbperf->iso_in_ep;
	if (ep) {
		result = config_ep_by_speed(cdev->gadget, &usbperf->function, ep);
		if (result)
			goto fail2;
		result = usb_ep_enable(ep);
		if (result < 0)
			goto fail2;
		ep->driver_data = usbperf;

		result = usbperf_start_ep(usbperf, true, true, speed);
		if (result < 0) {
fail3:
			ep = usbperf->iso_in_ep;
			if (ep)
				usb_ep_disable(ep);
			goto fail2;
		}
	}

	/* one iso endpoint reads (sinks) anything OUT (from the host) */
	ep = usbperf->iso_out_ep;
	if (ep) {
		result = config_ep_by_speed(cdev->gadget, &usbperf->function, ep);
		if (result)
			goto fail3;
		result = usb_ep_enable(ep);
		if (result < 0)
			goto fail3;
		ep->driver_data = usbperf;

		result = usbperf_start_ep(usbperf, false, true, speed);
		if (result < 0) {
			usb_ep_disable(ep);
			goto fail3;
		}
	}
out:
	usbperf->cur_alt = alt;

	DBG(cdev, "%s enabled, alt intf %d\n", usbperf->function.name, alt);
	return result;
}

static int usbperf_set_alt(struct usb_function *f,
			   unsigned int intf, unsigned int alt)
{
	struct f_usbperf		*usbperf = func_to_usbperf(f);
	struct usb_composite_dev	*cdev = f->config->cdev;

	disable_usbperf(usbperf);
	return enable_usbperf(cdev, usbperf, alt);
}

static int usbperf_get_alt(struct usb_function *f, unsigned int intf)
{
	struct f_usbperf		*usbperf = func_to_usbperf(f);

	return usbperf->cur_alt;
}

static void usbperf_disable(struct usb_function *f)
{
	struct f_usbperf	*usbperf = func_to_usbperf(f);

	disable_usbperf(usbperf);
}

/*-------------------------------------------------------------------------*/

static int usbperf_setup(struct usb_function *f,
			 const struct usb_ctrlrequest *ctrl)
{
	struct usb_configuration *c = f->config;
	struct usb_request	*req = c->cdev->req;
	int			value = -EOPNOTSUPP;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	req->length = USB_COMP_EP0_BUFSIZ;

	/* composite driver infrastructure handles everything except
	 * the two control test requests.
	 */
	switch (ctrl->bRequest) {
	/*
	 * These are the same vendor-specific requests supported by
	 * Intel's USB 2.0 compliance test devices.  We exceed that
	 * device spec by allowing multiple-packet requests.
	 *
	 * NOTE:  the Control-OUT data stays in req->buf ... better
	 * would be copying it into a scratch buffer, so that other
	 * requests may safely intervene.
	 */
	case 0x5b:	/* control WRITE test -- fill the buffer */
		if (ctrl->bRequestType != (USB_DIR_OUT | USB_TYPE_VENDOR))
			goto unknown;
		if (w_value || w_index)
			break;
		/* just read that many bytes into the buffer */
		if (w_length > req->length)
			break;
		value = w_length;
		break;
	case 0x5c:	/* control READ test -- return the buffer */
		if (ctrl->bRequestType != (USB_DIR_IN | USB_TYPE_VENDOR))
			goto unknown;
		if (w_value || w_index)
			break;
		/* expect those bytes are still in the buffer; send back */
		if (w_length > req->length)
			break;
		value = w_length;
		break;

	default:
unknown:
		VDBG(c->cdev,
		     "unknown control req%02x.%02x v%04x i%04x l%d\n",
		     ctrl->bRequestType, ctrl->bRequest,
		     w_value, w_index, w_length);
	}

	/* respond with data transfer or status phase? */
	if (value >= 0) {
		VDBG(c->cdev, "usbperf req%02x.%02x v%04x i%04x l%d\n",
		     ctrl->bRequestType, ctrl->bRequest,
		     w_value, w_index, w_length);
		req->zero = 0;
		req->length = value;
		value = usb_ep_queue(c->cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0)
			ERROR(c->cdev, "usbperf response, err %d\n", value);
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

static struct usb_function *usbperf_alloc_func(struct usb_function_instance *fi)
{
	struct f_usbperf *usbperf;
	struct f_usbperf_opts *opts;

	usbperf = kzalloc(sizeof(*usbperf), GFP_KERNEL);
	if (!usbperf)
		return ERR_PTR(-ENOMEM);

	opts =  container_of(fi, struct f_usbperf_opts, func_inst);

	mutex_lock(&opts->lock);
	opts->refcnt++;
	mutex_unlock(&opts->lock);

	usbperf->pattern = opts->pattern;
	usbperf->isoc_interval = opts->isoc_interval;
	usbperf->isoc_maxpacket = opts->isoc_maxpacket;
	usbperf->isoc_mult = opts->isoc_mult;
	usbperf->isoc_maxburst = opts->isoc_maxburst;
	usbperf->buflen = opts->bulk_buflen;
	usbperf->bulk_qlen = opts->bulk_qlen;
	usbperf->iso_qlen = opts->iso_qlen;
	usbperf->bulk_maxburst = opts->bulk_maxburst;

	usbperf->function.name = "usbperf";
	usbperf->function.bind = usbperf_bind;
	usbperf->function.set_alt = usbperf_set_alt;
	usbperf->function.get_alt = usbperf_get_alt;
	usbperf->function.disable = usbperf_disable;
	usbperf->function.setup = usbperf_setup;
	usbperf->function.strings = usbperf_strings;

	usbperf->function.free_func = usbperf_free_func;

	return &usbperf->function;
}

static inline struct f_usbperf_opts *to_f_usbperf_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_usbperf_opts,
			    func_inst.group);
}

static void usbperf_attr_release(struct config_item *item)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations usbperf_item_ops = {
	.release		= usbperf_attr_release,
};

static ssize_t f_usbperf_opts_pattern_show(struct config_item *item, char *page)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", opts->pattern);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_usbperf_opts_pattern_store(struct config_item *item,
					    const char *page, size_t len)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int ret;
	u8 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou8(page, 0, &num);
	if (ret)
		goto end;

	if (num != 0 && num != 1 && num != 2) {
		ret = -EINVAL;
		goto end;
	}

	opts->pattern = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_usbperf_opts_, pattern);

static ssize_t f_usbperf_opts_isoc_interval_show(struct config_item *item, char *page)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", opts->isoc_interval);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_usbperf_opts_isoc_interval_store(struct config_item *item,
						  const char *page, size_t len)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int ret;
	u8 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou8(page, 0, &num);
	if (ret)
		goto end;

	if (num > 16) {
		ret = -EINVAL;
		goto end;
	}

	opts->isoc_interval = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_usbperf_opts_, isoc_interval);

static ssize_t f_usbperf_opts_isoc_maxpacket_show(struct config_item *item, char *page)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", opts->isoc_maxpacket);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_usbperf_opts_isoc_maxpacket_store(struct config_item *item,
						   const char *page, size_t len)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int ret;
	u16 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou16(page, 0, &num);
	if (ret)
		goto end;

	if (num > 1024) {
		ret = -EINVAL;
		goto end;
	}

	opts->isoc_maxpacket = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_usbperf_opts_, isoc_maxpacket);

static ssize_t f_usbperf_opts_isoc_mult_show(struct config_item *item, char *page)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", opts->isoc_mult);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_usbperf_opts_isoc_mult_store(struct config_item *item,
					      const char *page, size_t len)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int ret;
	u8 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou8(page, 0, &num);
	if (ret)
		goto end;

	if (num > 2) {
		ret = -EINVAL;
		goto end;
	}

	opts->isoc_mult = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_usbperf_opts_, isoc_mult);

static ssize_t f_usbperf_opts_isoc_maxburst_show(struct config_item *item, char *page)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", opts->isoc_maxburst);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_usbperf_opts_isoc_maxburst_store(struct config_item *item,
						  const char *page, size_t len)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int ret;
	u8 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou8(page, 0, &num);
	if (ret)
		goto end;

	if (num > 15) {
		ret = -EINVAL;
		goto end;
	}

	opts->isoc_maxburst = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_usbperf_opts_, isoc_maxburst);

static ssize_t f_usbperf_opts_bulk_buflen_show(struct config_item *item, char *page)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", opts->bulk_buflen);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_usbperf_opts_bulk_buflen_store(struct config_item *item,
						const char *page, size_t len)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int ret;
	u32 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou32(page, 0, &num);
	if (ret)
		goto end;

	opts->bulk_buflen = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_usbperf_opts_, bulk_buflen);

static ssize_t f_usbperf_opts_bulk_qlen_show(struct config_item *item, char *page)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", opts->bulk_qlen);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_usbperf_opts_bulk_qlen_store(struct config_item *item,
					      const char *page, size_t len)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int ret;
	u32 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou32(page, 0, &num);
	if (ret)
		goto end;

	opts->bulk_qlen = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_usbperf_opts_, bulk_qlen);

static ssize_t f_usbperf_opts_iso_qlen_show(struct config_item *item, char *page)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", opts->iso_qlen);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_usbperf_opts_iso_qlen_store(struct config_item *item,
					     const char *page, size_t len)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int ret;
	u32 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou32(page, 0, &num);
	if (ret)
		goto end;

	opts->iso_qlen = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_usbperf_opts_, iso_qlen);

static ssize_t f_usbperf_opts_bulk_maxburst_show(struct config_item *item, char *page)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", opts->bulk_maxburst);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_usbperf_opts_bulk_maxburst_store(struct config_item *item,
						  const char *page, size_t len)
{
	struct f_usbperf_opts *opts = to_f_usbperf_opts(item);
	int ret;
	u8 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou8(page, 0, &num);
	if (ret)
		goto end;

	if (num > 15) {
		ret = -EINVAL;
		goto end;
	}

	opts->bulk_maxburst = num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_usbperf_opts_, bulk_maxburst);

static struct configfs_attribute *usbperf_attrs[] = {
	&f_usbperf_opts_attr_pattern,
	&f_usbperf_opts_attr_isoc_interval,
	&f_usbperf_opts_attr_isoc_maxpacket,
	&f_usbperf_opts_attr_isoc_mult,
	&f_usbperf_opts_attr_isoc_maxburst,
	&f_usbperf_opts_attr_bulk_buflen,
	&f_usbperf_opts_attr_bulk_qlen,
	&f_usbperf_opts_attr_iso_qlen,
	&f_usbperf_opts_attr_bulk_maxburst,
	NULL,
};

static const struct config_item_type usbperf_func_type = {
	.ct_item_ops    = &usbperf_item_ops,
	.ct_attrs	= usbperf_attrs,
	.ct_owner       = THIS_MODULE,
};

static void usbperf_free_instance(struct usb_function_instance *fi)
{
	struct f_usbperf_opts *opts;

	opts = container_of(fi, struct f_usbperf_opts, func_inst);
	kfree(opts);
}

static struct usb_function_instance *usbperf_alloc_inst(void)
{
	struct f_usbperf_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);
	mutex_init(&opts->lock);
	opts->func_inst.free_func_inst = usbperf_free_instance;
	opts->isoc_interval = ISOC_INTERVAL;
	opts->isoc_maxpacket = ISOC_MAXPACKET;
	opts->bulk_buflen = BULK_BUFLEN;
	opts->bulk_qlen = SS_BULK_QLEN;
	opts->iso_qlen = SS_ISO_QLEN;

	config_group_init_type_name(&opts->func_inst.group, "",
				    &usbperf_func_type);

	return &opts->func_inst;
}
DECLARE_USB_FUNCTION(usbperf, usbperf_alloc_inst, usbperf_alloc_func);

static int __init usbperf_modinit(void)
{
	return usb_function_register(&usbperfusb_func);
}

static void __exit usbperf_modexit(void)
{
	usb_function_unregister(&usbperfusb_func);
}
module_init(usbperf_modinit);
module_exit(usbperf_modexit);

MODULE_LICENSE("GPL");
