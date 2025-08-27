// SPDX-License-Identifier: GPL-2.0
/*
 * USB Core/HCD Performance Testing Driver
 * Based on usbtest driver
 *
 * Copyright (C) 2023 Realtek Semiconductor Corporation
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/scatterlist.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/usb.h>
#include <asm/div64.h>

/* Parameter for usbperf driver. */
struct usbperf_param_32 {
	/* inputs */
	__u32		test_num;	/* 0..(TEST_CASES-1) */
	__u32		iterations;
	__u32		length;
	__u32		vary;
	__u32		sglen;

	/* outputs */
	__s32		duration_sec;
	__s32		duration_usec;
};

/*
 * Compat parameter to the usbperf driver.
 * This supports older user space binaries compiled with 64 bit compiler.
 */
struct usbperf_param_64 {
	/* inputs */
	__u32		test_num;	/* 0..(TEST_CASES-1) */
	__u32		iterations;
	__u32		length;
	__u32		vary;
	__u32		sglen;

	/* outputs */
	__s64		duration_sec;
	__s64		duration_usec;
};

/* IOCTL interface to the driver. */
#define USBPERF_REQUEST_32    _IOWR('U', 100, struct usbperf_param_32)
/* COMPAT IOCTL interface to the driver. */
#define USBPERF_REQUEST_64    _IOWR('U', 100, struct usbperf_param_64)

struct usbperf_info {
	const char		*name;
	u8			ep_in;		/* bulk/intr source */
	u8			ep_out;		/* bulk/intr sink */
	unsigned int		autoconf:1;
	unsigned int		ctrl_out:1;
	unsigned int		iso:1;		/* try iso in/out */
	unsigned int		intr:1;		/* try interrupt in/out */
	int			alt;
};

/* this is accessed only through usbfs ioctl calls.
 * one ioctl to issue a test ... one lock per device.
 * tests create other threads if they need them.
 * urbs and buffers are allocated dynamically,
 * and data generated deterministically.
 */
struct usbperf_dev {
	struct usb_interface	*intf;
	struct usbperf_info	*info;
	int			in_pipe;
	int			out_pipe;
	int			alt;
	int			in_iso_pipe;
	int			out_iso_pipe;
	int			iso_alt;
	int			in_int_pipe;
	int			out_int_pipe;
	int			int_alt;
	struct usb_endpoint_descriptor	*iso_in, *iso_out;
	struct usb_endpoint_descriptor	*int_in, *int_out;

	/* mutex lock for ioctl */
	struct mutex		lock;

#define TBUF_SIZE	256
	u8			*buf;
};

static struct usb_device *testdev_to_usbdev(struct usbperf_dev *test)
{
	return interface_to_usbdev(test->intf);
}

#define SIMPLE_IO_TIMEOUT	10000	/* in milliseconds */

/* set up all urbs so they can be used with either bulk or interrupt */
#define	INTERRUPT_RATE		1	/* msec/transfer */

