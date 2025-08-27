// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF RTK heap exporter
 *
 * Copyright (c) 2022 Realtek Semiconductor Corp
 * Author: <cy.huang@realtek.com> .
 */

#include <linux/cma.h>
#include <linux/device.h>
#include <linux/device/class.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/dma-map-ops.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fdtable.h>
#include <linux/genalloc.h>
#include <linux/highmem.h>
#include <linux/kstrtox.h>
#include <linux/list_sort.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/syscalls.h>

#include <soc/realtek/memory.h>
#include <soc/realtek/rtk_media_heap.h>
#include "rtk_protect.h"

#ifdef CONFIG_ANDROID_VENDOR_HOOKS
#include <trace/hooks/dmabuf.h>
#endif

#include "cma.h"
#include "rtk_heap_helpers.h"

#define DEVNAME "rtk_media_heap"
#define TMP_BUF_MAX 256

unsigned long sys_flags;

struct rtk_flag_replace {
	unsigned long condition;
	unsigned long replace;
};

struct rheap_desc {
	int (*rheap_miscdev_init)(void);
	int (*rheap_sysfs_init)(struct platform_device *dev);
	void (*rheap_debugfs_init)(void);
	int android_hook_init;
};

static struct rtk_flag_replace rtk_flag_match[] = {

	{ .condition = RTK_FLAG_PROTECTED_V2_VIDEO_POOL,
	  .replace = RTK_FLAG_PROTECTED_V2_VIDEO_POOL |
			 RTK_FLAG_PROTECTED_EXT_BITS(1) },

	{ .condition = RTK_FLAG_PROTECTED_V2_VO_POOL,
	  .replace = RTK_FLAG_PROTECTED_V2_VO_POOL |
			 RTK_FLAG_PROTECTED_EXT_BITS(1) },

	{ .condition = 0,
	  .replace = 0 },
};

enum tag_type {
	tag_pre = 0,
	tag_gen,
	tag_cma,
};

struct rtk_heap_data {
	char *name;
	bool skip_set_protect;
	unsigned long flags;
	char *devname;
	unsigned long static_cma_size;
	void *priv;
};


static struct rtk_heap_data rheap_data[] = {
	{
		.name = "metadata",
		.skip_set_protect = true,
		.flags = (RTK_FLAG_HWIPACC |
			RTK_FLAG_VCPU_FWACC | RTK_FLAG_ACPUACC |
			RTK_FLAG_PROTECTED_V2_METADATA_POOL),
		.devname = "metadata_non-sc-ve_secure7",
	},

	{
		.name = "vo_dsc3",
		.flags = (RTK_FLAG_SCPUACC |
			RTK_FLAG_HWIPACC | RTK_FLAG_CMA | RTK_FLAG_VCPU_FWACC |
			RTK_FLAG_ACPUACC | RTK_FLAG_PROTECTED_DYNAMIC |
			RTK_FLAG_PROTECTED_V2_VO_POOL |
			RTK_FLAG_SUPPORT_NONCACHED |
			RTK_FLAG_SKIP_ZERO),
		.devname = "vo_non-ve",
	},

	{
		.name = "tp_ssc2",
		.flags = (RTK_FLAG_SCPUACC |
			RTK_FLAG_HWIPACC | RTK_FLAG_CMA |
			RTK_FLAG_PROTECTED_V2_TP_POOL),
		.devname = "tp_non-vc-ac-ve_secure2",
	},

	{
		.name = "audio_ssc1",
		.flags = (RTK_FLAG_SCPUACC |
			RTK_FLAG_HWIPACC | RTK_FLAG_CMA | RTK_FLAG_VCPU_FWACC |
			RTK_FLAG_ACPUACC | RTK_FLAG_PROTECTED_DYNAMIC |
			RTK_FLAG_PROTECTED_V2_AUDIO_POOL),
		.devname = "audio_u1p5_non-ve_secure1",
	},

	{
		.name = "video_dsc5",
		.flags = (RTK_FLAG_SCPUACC |
			RTK_FLAG_HWIPACC | RTK_FLAG_CMA | RTK_FLAG_VCPU_FWACC |
			RTK_FLAG_ACPUACC | RTK_FLAG_PROTECTED_DYNAMIC |
			RTK_FLAG_PROTECTED_V2_VIDEO_POOL |
			RTK_FLAG_SUPPORT_NONCACHED|
			RTK_FLAG_SKIP_ZERO),
		.devname = "video_u1p5_non-ve_secure5",
	},

	{
		.name = "ao_ssc6",
		.flags = (RTK_FLAG_SCPUACC |
			RTK_FLAG_HWIPACC | RTK_FLAG_CMA | RTK_FLAG_VCPU_FWACC |
			RTK_FLAG_ACPUACC | RTK_FLAG_PROTECTED_DYNAMIC |
			RTK_FLAG_PROTECTED_V2_AO_POOL) ,
		.devname = "ao_u1p5_non-ve_secure6",
	},

	{
		.name = "ota_dsc8",
		.flags = (RTK_FLAG_SCPUACC |
			RTK_FLAG_HWIPACC | RTK_FLAG_CMA | RTK_FLAG_VCPU_FWACC |
			RTK_FLAG_ACPUACC | RTK_FLAG_PROTECTED_DYNAMIC |
			RTK_FLAG_PROTECTED_V2_OTA_POOL |
			RTK_FLAG_SUPPORT_NONCACHED |
			RTK_FLAG_SKIP_ZERO),
		.devname = "ota_u1p5_non-ve_secure8",
	},

	{
		.name = "fwstack_ssc4",
		.flags = (RTK_FLAG_SCPUACC |
			RTK_FLAG_HWIPACC | RTK_FLAG_CMA | RTK_FLAG_VCPU_FWACC |
			RTK_FLAG_ACPUACC | RTK_FLAG_PROTECTED_DYNAMIC |
			RTK_FLAG_PROTECTED_V2_FW_STACK) ,
		.devname = "fwstack_u1p5_non-ve_secure4",
	},

	{
		.name = "hifi_b1p5",
		.flags = (RTK_FLAG_SCPUACC | RTK_FLAG_HWIPACC |
			RTK_FLAG_CMA | RTK_FLAG_HIFIACC |
			RTK_FLAG_SUPPORT_NONCACHED) ,
		.devname = "hifi_b1p5",
	},

	{
		.name = "vo_s_dsc3",
		.flags = (RTK_FLAG_SCPUACC |
			RTK_FLAG_HWIPACC | RTK_FLAG_CMA |
			RTK_FLAG_PROTECTED_DYNAMIC |
			RTK_FLAG_PROTECTED_V2_VO_POOL |
			RTK_FLAG_SUPPORT_NONCACHED |
			RTK_FLAG_SKIP_ZERO),
		.devname = "vo_non-vc-ac-ve",
	},
#if 0
	{
		.name = "tee",
		.flags = (RTK_FLAG_STATIC_CMA) ,
		/* tee is not CMA 4MB aligned, so 3MB is lost.
		 * Save 2MB back by STATIC CMA */
		.static_cma_size = 0x04200000,
		.devname = "tee",
	},

	{
		.name = "hifi",
		.flags = (RTK_FLAG_STATIC_CMA) ,
		.static_cma_size = 0x0,
		.devname = "hifi",
	},
#endif
};



LIST_HEAD(cheap_list);
LIST_HEAD(gheap_list);
LIST_HEAD(pheap_list);

static bool is_rtk_skip_zero(int flags)
{
	if (flags & RTK_FLAG_SKIP_ZERO)
		return true;
	return false;
}

static int rtk_toc_type(int flags)
{
	return (flags & RTK_FLAG_TOC_MASK);
}


int rtk_protected_type(int flags)
{
	return (flags & RTK_FLAG_PROTECTED_MASK);
}

int rtk_protected_ext_type(int flags)
{
	return (flags & RTK_FLAG_PROTECTED_EXT_MASK);
}

bool is_rtk_dynamic_protect(int flags)
{
	if (flags & RTK_FLAG_PROTECTED_DYNAMIC)
		return true;
	return false;
}

static int is_rtk_exclusive_pool(int flags)
{
	if (!is_rtk_dynamic_protect(flags))
		return false;

	switch (flags & RTK_FLAG_PROTECTED_MASK) {
	case RTK_FLAG_PROTECTED_V2_AUDIO_POOL:
	case RTK_FLAG_PROTECTED_V2_AO_POOL:
	case RTK_FLAG_PROTECTED_V2_FW_STACK:
		return true;
	default:
		return false;
	}
}

bool is_rtk_static_protect(int flags)
{
	if (is_rtk_dynamic_protect(flags))
		return false;

	if (rtk_protected_type(flags))
		return true;
	else
		return false;
}

bool is_rtk_gen_heap(int flags)
{
	if (flags & RTK_FLAG_CMA)
		return false;
	return true;


}

static struct rtk_protect_info *find_protect_info(struct rtk_heap *rtk_heap,
					 unsigned long offset, bool gen)
{
	struct rtk_protect_info *rtk_protect_info, *tmp, *ret = NULL;
	struct list_head *protect_list;
	unsigned long base;
	size_t size;

	protect_list = &rtk_heap->list;

	list_for_each_entry_safe(rtk_protect_info, tmp, protect_list, list) {
		base = rtk_protect_info->create_info.mem.base;
		size = rtk_protect_info->create_info.mem.size;
		if (offset >= base && offset < base + size) {
			ret = rtk_protect_info;
			pr_debug("%s base=0x%lx size=0x%lx \n", __func__,
						base, size);
			break;
		}
	}
	return ret;
}


static struct rtk_protect_info *find_protect_info_in(void *rtk_heap,
					 unsigned long offset, size_t size,
					 bool gen)
{
	struct rtk_protect_info *pr_info_0, *pr_info_1;

	pr_info_0 = find_protect_info(rtk_heap, offset, gen);
	if(!pr_info_0)
		return NULL;
	pr_info_1 = find_protect_info(rtk_heap, offset + size - 1, gen);
	if(!pr_info_1)
		return NULL;
	if(pr_info_0 != pr_info_1)
		return NULL;

	return pr_info_0;
}


static struct rtk_protect_ext_info *find_protect_ext_info(
				 struct rtk_heap *rtk_heap,
				 unsigned long offset, bool gen)
{
	struct rtk_protect_ext_info *rtk_protect_ext_info, *tmp, *ret = NULL;
	struct list_head *protect_list;

	unsigned long base;
	size_t size;

	protect_list = &rtk_heap->elist;

	list_for_each_entry_safe(rtk_protect_ext_info, tmp, protect_list,
					 list) {
		base = rtk_protect_ext_info->create_info.mem.base;
		size = rtk_protect_ext_info->create_info.mem.size;
		if (offset >= base && offset < base + size) {
			ret = rtk_protect_ext_info;
			pr_debug("%s base=0x%lx size=0x%lx \n", __func__,
						base, size);
			break;
		}
	}

	return ret;
}


static inline enum e_notifier_protect_ext _flag_to_notifier_protect_ext
					(unsigned long flags)
{
	enum e_notifier_protect_ext ret =
		(enum e_notifier_protect_ext) RTK_PROTECTED_EXT_GET(flags);
	return ret;
}


static int destroy_protect_ext_info(struct rtk_protect_ext_info *info)
{
	struct rtk_protect_ext_unset ext_unset;

	ext_unset.priv_virt = info->create_info.priv_virt;
	return rtk_protect_ext_unset(&ext_unset);
}

static struct rtk_protect_ext_info *create_protect_ext_info(unsigned long flags,
			 unsigned long base, unsigned long size,
			 struct rtk_protect_info *protect_info)
{
	struct rtk_protect_ext_info *ext_info;
	enum e_notifier_protect_ext ext;
	int ret;

