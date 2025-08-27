// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sync File validation framework
 *
 * Copyright (C) 2012 Google, Inc.
 * Copyright (C) 2023 Realtek Semiconductor Corp.
 */

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/sync_file.h>
#include <linux/uaccess.h>
#include <linux/module.h>

#include "rtk_sw_sync.h"

#define CREATE_TRACE_POINTS
#include "rtk_sync_trace.h"

struct miscdevice *rtk_sw_sync_dev;

/*
 * SW SYNC validation framework
 *
 * A sync object driver that uses a 32bit counter to coordinate
 * synchronization.  Useful when there is no hardware primitive backing
 * the synchronization.
 *
 * That will create a sync timeline, all fences created under this timeline
 * file descriptor will belong to the this timeline.
 *
 * The 'rtk_sw_sync' file can be opened many times as to create different
 * timelines.
 *
 * Fences can be created with SW_SYNC_IOC_CREATE_FENCE ioctl with struct
 * rtk_sw_sync_create_fence_data as parameter.
 *
 * To increment the timeline counter, SW_SYNC_IOC_INC ioctl should be used
 * with the increment as u32. This will update the last signaled value
 * from the timeline and signal any fence that has a seqno smaller or equal
 * to it.
 *
 * struct rtk_sw_sync_create_fence_data
 * @value:	the seqno to initialise the fence with
 * @name:	the name of the new sync point
 * @fence:	return the fd of the new sync_file with the created fence
 */
struct rtk_sw_sync_create_fence_data {
	__u32	value;
	char	name[32];
	__s32	fence; /* fd of new fence */
};

#define SW_SYNC_IOC_MAGIC	'W'

#define SW_SYNC_IOC_CREATE_FENCE	_IOWR(SW_SYNC_IOC_MAGIC, 0,\
		struct rtk_sw_sync_create_fence_data)

#define SW_SYNC_IOC_INC			_IOW(SW_SYNC_IOC_MAGIC, 1, __u32)

static const struct dma_fence_ops rtk_timeline_fence_ops;

static inline struct rtk_sync_pt *rtk_dma_fence_to_sync_pt(struct dma_fence *fence)
{
	if (fence->ops != &rtk_timeline_fence_ops)
		return NULL;
	return container_of(fence, struct rtk_sync_pt, base);
}

/**
 * rtk_sync_timeline_create() - creates a sync object
 * @name:	rtk_sync_timeline name
 *
 * Creates a new rtk_sync_timeline. Returns the rtk_sync_timeline object or NULL in
 * case of error.
 */
struct rtk_sync_timeline *rtk_sync_timeline_create(const char *name)
{
	struct rtk_sync_timeline *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return NULL;

	kref_init(&obj->kref);
	obj->context = dma_fence_context_alloc(1);
	strlcpy(obj->name, name, sizeof(obj->name));

	obj->pt_tree = RB_ROOT;
	INIT_LIST_HEAD(&obj->pt_list);
	spin_lock_init(&obj->lock);

	return obj;
}
EXPORT_SYMBOL(rtk_sync_timeline_create);

static void rtk_sync_timeline_free(struct kref *kref)
{
	struct rtk_sync_timeline *obj =
		container_of(kref, struct rtk_sync_timeline, kref);

	kfree(obj);
}

static void rtk_sync_timeline_get(struct rtk_sync_timeline *obj)
{
	kref_get(&obj->kref);
}

void rtk_sync_timeline_put(struct rtk_sync_timeline *obj)
{
	kref_put(&obj->kref, rtk_sync_timeline_free);
}
EXPORT_SYMBOL(rtk_sync_timeline_put);

static const char *rtk_timeline_fence_get_driver_name(struct dma_fence *fence)
{
	return "sw_sync";
}

static const char *rtk_timeline_fence_get_timeline_name(struct dma_fence *fence)
{
	struct rtk_sync_timeline *parent = rtk_dma_fence_parent(fence);

	return parent->name;
}

static void rtk_timeline_fence_release(struct dma_fence *fence)
{
	struct rtk_sync_pt *pt = rtk_dma_fence_to_sync_pt(fence);
	struct rtk_sync_timeline *parent = rtk_dma_fence_parent(fence);
	unsigned long flags;

	spin_lock_irqsave(fence->lock, flags);
	if (!list_empty(&pt->link)) {
		list_del(&pt->link);
		rb_erase(&pt->node, &parent->pt_tree);
	}
	spin_unlock_irqrestore(fence->lock, flags);

	rtk_sync_timeline_put(parent);
	dma_fence_free(fence);
}