#define ERROR(tdev, fmt, args...) \
	dev_err(&(tdev)->intf->dev, fmt, ## args)
#define WARNING(tdev, fmt, args...) \
	dev_warn(&(tdev)->intf->dev, fmt, ## args)

#define GUARD_BYTE	0xA5
#define MAX_SGLEN	128

static int get_altsetting(struct usbperf_dev *dev)
{
	struct usb_interface	*iface = dev->intf;
	struct usb_device	*udev = interface_to_usbdev(iface);
	int			retval;

	retval = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				 USB_REQ_GET_INTERFACE, USB_DIR_IN | USB_RECIP_INTERFACE,
				 0, iface->altsetting[0].desc.bInterfaceNumber,
			dev->buf, 1, USB_CTRL_GET_TIMEOUT);
	switch (retval) {
	case 1:
		return dev->buf[0];
	case 0:
		retval = -ERANGE;
		fallthrough;
	default:
		return retval;
	}
}

static int set_altsetting(struct usbperf_dev *dev, int alternate)
{
	struct usb_interface	*iface = dev->intf;
	struct usb_device	*udev;

	if (alternate < 0 || alternate >= 256)
		return -EINVAL;

	udev = interface_to_usbdev(iface);
	return usb_set_interface(udev,
				 iface->altsetting[0].desc.bInterfaceNumber,
				 alternate);
}

static inline void endpoint_update(int edi,
				   struct usb_host_endpoint **in,
				   struct usb_host_endpoint **out,
				   struct usb_host_endpoint *e)
{
	if (edi) {
		if (!*in)
			*in = e;
	} else {
		if (!*out)
			*out = e;
	}
}

static int
get_endpoints(struct usbperf_dev *dev, struct usb_interface *intf)
{
	struct usb_host_interface	*alt;
	struct usb_host_endpoint	*in = NULL;
	struct usb_host_endpoint	*out = NULL;
	struct usb_host_endpoint	*iso_in = NULL;
	struct usb_host_endpoint	*iso_out = NULL;
	struct usb_host_endpoint	*int_in = NULL;
	struct usb_host_endpoint	*int_out = NULL;
	struct usb_device		*udev;
	int				tmp;

	udev = testdev_to_usbdev(dev);
	for (tmp = 0; tmp < intf->num_altsetting; tmp++) {
		unsigned int ep;

		in = NULL;
		out = NULL;
		iso_in = NULL;
		iso_out = NULL;
		int_in = NULL;
		int_out = NULL;
		alt = intf->altsetting + tmp;

		/* take the first altsetting with in-bulk + out-bulk;
		 * ignore other endpoints and altsettings.
		 */
		for (ep = 0; ep < alt->desc.bNumEndpoints; ep++) {
			struct usb_host_endpoint	*e;
			int edi;

			e = alt->endpoint + ep;
			edi = usb_endpoint_dir_in(&e->desc);

			switch (usb_endpoint_type(&e->desc)) {
			case USB_ENDPOINT_XFER_BULK:
				dev->alt = tmp;
				endpoint_update(edi, &in, &out, e);
				continue;
			case USB_ENDPOINT_XFER_INT:
				dev->int_alt = tmp;
				if (dev->info->intr)
					endpoint_update(edi, &int_in, &int_out, e);
				continue;
			case USB_ENDPOINT_XFER_ISOC:
				dev->iso_alt = tmp;
				if (dev->info->iso)
					endpoint_update(edi, &iso_in, &iso_out, e);
				fallthrough;
			default:
				continue;
			}
		}

		if (in)
			dev->in_pipe = usb_rcvbulkpipe(udev,
						       in->desc.bEndpointAddress
						       & USB_ENDPOINT_NUMBER_MASK);
		if (out)
			dev->out_pipe = usb_sndbulkpipe(udev,
							out->desc.bEndpointAddress
							& USB_ENDPOINT_NUMBER_MASK);

		if (iso_in) {
			dev->iso_in = &iso_in->desc;
			dev->in_iso_pipe = usb_rcvisocpipe(udev,
							   iso_in->desc.bEndpointAddress
							   & USB_ENDPOINT_NUMBER_MASK);
		}

		if (iso_out) {
			dev->iso_out = &iso_out->desc;
			dev->out_iso_pipe = usb_sndisocpipe(udev,
							    iso_out->desc.bEndpointAddress
							    & USB_ENDPOINT_NUMBER_MASK);
		}

		if (int_in) {
			dev->int_in = &int_in->desc;
			dev->in_int_pipe = usb_rcvintpipe(udev,
							  int_in->desc.bEndpointAddress
							  & USB_ENDPOINT_NUMBER_MASK);
		}

		if (int_out) {
			dev->int_out = &int_out->desc;
			dev->out_int_pipe = usb_sndintpipe(udev,
							   int_out->desc.bEndpointAddress
							   & USB_ENDPOINT_NUMBER_MASK);
		}
	}

	if ((in && out)  ||  iso_in || iso_out || int_in || int_out)
		goto found;

	return -EINVAL;

found:
	if (dev->info->alt != 0) {
		tmp = set_altsetting(dev, dev->info->alt);
		if (tmp < 0)
			return tmp;
	}

	return 0;
}

static unsigned int get_maxpacket(struct usb_device *udev, int pipe)
{
	struct usb_host_endpoint *ep;

	ep = usb_pipe_endpoint(udev, pipe);
	return le16_to_cpup(&ep->desc.wMaxPacketSize);
}

static int ss_isoc_get_packet_num(struct usb_device *udev, int pipe)
{
	struct usb_host_endpoint *ep = usb_pipe_endpoint(udev, pipe);

	return USB_SS_MULT(ep->ss_ep_comp.bmAttributes)
		* (1 + ep->ss_ep_comp.bMaxBurst);
}

static inline unsigned long buffer_offset(void *buf)
{
	return (unsigned long)buf & (ARCH_KMALLOC_MINALIGN - 1);
}

static int check_guard_bytes(struct usbperf_dev *tdev, struct urb *urb)
{
	u8		*buf = urb->transfer_buffer;
	u8		*guard = buf - buffer_offset(buf);
	unsigned int	i;

	for (i = 0; guard < buf; i++, guard++) {
		if (*guard != GUARD_BYTE) {
			ERROR(tdev, "guard byte[%d] %d (not %d)\n", i, *guard, GUARD_BYTE);
			return -EINVAL;
		}
	}
	return 0;
}

/* We use scatterlist primitives to test queued I/O.
 * Yes, this also tests the scatterlist primitives.
 */

static void free_sglist(struct scatterlist *sg, int nents)
{
	unsigned int i;

	if (!sg)
		return;
	for (i = 0; i < nents; i++) {
		if (!sg_page(&sg[i]))
			continue;
		kfree(sg_virt(&sg[i]));
	}
	kfree(sg);
}

static struct scatterlist *
alloc_sglist(int nents, int max, int vary, struct usbperf_dev *dev, int pipe)
{
	struct scatterlist	*sg;
	unsigned int		i;
	unsigned int		size = max;

	if (max == 0)
		return NULL;

	sg = kmalloc_array(nents, sizeof(*sg), GFP_KERNEL);
	if (!sg)
		return NULL;
	sg_init_table(sg, nents);

	for (i = 0; i < nents; i++) {
		char *buf;

		buf = kzalloc(size, GFP_KERNEL);
		if (!buf) {
			free_sglist(sg, i);
			return NULL;
		}

		/* kmalloc pages are always physically contiguous! */
		sg_set_buf(&sg[i], buf, size);

		if (vary) {
			size += vary;
			size %= max;
			if (size == 0)
				size = (vary < max) ? vary : max;
		}
	}

	return sg;
}

struct sg_timeout {
	struct timer_list timer;
	struct usb_sg_request *req;
};

static void sg_timeout(struct timer_list *t)
{
	struct sg_timeout *timeout = from_timer(timeout, t, timer);

	usb_sg_cancel(timeout->req);
}

/*-------------------------------------------------------------------------*/

/* ISO/BULK tests ... mimics common usage
 *  - buffer length is split into N packets (mostly maxpacket sized)
 *  - multi-buffers according to sglen
 */
struct transfer_context {
	unsigned int		count;
	unsigned int		pending;
	/* spinlock for submit urb */
	spinlock_t		lock;
	struct completion	done;
	int			submit_error;
	unsigned long		errors;
	unsigned long		packet_count;
	struct usbperf_dev	*dev;
	bool			is_iso;
};

static void usbperf_free_urb(struct urb *urb)
{
	unsigned long offset = buffer_offset(urb->transfer_buffer);

	if (urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP)
		usb_free_coherent(urb->dev,
				  urb->transfer_buffer_length + offset,
				  urb->transfer_buffer - offset,
				  urb->transfer_dma - offset);
	else
		kfree(urb->transfer_buffer - offset);
	usb_free_urb(urb);
}

static struct urb *usbperf_alloc_urb(struct usb_device *udev,
				     int pipe,
				     unsigned long bytes,
				     unsigned int transfer_flags,
				     unsigned int offset,
				     u8 bInterval,
				     usb_complete_t complete_fn)
{
	struct urb *urb;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return urb;

	if (bInterval)
		usb_fill_int_urb(urb, udev, pipe, NULL, bytes, complete_fn,
				 NULL, bInterval);
	else
		usb_fill_bulk_urb(urb, udev, pipe, NULL, bytes, complete_fn,
				  NULL);

	urb->interval = (udev->speed == USB_SPEED_HIGH)
			? (INTERRUPT_RATE << 3)
			: INTERRUPT_RATE;
	urb->transfer_flags = transfer_flags;
	if (usb_pipein(pipe))
		urb->transfer_flags |= URB_SHORT_NOT_OK;

	if ((bytes + offset) == 0)
		return urb;

	if (urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP)
		urb->transfer_buffer = usb_alloc_coherent(udev, bytes + offset,
							  GFP_KERNEL, &urb->transfer_dma);
	else
		urb->transfer_buffer = kmalloc(bytes + offset, GFP_KERNEL);

	if (!urb->transfer_buffer) {
		usb_free_urb(urb);
		return NULL;
	}

	/* To test unaligned transfers add an offset and fill the
	 * unused memory with a guard value
	 */
	if (offset) {
		memset(urb->transfer_buffer, GUARD_BYTE, offset);
		urb->transfer_buffer += offset;
		if (urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP)
			urb->transfer_dma += offset;
	}

	/* For inbound transfers use guard byte so that test fails if
	 * data not correctly copied
	 */
	memset(urb->transfer_buffer,
	       usb_pipein(urb->pipe) ? GUARD_BYTE : 0, bytes);
	return urb;
}

static struct urb *usbperf_iso_alloc_urb(struct usb_device *udev,
					 int pipe,
					 unsigned long bytes,
					 unsigned int transfer_flags,
					 unsigned int offset,
					 struct usb_endpoint_descriptor *desc,
					 usb_complete_t complete_fn)
{
	struct urb	*urb;
	unsigned int	i, maxp, packets;

	if (!bytes || !desc)
		return NULL;

	maxp = usb_endpoint_maxp(desc);
	if (udev->speed >= USB_SPEED_SUPER)
		maxp *= ss_isoc_get_packet_num(udev, pipe);
	else
		maxp *= usb_endpoint_maxp_mult(desc);

	packets = DIV_ROUND_UP(bytes, maxp);

	urb = usb_alloc_urb(packets, GFP_KERNEL);
	if (!urb)
		return urb;
	urb->dev = udev;
	urb->pipe = pipe;

	urb->complete = complete_fn;
	/* urb->context = SET BY CALLER */
	urb->interval = 1 << (desc->bInterval - 1);
	urb->transfer_flags = URB_ISO_ASAP | transfer_flags;

	urb->number_of_packets = packets;
	urb->transfer_buffer_length = bytes;
	if (urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP)
		urb->transfer_buffer = usb_alloc_coherent(udev, bytes + offset,
							  GFP_KERNEL, &urb->transfer_dma);
	else
		urb->transfer_buffer = kmalloc(bytes + offset, GFP_KERNEL);

	if (!urb->transfer_buffer) {
		usb_free_urb(urb);
		return NULL;
	}

	if (offset) {
		memset(urb->transfer_buffer, GUARD_BYTE, offset);
		urb->transfer_buffer += offset;
		urb->transfer_dma += offset;
	}
	/* For inbound transfers use guard byte so that test fails if
	 * data not correctly copied
	 */
	memset(urb->transfer_buffer,
	       usb_pipein(urb->pipe) ? GUARD_BYTE : 0, bytes);

	for (i = 0; i < packets; i++) {
		/* here, only the last packet will be short */
		urb->iso_frame_desc[i].length = min_t(unsigned int, bytes, maxp);
		bytes -= urb->iso_frame_desc[i].length;

		urb->iso_frame_desc[i].offset = maxp * i;
	}

	return urb;
}

static void simple_callback(struct urb *urb)
{
	complete(urb->context);
}

static void complicated_callback(struct urb *urb)
{
	struct transfer_context	*ctx = urb->context;
	unsigned long		flags;

	spin_lock_irqsave(&ctx->lock, flags);
	ctx->count--;

	ctx->packet_count += urb->number_of_packets;
	if (urb->error_count > 0)
		ctx->errors += urb->error_count;
	else if (urb->status != 0)
		ctx->errors += (ctx->is_iso ? urb->number_of_packets : 1);
	else if (urb->actual_length != urb->transfer_buffer_length)
		ctx->errors++;
	else if (check_guard_bytes(ctx->dev, urb) != 0)
		ctx->errors++;

	if (urb->status == 0 && ctx->count > (ctx->pending - 1) && !ctx->submit_error) {
		int status = usb_submit_urb(urb, GFP_ATOMIC);

		switch (status) {
		case 0:
			goto done;
		default:
			dev_err(&ctx->dev->intf->dev, "resubmit err %d\n", status);
			fallthrough;
		case -ENODEV:			/* disconnected */
		case -ESHUTDOWN:		/* endpoint disabled */
			ctx->submit_error = 1;
			break;
		}
	}

	ctx->pending--;
	if (ctx->pending == 0) {
		if (ctx->errors)
			dev_err(&ctx->dev->intf->dev,
				"during the test, %lu errors out of %lu\n",
				ctx->errors, ctx->packet_count);
		complete(&ctx->done);
	}

done:
	spin_unlock_irqrestore(&ctx->lock, flags);
}

static u64 test_bulk_simple(struct usbperf_dev *dev,
			    int pipe, int len, int sglen)
{
	struct usb_device	*udev = testdev_to_usbdev(dev);
	struct urb		**urb;
	struct completion	 completion;
	unsigned long		expire;
	struct timespec64	start;
	struct timespec64	end;
	struct timespec64	duration;
	u64			time;
	int			i;
	u64			retval = 0;

	/* one round test for sglen*length */

	if (usb_pipein(pipe)) {
		unsigned int maxpacket = get_maxpacket(udev, pipe);

		if (len % maxpacket) {
			ERROR(dev, "%s: for bulk-in endpoint len=%d != maxpacket=%d\n",
			      __func__, len, maxpacket);
			return -EINVAL;
		}
	}

	/* allocate and init the urbs we'll queue.
	 * as with bulk/intr sglists, sglen is the queue depth; it also
	 * controls which subtests run (more tests than sglen) or rerun.
	 */
	urb = kcalloc(sglen, sizeof(struct urb *), GFP_KERNEL);
	if (!urb) {
		ERROR(dev, "urb kcalloc fail\n");
		return -ENOMEM;
	}

	for (i = 0; i < sglen; i++) {
		urb[i] = usbperf_alloc_urb(udev, pipe, len, 0, 0, 0, simple_callback);
		if (!urb[i]) {
			retval = -ENOMEM;
			ERROR(dev, "usbperf_alloc_urb fail retval=%lld\n", retval);
			goto cleanup;
		}
	}

	ktime_get_ts64(&start);

	/* submit the urbs */
	for (i = 0; i < sglen; i++) {
		urb[i]->context = &completion;
		init_completion(&completion);

		retval = usb_submit_urb(urb[i], GFP_ATOMIC);
		if (retval != 0) {
			ERROR(dev, "can't submit urb[%d], retval=%lld\n", i, retval);
			break;
		}

		expire = msecs_to_jiffies(SIMPLE_IO_TIMEOUT);
		if (!wait_for_completion_timeout(&completion, expire)) {
			usb_kill_urb(urb[i]);
			retval = (urb[i]->status == -ENOENT ?
				  -ETIMEDOUT : urb[i]->status);
			ERROR(dev, "wait_for_completion_timeout urb[%d], retval=%lld\n",
			      i, retval);
		} else {
			retval = urb[i]->status;
		}
		if (retval != 0) {
			ERROR(dev, "submit urb[%d], get error status retval=%lld\n",
			      i, retval);
			break;
		}
	}

	ktime_get_ts64(&end);
	duration = timespec64_sub(end, start);
	time = (duration.tv_sec * USEC_PER_SEC) + (duration.tv_nsec / NSEC_PER_USEC);
	if (!retval)
		retval = time;

cleanup:
	for (i = 0; i < sglen; i++) {
		if (!urb[i])
			continue;
		urb[i]->dev = udev;
		usbperf_free_urb(urb[i]);
	}
	kfree(urb);
	return retval;
}

static u64 test_bulk_queue(struct usbperf_dev *dev, int pipe, int len, int sglen)
{
	struct usb_device	*udev = testdev_to_usbdev(dev);
	struct scatterlist	*sg;
	struct usb_sg_request	req;
	struct sg_timeout	timeout = { .req = &req, };
	struct timespec64	start;
	struct timespec64	end;
	struct timespec64	duration;
	u64			time;
	u64			retval = 0;

	sg = alloc_sglist(sglen, len, 0, dev, pipe);
	if (!sg) {
		retval = -ENOMEM;
		return retval;
	}

	timer_setup_on_stack(&timeout.timer, sg_timeout, 0);

	retval = usb_sg_init(&req, udev, pipe,
			     (udev->speed == USB_SPEED_HIGH)
			     ? (INTERRUPT_RATE << 3) : INTERRUPT_RATE,
			     sg, sglen, 0, GFP_KERNEL);

	if (retval)
		goto out;

	ktime_get_ts64(&start);

	mod_timer(&timeout.timer, jiffies + msecs_to_jiffies(SIMPLE_IO_TIMEOUT));
	usb_sg_wait(&req);
	if (!del_timer_sync(&timeout.timer))
		retval = -ETIMEDOUT;
	else
		retval = req.status;

	ktime_get_ts64(&end);
	duration = timespec64_sub(end, start);
	time = (duration.tv_sec * USEC_PER_SEC) + (duration.tv_nsec / NSEC_PER_USEC);
	if (!retval)
		retval = time;

	destroy_timer_on_stack(&timeout.timer);

out:
	free_sglist(sg, sglen);

	return retval;
}

enum TEST_BULK_TYPE {
	TEST_BULK_SIMPLE = 0,
	TEST_BULK_QUEUE = 1,
};

static u64 test_bulk_run(struct usbperf_dev *dev, enum TEST_BULK_TYPE type,
			 int pipe, int len, int sglen)
{
	u64 data_size;
	u64 speed;
	u64 time;
	int retval = -EINVAL;

	data_size = sglen * len;

	switch (type) {
	case TEST_BULK_SIMPLE:
		retval = test_bulk_simple(dev, pipe, len, sglen);
		break;
	case TEST_BULK_QUEUE:
		retval = test_bulk_queue(dev, pipe, len, sglen);
		break;
	default:
		retval = -EINVAL;
		break;
	}
	if (retval < 0) {
		dev_err(&(dev)->intf->dev,
			"TEST fail return (retval=%d)\n", retval);
		return 0;
	}

	time = retval;
	speed = (data_size * USEC_PER_SEC);
	do_div(speed, time);

	dev_dbg(&(dev)->intf->dev,
		"TEST %llu bytes time=%dus speed %lld byte/s\n",
		data_size, (int)time, speed);

	return speed;
}

struct pipe_context {
	struct work_struct out_work;
	struct work_struct in_work;

	struct usbperf_dev *tdev;
	struct usbperf_param_32 *param;
	enum TEST_BULK_TYPE type;

	/* lock for submit urb */
	spinlock_t lock;

	struct completion out_completion;
	struct completion in_completion;
	u64 in_speed;
	u64 out_speed;
};

static void test_bulk_out_work(struct work_struct *work)
{
	struct pipe_context	*pipe_ctx = container_of(work,
						struct pipe_context, out_work);
	struct usbperf_dev	*dev = pipe_ctx->tdev;
	struct usbperf_param_32	*param = pipe_ctx->param;
	enum TEST_BULK_TYPE	type = pipe_ctx->type;
	int			pipe = dev->out_pipe;
	u64			speed;
	unsigned long		flags;

	speed = test_bulk_run(dev, type, pipe, param->length, param->sglen);

	spin_lock_irqsave(&pipe_ctx->lock, flags);

	pipe_ctx->out_speed = speed;
	complete(&pipe_ctx->out_completion);

	spin_unlock_irqrestore(&pipe_ctx->lock, flags);
}

static void test_bulk_in_work(struct work_struct *work)
{
	struct pipe_context	*pipe_ctx = container_of(work,
							 struct pipe_context, in_work);
	struct usbperf_dev	*dev = pipe_ctx->tdev;
	struct usbperf_param_32	*param = pipe_ctx->param;
	enum TEST_BULK_TYPE	type = pipe_ctx->type;
	int			pipe = dev->in_pipe;
	u64			speed;
	unsigned long		flags;

	speed = test_bulk_run(dev, type, pipe, param->length, param->sglen);

	spin_lock_irqsave(&pipe_ctx->lock, flags);

	pipe_ctx->in_speed = speed;
	complete(&pipe_ctx->in_completion);

	spin_unlock_irqrestore(&pipe_ctx->lock, flags);
}

#define TEST_CASE_IN BIT(0)
#define TEST_CASE_OUT BIT(1)
#define TEST_CASE_IN_OUT BIT(2)

static int __test_bulk_performance(struct usbperf_dev *dev,
				   struct usbperf_param_32 *param,
				   enum TEST_BULK_TYPE type, int test_case)
{
	u64	in_speed_avg, out_speed_avg;
	int	count = param->iterations;
	int	i;
	char	type_simple[] = "simple";
	char	type_queue[] = "queue";
	char	*type_str;

	switch (type) {
	case TEST_BULK_SIMPLE:
		type_str = type_simple;
		break;
	case TEST_BULK_QUEUE:
		type_str = type_queue;
		break;
	default:
		type_str = NULL;
		break;
	}

	if (!(test_case & TEST_CASE_OUT))
		goto test_case_in;

	/* OUT test */
	out_speed_avg = 0;
	for (i = 0; i < count; i++) {
		u64 speed;

		speed = test_bulk_run(dev, type, dev->out_pipe,
				      param->length, param->sglen);
		dev_dbg(&(dev)->intf->dev,
			"TEST %s urb for OUT:#%d write %u bytes speed %lld Byte/s\n",
			type_str, i, param->length * param->sglen, speed);

		out_speed_avg += speed;
	}
	do_div(out_speed_avg, count);
	/* report average performance */
	dev_info(&(dev)->intf->dev,
		 "TEST %s urb for OUT: write speed %lld Byte/s (%lld MB/s) (%lld MiB/s)\n",
		 type_str, out_speed_avg, out_speed_avg / (1000 * 1000),
		 out_speed_avg / (1024 * 1024));

test_case_in:
	if (!(test_case & TEST_CASE_IN))
		goto test_case_in_out;

	/* IN test */
	in_speed_avg = 0;
	for (i = 0; i < count; i++) {
		u64 speed;

		speed = test_bulk_run(dev, type, dev->in_pipe,
				      param->length, param->sglen);
		dev_dbg(&(dev)->intf->dev,
			"TEST %s urb for IN:#%d read %u bytes speed %lld Byte/s\n",
			type_str, i, param->length * param->sglen, speed);

		in_speed_avg += speed;
	}
	do_div(in_speed_avg, count);
	/* report average performance */
	dev_info(&(dev)->intf->dev,
		 "TEST %s urb for IN: read speed %lld Byte/s (%lld MB/s) (%lld MiB/s)\n",
		 type_str, in_speed_avg, in_speed_avg / (1000 * 1000),
		 in_speed_avg / (1024 * 1024));

test_case_in_out:
	if (!(test_case & TEST_CASE_IN_OUT))
		goto out;

	/* IN and OUT test */
	out_speed_avg = 0;
	in_speed_avg = 0;
	for (i = 0; i < count; i++) {
		struct pipe_context pipe_ctx;
		unsigned long expire;

		pipe_ctx.tdev = dev;
		pipe_ctx.param = param;
		pipe_ctx.type = type;

		spin_lock_init(&pipe_ctx.lock);
		init_completion(&pipe_ctx.out_completion);
		init_completion(&pipe_ctx.in_completion);

		INIT_WORK(&pipe_ctx.out_work, test_bulk_out_work);
		INIT_WORK(&pipe_ctx.in_work, test_bulk_in_work);

		schedule_work(&pipe_ctx.in_work);
		schedule_work(&pipe_ctx.out_work);

		expire = msecs_to_jiffies(SIMPLE_IO_TIMEOUT);
		if (!wait_for_completion_timeout(&pipe_ctx.out_completion, expire))
			ERROR(dev, "wait_for_completion_timeout out_completion\n");

		if (!wait_for_completion_timeout(&pipe_ctx.in_completion, expire))
			ERROR(dev, "wait_for_completion_timeout in_completion\n");

		dev_dbg(&(dev)->intf->dev,
			"TEST %s urb for IN/OUT:#%d write %u bytes speed %lld Byte/s\n",
			type_str, i, param->length * param->sglen,
			pipe_ctx.out_speed);
		dev_dbg(&(dev)->intf->dev,
			"TEST %s urb for IN/OUT:#%d read %u bytes speed %lld Byte/s\n",
			type_str, i, param->length * param->sglen,
			pipe_ctx.in_speed);

		out_speed_avg += pipe_ctx.out_speed;
		in_speed_avg += pipe_ctx.in_speed;
	}
	do_div(out_speed_avg, count);
	do_div(in_speed_avg, count);
	/* report average performance */
	dev_info(&(dev)->intf->dev,
		 "TEST %s urb for IN/OUT: write speed %lld Byte/s (%lld MB/s) (%lld MiB/s)\n",
		 type_str, out_speed_avg, out_speed_avg / (1000 * 1000),
		 out_speed_avg / (1024 * 1024));
	dev_info(&(dev)->intf->dev,
		 "TEST %s urb for IN/OUT: read speed %lld Byte/s (%lld MB/s) (%lld MiB/s)\n",
		 type_str, in_speed_avg, in_speed_avg / (1000 * 1000),
		 in_speed_avg / (1024 * 1024));

out:
	return 0;
}

static int
test_bulk_performance(struct usbperf_dev *dev, struct usbperf_param_32 *param)
{
	int retval;

	/* simple urb case */
	retval = __test_bulk_performance(dev, param, TEST_BULK_SIMPLE,
					 TEST_CASE_IN | TEST_CASE_OUT | TEST_CASE_IN_OUT);
	if (retval < 0) {
		ERROR(dev, "test_bulk_simple_performance fail (retval=%d)\n", retval);
		return retval;
	}

	/* queue urb case */
	retval = __test_bulk_performance(dev, param, TEST_BULK_QUEUE,
					 TEST_CASE_IN | TEST_CASE_OUT | TEST_CASE_IN_OUT);
	if (retval < 0) {
		ERROR(dev, "test_bulk_queue_performance fail (retval=%d)\n", retval);
		return retval;
	}

	return retval;
}

/* iso transfer */
static int test_iso_transfer(struct usbperf_dev *dev,
			     struct usbperf_param_32 *param, int pipe,
			     struct usb_endpoint_descriptor *desc, unsigned int offset)
{
	struct transfer_context	context;
	struct usb_device	*udev;
	unsigned int		i;
	unsigned long		packets = 0;
	int			status = 0;
	struct urb		**urbs;

	if (!param->sglen || param->iterations > UINT_MAX / param->sglen)
		return -EINVAL;

	if (param->sglen > MAX_SGLEN)
		return -EINVAL;

	urbs = kcalloc(param->sglen, sizeof(*urbs), GFP_KERNEL);
	if (!urbs)
		return -ENOMEM;

	memset(&context, 0, sizeof(context));
	context.count = param->iterations * param->sglen;
	context.dev = dev;
	context.is_iso = !!desc;
	init_completion(&context.done);
	spin_lock_init(&context.lock);

	udev = testdev_to_usbdev(dev);

	for (i = 0; i < param->sglen; i++) {
		urbs[i] = usbperf_iso_alloc_urb(udev, pipe,
						param->length, 0, offset, desc,
						complicated_callback);

		if (!urbs[i]) {
			status = -ENOMEM;
			goto fail;
		}
		packets += urbs[i]->number_of_packets;
		urbs[i]->context = &context;
	}
	packets *= param->iterations;

	if (context.is_iso) {
		int transaction_num;

		if (udev->speed >= USB_SPEED_SUPER)
			transaction_num = ss_isoc_get_packet_num(udev, pipe);
		else
			transaction_num = usb_endpoint_maxp_mult(desc);

		dev_info(&dev->intf->dev,
			 "iso period %d %sframes, wMaxPacket %d, transactions: %d\n",
			 1 << (desc->bInterval - 1),
			 (udev->speed >= USB_SPEED_HIGH) ? "micro" : "",
			 usb_endpoint_maxp(desc),
			 transaction_num);

		dev_info(&dev->intf->dev,
			 "total %lu msec (%lu packets)\n",
			 (packets * (1 << (desc->bInterval - 1)))
				/ ((udev->speed >= USB_SPEED_HIGH) ? 8 : 1),
			 packets);
	}

	spin_lock_irq(&context.lock);
	for (i = 0; i < param->sglen; i++) {
		++context.pending;
		status = usb_submit_urb(urbs[i], GFP_ATOMIC);
		if (status < 0) {
			ERROR(dev, "submit iso[%d], error %d\n", i, status);
			if (i == 0) {
				spin_unlock_irq(&context.lock);
				goto fail;
			}

			usbperf_free_urb(urbs[i]);
			urbs[i] = NULL;
			context.pending--;
			context.submit_error = 1;
			break;
		}
	}
	spin_unlock_irq(&context.lock);

	wait_for_completion(&context.done);

	for (i = 0; i < param->sglen; i++) {
		if (urbs[i])
			usbperf_free_urb(urbs[i]);
	}
	/*
	 * Isochronous transfers are expected to fail sometimes.  As an
	 * arbitrary limit, we will report an error if any submissions
	 * fail or if the transfer failure rate is > 10%.
	 */
	if (status != 0)
		;
	else if (context.submit_error)
		status = -EACCES;
	else if (context.errors >
			(context.is_iso ? context.packet_count / 10 : 0))
		status = -EIO;

	kfree(urbs);
	return status;

fail:
	for (i = 0; i < param->sglen; i++) {
		if (urbs[i])
			usbperf_free_urb(urbs[i]);
	}

	kfree(urbs);
	return status;
}

static u64 test_iso_run(struct usbperf_dev *dev, struct usbperf_param_32 *param,
			int pipe, struct usb_endpoint_descriptor *desc, unsigned int offset)
{
	u64			data_size;
	u64			speed;
	u64			time;
	int			retval = -EINVAL;
	struct timespec64	start;
	struct timespec64	end;
	struct timespec64	duration;

	data_size = param->iterations * param->sglen * param->length;
	ktime_get_ts64(&start);

	retval = test_iso_transfer(dev, param, pipe, desc, 0);
	if (retval < 0) {
		dev_err(&(dev)->intf->dev,
			"TEST fail return (retval=%d)\n", retval);
		return 0;
	}

	ktime_get_ts64(&end);
	duration = timespec64_sub(end, start);
	time = (duration.tv_sec * USEC_PER_SEC) + (duration.tv_nsec / NSEC_PER_USEC);

	speed = (data_size * USEC_PER_SEC);
	do_div(speed, time);

	dev_dbg(&(dev)->intf->dev,
		"TEST %llu bytes time=%dus speed %lld byte/s\n",
		data_size, (int)time, speed);

	return speed;
}

static void test_iso_out_work(struct work_struct *work)
{
	struct pipe_context		*pipe_ctx = container_of(work,
							 struct pipe_context,
							 out_work);
	struct usbperf_dev		*dev = pipe_ctx->tdev;
	struct usbperf_param_32		*param = pipe_ctx->param;
	int				pipe = dev->out_iso_pipe;
	struct usb_endpoint_descriptor	*desc = dev->iso_out;
	u64				speed;
	unsigned long			flags;

	speed = test_iso_run(dev, param, pipe, desc, 0);

	spin_lock_irqsave(&pipe_ctx->lock, flags);

	pipe_ctx->out_speed = speed;
	complete(&pipe_ctx->out_completion);

	spin_unlock_irqrestore(&pipe_ctx->lock, flags);
}

static void test_iso_in_work(struct work_struct *work)
{
	struct pipe_context		*pipe_ctx = container_of(work,
							struct pipe_context, in_work);
	struct usbperf_dev		*dev = pipe_ctx->tdev;
	struct usbperf_param_32		*param = pipe_ctx->param;
	int				pipe = dev->in_iso_pipe;
	struct usb_endpoint_descriptor	*desc = dev->iso_in;
	u64				speed;
	unsigned long			flags;

	speed = test_iso_run(dev, param, pipe, desc, 0);

	spin_lock_irqsave(&pipe_ctx->lock, flags);

	pipe_ctx->in_speed = speed;
	complete(&pipe_ctx->in_completion);

	spin_unlock_irqrestore(&pipe_ctx->lock, flags);
}

static int __test_iso_performance(struct usbperf_dev *dev, struct usbperf_param_32 *param)
{
	u64			in_speed, out_speed;
	struct pipe_context	pipe_ctx;
	unsigned long		expire;

	/* OUT test */
	out_speed = test_iso_run(dev, param, dev->out_iso_pipe,
				 dev->iso_out, 0);

	dev_info(&(dev)->intf->dev,
		 "TEST iso urb for OUT: write speed %lld Byte/s (%lld MB/s) (%lld MiB/s)\n",
		 out_speed, out_speed / (1000 * 1000), out_speed / (1024 * 1024));

	/* IN test */
	in_speed = test_iso_run(dev, param, dev->in_iso_pipe,
				dev->iso_in, 0);

	dev_info(&(dev)->intf->dev,
		 "TEST iso urb for IN: read speed %lld Byte/s (%lld MB/s) (%lld MiB/s)\n",
		 in_speed, in_speed / (1000 * 1000), in_speed / (1024 * 1024));

	/* IN and OUT test */
	pipe_ctx.tdev = dev;
	pipe_ctx.param = param;

	spin_lock_init(&pipe_ctx.lock);
	init_completion(&pipe_ctx.out_completion);
	init_completion(&pipe_ctx.in_completion);

	INIT_WORK(&pipe_ctx.out_work, test_iso_out_work);
	INIT_WORK(&pipe_ctx.in_work, test_iso_in_work);

	schedule_work(&pipe_ctx.in_work);
	schedule_work(&pipe_ctx.out_work);

	expire = msecs_to_jiffies(SIMPLE_IO_TIMEOUT);
	if (!wait_for_completion_timeout(&pipe_ctx.out_completion, expire))
		ERROR(dev, "wait_for_completion_timeout out_completion\n");

	if (!wait_for_completion_timeout(&pipe_ctx.in_completion, expire))
		ERROR(dev, "wait_for_completion_timeout in_completion\n");

	out_speed = pipe_ctx.out_speed;
	in_speed = pipe_ctx.in_speed;

	dev_info(&(dev)->intf->dev,
		 "TEST iso urb for IN/OUT: write speed %lld Byte/s (%lld MB/s) (%lld MiB/s)\n",
		 out_speed, out_speed / (1000 * 1000), out_speed / (1024 * 1024));
	dev_info(&(dev)->intf->dev,
		 "TEST iso urb for IN/OUT: read speed %lld Byte/s (%lld MB/s) (%lld MiB/s)\n",
		 in_speed, in_speed / (1000 * 1000),  in_speed / (1024 * 1024));

	return 0;
}

static int
test_iso_performance(struct usbperf_dev *dev, struct usbperf_param_32 *param)
{
	int retval;

	retval = __test_iso_performance(dev, param);
	if (retval < 0) {
		ERROR(dev, "%s fail (retval=%d)\n", __func__, retval);
		return retval;
	}

	return retval;
}

/* Run tests. */
static int
usbperf_do_ioctl(struct usb_interface *intf, struct usbperf_param_32 *param)
{
	struct usbperf_dev	*dev = usb_get_intfdata(intf);
	struct usb_device	*udev = testdev_to_usbdev(dev);
	int			retval = -EOPNOTSUPP;
	int			alt;
	int test_type = 0;
	int test_case = 0;

	if (param->iterations <= 0)
		return -EINVAL;
	if (param->sglen > MAX_SGLEN)
		return -EINVAL;
	/*
	 * Just a bunch of test cases that every HCD is expected to handle.
	 */
	switch (param->test_num) {
	case 0:
		/* report test case usage */
		dev_info(&intf->dev, "TEST cases list:\n");
		dev_info(&intf->dev, "TEST 1: All Bulk Performance Test\n");
		dev_info(&intf->dev, "TEST 2: Simple Bulk Performance Test\n");
		dev_info(&intf->dev, "TEST 3: Queue Bulk Performance Test\n");
		dev_info(&intf->dev, "TEST 11: All Iso Performance Test\n");

		break;
	/* bulk performance test */
	case 1:
		dev_info(&intf->dev, "TEST 1: Bulk Performance Test\n");

		if (dev->in_pipe == 0 || dev->out_pipe == 0 ||
		    param->iterations == 0 || param->sglen == 0 ||
		    param->length == 0)
			break;

		alt = get_altsetting(dev);
		if (dev->alt != alt) {
			retval = set_altsetting(dev, dev->alt);
			if (retval) {
				dev_err(&intf->dev,
					"set altsetting to %d failed, %d\n",
					dev->alt, retval);
				return retval;
			}
		}

		dev_info(&intf->dev,
			 "TEST: bulk read/write performance test (%d rounds %d Bytes)\n",
			 param->iterations, param->sglen * param->length);
		retval = test_bulk_performance(dev, param);

		break;

	/* bulk performance test simple urb case */
	case 2:
		dev_info(&intf->dev, "TEST 2: Bulk Performance Test: simple urb case\n");
		test_type = TEST_BULK_SIMPLE;
		test_case = TEST_CASE_IN | TEST_CASE_OUT | TEST_CASE_IN_OUT;

		goto run_bulk_test;

	/* bulk performance test queue urb case */
	case 3:
		dev_info(&intf->dev, "TEST 3: Bulk Performance Test: queue urb case\n");
		test_type = TEST_BULK_QUEUE;
		test_case = TEST_CASE_IN | TEST_CASE_OUT | TEST_CASE_IN_OUT;

		goto run_bulk_test;

	/* bulk performance test simple urb OUT case */
	case 4:
		dev_info(&intf->dev, "TEST 4: Bulk Performance Test: simple urb  OUT case\n");
		test_type = TEST_BULK_SIMPLE;
		test_case = TEST_CASE_OUT;

		goto run_bulk_test;

	/* bulk performance test simple urb IN case */
	case 5:
		dev_info(&intf->dev, "TEST 5: Bulk Performance Test: simple urb IN case\n");
		test_type = TEST_BULK_SIMPLE;
		test_case = TEST_CASE_IN;

		goto run_bulk_test;

	/* bulk performance test simple urb IN/OUT case */
	case 6:
		dev_info(&intf->dev, "TEST 6: Bulk Performance Test: simple urb IN/OUT case\n");
		test_type = TEST_BULK_SIMPLE;
		test_case = TEST_CASE_IN_OUT;

		goto run_bulk_test;

	/* bulk performance test queue urb OUT case */
	case 7:
		dev_info(&intf->dev, "TEST 7: Bulk Performance Test: queue urb OUT case\n");
		test_type = TEST_BULK_QUEUE;
		test_case = TEST_CASE_OUT;

		goto run_bulk_test;

	/* bulk performance test queue urb IN case */
	case 8:
		dev_info(&intf->dev, "TEST 8: Bulk Performance Test: queue urb IN case\n");
		test_type = TEST_BULK_QUEUE;
		test_case = TEST_CASE_IN;

		goto run_bulk_test;

	/* bulk performance test queue urb IN/OUT case */
	case 9:
		dev_info(&intf->dev, "TEST 9: Bulk Performance Test: queue urb IN/OUT case\n");
		test_type = TEST_BULK_QUEUE;
		test_case = TEST_CASE_IN_OUT;

run_bulk_test:
		if (dev->in_pipe == 0 || dev->out_pipe == 0 ||
		    param->iterations == 0 || param->sglen == 0 ||
		    param->length == 0)
			break;

		alt = get_altsetting(dev);
		if (dev->alt != alt) {
			retval = set_altsetting(dev, dev->alt);
			if (retval) {
				dev_err(&intf->dev,
					"set altsetting to %d failed, %d\n",
					dev->alt, retval);
				return retval;
			}
		}

		retval = __test_bulk_performance(dev, param, test_type, test_case);
		if (retval < 0) {
			ERROR(dev, "__test_bulk_performance fail (retval=%d)\n", retval);
			return retval;
		}

		break;

	/* iso performance test */
	case 11:
		dev_info(&intf->dev, "TEST 11: Iso Performance Test\n");

		if (dev->in_iso_pipe == 0 || dev->out_iso_pipe == 0 ||
		    param->iterations == 0 || param->sglen == 0 ||
		    param->length == 0)
			break;

#define USB2_ISO_MAX_DATA (1024 * 100)
#define USB3_ISO_MAX_DATA (1024 * 1000)

		if (udev->speed <= USB_SPEED_HIGH && param->length > USB2_ISO_MAX_DATA)
			return -EINVAL;
		else if (param->length > USB3_ISO_MAX_DATA)
			return -EINVAL;

		alt = get_altsetting(dev);
		if (dev->iso_alt != alt) {
			retval = set_altsetting(dev, dev->iso_alt);
			if (retval) {
				dev_err(&intf->dev,
					"set altsetting to %d failed, %d\n",
					dev->iso_alt, retval);
				return retval;
			}
		}

		dev_info(&intf->dev,
			 "TEST: iso read/write performance test (%d rounds %d Bytes)\n",
			 param->iterations, param->sglen * param->length);
		retval = test_iso_performance(dev, param);

		break;
	}
	return retval;
}

/*-------------------------------------------------------------------------*/

/* We only have this one interface to user space, through usbfs.
 * User mode code can scan usbfs to find N different devices (maybe on
 * different busses) to use when testing, and allocate one thread per
 * test.  So discovery is simplified, and we have no device naming issues.
 *
 * Don't use these only as stress/load tests.  Use them along with
 * other USB bus activity:  plugging, unplugging, mousing, mp3 playback,
 * video capture, and so on.  Run different tests at different times, in
 * different sequences.  Nothing here should interact with other devices,
 * except indirectly by consuming USB bandwidth and CPU resources for test
 * threads and request completion.  But the only way to know that for sure
 * is to test when HC queues are in use by many devices.
 *
 * WARNING:  Because usbfs grabs udev->dev.sem before calling this ioctl(),
 * it locks out usbcore in certain code paths.  Notably, if you disconnect
 * the device-under-test, hub_wq will wait block forever waiting for the
 * ioctl to complete ... so that usb_disconnect() can abort the pending
 * urbs and then call usbperf_disconnect().  To abort a test, you're best
 * off just killing the userspace task and waiting for it to exit.
 */

static int
usbperf_ioctl(struct usb_interface *intf, unsigned int code, void *buf)
{
	struct usbperf_dev	*dev = usb_get_intfdata(intf);
	struct usbperf_param_64	*param_64 = buf;
	struct usbperf_param_32	temp;
	struct usbperf_param_32	*param_32 = buf;
	struct timespec64	start;
	struct timespec64	end;
	struct timespec64	duration;
	int			retval = -EOPNOTSUPP;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;

	switch (code) {
	case USBPERF_REQUEST_64:
		temp.test_num = param_64->test_num;
		temp.iterations = param_64->iterations;
		temp.length = param_64->length;
		temp.sglen = param_64->sglen;
		temp.vary = param_64->vary;
		param_32 = &temp;
		break;

	case USBPERF_REQUEST_32:
		break;

	default:
		retval = -EOPNOTSUPP;
		goto free_mutex;
	}

	ktime_get_ts64(&start);

	retval = usbperf_do_ioctl(intf, param_32);
	if (retval < 0)
		goto free_mutex;

	ktime_get_ts64(&end);

	duration = timespec64_sub(end, start);

	temp.duration_sec = duration.tv_sec;
	temp.duration_usec = duration.tv_nsec / NSEC_PER_USEC;

	switch (code) {
	case USBPERF_REQUEST_32:
		param_32->duration_sec = temp.duration_sec;
		param_32->duration_usec = temp.duration_usec;
		break;

	case USBPERF_REQUEST_64:
		param_64->duration_sec = temp.duration_sec;
		param_64->duration_usec = temp.duration_usec;
		break;
	}

free_mutex:
	mutex_unlock(&dev->lock);
	return retval;
}

/*-------------------------------------------------------------------------*/

static int
usbperf_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device	*udev;
	struct usbperf_dev	*dev;
	struct usbperf_info	*info;
	char			*rtest, *wtest;
	char			*irtest, *iwtest;
	char			*intrtest, *intwtest;

	udev = interface_to_usbdev(intf);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	info = (struct usbperf_info *)id->driver_info;
	dev->info = info;
	mutex_init(&dev->lock);

	dev->intf = intf;

	/* cacheline-aligned scratch for i/o */
	dev->buf = kmalloc(TBUF_SIZE, GFP_KERNEL);
	if (!dev->buf) {
		kfree(dev);
		return -ENOMEM;
	}

	/* NOTE this doesn't yet test the handful of difference that are
	 * visible with high speed interrupts:  bigger maxpacket (1K) and
	 * "high bandwidth" modes (up to 3 packets/uframe).
	 */
	rtest = "";
	wtest = "";
	irtest = "";
	iwtest = "";
	intrtest = "";
	intwtest = "";
	if (udev->speed == USB_SPEED_LOW) {
		if (info->ep_in) {
			dev->in_pipe = usb_rcvintpipe(udev, info->ep_in);
			rtest = " intr-in";
		}
		if (info->ep_out) {
			dev->out_pipe = usb_sndintpipe(udev, info->ep_out);
			wtest = " intr-out";
		}
	} else {
		if (info->autoconf) {
			int status;

			status = get_endpoints(dev, intf);
			if (status < 0) {
				WARNING(dev, "couldn't get endpoints, %d\n", status);
				kfree(dev->buf);
				kfree(dev);
				return status;
			}
			/* may find bulk or ISO pipes */
		} else {
			if (info->ep_in)
				dev->in_pipe = usb_rcvbulkpipe(udev, info->ep_in);
			if (info->ep_out)
				dev->out_pipe = usb_sndbulkpipe(udev, info->ep_out);
		}
		if (dev->in_pipe)
			rtest = " bulk-in";
		if (dev->out_pipe)
			wtest = " bulk-out";
		if (dev->in_iso_pipe)
			irtest = " iso-in";
		if (dev->out_iso_pipe)
			iwtest = " iso-out";
		if (dev->in_int_pipe)
			intrtest = " int-in";
		if (dev->out_int_pipe)
			intwtest = " int-out";
	}

	usb_set_intfdata(intf, dev);
	dev_info(&intf->dev, "%s\n", info->name);
	dev_info(&intf->dev, "%s {control%s%s%s%s%s%s%s} tests%s\n",
		 usb_speed_string(udev->speed),
		 info->ctrl_out ? " in/out" : "",
		 rtest, wtest,
		 irtest, iwtest,
		 intrtest, intwtest,
		 info->alt >= 0 ? " (+alt)" : "");
	return 0;
}

