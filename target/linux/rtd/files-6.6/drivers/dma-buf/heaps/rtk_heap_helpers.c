// SPDX-License-Identifier: GPL-2.0
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <uapi/linux/dma-heap.h>
#include <soc/realtek/memory.h>

#include "rtk_heap_helpers.h"

static void *dma_heap_map_kernel(struct heap_helper_buffer *buffer)
{
	struct scatterlist *sg;
	int i, j;
	void *vaddr;
	pgprot_t pgprot;
	struct sg_table *table = buffer->sg_table;
	int npages = PAGE_ALIGN(buffer->heap_buffer.size) / PAGE_SIZE;
	struct page **pages = vmalloc(array_size(npages,
						 sizeof(struct page *)));
	struct page **tmp = pages;

	if (!pages)
		return ERR_PTR(-ENOMEM);

	pgprot = PAGE_KERNEL;

	for_each_sg(table->sgl, sg, table->nents, i) {
		int npages_this_entry = PAGE_ALIGN(sg->length) / PAGE_SIZE;
		struct page *page = sg_page(sg);

		WARN_ON(i >= npages);
		for (j = 0; j < npages_this_entry; j++)
			*(tmp++) = page++;
	}

	if (buffer->uncached)
		pgprot = pgprot_writecombine(pgprot);

	vaddr = vmap(pages, npages, VM_MAP, pgprot);
	vfree(pages);

	if (!vaddr)
		return ERR_PTR(-ENOMEM);

	return vaddr;
}

static int dma_heap_map_user(struct heap_helper_buffer *buffer,
			 struct vm_area_struct *vma)
{
	struct sg_table *table = buffer->sg_table;
	unsigned long addr = vma->vm_start;
	unsigned long offset = vma->vm_pgoff * PAGE_SIZE;
	struct scatterlist *sg;
	struct dma_heap_buffer *heap_buffer = &buffer->heap_buffer;
	int i;
	int ret;

	if (!(heap_buffer->flags & RTK_FLAG_SCPUACC) ||
			(heap_buffer->flags & RTK_FLAG_PROTECTED_MASK) )
			vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	else if (buffer->uncached)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);


	for_each_sg(table->sgl, sg, table->nents, i) {
		struct page *page = sg_page(sg);
		unsigned long remainder = vma->vm_end - addr;
		unsigned long len = sg->length;

		if (offset >= sg->length) {
			offset -= sg->length;
			continue;
		} else if (offset) {
			page += offset / PAGE_SIZE;
			len = sg->length - offset;
			offset = 0;
		}
		len = min(len, remainder);
		ret = remap_pfn_range(vma, addr, page_to_pfn(page), len,
				      vma->vm_page_prot);
		if (ret)
			return ret;
		addr += len;
		if (addr >= vma->vm_end)
			return 0;
	}

	return 0;
}


void dma_heap_buffer_destroy(struct dma_heap_buffer *heap_buffer)
{
	struct heap_helper_buffer *buffer = to_helper_buffer(heap_buffer);

	if (buffer->kmap_cnt > 0) {
		pr_warn_once("%s: buffer still mapped in the kernel\n",
			     __func__);
		vunmap(buffer->vaddr);
	}

	buffer->free(buffer);
}

static void *dma_heap_buffer_kmap_get(struct dma_heap_buffer *heap_buffer)
{
	struct heap_helper_buffer *buffer = to_helper_buffer(heap_buffer);
	void *vaddr;

	if (buffer->kmap_cnt) {
		buffer->kmap_cnt++;
		return buffer->vaddr;
	}
	vaddr = dma_heap_map_kernel(buffer);
	if (WARN_ONCE(!vaddr,
		      "heap->ops->map_kernel should return ERR_PTR on error"))
		return ERR_PTR(-EINVAL);
	if (IS_ERR_OR_NULL(vaddr))
		return vaddr;
	buffer->vaddr = vaddr;
	buffer->kmap_cnt++;
	return vaddr;
}

