// SPDX-License-Identifier: GPL-2.0
/*
 * DMA operations that map to virtual addresses .
 */
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/dma-map-ops.h>
#include <linux/scatterlist.h>
#include <linux/dma-direct.h>
#include <soc/realtek/memory.h>
#include <soc/realtek/rtk_media_heap.h>
#include <linux/dma-heap.h>

struct rheap_list {
	void *cpu_addr;
	dma_addr_t *dma_handle;
	struct rtk_heap *rtk_heap;
	size_t size;
	unsigned long flags;
	struct list_head plist;
	char *exp_name;
};

struct dma_pool {
	struct rtk_heap *rtk_heap;
	unsigned long flags;
	struct list_head pools;
	char caller[TASK_COMM_LEN];
};

LIST_HEAD(particle_list);
DEFINE_MUTEX(particle_mutex);

extern bool is_rtk_static_protect(int flags);
extern bool is_rtk_dynamic_protect(int flags);
extern bool is_rtk_gen_heap(int flags);
extern void rtk_check_flag(unsigned long *flags);
extern void rtk_static_secure_free(struct rtk_heap *rtk_heap,
				 struct page *pages, size_t size, char *name);
extern void rtk_dynamic_secure_free(struct rtk_heap *rtk_heap,
				 struct page *pages, size_t size, char *name);
extern void rtk_normal_free(struct rtk_heap *rtk_heap, struct page *pages,
				 size_t size, char *name);
extern void rtk_pool_free(struct rtk_heap *rtk_heap, struct page *pages,
				size_t size, char *name);
extern struct page *rtk_static_secure_allocate(struct rtk_heap *rtk_heap,
				 size_t size, unsigned long flags, char *caller);
extern struct page *rtk_dynamic_secure_allocate(struct rtk_heap *rtk_heap,
				 size_t size, unsigned long flags, char *caller);
extern struct page *rtk_normal_allocate(struct rtk_heap *rtk_heap,
				 size_t size, unsigned long flags, char *caller);
extern struct page *rtk_pool_allocate(struct rtk_heap *rtk_heap,
				 size_t size, unsigned long flags, char *caller);
extern void fill_best_fit_list(char *name, unsigned long flags,
				 struct list_head *best_list);

/******************************************************************************
 *****************************************************************************/
static void dma_pools_release(struct device *dev, void *res)
{
	struct list_head *dma_pools;
	struct dma_pool *dma_pool;

	dma_pools = (struct list_head *)res;

        for (;;) {
                if (list_empty(dma_pools))
			break;
                dma_pool = list_last_entry(dma_pools,
                                        struct dma_pool,
					pools);
		list_del(&dma_pool->pools);
		kfree(dma_pool);
	}


}

static struct page *rtk_secure_allocate(struct rtk_heap *rtk_heap, size_t size,
				 dma_addr_t *dma_handle, unsigned long flags, char *caller)
{
	struct page *pages;

	pages = rtk_static_secure_allocate(rtk_heap, size, flags, caller);
	if (!pages) {
		pr_debug("%s alloc fail\n", __func__);
	}

	return pages;
}

static struct page *rtk_dyn_secure_allocate(struct rtk_heap *rtk_heap,
				 size_t size, dma_addr_t *dma_handle,
				 unsigned long flags, char *caller)
{
	struct page *pages;

	pages = rtk_dynamic_secure_allocate(rtk_heap, size, flags, caller);
	if (!pages) {
		pr_debug("%s alloc fail\n", __func__);
	}

	return pages;

}

static struct page *rtk_nonsecure_allocate(struct rtk_heap *rtk_heap,
					 size_t size, dma_addr_t *dma_handle,
					 unsigned long flags, char *caller)
{
	struct page *pages;

	pages = rtk_normal_allocate(rtk_heap, size, flags, caller);
	if (!pages) {
		pr_debug("%s alloc fail\n", __func__);
	}

	return pages;
}

