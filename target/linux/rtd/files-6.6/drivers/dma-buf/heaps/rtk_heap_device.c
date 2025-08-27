// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Realtek Semiconductor Corp
 * Author: <cy.huang@realtek.com> .
 */

#include <linux/dma-buf.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <soc/realtek/memory.h>
#include <soc/realtek/rtk_media_heap.h>
#include <soc/realtek/uapi/rtk_heap.h>
/*
 * Ioctl definitions
 */

/* Use 'r' as magic number */
#define RHEAP_IOC_MAGIC 'r'
#define RHEAP_GET_BEST_HEAP _IOR(RHEAP_IOC_MAGIC, 1, struct rheap_best_info)

#define BUF_SIZE	256

struct rheap_best_info {
	u32 flags;
	u8 buf[BUF_SIZE];
};

struct miscdevice *r_mdev;

static void best_fit_heap_show(u32 flags, char *buf)
{
	LIST_HEAD(best_list);
	struct rtk_best_fit *best_fit;
	bool uncached = (flags & RTK_FLAG_NONCACHED) ? true : false;
	int n = 0;

	if (flags != 0) {
		fill_best_fit_list(NULL, flags, &best_list);
		list_for_each_entry(best_fit, &best_list, hlist) {
			if (uncached)
				n += scnprintf(buf+n, BUF_SIZE-n,
					 "%s_uncached : %u\n",
					 best_fit->name, best_fit->score);
			else
				n += scnprintf(buf+n, BUF_SIZE-n,
					 "%s : %u\n",
					 best_fit->name, best_fit->score);
			if (n > BUF_SIZE)
				BUG();
		}

		for (;;) {
			if (list_empty(&best_list))
				break;
			best_fit = list_last_entry(&best_list,
					 struct rtk_best_fit, hlist);
			list_del(&best_fit->hlist);
			kfree(best_fit);
		}
	}
}

static int rheap_get_phys(int handle, phys_addr_t *addr, size_t *len)
{
	struct dma_buf *dmabuf;
	struct sg_table *table;
	struct device *dev = r_mdev->this_device;
	struct dma_buf_attachment *attach;
	phys_addr_t paddr;
	int ret = 0;

	dmabuf = dma_buf_get(handle);
	if (IS_ERR_OR_NULL(dmabuf)) {
		dev_err(dev, "Failed to get dmabuf\n");
		ret = PTR_ERR(dmabuf);
		goto get_err;
	}

	attach = dma_buf_attach(dmabuf, dev);
	if (IS_ERR_OR_NULL(attach)) {
		dev_err(dev, "Failed to attach dmabuf\n");
		ret = PTR_ERR(attach);
		goto attach_err;
	}

	table = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(table)) {
		dev_err(dev, "Failed to map attachment\n");
		ret = PTR_ERR(table);
		goto map_err;
	}

	paddr = sg_phys(table->sgl);
	*addr = paddr;
	*len = dmabuf->size;

	dma_buf_unmap_attachment(attach, table, DMA_BIDIRECTIONAL);
map_err:
	dma_buf_detach(dmabuf, attach);
attach_err:
	dma_buf_put(dmabuf);
get_err:
	return ret;
}

static int rheap_get_phy_info(struct rheap_ioc_phy_info *phyInfo)
{
	int ret = 0;
	phys_addr_t addr;
	size_t len;

	if (phyInfo->handle < 0) {
		ret = -EINVAL;
		goto err;
	}

	ret = rheap_get_phys(phyInfo->handle, &addr, &len);

	phyInfo->addr = addr;
	phyInfo->len = len;

err:
	return ret;
}

struct dma_heap_buffer {
	struct dma_heap *heap;
	struct dma_buf *dmabuf;
	size_t size;
	unsigned long flags;
};