	ext = _flag_to_notifier_protect_ext(flags);
	ext_info = (struct rtk_protect_ext_info *)kzalloc(
			sizeof(struct rtk_protect_ext_info), GFP_KERNEL);
	if (ext_info == NULL)
		goto out;

	ext_info->create_info.mem.ext = ext;
	ext_info->create_info.mem.base = base;
	ext_info->create_info.mem.size = size;

	ext_info->create_info.mem.priv = protect_info->create_info.priv_virt;

	pr_debug("%s base=0x%lx size=0x%lx \n", __func__,
						base, size);



	ret = rtk_protect_ext_set(&ext_info->create_info);
	if (ret) {
		kfree(ext_info);
		ext_info = NULL;
	}

out:
	return ext_info;
}
void rtk_task_info_d(struct rtk_heap *rtk_heap , unsigned long offset, size_t
			size, const char* name)
{
	struct rtk_heap_task *rtk_heap_task, *tmp;
	struct rtk_alloc *rtk_alloc;

	list_for_each_entry_safe(rtk_heap_task, tmp, &rtk_heap->task_list,
					 list) {
		if (!strcmp(rtk_heap_task->comm, name)) {
			rtk_heap_task->size -= size;
			list_for_each_entry(rtk_alloc,
					     &rtk_heap_task->alloc_list, list) {
				if (offset == rtk_alloc->start &&
				    rtk_alloc->end == offset + size) {
					list_del(&rtk_alloc->list);
					kfree(rtk_alloc);
					break;
				}
			}
			break;
		}
	}

	WARN_ON(list_entry_is_head(rtk_heap_task, &rtk_heap->task_list,
					 list));

	if (rtk_heap_task->size == 0) {
		list_del(&rtk_heap_task->list);
		kfree(rtk_heap_task);
	}
}

void rtk_task_info_a(struct rtk_heap *rtk_heap , unsigned long offset, size_t
			size, char *caller)
{
	struct rtk_heap_task *rtk_heap_task, *tmp;
	struct rtk_alloc *rtk_alloc;

	list_for_each_entry_safe(rtk_heap_task, tmp, &rtk_heap->task_list,
					 list) {
		if (!strcmp(rtk_heap_task->comm, current->comm)) {
			rtk_heap_task->size += size;
			break;
		}
	}

	if (list_entry_is_head(rtk_heap_task, &rtk_heap->task_list,
					 list)) {
		rtk_heap_task = (struct rtk_heap_task *)kzalloc(
				sizeof(struct rtk_heap_task), GFP_KERNEL);
		strlcpy(rtk_heap_task->comm, current->comm,
				 sizeof(rtk_heap_task->comm));
		rtk_heap_task->size = size;
		list_add(&rtk_heap_task->list, &rtk_heap->task_list);

		INIT_LIST_HEAD(&rtk_heap_task->alloc_list);
	}

	rtk_alloc = (struct rtk_alloc *)kzalloc(
			sizeof(struct rtk_alloc), GFP_KERNEL);
	rtk_alloc->start = offset;
	rtk_alloc->end = offset + size;

	if(caller != NULL)
		strlcpy(rtk_alloc->ext_task, caller, sizeof(rtk_alloc->ext_task));
	else
		sprintf(rtk_alloc->ext_task, "tgid-%d", current->tgid);
	list_add(&rtk_alloc->list, &rtk_heap_task->alloc_list);

}


/******************************************************************************
 ******************************************************************************
 ******************************************************************************/
void rtk_pool_free(struct rtk_heap *rtk_heap, struct page *pages,
				size_t size, const char *name)
{
	unsigned long offset;
	struct rtk_protect_ext_info *protect_ext_info;

	pr_debug("%s(%pS)...\n", __func__,  __builtin_return_address(0));

	size = PAGE_ALIGN(size);
	offset = page_to_phys(pages);
	if (rtk_protected_type(rtk_heap->flag)) {
		protect_ext_info = find_protect_ext_info(rtk_heap,
						 offset, true);
		if (!protect_ext_info)
			goto free;

		if (destroy_protect_ext_info(protect_ext_info) != 0) {
			pr_err("%s destroy ext info error !\n", __func__);
			BUG();
		}
		list_del(&protect_ext_info->list);
		kfree(protect_ext_info);
	}

free:
	gen_pool_free(rtk_heap->gen_pool, PFN_PHYS(page_to_pfn(pages)), size);

	rtk_task_info_d(rtk_heap, offset, size, name);

	return;
}

static void rtk_gen_free(struct heap_helper_buffer *buffer)
{
	struct rtk_heap *rtk_heap = dma_heap_get_drvdata(
					buffer->heap_buffer.heap);
	struct dma_buf *dmabuf = buffer->heap_buffer.dmabuf;
	struct page *pages = buffer->priv_virt;

	mutex_lock(&rtk_heap->mutex);

	rtk_pool_free(rtk_heap, pages, buffer->heap_buffer.size,
			 dmabuf->exp_name);

	kfree(dmabuf->exp_name);
	sg_free_table(buffer->sg_table);
	kfree(buffer->sg_table);
	kfree(buffer);
	mutex_unlock(&rtk_heap->mutex);

	return ;

}

void rtk_normal_free(struct rtk_heap *rtk_heap, struct page *pages,
				size_t size, const char *name)
{
	struct cma *cma = rtk_heap->cma;
	unsigned long pfn, bitmap_no, bitmap_count;
	unsigned long nr_pages;
	unsigned long phy_addr;

	pr_debug("%s(%pS)...\n", __func__,  __builtin_return_address(0));

	size = PAGE_ALIGN(size);
	phy_addr = page_to_phys(pages);

	pfn = page_to_pfn(pages);
	bitmap_no = (pfn - cma->base_pfn) >> cma->order_per_bit;
	nr_pages = size >> PAGE_SHIFT;

	bitmap_count = ALIGN(size >> PAGE_SHIFT, 1UL << cma->order_per_bit)
			 >> cma->order_per_bit;

	bitmap_clear(rtk_heap->use_bitmap, bitmap_no, bitmap_count);
	bitmap_clear(rtk_heap->alloc_bitmap, bitmap_no,
				 bitmap_count);
	cma_release(cma, pages, nr_pages);

	rtk_task_info_d(rtk_heap, phy_addr, size, name);

	return;
}

static void rtk_cma_free(struct heap_helper_buffer *buffer)
{
	struct rtk_heap *rtk_heap = dma_heap_get_drvdata(
					buffer->heap_buffer.heap);
	struct dma_buf *dmabuf = buffer->heap_buffer.dmabuf;
	struct page *pages = buffer->priv_virt;
	size_t size = buffer->heap_buffer.size;

	mutex_lock(&rtk_heap->mutex);

	rtk_normal_free(rtk_heap, pages, size, dmabuf->exp_name);

	kfree(dmabuf->exp_name);
	/* release sg table */
	sg_free_table(buffer->sg_table);
	kfree(buffer->sg_table);
	kfree(buffer);
	mutex_unlock(&rtk_heap->mutex);

	return ;


}


void rtk_static_secure_free(struct rtk_heap *rtk_heap, struct page *pages,
				size_t size, const char *name)
{
	struct cma *cma = rtk_heap->cma;
	unsigned long pfn, offset, bitmap_no, bitmap_count;
	struct rtk_protect_ext_info *protect_ext_info;
	unsigned long phy_addr;


	pr_debug("%s(%pS)...\n", __func__,  __builtin_return_address(0));

	size = PAGE_ALIGN(size);

	phy_addr = page_to_phys(pages);
	pfn = page_to_pfn(pages);
	offset = page_to_phys(pages);
	protect_ext_info = find_protect_ext_info(rtk_heap, offset,
						 false);

	if (protect_ext_info) {
		if (destroy_protect_ext_info(protect_ext_info) != 0) {
			pr_err("%s destroy ext info error !\n", __func__);
			BUG();
		}
		list_del(&protect_ext_info->list);
		kfree(protect_ext_info);
	}
	bitmap_no = (pfn - cma->base_pfn) >> cma->order_per_bit;

	bitmap_count = ALIGN(size >> PAGE_SHIFT, 1UL << cma->order_per_bit)
			 >> cma->order_per_bit;

	bitmap_clear(rtk_heap->use_bitmap, bitmap_no, bitmap_count);

	rtk_task_info_d(rtk_heap, phy_addr, size, name);


	return;
}

static void rtk_static_protect_cma_free(struct heap_helper_buffer *buffer)
{
	struct rtk_heap *rtk_heap = dma_heap_get_drvdata(
					buffer->heap_buffer.heap);
	struct dma_buf *dmabuf = buffer->heap_buffer.dmabuf;
	struct page *pages = buffer->priv_virt;
	size_t size = buffer->heap_buffer.size;

	mutex_lock(&rtk_heap->mutex);

	rtk_static_secure_free(rtk_heap, pages, size, dmabuf->exp_name);

	kfree(dmabuf->exp_name);
	/* release sg table */
	sg_free_table(buffer->sg_table);
	kfree(buffer->sg_table);
	kfree(buffer);
	mutex_unlock(&rtk_heap->mutex);

	return ;
}