static struct page *rtk_genpool_allocate(struct rtk_heap *rtk_heap, size_t size,
				 dma_addr_t *dma_handle, unsigned long flags, char *caller)
{
	struct page *pages;

	pages = rtk_pool_allocate(rtk_heap, size, flags, caller);
	if (!pages) {
		pr_debug("%s alloc fail\n", __func__);
	}

	return pages;
}
/******************************************************************************
 *****************************************************************************/
static void *dma_rtk_contiguous_remap(struct page *page, size_t size,
			pgprot_t prot)
{
	int count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	struct page **pages;
	void *vaddr;
	int i;

	pages = kmalloc_array(count, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return NULL;
	for (i = 0; i < count; i++)
		pages[i] = nth_page(page, i);
	vaddr = vmap(pages, count, VM_DMA_COHERENT, prot);
	kfree(pages);

	return vaddr;
}



static void *dma_rtk_allocate(struct device *dev, struct rtk_heap *rtk_heap,
				 size_t size, dma_addr_t *dma_handle,
				 unsigned long flags, unsigned long attrs, char *caller)
{
	struct page *pages;
	void *ret = NULL;

	mutex_lock(&rtk_heap->mutex);

	if (is_rtk_gen_heap(rtk_heap->flag))
		pages = rtk_genpool_allocate(rtk_heap, size, dma_handle, flags, caller);
	else if (is_rtk_dynamic_protect(rtk_heap->flag))
		pages = rtk_dyn_secure_allocate(rtk_heap, size,
							 dma_handle, flags, caller);
	else if (is_rtk_static_protect(rtk_heap->flag))
		pages = rtk_secure_allocate(rtk_heap, size, dma_handle, flags, caller);
	else
		pages = rtk_nonsecure_allocate(rtk_heap, size, dma_handle,
						 flags, caller);
	mutex_unlock(&rtk_heap->mutex);

	if (pages == NULL)
		goto out;

	/* TODO : if iommu exists , should do rtk_map_page to get *dma_handle */
	*dma_handle = page_to_phys(pages);

	if (attrs & DMA_ATTR_NO_KERNEL_MAPPING) {
		ret = page_address(pages);
		goto out;
	}

	if ((IS_ENABLED(CONFIG_DMA_DIRECT_REMAP) && !dev_is_dma_coherent(dev))
		 || (IS_ENABLED(CONFIG_DMA_REMAP) && PageHighMem(pages))) {

		/* only support contiguous physical allocation */
		ret = dma_rtk_contiguous_remap(pages, size,
				pgprot_dmacoherent(PAGE_KERNEL));
		if (!ret) {
			/* TODO : release rtk heap buffer */
			pr_err("\033[1;32m"
			" %s remap error !!!! "
			"\033[m\n", __func__);
			BUG();
			goto out;
		}
		pr_debug("%s remap 0x%lx\n", __func__, (uintptr_t)ret);

	} else {
		ret = page_address(pages);
	}

out:
	return ret;
}

static void dma_rtk_free(struct rtk_heap *rtk_heap, size_t size,
				 dma_addr_t dma_addr, unsigned long flags,
				 char *name)
{
	struct page *pages;

	mutex_lock(&rtk_heap->mutex);

	pages = phys_to_page((unsigned long)dma_addr);

	if (is_rtk_gen_heap(rtk_heap->flag))
		rtk_pool_free(rtk_heap, pages, size, name);
	else if (is_rtk_dynamic_protect(rtk_heap->flag))
		rtk_dynamic_secure_free(rtk_heap, pages, size, name);
	else if (is_rtk_static_protect(rtk_heap->flag))
		rtk_static_secure_free(rtk_heap, pages, size, name);
	else
		rtk_normal_free(rtk_heap, pages, size, name);

	mutex_unlock(&rtk_heap->mutex);
	return;
}

static void add_rheap_list(void *cpu_addr, dma_addr_t *dma_handle,
			 struct rtk_heap *rtk_heap, size_t size,
			 unsigned long flags)
{
	struct rheap_list *rheap_list;

	rheap_list = kzalloc(sizeof(struct rheap_list), GFP_KERNEL);

	rheap_list->cpu_addr = cpu_addr;
	rheap_list->dma_handle = dma_handle;
	rheap_list->rtk_heap = rtk_heap;
	rheap_list->size = size;
	rheap_list->flags = flags;
	rheap_list->exp_name = (char *)kzalloc(TASK_COMM_LEN, GFP_KERNEL);
	strncpy((char *)rheap_list->exp_name, current->comm, TASK_COMM_LEN);
	mutex_lock(&particle_mutex);
	list_add(&rheap_list->plist, &particle_list);
	mutex_unlock(&particle_mutex);

	return;
}




static void *dma_rheap_alloc(struct device *dev, size_t size,
			    dma_addr_t *dma_handle, gfp_t gfp,
			    unsigned long attrs)
{
	struct dma_pool *dma_pool, *tmp;
	struct rtk_heap *rtk_heap;
	struct list_head *dma_pools;
	void *ret = NULL;

	dma_pools = devres_find(dev, dma_pools_release, NULL, NULL);
	if (!dma_pools) {
		pr_err("\033[1;32m"
			" %s can't find dma pools"
			"\033[m\n", __func__);
		return ret;
	}

	list_for_each_entry_safe(dma_pool, tmp, dma_pools, pools) {
		rtk_heap = dma_pool->rtk_heap;
		if (!rtk_heap)
			continue;
		ret = dma_rtk_allocate(dev, rtk_heap, size, dma_handle,
					 dma_pool->flags, attrs, dma_pool->caller);
		if(ret) {

			add_rheap_list(ret, dma_handle, dma_pool->rtk_heap,
					 size, dma_pool->flags);
			pr_debug("%s ret=0x%lx \n", __func__, (uintptr_t)ret);
			break;
		}
	}

	if (!ret) {
		pr_err("\033[1;32m"
			" %s can't find space for allocating"
			"\033[m\n", __func__);
		WARN_ON(1);
	}

	return ret;
}

#define low(x) ((u64)x & 0xffffffff) /* someone dont like 64 bits */

static void dma_rheap_free(struct device *dev, size_t size,
			  void *cpu_addr, dma_addr_t dma_addr,
			  unsigned long attrs)
{
	struct rheap_list *rheap_list, *tmp;
	struct rtk_heap *rtk_heap;


	mutex_lock(&particle_mutex);

	list_for_each_entry_safe(rheap_list, tmp, &particle_list, plist) {
		pr_debug("%s cpu_addr =0x%lx rheap_list->cpu_addr=0x%lx \n",
				 __func__, (uintptr_t)cpu_addr, (uintptr_t)rheap_list->cpu_addr);

		if (low(cpu_addr) == low(rheap_list->cpu_addr)) {

			rtk_heap = rheap_list->rtk_heap;

			if (IS_ENABLED(CONFIG_DMA_REMAP) &&
					 is_vmalloc_addr(cpu_addr)) {
				vunmap(cpu_addr);
				pr_debug("%s remap free 0x%lx \n", __func__,
						 (uintptr_t)cpu_addr);
			}

			dma_rtk_free(rtk_heap, size, dma_addr,
					 rheap_list->flags,
					 rheap_list->exp_name);

			list_del(&rheap_list->plist);

			kfree(rheap_list->exp_name);
			kfree(rheap_list);
			break;
		}
	}

	mutex_unlock(&particle_mutex);
}

pgprot_t dma_rheap_pgprot(struct device *dev, pgprot_t prot, unsigned long attrs)
{
	if (force_dma_unencrypted(dev))
		prot = pgprot_decrypted(prot);
	if (dev_is_dma_coherent(dev))
		return prot;
#ifdef CONFIG_ARCH_HAS_DMA_WRITE_COMBINE
	if (attrs & DMA_ATTR_WRITE_COMBINE)
		return pgprot_writecombine(prot);
#endif
	return pgprot_dmacoherent(prot);
}

static int dma_rheap_mmap(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs)
{
	unsigned long nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long pfn, off = vma->vm_pgoff;

	vma->vm_page_prot = dma_rheap_pgprot(dev, vma->vm_page_prot, attrs);

	if (off >= nr_pages || vma_pages(vma) > nr_pages - off)
		return -ENXIO;

	if (is_vmalloc_addr(cpu_addr)) {
                /* only support contiguous physical allocation */
		pfn = vmalloc_to_pfn(cpu_addr);
	} else {
		pfn = page_to_pfn(virt_to_page(cpu_addr));
	}

	return remap_pfn_range(vma, vma->vm_start, pfn + off,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}


int dma_rheap_get_sgtable(struct device *dev, struct sg_table *sgt,
                 void *cpu_addr, dma_addr_t dma_addr, size_t size,
                 unsigned long attrs)
{
	struct page *page;
	int ret;

	if (is_vmalloc_addr(cpu_addr)) {
                /* only support contiguous physical allocation */
		page = vmalloc_to_page(cpu_addr);
	} else {
		page = virt_to_page(cpu_addr);
	}

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (!ret)
		sg_set_page(sgt->sgl, page, PAGE_ALIGN(size), 0);
	return ret;
}

int dma_rheap_map_sg(struct device *dev, struct scatterlist *sgl, int nents,
		enum dma_data_direction dir, unsigned long attrs)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, nents, i) {
		phys_addr_t phys = page_to_phys(sg_page(sg)) + sg->offset;
		dma_addr_t dma_addr = (dma_addr_t)phys;

		sg->dma_address = dma_addr;

		if (!dev_is_dma_coherent(dev) &&
			 !(attrs & DMA_ATTR_SKIP_CPU_SYNC))
			dma_sync_single_for_device(dev,
					phys, sg->length, DMA_BIDIRECTIONAL);

		sg_dma_len(sg) = sg->length;
	}

	return nents;
}