static void dma_heap_buffer_kmap_put(struct dma_heap_buffer *heap_buffer)
{
	struct heap_helper_buffer *buffer = to_helper_buffer(heap_buffer);

	buffer->kmap_cnt--;
	if (!buffer->kmap_cnt) {
		vunmap(buffer->vaddr);
		buffer->vaddr = NULL;
	}
}

static struct sg_table *dup_sg_table(struct sg_table *table)
{
	struct sg_table *new_table;
	int ret, i;
	struct scatterlist *sg, *new_sg;

	new_table = kzalloc(sizeof(*new_table), GFP_KERNEL);
	if (!new_table)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(new_table, table->nents, GFP_KERNEL);
	if (ret) {
		kfree(new_table);
		return ERR_PTR(-ENOMEM);
	}

	new_sg = new_table->sgl;
	for_each_sg(table->sgl, sg, table->nents, i) {
		memcpy(new_sg, sg, sizeof(*sg));
		new_sg->dma_address = 0;
		new_sg = sg_next(new_sg);
	}

	return new_table;
}

static void free_duped_table(struct sg_table *table)
{
	sg_free_table(table);
	kfree(table);
}

struct dma_heaps_attachment {
	struct device *dev;
	struct sg_table *table;
	struct list_head list;
};

static int dma_heap_attach(struct dma_buf *dmabuf,
			      struct dma_buf_attachment *attachment)
{
	struct dma_heaps_attachment *a;
	struct sg_table *table;
	struct dma_heap_buffer *heap_buffer = dmabuf->priv;
	struct heap_helper_buffer *buffer = to_helper_buffer(heap_buffer);

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	table = dup_sg_table(buffer->sg_table);
	if (IS_ERR_OR_NULL(table)) {
		kfree(a);
		return -ENOMEM;
	}

	a->table = table;
	a->dev = attachment->dev;
	INIT_LIST_HEAD(&a->list);

	attachment->priv = a;

	mutex_lock(&buffer->lock);
	list_add(&a->list, &buffer->attachments);
	mutex_unlock(&buffer->lock);

	return 0;
}

static void dma_heap_detatch(struct dma_buf *dmabuf,
				struct dma_buf_attachment *attachment)
{
	struct dma_heaps_attachment *a = attachment->priv;
	struct dma_heap_buffer *heap_buffer = dmabuf->priv;
	struct heap_helper_buffer *buffer = to_helper_buffer(heap_buffer);

	mutex_lock(&buffer->lock);
	list_del(&a->list);
	mutex_unlock(&buffer->lock);
	free_duped_table(a->table);

	kfree(a);
}

static struct sg_table *dma_heap_map_dma_buf(
					struct dma_buf_attachment *attachment,
					enum dma_data_direction direction)
{
	struct dma_heaps_attachment *a = attachment->priv;
	struct sg_table *table;
	struct dma_buf *dmabuf = attachment->dmabuf;
	struct dma_heap_buffer *heap_buffer = dmabuf->priv;

	table = a->table;

	if (!(heap_buffer->flags & RTK_FLAG_SCPUACC) ||
			 (heap_buffer->flags & RTK_FLAG_PROTECTED_MASK) ) {
		if (!dma_map_sg_attrs(attachment->dev, table->sgl, table->nents,
				direction, DMA_ATTR_SKIP_CPU_SYNC))
			table = ERR_PTR(-ENOMEM);
		return table;
	}

	if (!dma_map_sg(attachment->dev, table->sgl, table->nents,
			direction))
		table = ERR_PTR(-ENOMEM);
	return table;
}

static void dma_heap_unmap_dma_buf(struct dma_buf_attachment *attachment,
			      struct sg_table *table,
			      enum dma_data_direction direction)
{
	struct dma_buf *dmabuf = attachment->dmabuf;
	struct dma_heap_buffer *heap_buffer = dmabuf->priv;