void rtk_dynamic_secure_free(struct rtk_heap *rtk_heap, struct page *pages,
				size_t len, const char *name)
{
	struct cma *cma = rtk_heap->cma;
	struct rtk_protect_info *rtk_protect_info;
	unsigned long pfn, offset, bitmap_no, bitmap_count, *tmp_bitmap;
	unsigned long nr_pages, bit_no, bit_count;
	struct protect_region *mem;
	struct rtk_protect_change_info c_info;
	struct rtk_protect_destroy_info d_info;
	struct rtk_protect_ext_info *protect_ext_info;
	unsigned long phy_addr;
	size_t size;
	int ret;

	size = PAGE_ALIGN(len);

	phy_addr = page_to_phys(pages);
	pfn = page_to_pfn(pages);
	offset = page_to_phys(pages);
	pr_debug("%s(%pS) offset=0x%lx size=0x%lx \n", __func__,
			 __builtin_return_address(0), offset, size);
	protect_ext_info = find_protect_ext_info(rtk_heap, offset,
						 false);

	if (protect_ext_info) {
		if (destroy_protect_ext_info(protect_ext_info) != 0) {
			pr_err("%s destroy ext info error !\n", __func__);
			BUG();
		}
		pr_debug("%s destroy protect_ext_info base=0x%lx size=0x%lx \n",
			__func__,
			protect_ext_info->create_info.mem.base,
			protect_ext_info->create_info.mem.size);
		list_del(&protect_ext_info->list);
		kfree(protect_ext_info);
	}
	bitmap_no = (pfn - cma->base_pfn) >> cma->order_per_bit;

	bitmap_count = ALIGN(size >> PAGE_SHIFT, 1UL << cma->order_per_bit)
			 >> cma->order_per_bit;

	nr_pages = size >> PAGE_SHIFT;
	bitmap_clear(rtk_heap->use_bitmap, bitmap_no, bitmap_count);

	rtk_protect_info = find_protect_info(rtk_heap, offset,
						 false);

	if (!rtk_protect_info) {
		bitmap_clear(rtk_heap->alloc_bitmap, bitmap_no,
				 bitmap_count);
		cma_release(cma, pages, nr_pages);

		goto finished;
	}

	pfn =__phys_to_pfn(rtk_protect_info->create_info.mem.base);
	bitmap_no = (pfn - cma->base_pfn) >> cma->order_per_bit;

	size = rtk_protect_info->create_info.mem.size;
	bitmap_count = ALIGN(size >> PAGE_SHIFT, 1UL << cma->order_per_bit)
			 >> cma->order_per_bit;
	/* bitmap_no always align  */
	tmp_bitmap = rtk_heap->use_bitmap + bitmap_no/(BITS_PER_BYTE *
							 sizeof(unsigned long));
	/* check whole protect region */
	if (bitmap_empty(tmp_bitmap, bitmap_count)) {
		d_info.priv_virt = rtk_protect_info->create_info.priv_virt;
		ret = rtk_protect_destroy(&d_info);
		if (ret) {
			pr_err("%s:%d rtk_protect_destroy"
				"notify return ERROR! (priv_virt=%p)\n",
				 __func__, __LINE__, d_info.priv_virt);
			BUG();
		}
		pr_debug("%s clear whole region : base = 0x%lx size = 0x%lx \n",
				 __func__,
				rtk_protect_info->create_info.mem.base,
				rtk_protect_info->create_info.mem.size);

		list_del(&rtk_protect_info->list);
		kfree(rtk_protect_info);
		bitmap_clear(rtk_heap->alloc_bitmap, bitmap_no,
			 bitmap_count);

		pages = pfn_to_page(pfn);
		nr_pages = size >> PAGE_SHIFT;
		cma_release(cma, pages, nr_pages);

		goto finished;
	}

	mem = &rtk_protect_info->create_info.mem;

	bit_no = find_first_bit(tmp_bitmap, bitmap_count);
	bit_count = bit_no;
	size = ALIGN_DOWN(((bit_count) << cma->order_per_bit) << PAGE_SHIFT,
			SZ_2M);
	/* check upper part of protect region */
	if (size) {
		c_info.mem.base = mem->base + size;
		c_info.mem.size = mem->size - size;
		c_info.mem.type = (enum e_notifier_protect_type)
				RTK_PROTECTED_TYPE_GET(rtk_heap->flag);
		c_info.priv_virt = rtk_protect_info->
					create_info.priv_virt;
		ret = rtk_protect_change(&c_info);
		if (ret) {
			pr_err("%s:%d rtk_protect_change"
				"notify return ERROR! (priv_virt=%p)\n",
				 __func__, __LINE__, c_info.priv_virt);
			BUG();
		}

		pr_debug("%s clear(0x%lx)hi region: from base/size = 0x%lx/0x%lx "
			"to base/size = 0x%lx/0x%lx \n", __func__, size,
				rtk_protect_info->create_info.mem.base,
				rtk_protect_info->create_info.mem.size,
				c_info.mem.base, c_info.mem.size);

		mem->base = c_info.mem.base;
		mem->size = c_info.mem.size;

		bitmap_no = (pfn - cma->base_pfn) >> cma->order_per_bit;
		bitmap_count = ALIGN(size >> PAGE_SHIFT,
			1UL << cma->order_per_bit)>> cma->order_per_bit;

		bitmap_clear(rtk_heap->alloc_bitmap,
					 bitmap_no, bitmap_count);

		pages = pfn_to_page(pfn);
		nr_pages = size >> PAGE_SHIFT;
		cma_release(cma, pages, nr_pages);

		goto finished;

	}

	bit_no = find_last_bit(tmp_bitmap, bitmap_count);
	if (bit_no == bitmap_count)
		bit_count = bitmap_count;
	else
		bit_count = bitmap_count - bit_no - 1;
	size = ALIGN_DOWN((bit_count << cma->order_per_bit) << PAGE_SHIFT,
			SZ_2M);

	/* check lower part of protect region */
	if (size) {
		c_info.mem.base = mem->base;
		/* mem.size should be bigger than size */
		c_info.mem.size = mem->size - size;
		c_info.mem.type = (enum e_notifier_protect_type)
				RTK_PROTECTED_TYPE_GET(rtk_heap->flag);
		c_info.priv_virt = rtk_protect_info->
					create_info.priv_virt;
		ret = rtk_protect_change(&c_info);
		if (ret) {
			pr_err("%s:%d rtk_protect_change"
				" return ERROR! (priv_virt=%p)\n",
				 __func__, __LINE__, c_info.priv_virt);
			BUG();
		}

		pr_debug("%s clear(0x%lx)lo region: from base/size = 0x%lx/0x%lx "
			"to base/size = 0x%lx/0x%lx \n", __func__, size,
				rtk_protect_info->create_info.mem.base,
				rtk_protect_info->create_info.mem.size,
				c_info.mem.base, c_info.mem.size);

		mem->base = c_info.mem.base;
		mem->size = c_info.mem.size;

		pfn =__phys_to_pfn(mem->base + mem->size);
		bitmap_no = (pfn - cma->base_pfn) >> cma->order_per_bit;

		bitmap_count = ALIGN(size >> PAGE_SHIFT,
			1UL << cma->order_per_bit)>> cma->order_per_bit;

		bitmap_clear(rtk_heap->alloc_bitmap, bitmap_no,
				bitmap_count);

		pages = pfn_to_page(pfn);
		nr_pages = size >> PAGE_SHIFT;
		cma_release(cma, pages, nr_pages);

		goto finished;
	}

finished:
	size = PAGE_ALIGN(len);
	rtk_task_info_d(rtk_heap, phy_addr, size, name);

	return;
}

static void rtk_dynamic_protect_cma_free(struct heap_helper_buffer *buffer)
{
	struct rtk_heap *rtk_heap = dma_heap_get_drvdata(
					buffer->heap_buffer.heap);
	struct dma_buf *dmabuf = buffer->heap_buffer.dmabuf;
	struct page *pages = buffer->priv_virt;
	size_t size = buffer->heap_buffer.size;

	mutex_lock(&rtk_heap->mutex);
	size = buffer->heap_buffer.size;

	rtk_dynamic_secure_free(rtk_heap, pages, size, dmabuf->exp_name);

	kfree(dmabuf->exp_name);
	/* release sg table */
	sg_free_table(buffer->sg_table);
	kfree(buffer->sg_table);
	kfree(buffer);
	mutex_unlock(&rtk_heap->mutex);

	return ;
}



static struct page *rtk_cma_alloc(struct rtk_heap *rtk_heap, size_t count,
			unsigned int align)
{
	unsigned long *ref_bitmap;
	struct cma *cma = rtk_heap->cma;
	unsigned long pfn, bitmap_count, bitmap_size;
	unsigned long mask, offset, start = 0;
	struct page *page = NULL;
	struct rtk_protect_info *pr_info;
	int bit_id;


	if (bitmap_empty(rtk_heap->alloc_bitmap, rtk_heap->nbits))
		return NULL;

	mask = (align <= cma->order_per_bit) ? 0 :
		 (1UL << (align - cma->order_per_bit)) - 1;
        offset = (cma->base_pfn & ((1UL << align) - 1)) >> cma->order_per_bit;
	bitmap_count = ALIGN(count, 1UL << cma->order_per_bit)
				 >> cma->order_per_bit;

	bitmap_size = rtk_heap->nbits;

	ref_bitmap = bitmap_zalloc(bitmap_size, GFP_KERNEL);

	bitmap_complement(ref_bitmap, rtk_heap->alloc_bitmap,
				bitmap_size);
	bitmap_or(ref_bitmap, ref_bitmap, rtk_heap->use_bitmap,
			bitmap_size);

	for (;;) {
		bit_id = bitmap_find_next_zero_area_off(ref_bitmap,
				bitmap_size, start, bitmap_count, mask, offset);

		if (bit_id > bitmap_size)
			goto out;

		pfn = cma->base_pfn + (bit_id << cma->order_per_bit);
		/* have to be in unique protect region */
		pr_info = find_protect_info_in(rtk_heap, PFN_PHYS(pfn),
				count << PAGE_SHIFT, 0);
		if (pr_info)
			break;
		start = bit_id + mask + 1;
		pr_debug("%s find next area bit_id = 0x%x , start =0x%lx \n",
				__func__, bit_id, start);
	}

	bitmap_set(rtk_heap->use_bitmap, bit_id, bitmap_count);

	pfn = cma->base_pfn + (bit_id << cma->order_per_bit);
	page = pfn_to_page(pfn);
out:
	bitmap_free(ref_bitmap);
	return page;

}

static int rtk_adjust_protect_area(struct rtk_heap *rtk_heap,
			struct page *pages, unsigned long nr_pages)
{
	struct rtk_protect_info *rtk_protect_info, *tmp;
	struct rtk_protect_change_info info;
	unsigned long base = page_to_phys(pages);
	unsigned long limit = base + (nr_pages << PAGE_SHIFT);
	struct protect_region *mem;
	int ret = 0;

	list_for_each_entry_safe(rtk_protect_info, tmp, &rtk_heap->list,
				 list) {
		mem = &rtk_protect_info->create_info.mem;
		if (limit == mem->base) {
			info.mem.base = base;
			info.mem.size = mem->size + (nr_pages << PAGE_SHIFT);
			info.mem.type = (enum e_notifier_protect_type)
				RTK_PROTECTED_TYPE_GET(rtk_heap->flag);
			info.priv_virt =
					rtk_protect_info->create_info.priv_virt;
			ret = rtk_protect_change(&info);
			if (ret) {
				pr_err("%s:%d rtk_protect_change "
				"return ERROR! (priv_virt=%p)\n",__func__,
				 __LINE__, info.priv_virt);
				continue;
			}

			pr_debug("%s change from base/size = 0x%lx/0x%lx "
				"to base/size = 0x%lx/0x%lx \n", __func__,
				mem->base, mem->size,
				info.mem.base, info.mem.size);

			mem->base = info.mem.base;
			mem->size = info.mem.size;

			ret = 0;
			goto out;
		}
		else if (base == mem->base + mem->size) {
			info.mem.base = mem->base;
			info.mem.size = limit - mem->base;
			info.mem.type = (enum e_notifier_protect_type)
				RTK_PROTECTED_TYPE_GET(rtk_heap->flag);
			info.priv_virt = rtk_protect_info->create_info.priv_virt;
			ret = rtk_protect_change(&info);
			if (ret) {
				pr_err("%s:%d rtk_protect_change "
				" return ERROR! (priv_virt=%p)\n",__func__,
				 __LINE__, info.priv_virt);
				continue;
			}

			pr_debug("%s change from base/size = 0x%lx/0x%lx "
				"to base/size = 0x%lx/0x%lx \n", __func__,
				mem->base, mem->size,
				info.mem.base, info.mem.size);

			mem->base = info.mem.base;
			mem->size = info.mem.size;

			ret = 0;
			goto out;
		}
	} /* list_for_each_entry_safe */

	rtk_protect_info = kzalloc(sizeof(struct rtk_protect_info),
				GFP_KERNEL);
	if (!rtk_protect_info) {
		pr_err("%s:%d ERROR!\n", __func__, __LINE__);
		ret = -EINVAL;
		goto out;
	}

	rtk_protect_info->create_info.mem.base = base;
	rtk_protect_info->create_info.mem.size = nr_pages << PAGE_SHIFT;
	rtk_protect_info->create_info.mem.type = (enum e_notifier_protect_type)
				RTK_PROTECTED_TYPE_GET(rtk_heap->flag);

	ret = rtk_protect_create(&rtk_protect_info->create_info);
	if (ret) {
		kfree(rtk_protect_info);
		pr_err("%s:%d rtk_protect_create return ERROR!"
			" (priv_virt=??)\n", __func__, __LINE__);
		ret = -EINVAL;
		goto out;
	}
	pr_debug("%s rtk_protect_create_notify  base =0x%lx size =0x%lx \n",
				__func__, base, nr_pages << PAGE_SHIFT);