void dma_rheap_unmap_sg(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i) {
		phys_addr_t phys = (phys_addr_t)sg->dma_address ;

		if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC) &&
			!dev_is_dma_coherent(dev)) {
			dma_sync_single_for_cpu(dev, phys, sg->length
					, DMA_BIDIRECTIONAL);
		}
	}
}

const struct dma_map_ops rheap_dma_ops = {
	.alloc			= dma_rheap_alloc,
	.free			= dma_rheap_free,
	.mmap			= dma_rheap_mmap,
	.get_sgtable		= dma_rheap_get_sgtable,
	.map_sg			= dma_rheap_map_sg,
	.unmap_sg		= dma_rheap_unmap_sg,

};
EXPORT_SYMBOL(rheap_dma_ops);

void rheap_setup_dma_pools(struct device *dev, char *name,
				 unsigned long heap_flags, const char *caller)
{
	struct rtk_best_fit *best_fit;
	struct rtk_heap *rtk_heap;
	struct dma_pool *dma_pool;
	struct list_head *dma_pools;

	LIST_HEAD(best_list);

	pr_debug("%s(%pS)...\n", __func__,  __builtin_return_address(0));

	devres_release(dev, dma_pools_release, NULL, NULL);

	rtk_check_flag(&heap_flags);

	dma_pools = devres_alloc(dma_pools_release, sizeof(*dma_pools),
				 GFP_KERNEL);
	INIT_LIST_HEAD(dma_pools);

	fill_best_fit_list(name,  heap_flags, &best_list);

	list_for_each_entry(best_fit, &best_list, hlist) {
		rtk_heap = (struct rtk_heap *)best_fit->data;
		dma_pool = kzalloc(sizeof(struct dma_pool), GFP_KERNEL);
		dma_pool->rtk_heap = rtk_heap;
		dma_pool->flags = heap_flags;
		strlcpy(dma_pool->caller, caller, sizeof(dma_pool->caller));
		list_add_tail_rcu(&dma_pool->pools, dma_pools);
	}

	devres_add(dev, dma_pools);

	for (;;) {
		if (list_empty(&best_list))
			break;
		best_fit = list_last_entry(&best_list,
					struct rtk_best_fit,
					hlist);
		list_del(&best_fit->hlist);
		kfree(best_fit);
	}

	return ;
}
EXPORT_SYMBOL(rheap_setup_dma_pools);