static int rheap_sync_for_device(int fd, int cmd)
{
	struct dma_buf *dmabuf;
	enum dma_data_direction dir = (cmd == RHEAP_INVALIDATE)
				 ? DMA_FROM_DEVICE : DMA_TO_DEVICE;

	struct sg_table *table;
	struct device *dev = r_mdev->this_device;
	struct dma_buf_attachment *attach;
	int ret = 0;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(dmabuf)) {
		dev_err(dev, "Failed to get dmabuf\n");
		ret = PTR_ERR(dmabuf);
		goto get_err;
	}

	attach = dma_buf_attach(dmabuf, dev);
	if (IS_ERR_OR_NULL(attach)) {
		dev_err(dev, "Failed to attach dmabuf\n");
		ret = PTR_ERR(attach);
		goto attach_err;
	}

	table = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(table)) {
		dev_err(dev, "Failed to map attachment\n");
		ret = PTR_ERR(table);
		goto map_err;
	}

	if (dmabuf->ops->end_cpu_access) {
		ret = dmabuf->ops->end_cpu_access(dmabuf, dir);
	} else {
		int nents = dma_map_sg(r_mdev->this_device,
				       table->sgl,
				       table->nents, dir);
		dma_sync_sg_for_device(r_mdev->this_device,
				       table->sgl,
				       table->nents, dir);
		if (nents > 0)
			dma_unmap_sg(r_mdev->this_device,
				     table->sgl,
				     table->nents, dir);
	}

	dma_buf_unmap_attachment(attach, table, DMA_BIDIRECTIONAL);
map_err:
	dma_buf_detach(dmabuf, attach);
attach_err:
	dma_buf_put(dmabuf);
get_err:
	return ret;
}

static int rheap_sync_range_for_device(int handle, int cmd,
				struct rheap_ioc_sync_range *range_data)
{
	struct dma_buf *dmabuf;
	enum dma_data_direction dir = (cmd == RHEAP_INVALIDATE_RANGE) ?
				       DMA_FROM_DEVICE : DMA_TO_DEVICE;
	struct sg_table *table;
	struct device *dev = r_mdev->this_device;
	struct dma_buf_attachment *attach;
	phys_addr_t addr;
	size_t len;
	size_t offset;
	dma_addr_t paddr;
	int ret = 0;
	int fd;

	fd = (int)range_data->handle & -1U;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(dmabuf)) {
		dev_err(dev, "Failed to get dmabuf\n");
		ret = PTR_ERR(dmabuf);
		goto get_err;
	}

	attach = dma_buf_attach(dmabuf, dev);
	if (IS_ERR_OR_NULL(attach)) {
		dev_err(dev, "Failed to attach dmabuf\n");
		ret = PTR_ERR(attach);
		goto attach_err;
	}

	table = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(table)) {
		dev_err(dev, "Failed to map attachment\n");
		ret = PTR_ERR(table);
		goto map_err;
	}

	if (rheap_get_phys(handle, &addr, &len) == 0) {
		offset = range_data->phyAddr - addr;

		if (dmabuf->ops->end_cpu_access_partial) {
			ret = dmabuf->ops->end_cpu_access_partial(dmabuf,
								  dir,
								  offset,
								  len);
		} else {
			paddr = dma_map_page(r_mdev->this_device,
					     sg_page(table->sgl),
					     offset,
					     range_data->len,
					     dir);
			dma_sync_single_for_device(r_mdev->this_device,
						   range_data->phyAddr,
						   range_data->len, dir);
			dma_unmap_page(r_mdev->this_device,
				       paddr,
				       range_data->len,
				       dir);
		}
	}

	dma_buf_unmap_attachment(attach, table, DMA_BIDIRECTIONAL);
map_err:
	dma_buf_detach(dmabuf, attach);
attach_err:
	dma_buf_put(dmabuf);
get_err:
	return ret;
}