	pr_debug("%s create from base/size = 0x%lx/0x%lx "
		"\n", __func__,
		rtk_protect_info->create_info.mem.base,
		rtk_protect_info->create_info.mem.size);


	list_add(&rtk_protect_info->list, &rtk_heap->list);

out:
	return ret;
}


static void pages_clear(struct page *pages, unsigned long nr_pages, bool gen)
{
	size_t size = nr_pages << PAGE_SHIFT;

	if (PageHighMem(pages)) {
		unsigned long nr_clear_pages = nr_pages;
		struct page *page = pages;
		while (nr_clear_pages > 0) {
			void *vaddr = kmap_atomic(page);

			memset(vaddr, 0, PAGE_SIZE);
			kunmap_atomic(vaddr);
			page++;
			nr_clear_pages--;
		}
		pr_debug("%s of cma heap of high mem \n", __func__);
	} else {
		memset(page_address(pages), 0, size);
	}

}

static struct dma_buf *dma_buf_allocate(struct dma_heap *heap, unsigned long len,
		struct page *pages, void *free_func,
		unsigned long flags, bool uncached)
{
	struct heap_helper_buffer *helper_buffer;
	struct sg_table *table;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	int ret = 0;
	unsigned long nr_pages;
	size_t size;

	size = len;
	nr_pages = size >> PAGE_SHIFT;

	helper_buffer = kzalloc(sizeof(*helper_buffer), GFP_KERNEL);
	if (!helper_buffer) {
		dmabuf = ERR_PTR(-ENOMEM);
		goto out;
	}

	INIT_HEAP_HELPER_BUFFER(helper_buffer, free_func);
	helper_buffer->heap_buffer.flags = flags;
	helper_buffer->heap_buffer.heap = heap;
	helper_buffer->heap_buffer.size = len;
	helper_buffer->uncached = uncached;

	table = kmalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		goto free_buf;

	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret)
		goto free_table;
	sg_set_page(table->sgl, pages, size, 0);

	/* create the dmabuf */
	exp_info.ops = &heap_helper_ops;
	exp_info.size = len;
	exp_info.flags = O_RDWR;
	exp_info.priv = &helper_buffer->heap_buffer;
	exp_info.exp_name = (char *)kzalloc(TASK_COMM_LEN, GFP_KERNEL);
	strncpy((char *)exp_info.exp_name, current->comm, TASK_COMM_LEN);
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR_OR_NULL(dmabuf)) {
		goto free_table;
	}

	helper_buffer->heap_buffer.dmabuf = dmabuf;
	helper_buffer->priv_virt = pages;
	helper_buffer->sg_table = table;

out:
	return dmabuf;
free_table:
	kfree(table);
free_buf:
	kfree(helper_buffer);
	pr_err("%s error \n", __func__);
	return ERR_PTR(-ENOMEM);
}


struct page *rtk_dynamic_secure_allocate(struct rtk_heap *rtk_heap,
					size_t len, unsigned long flags, char *caller)
{
	struct cma *cma = rtk_heap->cma;
	struct rtk_protect_info *rtk_protect_info;
	struct rtk_protect_ext_info *rtk_protect_ext_info;
	unsigned long offset;
	struct page *pages = NULL;
	unsigned long pfn, nr_pages, bitmap_count, align;
	size_t size, p_size;
	int bit_id;

	pr_debug("%s(%pS)...\n", __func__,  __builtin_return_address(0));

	size = PAGE_ALIGN(len);
	nr_pages = size >> PAGE_SHIFT;
	align = 0;

	if (rtk_protected_type(flags)) {
		pages = rtk_cma_alloc(rtk_heap, nr_pages, align);
		if (!pages) {
			align = get_order(SZ_2M);
			p_size = ALIGN(len, SZ_2M);
			nr_pages = p_size >> PAGE_SHIFT;
			pages = cma_alloc(cma, nr_pages, align, GFP_KERNEL);
			if (!pages)
				goto out;

			bitmap_count = ALIGN(nr_pages,
					 1UL << cma->order_per_bit)
					 >> cma->order_per_bit;
			pfn = __page_to_pfn(pages);
			bit_id = (pfn - cma->base_pfn) >> cma->order_per_bit;
			bitmap_set(rtk_heap->alloc_bitmap, bit_id,
					 bitmap_count);

			dma_sync_single_for_device(
					dma_heap_get_dev(rtk_heap->heap),
					page_to_phys(pages), p_size
					, DMA_BIDIRECTIONAL);

			if (rtk_adjust_protect_area(rtk_heap,
						 pages, nr_pages)) {
				BUG();
				goto out;
			}

			size = PAGE_ALIGN(len);
			nr_pages = size >> PAGE_SHIFT;
			align = 0;
			pages = rtk_cma_alloc(rtk_heap, nr_pages, align);
			if (!pages)
				goto out;
		}

		offset = page_to_phys(pages);
		pr_debug("%s(%pS) secure offset=0x%lx size=0x%lx\n", __func__,
			 __builtin_return_address(0), offset, size);

		if (!rtk_protected_ext_type(rtk_heap->flag) &&
			rtk_protected_ext_type(flags)) {

			rtk_protect_info = find_protect_info(
					(void *)rtk_heap, offset, false);
			if (!rtk_protect_info) {
				pr_err("%s %d rtk_protect_info = 0x%p\n",
					 __func__, __LINE__, rtk_protect_info);
				BUG();
			}

			rtk_protect_ext_info = create_protect_ext_info(flags,
					 offset, size, rtk_protect_info);

			if (rtk_protect_ext_info == NULL) {
				/* TODO : free cma ; free rtk cma*/
				BUG();
				goto out;
			}
			pr_debug("%s create protect_ext_info base=0x%lx"
				" size=0x%lx \n",
				__func__,
				rtk_protect_ext_info->create_info.mem.base,
				rtk_protect_ext_info->create_info.mem.size);

			list_add(&rtk_protect_ext_info->list,
					 &rtk_heap->elist);

		}

	} else {
		pages = cma_alloc(cma, nr_pages, align, GFP_KERNEL);
		if (!pages)
			goto out;

		offset = page_to_phys(pages);
		bitmap_count = ALIGN(nr_pages, 1UL << cma->order_per_bit)
				 >> cma->order_per_bit;
		pfn = __page_to_pfn(pages);
		bit_id = (pfn - cma->base_pfn) >> cma->order_per_bit;
		bitmap_set(rtk_heap->alloc_bitmap, bit_id, bitmap_count);

		bitmap_set(rtk_heap->use_bitmap, bit_id, bitmap_count);
		pr_debug("%s(%pS) no secure offset=0x%llx size=0x%lx\n",
			 __func__, __builtin_return_address(0),
			 page_to_phys(pages), size);
		if (!is_rtk_skip_zero(flags))
			pages_clear(pages, nr_pages, 0);
		dma_sync_single_for_device(
				dma_heap_get_dev(rtk_heap->heap),
				page_to_phys(pages), size
				, DMA_BIDIRECTIONAL);
	}

	rtk_task_info_a(rtk_heap, offset, size, caller);

out:
	return pages;
}

static struct dma_buf *rtk_dyn_protect_cma_do_allocate(struct dma_heap *heap,
	size_t size, unsigned long flags, bool uncached)
{
	struct rtk_heap *rtk_heap = dma_heap_get_drvdata(heap);
	struct page *pages;
	struct dma_buf *ret = ERR_PTR(-ENOMEM);

	mutex_lock(&rtk_heap->mutex);

	size = PAGE_ALIGN(size);
	pages = rtk_dynamic_secure_allocate(rtk_heap, size, flags, NULL);
	if (!pages)
		goto out;

	ret = dma_buf_allocate(heap, size, pages,
		 	rtk_dynamic_protect_cma_free, flags, uncached);
	if (IS_ERR_OR_NULL(ret)) {
		BUG();
		goto out;
	}

out:
	mutex_unlock(&rtk_heap->mutex);

	return ret;

}


struct page *rtk_normal_allocate(struct rtk_heap *rtk_heap,
					size_t size, unsigned long flags, char *caller)
{
	struct page *pages = NULL;
	struct cma *cma = rtk_heap->cma;
	unsigned long pfn, nr_pages, bitmap_count, align;
	int bit_id;
	unsigned long offset;

	pr_debug("%s(%pS)...\n", __func__,  __builtin_return_address(0));

	size = PAGE_ALIGN(size);
	align = 0;
	nr_pages = size >> PAGE_SHIFT;

	pages = cma_alloc(cma, nr_pages, align, GFP_KERNEL);
	if (!pages)
		goto out;
	if (!is_rtk_skip_zero(flags))
		pages_clear(pages, nr_pages, 0);
	dma_sync_single_for_device(
			dma_heap_get_dev(rtk_heap->heap),
			page_to_phys(pages), size
			, DMA_BIDIRECTIONAL);

	bitmap_count = ALIGN(nr_pages, 1UL << cma->order_per_bit)
				 >> cma->order_per_bit;
	pfn = __page_to_pfn(pages);
	offset = page_to_phys(pages);

	bit_id = (pfn - cma->base_pfn) >> cma->order_per_bit;

	bitmap_set(rtk_heap->alloc_bitmap, bit_id, bitmap_count);
	bitmap_set(rtk_heap->use_bitmap, bit_id, bitmap_count);

	rtk_task_info_a(rtk_heap, offset, size, caller);

out:
	return pages;

}

static struct dma_buf *rtk_cma_do_allocate(struct dma_heap *heap,
	unsigned long len, unsigned long flags, bool uncached)
{
	struct rtk_heap *rtk_heap = dma_heap_get_drvdata(heap);
	struct page *pages;
	struct dma_buf *ret = ERR_PTR(-ENOMEM);
	size_t size;

	mutex_lock(&rtk_heap->mutex);

	size = PAGE_ALIGN(len);
	pages = rtk_normal_allocate(rtk_heap, size, flags, NULL);

	if (!pages)
		goto out;

	ret = dma_buf_allocate(heap, size, pages, rtk_cma_free,
				 flags, uncached);
	if (IS_ERR_OR_NULL(ret)) {
		BUG();
		goto out;
	}

out:
	mutex_unlock(&rtk_heap->mutex);
	return ret;

}

struct page *rtk_static_secure_allocate(struct rtk_heap *rtk_heap,
					size_t size, unsigned long flags, char *caller)
{
	struct rtk_protect_info *rtk_protect_info;
	struct rtk_protect_ext_info *rtk_protect_ext_info;
	struct page *pages = NULL;
	unsigned long nr_pages, align;
	unsigned long offset;

	pr_debug("%s(%pS)...\n", __func__,  __builtin_return_address(0));

	size = PAGE_ALIGN(size);
	align = 0;
	nr_pages = size >> PAGE_SHIFT;

	pages = rtk_cma_alloc(rtk_heap, nr_pages, align);

	if (!pages)
		goto out;

	offset = page_to_phys(pages);
	/* this function is only static protect */
	if (rtk_protected_type(rtk_heap->flag) &&
			!rtk_protected_ext_type(rtk_heap->flag) &&
			rtk_protected_ext_type(flags)) {

		rtk_protect_info = find_protect_info((void *)rtk_heap,
						 offset, false);
		rtk_protect_ext_info = create_protect_ext_info(flags, offset,
						 size, rtk_protect_info);

		if (rtk_protect_ext_info == NULL) {
			/* TODO : free cma ; free rtk cma*/
			pages = NULL;
			BUG();
			goto out;
		}
		list_add(&rtk_protect_ext_info->list, &rtk_heap->elist);
	}

	rtk_task_info_a(rtk_heap, offset, size, caller);

out:
	return pages;

}