	if (!(heap_buffer->flags & RTK_FLAG_SCPUACC) ||
			 (heap_buffer->flags & RTK_FLAG_PROTECTED_MASK) )
		dma_unmap_sg_attrs(attachment->dev, table->sgl, table->nents,
				 direction, DMA_ATTR_SKIP_CPU_SYNC);
	else
		dma_unmap_sg(attachment->dev, table->sgl, table->nents,
				 direction);
}

static int dma_heap_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct dma_heap_buffer *heap_buffer = dmabuf->priv;
	struct heap_helper_buffer *buffer = to_helper_buffer(heap_buffer);
	int ret = 0;

	mutex_lock(&buffer->lock);
	/* now map it to userspace */
	ret = dma_heap_map_user(buffer, vma);
	mutex_unlock(&buffer->lock);

	if (ret)
		pr_err("%s: failure mapping buffer to userspace\n",
		       __func__);

	return ret;
}

static void dma_heap_dma_buf_release(struct dma_buf *dmabuf)
{
	struct dma_heap_buffer *buffer = dmabuf->priv;

	dma_heap_buffer_destroy(buffer);
}

static int dma_heap_dma_buf_kmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct dma_heap_buffer *heap_buffer = dmabuf->priv;
	struct heap_helper_buffer *buffer = to_helper_buffer(heap_buffer);

	map->vaddr = buffer->vaddr;
	map->is_iomem = false;

	return 0;
}

static void dma_heap_dma_buf_kunmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
}

static int dma_heap_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
					enum dma_data_direction direction)
{
	struct dma_heap_buffer *heap_buffer = dmabuf->priv;
	struct heap_helper_buffer *buffer = to_helper_buffer(heap_buffer);
	void *vaddr;
	struct dma_heaps_attachment *a;
	int ret = 0;

	mutex_lock(&buffer->lock);
	vaddr = dma_heap_buffer_kmap_get(heap_buffer);
	if (IS_ERR_OR_NULL(vaddr)) {
		ret = PTR_ERR(vaddr);
		goto unlock;
	}

	if (!(heap_buffer->flags & RTK_FLAG_SCPUACC)
		|| (heap_buffer->flags & RTK_FLAG_PROTECTED_MASK)
		|| (buffer->uncached))
		goto unlock;

	if (!buffer->uncached) {
		list_for_each_entry(a, &buffer->attachments, list) {
			dma_sync_sg_for_cpu(a->dev, a->table->sgl,
				a->table->nents, direction);
		}
	}
unlock:
	mutex_unlock(&buffer->lock);
	return ret;
}

static int dma_heap_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
				      enum dma_data_direction direction)
{
	struct dma_heap_buffer *heap_buffer = dmabuf->priv;
	struct heap_helper_buffer *buffer = to_helper_buffer(heap_buffer);
	struct dma_heaps_attachment *a;

	mutex_lock(&buffer->lock);
	dma_heap_buffer_kmap_put(heap_buffer);

	if (!(heap_buffer->flags & RTK_FLAG_SCPUACC)
		|| (heap_buffer->flags & RTK_FLAG_PROTECTED_MASK)
		|| (buffer->uncached))
		goto unlock;

	if (!buffer->uncached) {
		list_for_each_entry(a, &buffer->attachments, list) {
			dma_sync_sg_for_device(a->dev, a->table->sgl,
					 a->table->nents, direction);
		}
	}
unlock:
	mutex_unlock(&buffer->lock);

	return 0;
}

const struct dma_buf_ops heap_helper_ops = {
	.cache_sgt_mapping = true,
	.map_dma_buf = dma_heap_map_dma_buf,
	.unmap_dma_buf = dma_heap_unmap_dma_buf,
	.mmap = dma_heap_mmap,
	.release = dma_heap_dma_buf_release,
	.attach = dma_heap_attach,
	.detach = dma_heap_detatch,
	.begin_cpu_access = dma_heap_dma_buf_begin_cpu_access,
	.end_cpu_access = dma_heap_dma_buf_end_cpu_access,
	.vmap = dma_heap_dma_buf_kmap,
	.vunmap = dma_heap_dma_buf_kunmap,
};
EXPORT_SYMBOL(heap_helper_ops);