static int usbperf_suspend(struct usb_interface *intf, pm_message_t message)
{
	return 0;
}

static int usbperf_resume(struct usb_interface *intf)
{
	return 0;
}

static void usbperf_disconnect(struct usb_interface *intf)
{
	struct usbperf_dev	*dev = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	dev_dbg(&intf->dev, "disconnect\n");
	kfree(dev->buf);
	kfree(dev);
}

/* Basic testing only needs a device that can source or sink bulk traffic.
 * Any device can test control transfers. The peripheral running Linux and
 * 'zero.c' test firmware.
 */
static struct usbperf_info gz_info = {
	.name		= "Linux gadget zero performance testing",
	.autoconf	= 1,
	.ctrl_out	= 1,
	.iso		= 1,
	.intr		= 1,
	.alt		= 0,
};

static const struct usb_device_id id_table[] = {
	/* "Gadget Zero" firmware runs under Linux for performance testing */
	{ USB_DEVICE(0x0525, 0xa4a1),
		.driver_info = (unsigned long)&gz_info,
	},
	{ }
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver usbperf_driver = {
	.name =		"usbperf",
	.id_table =	id_table,
	.probe =	usbperf_probe,
	.unlocked_ioctl = usbperf_ioctl,
	.disconnect =	usbperf_disconnect,
	.suspend =	usbperf_suspend,
	.resume =	usbperf_resume,
};

/*-------------------------------------------------------------------------*/

static int __init usbperf_init(void)
{
	return usb_register(&usbperf_driver);
}
module_init(usbperf_init);

static void __exit usbperf_exit(void)
{
	usb_deregister(&usbperf_driver);
}
module_exit(usbperf_exit);

MODULE_DESCRIPTION("USB Core/HCD Performance Testing Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stanley Chang <stanley_chang@realtek.com>");
