/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DMABUF Heaps helper code
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019 Linaro Ltd.
 */

#ifndef _HEAP_HELPERS_H
#define _HEAP_HELPERS_H

#include <linux/dma-heap.h>
#include <linux/list.h>

/**
 * struct dma_heap_buffer - metadata for a particular buffer
 * @heap:>------>-------back pointer to the heap the buffer came from
 * @dmabuf:>---->-------backing dma-buf for this buffer
 * @size:>------>-------size of the buffer
 * @flags:>----->-------buffer specific flags
 */
struct dma_heap_buffer {
	struct dma_heap *heap;
	struct dma_buf *dmabuf;
	size_t size;
	unsigned long flags;
};



struct heap_helper_buffer {
	struct dma_heap_buffer heap_buffer;

	unsigned long private_flags;
	void *priv_virt;
	struct mutex lock;
	int kmap_cnt;
	void *vaddr;
	struct sg_table *sg_table;
	struct list_head attachments;

	void (*free)(struct heap_helper_buffer *buffer);
	bool uncached;
};

#define to_helper_buffer(x) \
	container_of(x, struct heap_helper_buffer, heap_buffer)

static inline void INIT_HEAP_HELPER_BUFFER(struct heap_helper_buffer *buffer,
				 void (*free)(struct heap_helper_buffer *))
{
	buffer->private_flags = 0;
	buffer->priv_virt = NULL;
	mutex_init(&buffer->lock);
	buffer->kmap_cnt = 0;
	buffer->vaddr = NULL;
	buffer->sg_table = NULL;
	INIT_LIST_HEAD(&buffer->attachments);
	buffer->free = free;
}

extern const struct dma_buf_ops heap_helper_ops;

#endif /* _HEAP_HELPERS_H */