static bool rtk_timeline_fence_signaled(struct dma_fence *fence)
{
	struct rtk_sync_timeline *parent = rtk_dma_fence_parent(fence);

	return !__dma_fence_is_later(fence->seqno, parent->value, fence->ops);
}

static bool rtk_timeline_fence_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static void rtk_timeline_fence_value_str(struct dma_fence *fence,
				    char *str, int size)
{
	snprintf(str, size, "%lld", fence->seqno);
}

static void rtk_timeline_fence_timeline_value_str(struct dma_fence *fence,
					     char *str, int size)
{
	struct rtk_sync_timeline *parent = rtk_dma_fence_parent(fence);

	snprintf(str, size, "%d", parent->value);
}

static const struct dma_fence_ops rtk_timeline_fence_ops = {
	.get_driver_name = rtk_timeline_fence_get_driver_name,
	.get_timeline_name = rtk_timeline_fence_get_timeline_name,
	.enable_signaling = rtk_timeline_fence_enable_signaling,
	.signaled = rtk_timeline_fence_signaled,
	.release = rtk_timeline_fence_release,
	.fence_value_str = rtk_timeline_fence_value_str,
	.timeline_value_str = rtk_timeline_fence_timeline_value_str,
};

/**
 * rtk_sync_timeline_signal() - signal a status change on a sync_timeline
 * @obj:	rtk_sync_timeline to signal
 * @inc:	num to increment on timeline->value
 *
 * A sync implementation should call this any time one of it's fences
 * has signaled or has an error condition.
 */
void rtk_sync_timeline_signal(struct rtk_sync_timeline *obj, unsigned int inc)
{
	struct rtk_sync_pt *pt, *next;

	trace_rtk_sync_timeline(obj);

	spin_lock_irq(&obj->lock);

	obj->value += inc;

	list_for_each_entry_safe(pt, next, &obj->pt_list, link) {
		if (!rtk_timeline_fence_signaled(&pt->base))
			break;

		list_del_init(&pt->link);
		rb_erase(&pt->node, &obj->pt_tree);

		/*
		 * A signal callback may release the last reference to this
		 * fence, causing it to be freed. That operation has to be
		 * last to avoid a use after free inside this loop, and must
		 * be after we remove the fence from the timeline in order to
		 * prevent deadlocking on timeline->lock inside
		 * rtk_timeline_fence_release().
		 */
		dma_fence_signal_locked(&pt->base);
	}

	spin_unlock_irq(&obj->lock);
}
EXPORT_SYMBOL(rtk_sync_timeline_signal);

/**
 * rtk_sync_pt_create() - creates a sync pt
 * @obj:	parent rtk_sync_timeline
 * @value:	value of the fence
 *
 * Creates a new rtk_sync_pt (fence) as a child of @parent.  @size bytes will be
 * allocated allowing for implementation specific data to be kept after
 * the generic rtk_sync_timeline struct. Returns the rtk_sync_pt object or
 * NULL in case of error.
 */
struct rtk_sync_pt *rtk_sync_pt_create(struct rtk_sync_timeline *obj,
				      unsigned int value)
{
	struct rtk_sync_pt *pt;

	pt = kzalloc(sizeof(*pt), GFP_KERNEL);
	if (!pt)
		return NULL;

	rtk_sync_timeline_get(obj);
	dma_fence_init(&pt->base, &rtk_timeline_fence_ops, &obj->lock,
		       obj->context, value);
	INIT_LIST_HEAD(&pt->link);

	spin_lock_irq(&obj->lock);
	if (!dma_fence_is_signaled_locked(&pt->base)) {
		struct rb_node **p = &obj->pt_tree.rb_node;
		struct rb_node *parent = NULL;

		while (*p) {
			struct rtk_sync_pt *other;
			int cmp;

			parent = *p;
			other = rb_entry(parent, typeof(*pt), node);
			cmp = value - other->base.seqno;
			if (cmp > 0) {
				p = &parent->rb_right;
			} else if (cmp < 0) {
				p = &parent->rb_left;
			} else {
				if (dma_fence_get_rcu(&other->base)) {
					rtk_sync_timeline_put(obj);
					kfree(pt);
					pt = other;
					goto unlock;
				}
				p = &parent->rb_left;
			}
		}
		rb_link_node(&pt->node, parent, p);
		rb_insert_color(&pt->node, &obj->pt_tree);

		parent = rb_next(&pt->node);
		list_add_tail(&pt->link,
			      parent ? &rb_entry(parent, typeof(*pt), node)->link : &obj->pt_list);
	}
unlock:
	spin_unlock_irq(&obj->lock);

	return pt;
}
EXPORT_SYMBOL(rtk_sync_pt_create);

