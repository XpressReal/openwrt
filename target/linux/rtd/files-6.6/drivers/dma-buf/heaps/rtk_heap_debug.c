// SPDX-License-Identifier: GPL-2.0
/*
 * RTK HEAP DebugFS Interface
 *
 * Copyright (c) 2022 <cy.huang@realtek.com>
 */


#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/genalloc.h>
#include <linux/cma.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <soc/realtek/rtk_media_heap.h>
#include <soc/realtek/memory.h>
#include "rtk_protect.h"

#include "cma.h"

struct dentry *rheap_debugfs_root = NULL;

static void bitmap_show(struct seq_file *m, u8 *ptr, u32 len)
{

	int i, linelen, remaining = len;
	unsigned char linebuf[32 * 3 + 2 + 32 + 1];


	for (i = 0; i < len; i += 16) {
		linelen = min(remaining, 16);
		remaining -= 16;

		hex_dump_to_buffer(ptr + i, linelen, 16, 4,
				   linebuf, sizeof(linebuf), false);

		seq_printf(m, "%.8x: %s\n", i, linebuf);
	}


}


static int gh_phyaddr_get(void *data, u64 *val)
{
	struct rtk_heap *gh = data;
	struct gen_pool *pool;
	struct gen_pool_chunk *chunk;

	mutex_lock(&gh->mutex);

	pool = gh->gen_pool;

	rcu_read_lock();
	chunk = list_first_or_null_rcu(&pool->chunks, struct gen_pool_chunk,
					next_chunk);
	rcu_read_unlock();
	if (!chunk)
		WARN_ONCE(!chunk, "%s: no storage pool!\n", __func__);
	else
		*val = (u64)chunk->start_addr;
	mutex_unlock(&gh->mutex);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(gh_phyaddr_fops, gh_phyaddr_get, NULL, "0x%llx\n");

static int gh_size_get(void *data, u64 *val)
{
	struct rtk_heap *gh = data;
	struct gen_pool *pool;
	mutex_lock(&gh->mutex);
	pool = gh->gen_pool;
	*val = (u64)gen_pool_size(pool);
	mutex_unlock(&gh->mutex);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(gh_size_fops, gh_size_get, NULL, "0x%llx\n");

static int gh_avail_get(void *data, u64 *val)
{
	struct rtk_heap *gh = data;
	struct gen_pool *pool;

	mutex_lock(&gh->mutex);

	pool = gh->gen_pool;
	*val = (u64)gen_pool_avail(pool);
	mutex_unlock(&gh->mutex);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(gh_avail_fops, gh_avail_get, NULL, "0x%llx\n");

static int gh_maxchunk_get(void *data, u64 *val)
{
	struct rtk_heap *gh = data;
	struct gen_pool *pool;
	struct gen_pool_chunk *chunk;
	unsigned long bitmap_maxno;
	unsigned long maxchunk = 0;
	unsigned long start, end = 0;

	mutex_lock(&gh->mutex);

	pool = gh->gen_pool;
	rcu_read_lock();
	chunk = list_first_or_null_rcu(&pool->chunks, struct gen_pool_chunk,
					next_chunk);
	rcu_read_unlock();

	pool = gh->gen_pool;

	bitmap_maxno= gen_pool_size(pool) >> pool->min_alloc_order;

	for (;;) {
		start = find_next_zero_bit(chunk->bits, bitmap_maxno, end);
		if (start >= bitmap_maxno)
			break;
		end = find_next_bit(chunk->bits, bitmap_maxno, start);
		maxchunk = max(end - start, maxchunk);
	}
	*val = (u64)maxchunk << pool->min_alloc_order;
	mutex_unlock(&gh->mutex);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(gh_maxchunk_fops, gh_maxchunk_get, NULL, "0x%llx\n");

static int gheap_task_show(struct seq_file *m, void *v)
{
	struct rtk_heap *gh = m->private;
	struct rtk_heap_task *rtk_heap_task, *tmp;

	mutex_lock(&gh->mutex);

	list_for_each_entry_safe(rtk_heap_task, tmp, &gh->task_list,
				list) {
		seq_printf(m, "name: %s     0x%lx \n", rtk_heap_task->comm,
				rtk_heap_task->size);
	}

	mutex_unlock(&gh->mutex);

	return 0;
}

static int gheap_task_open(struct inode *inode, struct file *file)
{
	return single_open(file, gheap_task_show, inode->i_private);
}

static const struct file_operations gheap_task_fops = {
	.open		= gheap_task_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int gheap_attr_show(struct seq_file *m, void *v)
{
	struct rtk_heap *gh = m->private;
	char *name;

	mutex_lock(&gh->mutex);

	seq_printf(m, "flags : 0x%x \n", gh->flag);
	seq_printf(m, "%s \n", name = ka_dispflag(gh->flag, GFP_KERNEL));
	kfree(name);

	mutex_unlock(&gh->mutex);

	return 0;
}

static int gheap_attr_open(struct inode *inode, struct file *file)
{
	return single_open(file, gheap_attr_show, inode->i_private);
}

static const struct file_operations gheap_attr_fops = {
	.open		= gheap_attr_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int gheap_secure_show(struct seq_file *m, void *v)
{
	struct rtk_heap *gh = m->private;
	unsigned long base;
	size_t size;
	struct rtk_protect_info *rtk_protect_info, *tmp;
	struct list_head *protect_list = &gh->list;

	mutex_lock(&gh->mutex);

	list_for_each_entry_safe(rtk_protect_info, tmp, protect_list, list) {
		base = rtk_protect_info->create_info.mem.base;
		size = rtk_protect_info->create_info.mem.size;
		seq_printf(m, "base = 0x%lx , size = 0x%lx \n", base, size);
	}

	mutex_unlock(&gh->mutex);

	return 0;
}

static int gheap_secure_open(struct inode *inode, struct file *file)
{
	return single_open(file, gheap_secure_show, inode->i_private);
}

static const struct file_operations gheap_secure_fops = {
	.open		= gheap_secure_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int gheap_bitmap_show(struct seq_file *m, void *v)
{
	struct rtk_heap *gh = m->private;
	struct gen_pool *pool;
	struct gen_pool_chunk *chunk;
	unsigned long bitmap_maxno;
	u8 *ptr;
	u32 len;

	pool = gh->gen_pool;
	bitmap_maxno = gen_pool_size(pool) >> pool->min_alloc_order;

	rcu_read_lock();
	chunk = list_first_or_null_rcu(&pool->chunks, struct gen_pool_chunk,
					next_chunk);
	rcu_read_unlock();

	ptr = (u8 *)chunk->bits;
	bitmap_maxno= gen_pool_size(pool) >> pool->min_alloc_order;

	len = DIV_ROUND_UP(bitmap_maxno, BITS_PER_BYTE);

	mutex_lock(&gh->mutex);
	bitmap_show(m, ptr, len);
	mutex_unlock(&gh->mutex);

	return 0;
}

static int gheap_bitmap_open(struct inode *inode, struct file *file)
{
	return single_open(file, gheap_bitmap_show, inode->i_private);
}


static const struct file_operations gheap_bitmap_fops = {
	.open		= gheap_bitmap_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*****************************************************************************/
static int cheap_maxchunk_get(void *data, u64 *val)
{
	struct rtk_heap *ch = data;
	struct cma *cma = ch->cma;
	unsigned long maxchunk = 0;
	unsigned long start, end = 0;
	unsigned long bitmap_maxno = cma_bitmap_maxno(cma);

	mutex_lock(&ch->mutex);
	for (;;) {
		start = find_next_zero_bit(ch->alloc_bitmap, bitmap_maxno, end);
		if (start >= bitmap_maxno)
			break;
		end = find_next_bit(ch->alloc_bitmap, bitmap_maxno, start);
		maxchunk = max(end - start, maxchunk);
	}
	mutex_unlock(&ch->mutex);

	*val = (u64)maxchunk << cma->order_per_bit;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(cheap_maxchunk_fops, cheap_maxchunk_get, NULL,
					 "%llu\n");


static int cheap_task_show(struct seq_file *m, void *v)
{
	struct rtk_heap *ch = m->private;
	struct rtk_heap_task *rtk_heap_task, *tmp;

	mutex_lock(&ch->mutex);

	list_for_each_entry_safe(rtk_heap_task, tmp, &ch->task_list,
				list) {
		seq_printf(m, "name: %s     0x%lx \n", rtk_heap_task->comm,
				rtk_heap_task->size);
	}

	mutex_unlock(&ch->mutex);

	return 0;
}

static int cheap_task_open(struct inode *inode, struct file *file)
{
	return single_open(file, cheap_task_show, inode->i_private);
}

static const struct file_operations cheap_task_fops = {
	.open		= cheap_task_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int cheap_attr_show(struct seq_file *m, void *v)
{
	struct rtk_heap *ch = m->private;
	char *name;

	mutex_lock(&ch->mutex);

	seq_printf(m, "cma name : %s \n", ch->cma->name);
	seq_printf(m, "flags : 0x%x \n", ch->flag);
	seq_printf(m, "%s \n", name = ka_dispflag(ch->flag, GFP_KERNEL));
	kfree(name);

	mutex_unlock(&ch->mutex);

	return 0;
}

static int cheap_attr_open(struct inode *inode, struct file *file)
{
	return single_open(file, cheap_attr_show, inode->i_private);
}

static const struct file_operations cheap_attr_fops = {
	.open		= cheap_attr_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int cheap_secure_show(struct seq_file *m, void *v)
{
	struct rtk_heap *ch = m->private;
	unsigned long base;
	size_t size;
	struct rtk_protect_info *rtk_protect_info, *tmp;
	struct list_head *protect_list = &ch->list;

	mutex_lock(&ch->mutex);

	list_for_each_entry_safe(rtk_protect_info, tmp, protect_list, list) {
		base = rtk_protect_info->create_info.mem.base;
		size = rtk_protect_info->create_info.mem.size;
		seq_printf(m, "base = 0x%lx , size = 0x%lx \n", base, size);
	}

	mutex_unlock(&ch->mutex);

	return 0;
}

static int cheap_secure_open(struct inode *inode, struct file *file)
{
	return single_open(file, cheap_secure_show, inode->i_private);
}

static const struct file_operations cheap_secure_fops = {
	.open		= cheap_secure_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int cheap_allocbitmap_show(struct seq_file *m, void *v)
{
	struct rtk_heap *ch = m->private;
	u8 *ptr = (u8 *)ch->alloc_bitmap;
	u32 len = DIV_ROUND_UP(cma_bitmap_maxno(ch->cma),
					 BITS_PER_BYTE );
	mutex_lock(&ch->mutex);
	bitmap_show(m, ptr, len);
	mutex_unlock(&ch->mutex);

	return 0;
}

static int cheap_allocbitmap_open(struct inode *inode, struct file *file)
{
	return single_open(file, cheap_allocbitmap_show, inode->i_private);
}


static const struct file_operations cheap_allocbitmap_fops = {
	.open		= cheap_allocbitmap_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int cheap_usebitmap_show(struct seq_file *m, void *v)
{
	struct rtk_heap *ch = m->private;
	u8 *ptr = (u8 *)ch->use_bitmap;
	u32 len = DIV_ROUND_UP(cma_bitmap_maxno(ch->cma),
					 BITS_PER_BYTE );
	mutex_lock(&ch->mutex);
	bitmap_show(m, ptr, len);
	mutex_unlock(&ch->mutex);

	return 0;
}

static int cheap_usebitmap_open(struct inode *inode, struct file *file)
{
	return single_open(file, cheap_usebitmap_show, inode->i_private);
}


static const struct file_operations cheap_usebitmap_fops = {
	.open		= cheap_usebitmap_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void rheap_gh_debugfs_add(struct rtk_heap *gh, struct dentry
					 *root_dentry)
{
	struct dentry *tmp;
	struct gen_pool *pool;
	struct gen_pool_chunk *chunk;
	unsigned long bitmap_maxno;

	tmp = debugfs_create_dir(dma_heap_get_name(gh->heap), root_dentry);
	if (IS_ERR_OR_NULL(tmp))
		return;

	debugfs_create_file("phy_addr", 0444, tmp, gh, &gh_phyaddr_fops);
	debugfs_create_file("size", 0444, tmp, gh, &gh_size_fops);
	debugfs_create_file("avail", 0444, tmp, gh, &gh_avail_fops);
	debugfs_create_file("maxchunk", 0444, tmp, gh, &gh_maxchunk_fops);


	pool = gh->gen_pool;

	bitmap_maxno = gen_pool_size(pool) >> pool->min_alloc_order;

	rcu_read_lock();
	chunk = list_first_or_null_rcu(&pool->chunks, struct gen_pool_chunk,
					next_chunk);
	rcu_read_unlock();

	debugfs_create_file("bitmap", 0444, tmp, gh,
					 &gheap_bitmap_fops);

	debugfs_create_file("task", 0400, tmp, gh, &gheap_task_fops);
	debugfs_create_file("attribute", 0400, tmp, gh, &gheap_attr_fops);
	debugfs_create_file("secure", 0444, tmp, gh, &gheap_secure_fops);
}
static int rtk_heap_summay_fops_show(struct seq_file *m, void *v)
{
	struct rtk_heap *ch;
	struct rtk_heap *gh;
	struct rtk_heap_task *rtk_heap_task, *tmp;
	struct rtk_alloc *rtk_alloc;
	struct rtk_protect_info *protect_info, *pro_tmp;
	struct protect_region *mem;
	unsigned long used_bit, used_pages;
	u64 heap_base_addr, heap_size, heap_avail, heap_end_addr;
	char *name;

	seq_printf(m, "CMA heaps info:\n");
	list_for_each_entry(ch, &cheap_list, hlist) {
		mutex_lock(&ch->mutex);
		heap_base_addr = __pfn_to_phys(ch->cma->base_pfn);
		heap_end_addr = __pfn_to_phys(ch->cma->base_pfn) +
				(ch->cma->count << PAGE_SHIFT) - 1;
		seq_printf(m,
			   "\n\tHeap: %-30s(%-12s):[0x%llx ~ 0x%llx], "
			   "size = 0x%-8lx pages, flag = 0x%-8x\n",
			   dma_heap_get_name(ch->heap), ch->cma->name,
			   heap_base_addr, heap_end_addr, ch->cma->count,
			   ch->flag);
		seq_printf(m, "\tflag:%s \n",
			   name = ka_dispflag(ch->flag, GFP_KERNEL));
		kfree(name);
		used_bit = bitmap_weight(ch->use_bitmap,
					 (int)cma_bitmap_maxno(ch->cma));
		used_pages = used_bit << ch->cma->order_per_bit;
		seq_printf(m, "\t\tUsage: 0x%-8lx pages, free: 0x%-8lx pages\n",
			   used_pages, (ch->cma->count) - used_pages);
		list_for_each_entry_safe(rtk_heap_task, tmp, &ch->task_list,
					  list) {
			seq_printf(m, "\t\t%-18s: alloc %-8ld bytes\n",
				   rtk_heap_task->comm, rtk_heap_task->size);
			list_for_each_entry(rtk_alloc,
					     &rtk_heap_task->alloc_list, list) {
				seq_printf(m,
					   "\t\t    range [%-8lx ~ %-8lx]: "
					   "%ld bytes, owner: %s\n",
					   rtk_alloc->start,
					   (rtk_alloc->end - 1),
					   rtk_alloc->end - rtk_alloc->start,
					   rtk_alloc->ext_task);
			}
		}
		list_for_each_entry_safe (protect_info, pro_tmp, &ch->list,
					  list) {
			mem = &protect_info->create_info.mem;
			seq_printf(m, "\t\tprotected range: [%lx ~ %lx]\n",
				   mem->base, mem->base + mem->size - 1);
		}
		mutex_unlock(&ch->mutex);
	}
	seq_printf(m, "GEN heaps info:\n");
	list_for_each_entry(gh, &gheap_list, hlist) {
		gh_size_get(gh, &heap_size);
		gh_phyaddr_get(gh, &heap_base_addr);
		gh_avail_get(gh, &heap_avail);
		seq_printf(
			m,
			"\n\tHeap: %-30s: [0x%llx ~ 0x%llx], size = 0x%-8llx pages, "
			"flag = 0x%-8x\n",
			dma_heap_get_name(gh->heap), heap_base_addr,
			heap_base_addr + heap_size - 1, heap_size >> PAGE_SHIFT,
			gh->flag);
		seq_printf(m, "\tflag:%s \n",
			   name = ka_dispflag(gh->flag, GFP_KERNEL));
		kfree(name);
		used_pages = (heap_size - heap_avail) >> PAGE_SHIFT;
		seq_printf(m,
			   "\t\tUsage: 0x%-8lx pages, free: 0x%-8llx pages\n",
			   used_pages, heap_avail >> PAGE_SHIFT);
		mutex_lock(&gh->mutex);
		list_for_each_entry_safe(rtk_heap_task, tmp, &gh->task_list,
					  list) {
			seq_printf(m, "\t\t%-18s: alloc %-8ld bytes\n",
				   rtk_heap_task->comm, rtk_heap_task->size);
			list_for_each_entry(rtk_alloc,
					     &rtk_heap_task->alloc_list, list) {
				seq_printf(m,
					   "\t\t    range[%-8lx ~ %-8lx]:  "
					   "%ld bytes, owner: %s\n",
					   rtk_alloc->start,
					   (rtk_alloc->end - 1),
					   rtk_alloc->end - rtk_alloc->start,
					   rtk_alloc->ext_task);
			}
		}

		list_for_each_entry_safe (protect_info, pro_tmp, &gh->list,
					  list) {
			mem = &protect_info->create_info.mem;
			seq_printf(m, "\t\tprotected range: [%lx ~ %lx]\n",
				   mem->base, mem->base + mem->size - 1);
		}
		mutex_unlock(&gh->mutex);
	}

	seq_printf(m, "Pre-alloc heaps info:\n");
	list_for_each_entry(gh, &pheap_list, plist) {
		gh_size_get(gh, &heap_size);
		gh_phyaddr_get(gh, &heap_base_addr);
		gh_avail_get(gh, &heap_avail);
		seq_printf(m,
			   "\n\t%-30s: [0x%llx ~ 0x%llx], size = 0x%8llx pages, "
			   "flag = 0x%-8x\n",
			   dma_heap_get_name(gh->heap), heap_base_addr,
			   heap_base_addr + heap_size - 1,
			   heap_size >> PAGE_SHIFT, gh->flag);
		seq_printf(m, "\tflag:%s \n",
			   name = ka_dispflag(gh->flag, GFP_KERNEL));
		kfree(name);
		used_pages = (heap_size - heap_avail) >> PAGE_SHIFT;
		seq_printf(m, "\t\tUsage: 0x%8lx pages, free: 0x%8llx pages\n",
			   used_pages, heap_avail >> PAGE_SHIFT);
		mutex_lock(&gh->mutex);
		list_for_each_entry_safe(rtk_heap_task, tmp, &gh->task_list,
					  list) {
			seq_printf(m, "\t\t%-18s: alloc %-8ld bytes\n",
				   rtk_heap_task->comm, rtk_heap_task->size);
			list_for_each_entry(rtk_alloc,
					     &rtk_heap_task->alloc_list, list) {
				seq_printf(m,
					   "\t\t    range [%-8lx ~ %-8lx]: "
					   "%ld bytes, owner: %s\n",
					   rtk_alloc->start,
					   (rtk_alloc->end - 1),
					   rtk_alloc->end - rtk_alloc->start,
					   rtk_alloc->ext_task);
			}
		}
		mutex_unlock(&gh->mutex);
	}

	return 0;
}

static int rtk_task_summay_fops_show(struct seq_file *m, void *v)
{
	struct rtk_heap *ch;
	struct rtk_heap *gh;
	struct rtk_heap_task *rtk_heap_task, *tmp, *tmp_task;
	struct rtk_alloc *rtk_alloc, *tmp_alloc;

	LIST_HEAD(ptask_list);
	list_for_each_entry(ch, &cheap_list, hlist) {
		list_for_each_entry_safe(rtk_heap_task, tmp, &ch->task_list,
					  list) {
			list_for_each_entry(tmp_task, &ptask_list, list) {
				if (!strcmp(rtk_heap_task->comm,
					    tmp_task->comm)) {
					tmp_task->size += rtk_heap_task->size;
					list_for_each_entry(
						rtk_alloc,
						&rtk_heap_task->alloc_list,
						list) {
						tmp_alloc = (struct rtk_alloc *)
							kzalloc(sizeof
							(struct rtk_alloc),
							GFP_KERNEL);
						tmp_alloc->start =
							rtk_alloc->start;
						tmp_alloc->end = rtk_alloc->end;
						list_add(&tmp_alloc->list,
							 &tmp_task->alloc_list);
					}
					break;
				}
			}
			if (list_entry_is_head(tmp_task, &ptask_list, list)) {
				tmp_task = (struct rtk_heap_task *)kzalloc(
					sizeof(struct rtk_heap_task),
					GFP_KERNEL);
				strlcpy(tmp_task->comm, rtk_heap_task->comm,
					sizeof(tmp_task->comm));
				tmp_task->size = rtk_heap_task->size;
				INIT_LIST_HEAD(&tmp_task->alloc_list);
				list_for_each_entry(rtk_alloc,
						     &rtk_heap_task->alloc_list,
						     list) {
					tmp_alloc = (struct rtk_alloc *)kzalloc(
						sizeof(struct rtk_alloc),
						GFP_KERNEL);
					tmp_alloc->start = rtk_alloc->start;
					tmp_alloc->end = rtk_alloc->end;
					list_add(&tmp_alloc->list,
						 &tmp_task->alloc_list);
				}
				list_add(&tmp_task->list, &ptask_list);
			}
		}
	}
	list_for_each_entry(gh, &gheap_list, hlist) {
		list_for_each_entry_safe(rtk_heap_task, tmp, &gh->task_list,
					  list) {
			list_for_each_entry(tmp_task, &ptask_list, list) {
				if (!strcmp(rtk_heap_task->comm,
					    tmp_task->comm)) {
					tmp_task->size += rtk_heap_task->size;
					list_for_each_entry(
						rtk_alloc,
						&rtk_heap_task->alloc_list,
						list) {
						tmp_alloc = (struct rtk_alloc *)
						    kzalloc(sizeof(struct rtk_alloc),
						    GFP_KERNEL);
						tmp_alloc->start =
							rtk_alloc->start;
						tmp_alloc->end = rtk_alloc->end;
						list_add(&tmp_alloc->list,
							 &tmp_task->alloc_list);
					}
					break;
				}
			}
			if (list_entry_is_head(tmp_task, &ptask_list, list)) {
				tmp_task = (struct rtk_heap_task *)kzalloc(
					sizeof(struct rtk_heap_task),
					GFP_KERNEL);
				strlcpy(tmp_task->comm, rtk_heap_task->comm,
					sizeof(tmp_task->comm));
				tmp_task->size = rtk_heap_task->size;
				INIT_LIST_HEAD(&tmp_task->alloc_list);
				list_for_each_entry(rtk_alloc,
						     &rtk_heap_task->alloc_list,
						     list) {
					tmp_alloc = (struct rtk_alloc *)kzalloc(
						sizeof(struct rtk_alloc),
						GFP_KERNEL);
					tmp_alloc->start = rtk_alloc->start;
					tmp_alloc->end = rtk_alloc->end;
					list_add(&tmp_alloc->list,
						 &tmp_task->alloc_list);
				}
				list_add(&tmp_task->list, &ptask_list);
			}
		}
	}
	list_for_each_entry(gh, &pheap_list, hlist) {
		list_for_each_entry_safe(rtk_heap_task, tmp, &gh->task_list,
					  list) {
			list_for_each_entry(tmp_task, &ptask_list, list) {
				if (!strcmp(rtk_heap_task->comm,
					    tmp_task->comm)) {
					tmp_task->size += rtk_heap_task->size;
					list_for_each_entry(
						rtk_alloc,
						&rtk_heap_task->alloc_list,
						list) {
						tmp_alloc = (struct rtk_alloc *)
						    kzalloc(sizeof(struct rtk_alloc),
						    GFP_KERNEL);
						tmp_alloc->start =
							rtk_alloc->start;
						tmp_alloc->end = rtk_alloc->end;
						list_add(&tmp_alloc->list,
							 &tmp_task->alloc_list);
					}
					break;
				}
			}
			if (list_entry_is_head(tmp_task, &ptask_list, list)) {
				tmp_task = (struct rtk_heap_task *)kzalloc(
					sizeof(struct rtk_heap_task),
					GFP_KERNEL);
				strlcpy(tmp_task->comm, rtk_heap_task->comm,
					sizeof(tmp_task->comm));
				tmp_task->size = rtk_heap_task->size;
				INIT_LIST_HEAD(&tmp_task->alloc_list);
				list_for_each_entry(rtk_alloc,
						     &rtk_heap_task->alloc_list,
						     list) {
					tmp_alloc = (struct rtk_alloc *)kzalloc(
						sizeof(struct rtk_alloc),
						GFP_KERNEL);
					tmp_alloc->start = rtk_alloc->start;
					tmp_alloc->end = rtk_alloc->end;
					list_add(&tmp_alloc->list,
						 &tmp_task->alloc_list);
				}
				list_add(&tmp_task->list, &ptask_list);
			}
		}
	}
	list_for_each_entry_safe(tmp_task, tmp, &ptask_list, list) {
		seq_printf(m, "\t\t%-18s: alloc %-8ld bytes\n", tmp_task->comm,
			   tmp_task->size);
		list_for_each_entry_safe(rtk_alloc, tmp_alloc,
					  &tmp_task->alloc_list, list) {
			seq_printf(m,
				   "\t\t    range [%-8lx ~ %-8lx]: %ld bytes\n",
				   rtk_alloc->start, (rtk_alloc->end - 1),
				   rtk_alloc->end - rtk_alloc->start);
			list_del(&rtk_alloc->list);
			kfree(rtk_alloc);
		}
		list_del(&tmp_task->list);
		kfree(tmp_task);
	}

	return 0;
}

static int rtk_heap_summay_fops_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtk_heap_summay_fops_show, inode->i_private);
}

static const struct file_operations rtk_heap_summay_fops = {
	.open = rtk_heap_summay_fops_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtk_task_summary_fops_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtk_task_summay_fops_show, inode->i_private);
}

static const struct file_operations rtk_task_summay_fops = {
	.open = rtk_task_summary_fops_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void rheap_ch_debugfs_add(struct rtk_heap *ch, struct dentry
					 *root_dentry)
{
	struct dentry *tmp;
	struct cma *cma;
	char name[100];

	tmp = debugfs_create_dir(dma_heap_get_name(ch->heap), root_dentry);
	if (IS_ERR_OR_NULL(tmp))
		return;

	cma = ch->cma;

#ifdef CONFIG_CMA_DEBUGFS
	snprintf(name, sizeof(name), "/sys/kernel/debug/cma/cma-%s/bitmap",
					 cma->name);
	debugfs_create_symlink("cma_bitmap", tmp, name);

	snprintf(name, sizeof(name), "/sys/kernel/debug/cma/cma-%s/maxchunk",
					 cma->name);
	debugfs_create_symlink("maxchunk_cma", tmp, name);

	snprintf(name, sizeof(name), "/sys/kernel/debug/cma/cma-%s/base_pfn",
					 cma->name);
	debugfs_create_symlink("base_pfn_cma", tmp, name);

	snprintf(name, sizeof(name), "/sys/kernel/debug/cma/cma-%s/count",
					 cma->name);
	debugfs_create_symlink("count_cma", tmp, name);

	snprintf(name, sizeof(name), "/sys/kernel/debug/cma/cma-%s/used",
					 cma->name);
	debugfs_create_symlink("used_cma", tmp, name);

#endif

	debugfs_create_file("alloc_bitmap", 0444, tmp, ch,
					 &cheap_allocbitmap_fops);
	debugfs_create_file("use_bitmap", 0444, tmp, ch,
					 &cheap_usebitmap_fops);


	debugfs_create_file("maxchunk_alloc", 0444, tmp, ch,
					 &cheap_maxchunk_fops);

	debugfs_create_file("task", 0444, tmp, ch, &cheap_task_fops);
	debugfs_create_file("attribute", 0444, tmp, ch, &cheap_attr_fops);
	debugfs_create_file("secure", 0444, tmp, ch, &cheap_secure_fops);

}

void rheap_debugfs_init(void)
{
	struct rtk_heap *gh;
	struct rtk_heap *ch;

	if (!rheap_debugfs_root) {
		rheap_debugfs_root = debugfs_create_dir("rtk_heap", NULL);
		if (IS_ERR_OR_NULL(rheap_debugfs_root))
			return;
	}

	list_for_each_entry(gh, &pheap_list, plist)
		rheap_gh_debugfs_add(gh, rheap_debugfs_root);

	list_for_each_entry(gh, &gheap_list, hlist)
		rheap_gh_debugfs_add(gh, rheap_debugfs_root);

	list_for_each_entry(ch, &cheap_list, hlist)
		rheap_ch_debugfs_add(ch, rheap_debugfs_root);

	debugfs_create_file("heap_summary", 0444, rheap_debugfs_root, NULL,
			    &rtk_heap_summay_fops);
	debugfs_create_file("task_summary", 0444, rheap_debugfs_root, NULL,
			    &rtk_task_summay_fops);
}
EXPORT_SYMBOL(rheap_debugfs_init);
