
// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sync File validation framework and debug information
 *
 * Copyright (C) 2012 Google, Inc.
 */

#ifndef _RTK_SW_SYNC_H
#define _RTK_SW_SYNC_H

#include <linux/dma-fence.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/sync_file.h>

#include <uapi/linux/sync_file.h>

/**
 * struct rtk_sync_timeline - sync object
 * @kref:		reference count on fence.
 * @name:		name of the sync_timeline. Useful for debugging
 * @lock:		lock protecting @pt_list and @value
 * @pt_tree:		rbtree of active (unsignaled/errored) sync_pts
 * @pt_list:		list of active (unsignaled/errored) sync_pts
 * @sync_timeline_list:	membership in global sync_timeline_list
 */
struct rtk_sync_timeline {
	struct kref		kref;
	char			name[32];

	/* protected by lock */
	u64			context;
	int			value;

	struct rb_root		pt_tree;
	struct list_head	pt_list;
	spinlock_t		lock;

	struct list_head	sync_timeline_list;
};

static inline struct rtk_sync_timeline *rtk_dma_fence_parent(struct dma_fence *fence)
{
	return container_of(fence->lock, struct rtk_sync_timeline, lock);
}

/**
 * struct sync_pt - sync_pt object
 * @base: base fence object
 * @link: link on the sync timeline's list
 * @node: node in the sync timeline's tree
 */
struct rtk_sync_pt {
	struct dma_fence base;
	struct list_head link;
	struct rb_node node;
};

#endif /* _RTK_SW_SYNC_H */