/*
 * *WARNING*
 *
 * improper use of this can result in deadlocking kernel drivers from userspace.
 */

/* opening sw_sync create a new sync obj */
static int rtk_sw_sync_open(struct inode *inode, struct file *file)
{
	struct rtk_sync_timeline *obj;
	char task_comm[TASK_COMM_LEN];

	get_task_comm(task_comm, current);

	obj = rtk_sync_timeline_create(task_comm);
	if (!obj)
		return -ENOMEM;

	file->private_data = obj;

	return 0;
}

static int rtk_sw_sync_release(struct inode *inode, struct file *file)
{
	struct rtk_sync_timeline *obj = file->private_data;
	struct rtk_sync_pt *pt, *next;

	spin_lock_irq(&obj->lock);

	list_for_each_entry_safe(pt, next, &obj->pt_list, link) {
		dma_fence_set_error(&pt->base, -ENOENT);
		dma_fence_signal_locked(&pt->base);
	}

	spin_unlock_irq(&obj->lock);

	rtk_sync_timeline_put(obj);
	return 0;
}

static long rtk_sw_sync_ioctl_create_fence(struct rtk_sync_timeline *obj,
				       unsigned long arg)
{
	int fd = get_unused_fd_flags(O_CLOEXEC);
	int err;
	struct rtk_sync_pt *pt;
	struct sync_file *sync_file;
	struct rtk_sw_sync_create_fence_data data;

	if (fd < 0)
		return fd;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		err = -EFAULT;
		goto err;
	}

	pt = rtk_sync_pt_create(obj, data.value);
	if (!pt) {
		err = -ENOMEM;
		goto err;
	}

	sync_file = sync_file_create(&pt->base);
	dma_fence_put(&pt->base);
	if (!sync_file) {
		err = -ENOMEM;
		goto err;
	}

	data.fence = fd;
	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		fput(sync_file->file);
		err = -EFAULT;
		goto err;
	}

	fd_install(fd, sync_file->file);

	return 0;

err:
	put_unused_fd(fd);
	return err;
}

static long rtk_sw_sync_ioctl_inc(struct rtk_sync_timeline *obj, unsigned long arg)
{
	u32 value;

	if (copy_from_user(&value, (void __user *)arg, sizeof(value)))
		return -EFAULT;

	while (value > INT_MAX)  {
		rtk_sync_timeline_signal(obj, INT_MAX);
		value -= INT_MAX;
	}

	rtk_sync_timeline_signal(obj, value);

	return 0;
}

static long rtk_sw_sync_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct rtk_sync_timeline *obj = file->private_data;

	switch (cmd) {
	case SW_SYNC_IOC_CREATE_FENCE:
		return rtk_sw_sync_ioctl_create_fence(obj, arg);

	case SW_SYNC_IOC_INC:
		return rtk_sw_sync_ioctl_inc(obj, arg);

	default:
		return -ENOTTY;
	}
}

static const struct file_operations rtk_sw_sync_fops = {
	.owner		= THIS_MODULE,
	.open		= rtk_sw_sync_open,
	.release	= rtk_sw_sync_release,
	.unlocked_ioctl = rtk_sw_sync_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};

static int __init rtk_sw_sync_init(void)
{
	int ret;

	rtk_sw_sync_dev = kzalloc(sizeof(*rtk_sw_sync_dev), GFP_KERNEL);
	if (!rtk_sw_sync_dev)
		return -ENOMEM;

	rtk_sw_sync_dev->minor = MISC_DYNAMIC_MINOR;
	rtk_sw_sync_dev->name = "rtk_sw_sync";
	rtk_sw_sync_dev->fops = &rtk_sw_sync_fops;
	rtk_sw_sync_dev->parent = NULL;

	ret = misc_register(rtk_sw_sync_dev);
	if (ret) {
		pr_err("rtk_sw_sync: failed to register misc device.\n");
		kfree(rtk_sw_sync_dev);
		return ret;
	}

	return 0;
}
module_init(rtk_sw_sync_init);

static void __exit rtk_sw_sync_exit(void)
{
	misc_deregister(rtk_sw_sync_dev);
}
module_exit(rtk_sw_sync_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("james.tai@realtek.com>");