static struct dma_buf *rtk_stc_cma_do_allocate(struct dma_heap *heap,
	unsigned long len, unsigned long flags, bool uncached)
{
	struct rtk_heap *rtk_heap = dma_heap_get_drvdata(heap);
	struct page *pages;
	struct dma_buf *ret = ERR_PTR(-ENOMEM);
	size_t size;

	mutex_lock(&rtk_heap->mutex);

	size = PAGE_ALIGN(len);
	pages = rtk_static_secure_allocate(rtk_heap, size, flags, NULL);
	if (!pages)
		goto out;

	ret = dma_buf_allocate(heap, size, pages, rtk_static_protect_cma_free,
				 flags, uncached);
	if (IS_ERR_OR_NULL(ret)) {
		BUG();
		goto out;
	}

out:
	mutex_unlock(&rtk_heap->mutex);
	return ret;

}

struct page *rtk_pool_allocate(struct rtk_heap *rtk_heap,
					size_t size, unsigned long flags, char *caller)
{
	struct rtk_protect_info *rtk_protect_info;
	struct rtk_protect_ext_info *rtk_protect_ext_info;
	struct page *pages = NULL;
	unsigned long offset;

	pr_debug("%s(%pS)...\n", __func__,  __builtin_return_address(0));

	size = PAGE_ALIGN(size);

	offset = gen_pool_alloc(rtk_heap->gen_pool, (size_t)size);

	if (!offset)
		goto out;

	pages = pfn_to_page(__phys_to_pfn(offset));
	if (!is_rtk_static_protect(rtk_heap->flag) && (
		rtk_heap->flag & RTK_FLAG_SCPUACC)) {
		unsigned long nr_pages = size >> PAGE_SHIFT;
		if (!is_rtk_skip_zero(flags))
			pages_clear(pages, nr_pages, 1);
		dma_sync_single_for_device(
				dma_heap_get_dev(rtk_heap->heap),
				page_to_phys(pages), size
				, DMA_BIDIRECTIONAL);
	}

	/* gen heap is only static protect or free protect */
	if (rtk_protected_type(rtk_heap->flag) &&
		!rtk_protected_ext_type(rtk_heap->flag) &&
		rtk_protected_ext_type(flags)) {

		rtk_protect_info = find_protect_info((void *)
					rtk_heap, offset, true);
		rtk_protect_ext_info = create_protect_ext_info(
				 flags, offset, size, rtk_protect_info);

		if (rtk_protect_ext_info == NULL) {
			gen_pool_free(rtk_heap->gen_pool, offset,
					 size);
			pages = NULL;
			goto out;
		}
		list_add(&rtk_protect_ext_info->list, &rtk_heap->elist);
	}

	rtk_task_info_a(rtk_heap, offset, size, caller);

out:
	return pages;

}

static struct dma_buf *rtk_gen_do_allocate(struct dma_heap *heap,
			 unsigned long len, unsigned long flags, bool uncached)
{
	struct rtk_heap *rtk_heap = dma_heap_get_drvdata(heap);
	struct page *pages;
	unsigned long offset;
	size_t size;
	struct dma_buf *ret = ERR_PTR(-ENOMEM);

	mutex_lock(&rtk_heap->mutex);
	size = PAGE_ALIGN(len);

	pages = rtk_pool_allocate(rtk_heap, size, flags, NULL);
	if (!pages)
		goto out;

	ret = dma_buf_allocate(heap, size, pages, rtk_gen_free, flags,
				uncached);
	if (IS_ERR_OR_NULL(ret)) {
		BUG();
		gen_pool_free(rtk_heap->gen_pool, offset, size);
		goto out;
	}

out:
	mutex_unlock(&rtk_heap->mutex);

	return ret;
}

/*****************************************************************************
 ****************************************************************************/

static struct dma_buf *rtk_dynamic_protect_cma_allocate(struct dma_heap *heap,
	unsigned long len, unsigned long flags, unsigned long heap_flags)
{
	return rtk_dyn_protect_cma_do_allocate(heap, len, heap_flags, false);
}

static struct dma_buf *rtk_dynamic_uncached_protect_cma_allocate(
	struct dma_heap *heap, unsigned long len, unsigned long flags,
	 unsigned long heap_flags)
{
	return rtk_dyn_protect_cma_do_allocate(heap, len, heap_flags, true);
}

static struct dma_buf *rtk_static_protect_cma_allocate(struct dma_heap *heap,
	unsigned long len, unsigned long flags, unsigned long heap_flags)
{
	return rtk_stc_cma_do_allocate(heap, len, heap_flags, false);
}

static struct dma_buf *rtk_static_uncached_protect_cma_allocate(
	struct dma_heap *heap, unsigned long len, unsigned long flags,
	 unsigned long heap_flags)
{
	return rtk_stc_cma_do_allocate(heap, len, heap_flags, true);
}

static struct dma_buf *rtk_cma_allocate(struct dma_heap *heap,
	 unsigned long len, unsigned long flags, unsigned long heap_flags)
{
	return rtk_cma_do_allocate(heap, len, heap_flags, false);
}

static struct dma_buf *rtk_uncached_cma_allocate(struct dma_heap *heap,
	 unsigned long len, unsigned long flags, unsigned long heap_flags)
{
	return rtk_cma_do_allocate(heap, len, heap_flags, true);
}


static struct dma_buf *rtk_gen_allocate(struct dma_heap *heap,
	 unsigned long len, unsigned long flags, unsigned long heap_flags)
{
	return rtk_gen_do_allocate(heap, len, heap_flags, false);
}

static struct dma_buf *rtk_uncached_gen_allocate(struct dma_heap *heap,
	 unsigned long len, unsigned long flags, unsigned long heap_flags)
{
	return rtk_gen_do_allocate(heap, len, heap_flags, true);
}

static struct dma_buf *rtk_static_protect_gen_allocate(struct dma_heap *heap,
	unsigned long len, unsigned long flags, unsigned long heap_flags)
{
	return rtk_gen_do_allocate(heap, len, heap_flags, false);
}

static struct dma_buf *rtk_static_uncached_protect_gen_allocate(
	struct dma_heap *heap, unsigned long len, unsigned long flags,
	 unsigned long heap_flags)
{
	return rtk_gen_do_allocate(heap, len, heap_flags, true);
}

static struct dma_buf *rtk_not_initialized(struct dma_heap *heap,
				unsigned long len, unsigned long flags,
				unsigned long heap_flags)
{
	return ERR_PTR(-EBUSY);
}

/******************************************************************************
 * dma_heap_ops
 ******************************************************************************/

static struct dma_heap_ops dynamic_protect_cma_ops = {
	.allocate = rtk_dynamic_protect_cma_allocate,
};

static struct dma_heap_ops dynamic_uncached_protect_cma_ops = {
	.allocate = rtk_not_initialized,
};

static struct dma_heap_ops static_protect_cma_ops = {
	.allocate = rtk_static_protect_cma_allocate,
};

static struct dma_heap_ops static_uncached_protect_cma_ops = {
	.allocate = rtk_not_initialized,
};

static struct dma_heap_ops cma_ops = {
	.allocate = rtk_cma_allocate,
};

static struct dma_heap_ops uncached_cma_ops = {
	.allocate = rtk_not_initialized,
};

static struct dma_heap_ops static_protect_gen_ops = {
	.allocate = rtk_static_protect_gen_allocate,
};

static struct dma_heap_ops static_uncached_protect_gen_ops = {
	.allocate = rtk_not_initialized,
};

static struct dma_heap_ops gen_ops = {
	.allocate = rtk_gen_allocate,
};

static struct dma_heap_ops uncached_gen_ops = {
	.allocate = rtk_not_initialized,
};

static struct rtk_heap *alloc_rtk_heap(struct cma *cma, u32 cma_flags)
{
	struct rtk_heap *rtk_heap;
	int bitmap_size;

	rtk_heap = kzalloc(sizeof(struct rtk_heap), GFP_KERNEL);
	if (!rtk_heap) {
		pr_err("%s kzalloc err \n", __func__);
		return NULL;
	}

	rtk_heap->cma = cma;
	bitmap_size = cma->count >> cma->order_per_bit;
	rtk_heap->alloc_bitmap = bitmap_zalloc(bitmap_size, GFP_KERNEL);
	rtk_heap->use_bitmap = bitmap_zalloc(bitmap_size, GFP_KERNEL);
	rtk_heap->nbits = cma->count >> cma->order_per_bit;
	rtk_heap->flag = cma_flags;

	return rtk_heap;

};
static void *rtk_dynamic_proctect_cma_create(struct cma *cma,
					char *name, u32 cma_flags)
{
	struct rtk_heap *rtk_heap;
	struct dma_heap_export_info exp_info;

	rtk_heap = alloc_rtk_heap(cma, cma_flags);
	if (rtk_heap == NULL) {
		return NULL;
	}
	INIT_LIST_HEAD(&rtk_heap->task_list);

	exp_info.name = name;
	exp_info.ops =  &dynamic_protect_cma_ops;
	exp_info.priv = rtk_heap;

	rtk_heap->ops = &dynamic_protect_cma_ops;
	rtk_heap->heap = dma_heap_add(&exp_info);
	if (IS_ERR(rtk_heap->heap)) {
		kfree(rtk_heap);
		rtk_heap = NULL;
		goto out;
	}

	INIT_LIST_HEAD(&rtk_heap->list);
	INIT_LIST_HEAD(&rtk_heap->elist);

	list_add(&rtk_heap->hlist, &cheap_list);
	mutex_init(&rtk_heap->mutex);

	dma_coerce_mask_and_coherent(dma_heap_get_dev
			(rtk_heap->heap), DMA_BIT_MASK(32));
	mb();

	if (!(cma_flags & RTK_FLAG_SUPPORT_NONCACHED))
		goto out;

	exp_info.name =  kasprintf(GFP_KERNEL,
				"%s_uncached", name);
	exp_info.ops =   &dynamic_uncached_protect_cma_ops;
	exp_info.priv = rtk_heap;

	rtk_heap->uncached_ops = &dynamic_uncached_protect_cma_ops;
	rtk_heap->uncached_heap = dma_heap_add(&exp_info);
	if (IS_ERR_OR_NULL(rtk_heap->uncached_heap)) {
		kfree(rtk_heap);
		rtk_heap = NULL;
		goto out;
	}

	dma_coerce_mask_and_coherent(dma_heap_get_dev
			(rtk_heap->uncached_heap), DMA_BIT_MASK(32));
	mb();

	dynamic_uncached_protect_cma_ops.allocate =
				rtk_dynamic_uncached_protect_cma_allocate;

out:
	return (void *)rtk_heap;

}

