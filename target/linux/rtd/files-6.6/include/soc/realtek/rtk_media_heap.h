/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DMABUF Heaps Allocation Infrastructure
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019 Linaro Ltd.
 */

#ifndef _RTK_MEDIA_HEAPS_H
#define _RTK_MEDIA_HEAPS_H

#include <linux/dma-heap.h>
#include <linux/sched.h>


struct rtk_heap {
	struct dma_heap *heap;
	struct dma_heap *uncached_heap;
	struct dma_heap_ops *ops;
	struct dma_heap_ops *uncached_ops;

	struct cma *cma;
	int nbits;

	struct gen_pool *gen_pool;
	u32 flag;
	struct mutex mutex;
	unsigned long *alloc_bitmap;
	unsigned long *use_bitmap;

	struct list_head hlist;
	struct list_head list;		/* protect_info list */
	struct list_head plist;		/* pre-allocate list */
	struct list_head elist;		/* protect_ext_info list */
	struct device_node *node;
	struct list_head task_list;
};

struct rtk_heap_task {
	char comm[TASK_COMM_LEN];
	size_t size;
	struct list_head list;
	struct list_head alloc_list;
};

struct rtk_alloc{
	unsigned long start;
	unsigned long end;
	char ext_task[TASK_COMM_LEN];
	struct list_head list;
};

struct rtk_best_fit {
	void *data;
	int tag;
	const char *name;
	int score;
	size_t freed_pages;
	struct list_head hlist;
};

extern struct list_head cheap_list;
extern struct list_head gheap_list;
extern struct list_head pheap_list;
extern const struct dma_map_ops rheap_dma_ops;

extern struct dma_buf *rheap_alloc(char *name, unsigned long len,
					 unsigned long flags);

extern void rheap_setup_dma_pools(struct device *dev, char *name,
				 unsigned long heap_flags, const char *caller);

extern void fill_best_fit_list(char *name, unsigned long flags,
					 struct list_head *best_list);

extern void rheap_debugfs_init(void);
extern int rheap_miscdev_register(void);

#define list_entry_is_head(pos, head, member) (&pos->member == (head))

#endif /* _RTK_MEDIA_HEAPS_H */