static long rheap_dev_ioctl(struct file *filp, unsigned int cmd,
			    unsigned long arg)
{
	int ret = 0;
	int fd;
	struct rheap_ioc_sync_range range_data;
	struct device *dev = r_mdev->this_device;

	switch (cmd) {
	case RHEAP_GET_BEST_HEAP: {
		LIST_HEAD(best_list);
		struct rheap_best_info best_info;

		if (copy_from_user(&best_info, (void __user *)arg,
				   sizeof(best_info))) {
			dev_err(dev, "RHEAP_GET_BEST_HEAP failed\n");
			ret = -EFAULT;
			break;
		}

		best_fit_heap_show(best_info.flags, best_info.buf);

		ret = copy_to_user((void __user *)arg, &best_info,
					sizeof(best_info));
		if (ret) {
			dev_err(dev, "copy_to_user failed!(ret = %d)\n", ret);
			return -EFAULT;
		}

		break;
	}
	case RHEAP_GET_LAST_ALLOC_ADDR: {
		dev_err(dev, "dont support RTK_ION_GET_LAST_ALLOC_ADDR\n");
		ret = -EFAULT;
		break;
	}
	case RHEAP_INVALIDATE:
	case RHEAP_FLUSH: {
		if (copy_from_user(&fd, (void __user *)arg, sizeof(fd))) {
			dev_err(dev, "copy_from_user failed!\n");
			ret = -EFAULT;
		}
		if (rheap_sync_for_device(fd, cmd) != 0) {
			dev_err(dev, "rheap_sync_for_device failed!(cmd:%x fd:%d)\n",
				cmd, fd);
			ret = -EFAULT;
		}
		break;
	}
	case RHEAP_FLUSH_RANGE:
	case RHEAP_INVALIDATE_RANGE: {
		if (copy_from_user(&range_data, (void __user *)arg,
				 sizeof(range_data))) {
			dev_err(dev, "copy_from_user failed!\n");
			ret = -EFAULT;
			break;
		}
		if (rheap_sync_range_for_device(range_data.handle, cmd,
					 &range_data)) {
			dev_err(dev, "rheap_sync_range_for_device failed!(cmd:%d handle:%d)\n",
				cmd, (int)range_data.handle);
			ret = -EFAULT;
		}
		break;
	}
	case RHEAP_GET_PHYINFO:{
		struct rheap_ioc_phy_info phyInfo;

		if (copy_from_user(&phyInfo, (void __user *)arg,
				 sizeof(phyInfo))) {
			dev_err(dev, "copy_from_user failed!\n");
			ret = -EFAULT;
			break;
		}
		if (rheap_get_phy_info(&phyInfo)) {
			dev_err(dev, "rheap_get_phy_info failed!\n");
			ret = -EFAULT;
			break;
		}

		ret = copy_to_user((void __user *)arg, &phyInfo,
				   sizeof(phyInfo));
		if (ret) {
			dev_err(dev, "copy_to_user failed! (ret = %d)\n", ret);
			return -EFAULT;
		}

		break;
	}
	case RHEAP_GET_MEMORY_INFO:{
		dev_err(dev, "not yet support RTK_ION_IOC_GET_MEMORY_INFO\n");
		ret = -EFAULT;
		break;
	}
	default:
		dev_err(dev, "Unknown custom ioctl\n");
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static const struct file_operations rheap_dev_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = rheap_dev_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
};

int rheap_miscdev_register(void)
{
	int ret = 0;

	r_mdev = kzalloc(sizeof(*r_mdev), GFP_KERNEL);
	if (!r_mdev)
		return -ENOMEM;

	r_mdev->minor  = MISC_DYNAMIC_MINOR;
	r_mdev->name   = "rtk_heap";
	r_mdev->fops   = &rheap_dev_fops;
	r_mdev->parent = NULL;

	ret = misc_register(r_mdev);
	if (ret) {
		dev_err(r_mdev->this_device, "failed to register misc device.\n");
		kfree(r_mdev);
		goto exit;
	}

	r_mdev->this_device->coherent_dma_mask = DMA_BIT_MASK(64);
	r_mdev->this_device->dma_mask = (u64 *)&r_mdev->this_device->coherent_dma_mask;

exit:
	return ret;
}