static void *rtk_cma_create(struct cma *cma, char *name,
				 u32 cma_flags)
{
	struct rtk_heap *rtk_heap;
	struct dma_heap_export_info exp_info;

	rtk_heap = alloc_rtk_heap(cma, cma_flags);
	if (rtk_heap == NULL) {
		return NULL;
	}
	INIT_LIST_HEAD(&rtk_heap->task_list);

	exp_info.name = name;
	exp_info.ops = &cma_ops;
	exp_info.priv = rtk_heap;

	rtk_heap->ops = &cma_ops;
	rtk_heap->heap = dma_heap_add(&exp_info);
	if (IS_ERR(rtk_heap->heap)) {
		kfree(rtk_heap);
		rtk_heap = NULL;
		goto out;
	}


	INIT_LIST_HEAD(&rtk_heap->list);
	INIT_LIST_HEAD(&rtk_heap->elist);

	list_add(&rtk_heap->hlist, &cheap_list);
	mutex_init(&rtk_heap->mutex);

	dma_coerce_mask_and_coherent(dma_heap_get_dev
			(rtk_heap->heap), DMA_BIT_MASK(32));
	mb();

	if (!(cma_flags & RTK_FLAG_SUPPORT_NONCACHED))
		goto out;

	exp_info.name =  kasprintf(GFP_KERNEL,
				"%s_uncached", name);
	exp_info.ops =  &uncached_cma_ops;
	exp_info.priv = rtk_heap;

	rtk_heap->uncached_ops = &uncached_cma_ops;
	rtk_heap->uncached_heap = dma_heap_add(&exp_info);
	if (IS_ERR_OR_NULL(rtk_heap->uncached_heap)) {
		kfree(rtk_heap);
		rtk_heap = NULL;
		goto out;
	}

	dma_coerce_mask_and_coherent(dma_heap_get_dev
			(rtk_heap->uncached_heap), DMA_BIT_MASK(32));
	mb();

	uncached_cma_ops.allocate = rtk_uncached_cma_allocate;

out:
	return (void *)rtk_heap;

}


static void *rtk_static_proctect_cma_create(struct cma *cma,
					char *name, u32 cma_flags)
{
	struct rtk_protect_info *rtk_protect_info;
	struct rtk_heap *rtk_heap;
	unsigned long bitmap_count = cma->count >> cma->order_per_bit;
	unsigned long pfn, base;
	struct page *pages;
	int bit_id;
	struct dma_heap_export_info exp_info;
	int ret;

	rtk_heap = alloc_rtk_heap(cma, cma_flags);
	if (rtk_heap == NULL) {
		return NULL;
	}
	INIT_LIST_HEAD(&rtk_heap->task_list);
	pages = cma_alloc(cma, cma->count, 0, GFP_KERNEL);
	pfn = __page_to_pfn(pages);
	base = page_to_phys(pages);

	bit_id = (pfn - cma->base_pfn) >> cma->order_per_bit;
	bitmap_set(rtk_heap->alloc_bitmap, bit_id, bitmap_count);


	exp_info.name = name;
	exp_info.ops = &static_protect_cma_ops;
	exp_info.priv = rtk_heap;

	rtk_heap->ops = &static_protect_cma_ops;
	rtk_heap->heap = dma_heap_add(&exp_info);
	if (IS_ERR(rtk_heap->heap)) {
		kfree(rtk_heap);
		rtk_heap = NULL;
		goto out;
	}


	list_add(&rtk_heap->hlist, &cheap_list);
	mutex_init(&rtk_heap->mutex);

	dma_sync_single_for_device(dma_heap_get_dev(rtk_heap->heap), base,
			 cma->count << PAGE_SHIFT, DMA_BIDIRECTIONAL);


	rtk_protect_info = kzalloc(sizeof(struct rtk_protect_info),
				GFP_KERNEL);
	if (!rtk_protect_info) {
		pr_err("%s:%d ERROR!\n", __func__, __LINE__);
		kfree(rtk_heap);
		cma_release(cma, pages, cma->count);
		rtk_heap = NULL;
		goto out;
	}

	rtk_protect_info->create_info.mem.base = base;
	rtk_protect_info->create_info.mem.size = cma->count << PAGE_SHIFT;
	rtk_protect_info->create_info.mem.type = (enum e_notifier_protect_type)
					RTK_PROTECTED_TYPE_GET(cma_flags);

	ret = rtk_protect_create(&rtk_protect_info->create_info);
	if (ret) {
		pr_err("%s:%d rtk_protect_create_notify return ERROR!"
			" (priv_virt=0x%lx)\n", __func__, __LINE__, 
			(long unsigned int)
			rtk_protect_info->create_info.priv_virt);
		kfree(rtk_protect_info);
		kfree(rtk_heap);
		cma_release(cma, pages, cma->count);
		rtk_heap = NULL;
		goto out;
	}


	INIT_LIST_HEAD(&rtk_heap->list);
	INIT_LIST_HEAD(&rtk_heap->elist);

	list_add(&rtk_protect_info->list, &rtk_heap->list);



	dma_coerce_mask_and_coherent(dma_heap_get_dev
			(rtk_heap->heap), DMA_BIT_MASK(32));
	mb();

	if (!(cma_flags & RTK_FLAG_SUPPORT_NONCACHED))
		goto out;

	exp_info.name = kasprintf(GFP_KERNEL,
				"%s_uncached", name);
	exp_info.ops =  &static_uncached_protect_cma_ops;
	exp_info.priv = rtk_heap;

	rtk_heap->uncached_ops = &static_uncached_protect_cma_ops;
	rtk_heap->uncached_heap = dma_heap_add(&exp_info);
	if (IS_ERR_OR_NULL(rtk_heap->uncached_heap)) {

		kfree(rtk_protect_info);
		kfree(rtk_heap);
		rtk_heap = NULL;
		goto out;
	}

	dma_coerce_mask_and_coherent(dma_heap_get_dev
			(rtk_heap->uncached_heap), DMA_BIT_MASK(32));
	mb();

	static_uncached_protect_cma_ops.allocate =
				rtk_static_uncached_protect_cma_allocate;

out:
	return (void *)rtk_heap;

}


static void *rtk_static_proctect_gen_create(unsigned long base,
				 unsigned long size, char *name, u32 flags)
{
	struct rtk_protect_info *rtk_protect_info;
	struct rtk_heap *rtk_heap;
	struct dma_heap_export_info exp_info;
	int ret;

	rtk_heap = kzalloc(sizeof(*rtk_heap), GFP_KERNEL);
	if (!rtk_heap)
		return NULL;

	INIT_LIST_HEAD(&rtk_heap->task_list);

	rtk_heap->gen_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!rtk_heap->gen_pool) {
		kfree(rtk_heap);
		pr_err("%s:%d gen_pool_create \n", __func__, __LINE__);
		rtk_heap = NULL;
		goto out;
	}

	gen_pool_set_algo(rtk_heap->gen_pool, gen_pool_best_fit, NULL);
	gen_pool_add(rtk_heap->gen_pool, base, size, -1);

	exp_info.name = name;
	exp_info.ops = &static_protect_gen_ops;
	exp_info.priv = rtk_heap;

	rtk_heap->ops = &static_protect_gen_ops;
	rtk_heap->flag = flags;

	rtk_heap->heap = dma_heap_add(&exp_info);
	if (IS_ERR_OR_NULL(rtk_heap->heap)) {
		kfree(rtk_heap);
		rtk_heap = NULL;
		goto out;
	}

	list_add(&rtk_heap->hlist, &gheap_list);
	mutex_init(&rtk_heap->mutex);

	dma_sync_single_for_device(dma_heap_get_dev(rtk_heap->heap),
			 base, size, DMA_BIDIRECTIONAL);

	rtk_protect_info = kzalloc(sizeof(struct rtk_protect_info),
				GFP_KERNEL);
	if (!rtk_protect_info) {
		pr_err("%s:%d ERROR!\n", __func__, __LINE__);
		kfree(rtk_heap);
		rtk_heap = NULL;
		goto out;
	}


	rtk_protect_info->create_info.mem.base = base ;

	rtk_protect_info->create_info.mem.size = size;

	rtk_protect_info->create_info.mem.type = (enum e_notifier_protect_type)
					RTK_PROTECTED_TYPE_GET(flags);

	ret = rtk_protect_create(&rtk_protect_info->create_info);
	if (ret) {
		kfree(rtk_protect_info);
		kfree(rtk_heap);
		pr_err("%s:%d rtk_protect_create return ERROR!"
			" (priv_virt=??)\n", __func__, __LINE__);
		rtk_heap = NULL;
		goto out;
	}


	INIT_LIST_HEAD(&rtk_heap->list);
	INIT_LIST_HEAD(&rtk_heap->elist);

	list_add(&rtk_protect_info->list, &rtk_heap->list);

	dma_coerce_mask_and_coherent(dma_heap_get_dev
			(rtk_heap->heap), DMA_BIT_MASK(32));
	mb();

	if (!(flags & RTK_FLAG_SUPPORT_NONCACHED))
		goto out;

	exp_info.name = kasprintf(GFP_KERNEL,
				"%s_uncached", name);
	exp_info.ops = &static_uncached_protect_gen_ops;
	exp_info.priv = rtk_heap;

	rtk_heap->uncached_ops = &static_uncached_protect_gen_ops;
	rtk_heap->uncached_heap = dma_heap_add(&exp_info);
	if (IS_ERR_OR_NULL(rtk_heap->uncached_heap)) {
		kfree(rtk_protect_info);
		kfree(rtk_heap);
		rtk_heap = NULL;
		goto out;
	}

	dma_coerce_mask_and_coherent(dma_heap_get_dev
			(rtk_heap->uncached_heap), DMA_BIT_MASK(32));
	mb();

	static_uncached_protect_gen_ops.allocate =
				rtk_static_uncached_protect_gen_allocate;


out:
	return (void *)rtk_heap;

}
#if 0
static void rtk_static_cma_create(struct cma *cma, char *name,
		u32 static_cma_size)
{
	struct page *pages;
	size_t nr_pages = static_cma_size >> PAGE_SHIFT;

	pr_info("%s: Try to occupy the whole cma %p name %s pages:%lu\n", __func__,
			(void *)cma, name, nr_pages ? nr_pages : cma->count);

	if (static_cma_size) {
		pages = cma_alloc(cma, nr_pages, 0, GFP_KERNEL);
		if ((!pages) || (cma->base_pfn != page_to_pfn(pages)))
			goto err;
	} else {
		if (!cma_alloc(cma, cma->count, 0, GFP_KERNEL))
			goto err;
	}

	return;
err:
	/* Static CMA must 100% succeed; otherwise, disaster is coming. */
	pr_err("Failed to occupied STATIC CMA at early boot-up, PANIC!\n");
	BUG();
}
#endif

static void *rtk_gen_create(unsigned long base, unsigned long size, char *name,
				 u32 flags)
{
	struct rtk_heap *rtk_heap;
	struct dma_heap_export_info exp_info;

	rtk_heap = kzalloc(sizeof(*rtk_heap), GFP_KERNEL);
	if (!rtk_heap)
		return NULL;

	INIT_LIST_HEAD(&rtk_heap->task_list);
	/* maybe we dont need these 2 , but helpful for debugfs */
	INIT_LIST_HEAD(&rtk_heap->list);
	INIT_LIST_HEAD(&rtk_heap->elist);

	rtk_heap->gen_pool = gen_pool_create(PAGE_SHIFT, -1);

	gen_pool_set_algo(rtk_heap->gen_pool, gen_pool_best_fit, NULL);
	gen_pool_add(rtk_heap->gen_pool, base, size, -1);

	rtk_heap->flag = flags;

	exp_info.name = name;
	exp_info.ops = &gen_ops;
	exp_info.priv = rtk_heap;

	rtk_heap->ops = &gen_ops;
	rtk_heap->heap = dma_heap_add(&exp_info);
	if (IS_ERR_OR_NULL(rtk_heap->heap)) {
		kfree(rtk_heap);
		return NULL;
	}

	INIT_LIST_HEAD(&rtk_heap->elist);
	list_add(&rtk_heap->hlist, &gheap_list);
	mutex_init(&rtk_heap->mutex);

	dma_coerce_mask_and_coherent(dma_heap_get_dev
			(rtk_heap->heap), DMA_BIT_MASK(32));
	mb();

	if (!(flags & RTK_FLAG_SUPPORT_NONCACHED))
		goto out;

	exp_info.name = kasprintf(GFP_KERNEL,
				"%s_uncached", name);
	exp_info.ops = &uncached_gen_ops;
	exp_info.priv = rtk_heap;

	rtk_heap->uncached_ops = &uncached_gen_ops;
	rtk_heap->uncached_heap = dma_heap_add(&exp_info);
	if (IS_ERR_OR_NULL(rtk_heap->uncached_heap)) {
		kfree(rtk_heap);
		return NULL;
	}

	dma_coerce_mask_and_coherent(dma_heap_get_dev
			(rtk_heap->uncached_heap), DMA_BIT_MASK(32));
	mb();

	uncached_gen_ops.allocate = rtk_uncached_gen_allocate;

out:
	return (void *)rtk_heap;

}

/******************************************************************************
 ******************************************************************************/
static int best_fit_cmp(void *priv, const struct list_head *a,
			const struct list_head *b)
{
	struct rtk_best_fit *bfa = container_of(a, struct rtk_best_fit, hlist);
	struct rtk_best_fit *bfb = container_of(b, struct rtk_best_fit, hlist);

	if (bfb->score == bfa->score)
		return bfb->freed_pages > bfa->freed_pages;

	return bfb->score > bfa->score;
}

void fill_best_fit_list(char *name, unsigned long flags,
				 struct list_head *best_list)
{
	struct rtk_heap *gh;
	struct rtk_heap *ch;
	struct rtk_best_fit *best_fit;
	int score = 0;
	const int unit = 1;
	unsigned long used_bit, used_pages;

	/* check prealloc heaps first */
	list_for_each_entry(gh, &pheap_list, plist) {
		/* input flags should be a subset of gh->flag */
		if ((flags & gh->flag) != flags) {
			continue;
		}
		/* input flags ext bit should be the same as gh->flag ext bit*/
		if (rtk_protected_ext_type(flags)) {
			if (rtk_protected_ext_type(flags) !=
				rtk_protected_ext_type(gh->flag))
				continue;
		}

		score = 128;
		score += unit*2;

		if (rtk_toc_type(flags) != rtk_toc_type(gh->flag)) {
			continue;
		}

		/* input flags protection type should be the same as gh->flag */
		if (rtk_protected_type(flags)) {
			if (rtk_protected_type(flags) !=
				rtk_protected_type(gh->flag))
				continue;
		/* gh->flag has unique protection type ,
		 * but input flags is without protection type */
		} else if (is_rtk_static_protect(gh->flag)) {
			continue;
		} else if (is_rtk_dynamic_protect(gh->flag)) {
			score -= unit*2;
		}

		score -= hweight_long((gh->flag &
			 ~(RTK_FLAG_PROTECTED_MASK |
				 RTK_FLAG_PROTECTED_EXT_MASK))) * unit * 4;

		best_fit = kmalloc(sizeof(*best_fit), GFP_KERNEL);
		best_fit->data = (void *)gh;
		best_fit->tag = tag_pre;
		best_fit->name = dma_heap_get_name(gh->heap);
		best_fit->score = score;
		best_fit->freed_pages = gen_pool_avail(gh->gen_pool) >> PAGE_SHIFT;

		list_add(&best_fit->hlist, best_list);

	}

	/* general heaps */
	list_for_each_entry(gh, &gheap_list, hlist) {

		if (((flags & ~RTK_FLAG_PROTECTED_EXT_MASK) & gh->flag)
			 != (flags & ~RTK_FLAG_PROTECTED_EXT_MASK)) {
			continue;
		}

		score = 128;

		if (rtk_toc_type(flags) != rtk_toc_type(gh->flag)) {
			continue;
		}

		/* check protect type equals heap's protect type */
		if (rtk_protected_type(flags)) {
			if (rtk_protected_type(flags) !=
				rtk_protected_type(gh->flag)) {
				continue;
			}
		} else if (is_rtk_static_protect(gh->flag)) {
			continue;
		} else if (is_rtk_dynamic_protect(gh->flag)) {
			score -= unit*2;
		}

		score -= hweight_long((gh->flag &
				 ~(RTK_FLAG_PROTECTED_MASK |
				 RTK_FLAG_PROTECTED_EXT_MASK))) * unit * 4;

		best_fit = kmalloc(sizeof(*best_fit), GFP_KERNEL);
		best_fit->data = (void *)gh;
		best_fit->tag = tag_gen;
		best_fit->name = dma_heap_get_name(gh->heap);
		best_fit->score = score;
		best_fit->freed_pages = gen_pool_avail(gh->gen_pool) >> PAGE_SHIFT;

		list_add(&best_fit->hlist, best_list);

	}

	/* cma heaps */
	list_for_each_entry(ch, &cheap_list, hlist) {
		/* Exclusive pool doesn't support non-secure memory */
		if (!rtk_protected_type(flags) && is_rtk_exclusive_pool(ch->flag))
			continue;

		if (((flags & ~RTK_FLAG_PROTECTED_EXT_MASK) & ch->flag)
			 != (flags & ~RTK_FLAG_PROTECTED_EXT_MASK)) {
			continue;
		}

		score = 128;
		score -= unit*2;

		if (rtk_toc_type(flags) != rtk_toc_type(ch->flag)) {
			continue;
		}

		if (rtk_protected_type(flags)) {
			if (rtk_protected_type(flags) !=
				rtk_protected_type(ch->flag)) {
				continue;
			}
		} else if (is_rtk_static_protect(ch->flag)) {
			continue;
		} else if (is_rtk_dynamic_protect(ch->flag)) {
			score -= unit*2;
		}

		score -= hweight_long((ch->flag &
				 ~(RTK_FLAG_PROTECTED_MASK |
				 RTK_FLAG_PROTECTED_EXT_MASK))) * unit * 4;

		best_fit = kmalloc(sizeof(*best_fit), GFP_KERNEL);
		best_fit->data = (void *)ch;
		best_fit->tag = tag_cma;
		best_fit->name = dma_heap_get_name(ch->heap);
		best_fit->score = score;
		used_bit = bitmap_weight(ch->use_bitmap,
					 (int)cma_bitmap_maxno(ch->cma));
		used_pages = used_bit << ch->cma->order_per_bit;
		best_fit->freed_pages = (ch->cma->count) - used_pages;

		list_add(&best_fit->hlist, best_list);

	}

	list_sort(NULL, best_list, best_fit_cmp);
}

static void dump_pool_bitmap(struct gen_pool *pool)
{
	struct gen_pool_chunk *chunk;
	unsigned long bitmap_maxno;
	u32 u8s;

	bitmap_maxno = gen_pool_size(pool) >> pool->min_alloc_order;

	u8s = DIV_ROUND_UP(bitmap_maxno, BITS_PER_BYTE);
	rcu_read_lock();
	chunk = list_first_or_null_rcu(&pool->chunks, struct gen_pool_chunk,
					next_chunk);
	rcu_read_unlock();

	print_hex_dump(KERN_DEBUG, "bitmap", DUMP_PREFIX_OFFSET,
			16, 4,
			chunk->bits, u8s, false);

}

void dump_best_fit_info(struct rtk_best_fit *best_fit)
{
	struct rtk_heap_task *rtk_heap_task, *tmp;
	char *name;
	unsigned long base;
	size_t size;
	struct rtk_protect_info *rtk_protect_info, *p_tmp;

	pr_debug("\033[1;32m"
		"Warning............heap : %s"
		"\033[m\n", best_fit->name);

	if (best_fit->tag == tag_pre || best_fit->tag == tag_gen) {
		struct rtk_heap *gh = (struct rtk_heap *)best_fit->data;
		struct list_head *protect_list = &gh->list;

		mutex_lock(&gh->mutex);

		pr_debug("flags : 0x%x \n", gh->flag);
		pr_debug("%s \n", name = ka_dispflag(gh->flag, GFP_KERNEL));
		kfree(name);
		list_for_each_entry_safe(rtk_heap_task, tmp, &gh->task_list,
			list) {
			pr_debug("task: %s     0x%lx \n", rtk_heap_task->comm,
				rtk_heap_task->size);
		}
		list_for_each_entry_safe(rtk_protect_info, p_tmp, protect_list,
						 list) {
			base = rtk_protect_info->create_info.mem.base;
			size = rtk_protect_info->create_info.mem.size;
			pr_debug("protect base : 0x%lx , size : 0x%lx \n",
					 base, size);
		}

		dump_pool_bitmap(gh->gen_pool);

		mutex_unlock(&gh->mutex);
	} else {
		struct rtk_heap *ch = (struct rtk_heap *)best_fit->data;
		struct list_head *protect_list = &ch->list;
		u32 u8s = DIV_ROUND_UP(cma_bitmap_maxno(ch->cma),
					 BITS_PER_BYTE );

		mutex_lock(&ch->mutex);

		pr_debug("flags : 0x%x \n", ch->flag);
		pr_debug("%s \n", name = ka_dispflag(ch->flag, GFP_KERNEL));
		kfree(name);
		list_for_each_entry_safe(rtk_heap_task, tmp, &ch->task_list,
			list) {
			pr_debug("task: %s     0x%lx \n", rtk_heap_task->comm,
				rtk_heap_task->size);
		}
		list_for_each_entry_safe(rtk_protect_info, p_tmp, protect_list,
						 list) {
			base = rtk_protect_info->create_info.mem.base;
			size = rtk_protect_info->create_info.mem.size;
			pr_debug("protect base : 0x%lx , size : 0x%lx \n",
					 base, size);
		}

		print_hex_dump(KERN_DEBUG, "bitmap", DUMP_PREFIX_OFFSET,
				16, 4,
				ch->use_bitmap, u8s, false);

		mutex_unlock(&ch->mutex);
	}

	return;
}

static struct dma_buf *rheap_alloc_best_fit(char *name, unsigned long len,
			 unsigned long flags, struct list_head *best_list)
{
	struct rtk_heap *gh;
	struct rtk_heap *ch;
	struct dma_heap *heap = NULL;
	bool uncached = (flags & RTK_FLAG_NONCACHED) ? true : false;
	struct rtk_best_fit *best_fit;
	struct dma_buf *dmabuf = NULL;

	fill_best_fit_list(name, flags, best_list);

	list_for_each_entry(best_fit, best_list, hlist)
		pr_debug("flags=0x%lx candidate heap=%s score=%d freed_pages=%lu\n",
			flags,  best_fit->name, best_fit->score, best_fit->freed_pages);

	list_for_each_entry(best_fit, best_list, hlist) {
		if (best_fit->tag == tag_pre ||
				 best_fit->tag == tag_gen) {

			gh = (struct rtk_heap *)best_fit->data;
			if (uncached) {
				heap = gh->uncached_heap;
				dmabuf = gh->uncached_ops->allocate(heap, len,
						 0, flags);
			}
			else {
				heap = gh->heap;
				dmabuf = gh->ops->allocate(heap, len, 0, flags);
			}
			if (!IS_ERR_OR_NULL(dmabuf)) {
				pr_debug("flags = 0x%lx atari heap = %s\n",
					flags, best_fit->name);
				break;
			}
		}

		if (best_fit->tag == tag_cma ) {

			ch = (struct rtk_heap *)best_fit->data;
			if (uncached) {
				heap = ch->uncached_heap;
				dmabuf = ch->uncached_ops->allocate(heap, len,
						 0, flags);
			}
			else {
				heap = ch->heap;
				dmabuf = ch->ops->allocate(heap, len, 0, flags);
			}
			if (!IS_ERR_OR_NULL(dmabuf)) {
				pr_debug("flags = 0x%lx atari heap = %s\n",
					flags, best_fit->name);
				break;
			}
		}
	}

	if (IS_ERR_OR_NULL(dmabuf)) {
		list_for_each_entry(best_fit, best_list, hlist)
			dump_best_fit_info(best_fit);
	}

	for (;;) {
		if (list_empty(best_list))
			break;
		best_fit = list_last_entry(best_list,
				     struct rtk_best_fit,
				     hlist);
		list_del(&best_fit->hlist);
		kfree(best_fit);
	}
	return dmabuf;
}

void rtk_check_flag(unsigned long *flags)
{
	struct rtk_flag_replace *match;
	unsigned long condition;

	for (match = rtk_flag_match; match->condition; match++) {
		condition = match->condition | RTK_FLAG_PROTECTED_MASK |
				 RTK_FLAG_PROTECTED_EXT_MASK;

		if ((*flags & condition) == match->condition) {
 			*flags &= ~match->condition;
			*flags |= match->replace;
			break;
		}
	}

}

/*******************************************************************************
 * main alloc function
 ******************************************************************************/
struct dma_buf *rheap_alloc(char *name, unsigned long len, unsigned long flags)
{
	bool uncached = (flags & RTK_FLAG_NONCACHED) ? true : false;
	struct dma_buf *dmabuf = NULL;
	char *r_name;
 	LIST_HEAD(best_list);

	pr_debug("%s(%pS) name=%s len=0x%lx flags=0x%lx uncached=%s \n", __func__,
		 __builtin_return_address(0), name, len, flags,
		uncached ? "true" : "false");

	rtk_check_flag(&flags);

	/* some users do the fault */
	if ((flags & RTK_FLAG_NONCACHED) && !(flags & RTK_FLAG_SCPUACC))
		flags &= ~RTK_FLAG_NONCACHED;
	/* some users do the fault
	 * There is no need to zero out secure memory, TEE will cover that */
	if (rtk_protected_type(flags))
		flags &= ~RTK_FLAG_SKIP_ZERO;
	/* some users dont know how to do */
	if ((flags & RTK_FLAG_POOL_CONDITION) == 0) {
		pr_info(" Warning: flags is 0x%lx!! "
			 "the default value is set (RTK_FLAG_ACPUACC | "
			 "RTK_FLAG_SCPUACC | RTK_FLAG_HWIPACC | "
			 "RTK_FLAG_NONCACHED) \n",
			flags);
		flags = RTK_FLAG_ACPUACC | RTK_FLAG_SCPUACC |
			RTK_FLAG_HWIPACC | RTK_FLAG_NONCACHED;
	}

	dmabuf = rheap_alloc_best_fit(name, len, flags, &best_list);

	if (IS_ERR_OR_NULL(dmabuf)) {
		pr_err("\033[1;32m"
			 "Couldn't find/alloc the heap by your node name %s "
			 "your flag 0x%lx from %ps "
			 "your size 0x%lx"
			 "\033[m\n", name, flags, (void *)_RET_IP_, len);
		return ERR_PTR(-ENOMEM);
	}

	dma_buf_set_name(dmabuf, r_name = kasprintf(GFP_KERNEL, "%ps",
						 (void *)_RET_IP_));
	kfree(r_name);

	return dmabuf;

}
EXPORT_SYMBOL_GPL(rheap_alloc);
#if 0
static unsigned long pre_alloc(struct rtk_heap *rtk_heap, u32 flags, u32 len,
				 bool gen, bool _static)
{
	unsigned long offset = 0;
	struct cma *cma;
	unsigned long nr_pages, align;
	unsigned long bitmap_count;
	unsigned long pfn;
	struct page *pages;
	int bit_id;
	struct rtk_protect_ext_info *rtk_protect_ext_info;
	struct rtk_protect_info *rtk_protect_info;

	if (gen) {
		/* gen heap only has general and static protectect type */
		offset = gen_pool_alloc(rtk_heap->gen_pool, (size_t)len);
		if (!_static)
			goto out;
		if (!rtk_protected_ext_type(flags))
			goto out;

		rtk_protect_info = find_protect_info(rtk_heap,
					 offset, true);
		if (!rtk_protect_info) {
			pr_err("%s %d rtk_protect_info = 0x%p\n", __func__,
				 __LINE__, rtk_protect_info);
			BUG();
		}

		rtk_protect_ext_info = create_protect_ext_info(flags, offset,
						 len, rtk_protect_info);

		if (rtk_protect_ext_info == NULL) {
			/* TODO : free gen pool*/
			BUG();
			offset = 0;
			goto out;
		}
		list_add(&rtk_protect_ext_info->list, &rtk_heap->elist);
		goto out;
	}

	if (_static){
		/* cma heap with static protect */
		align = get_order(SZ_2M);
		len = ALIGN(len, SZ_2M);
		pages = rtk_cma_alloc(rtk_heap, len >> PAGE_SHIFT, align);
		if (!pages)
			goto out;
		offset = page_to_phys(pages);
		rtk_protect_info = find_protect_info(rtk_heap,
					 offset, false);
		if (!rtk_protect_info) {
			pr_err("%s %d rtk_protect_info = 0x%p\n", __func__,
				 __LINE__, rtk_protect_info);
			BUG();
		}

		rtk_protect_ext_info = create_protect_ext_info(flags, offset,
						 len, rtk_protect_info);

		if (rtk_protect_ext_info == NULL) {
			/* TODO : free gen pool*/
			BUG();
			offset = 0;
			goto out;
		}
		list_add(&rtk_protect_ext_info->list, &rtk_heap->elist);

	} else {
		/* cma heap with dynamic protect */
		align = get_order(SZ_2M);
		len = ALIGN(len, SZ_2M);
		nr_pages = len >> PAGE_SHIFT;

		cma = rtk_heap->cma;
		pages = cma_alloc(cma, len >> PAGE_SHIFT, align, GFP_KERNEL);
		if (!pages)
			goto out;
		offset = page_to_phys(pages);
		bitmap_count = ALIGN(nr_pages,
			 1UL << cma->order_per_bit) >> cma->order_per_bit;
		pfn = __page_to_pfn(pages);
		bit_id = (pfn - cma->base_pfn) >> cma->order_per_bit;
		bitmap_set(rtk_heap->alloc_bitmap, bit_id, bitmap_count);
		bitmap_set(rtk_heap->use_bitmap, bit_id, bitmap_count);

	}
out:

	return offset;
}
#endif
/*****************************************************************************
 ****************************************************************************/
/*****************************************************************************
 ****************************************************************************/
__attribute__((unused)) static void dmabuf_heap_flags_validation(void *data, struct dma_heap *heap,
				 size_t len, unsigned int fd_flags,
				 unsigned int heap_flags, bool *skip)
{
	*skip = true;
	return;

}
/*****************************************************************************
 ****************************************************************************/
static ssize_t best_fit_heap_store(const struct class *class,
				    const struct class_attribute *attr,
				    const char *buf,
				    size_t count)
{
	sys_flags = simple_strtoul(buf, NULL, 0);
	return count;
}

static ssize_t best_fit_heap_show(const struct class *class,
				 const struct class_attribute *attr,
				 char *buf)
{
	LIST_HEAD(best_list);
	struct rtk_best_fit *best_fit;
	bool uncached = (sys_flags & RTK_FLAG_NONCACHED) ? true : false;
	int n = 0;

	if (sys_flags != 0) {
		fill_best_fit_list(NULL,  sys_flags, &best_list);
		list_for_each_entry(best_fit, &best_list, hlist) {
			if (uncached)
				n += scnprintf(buf+n, TMP_BUF_MAX-n,
					 "%s_uncached : %u \n",
					 best_fit->name, best_fit->score);
			else
				n += scnprintf(buf+n, TMP_BUF_MAX-n,
					 "%s : %u \n",
					 best_fit->name, best_fit->score);
		}
		for (;;) {
			if (list_empty(&best_list))
				break;
			best_fit = list_last_entry(&best_list,
				     struct rtk_best_fit,
				     hlist);
			list_del(&best_fit->hlist);
			kfree(best_fit);
		}
		sys_flags = 0;
		return n;
	}
	return -EINVAL;
}

static CLASS_ATTR_RW(best_fit_heap);


struct class *rtk_heap_class = NULL;
/******************************************************************************
 *
 *
 *****************************************************************************/
static int rheap_cma_check(struct cma *cma, void *data)
{
	unsigned long flag;
	int i;

	for (i = 0; i < ARRAY_SIZE(rheap_data); i++) {

		if (strcmp(cma->name, rheap_data[i].name))
			continue;

		pr_info("rheap %s\n", cma->name);

		flag = rheap_data[i].flags;
		if (flag & RTK_FLAG_PROTECTED_DYNAMIC) {
			rtk_dynamic_proctect_cma_create(
				cma, (char *)rheap_data[i].devname, flag);
			goto out;
		}

		if (flag & RTK_FLAG_PROTECTED_MASK) {
			rtk_static_proctect_cma_create(
				cma, (char *)rheap_data[i].devname, flag);
			goto out;
		}
#if 0
		if (flag & RTK_FLAG_STATIC_CMA) {
			rtk_static_cma_create(cma,
				 (char *)rheap_data[i].devname,
				rheap_data[i].static_cma_size);
			goto out;
		}
#endif
		rtk_cma_create(cma, (char *)rheap_data[i].devname, flag);
	}
out:
	return 0;
}

static int rheap_rmem_check(void)
{
	unsigned long flag;
	bool skip;
	struct device_node *node;
	struct resource res;
	int i, ret;

	node = of_find_compatible_node(NULL, NULL, "metadata");
	if (!node) {
		pr_err("%s: Unable to get metadata node", __func__);
		goto out;
	}

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		pr_err("%s: Unable to get resource", __func__);
		goto out;
	}

	for (i = 0; i < ARRAY_SIZE(rheap_data); i++) {

		if (strcmp("metadata", rheap_data[i].name))
			continue;

		flag = rheap_data[i].flags;
		skip = rheap_data[i].skip_set_protect;

		pr_info("rheap %s\n", rheap_data[i].name);

		if (!skip && flag & RTK_FLAG_PROTECTED_MASK) {

			rtk_static_proctect_gen_create(res.start,
					(unsigned long) resource_size(&res),
					(char *)rheap_data[i].devname, flag);
		} else {

			rtk_gen_create(res.start,
					(unsigned long) resource_size(&res),
					(char *)rheap_data[i].devname, flag);
		}
		break;
	}

out:

	return 0;
}




static int __init rheap_init(void)
{
	int ret;

	if (!rtk_protect_handler_ready()) {
		pr_err("no protect region handler\n");
		return -EINVAL;
	}

	cma_for_each_area(rheap_cma_check, NULL);
	rheap_rmem_check();

	rtk_heap_class = class_create(DEVNAME);
	if (IS_ERR(rtk_heap_class)) {
		ret = PTR_ERR(rtk_heap_class);
		rtk_heap_class = NULL;
		return ret;
	}

	ret = class_create_file(rtk_heap_class, &class_attr_best_fit_heap);
	if (ret) {
		pr_err("create class file failed\n");
		return -EINVAL;
	}

#ifdef CONFIG_ANDROID_VENDOR_HOOKS
	if (IS_ENABLED(CONFIG_TRACEPOINTS) &&
			 IS_ENABLED(CONFIG_ANDROID_VENDOR_HOOKS))
		register_trace_android_vh_dmabuf_heap_flags_validation(
		dmabuf_heap_flags_validation, NULL);
#endif

	rheap_debugfs_init();
	rheap_miscdev_register();

	return 0;

}

fs_initcall(rheap_init);

MODULE_DESCRIPTION("DMA-BUF RTK Heap");
MODULE_LICENSE("GPL v2");
