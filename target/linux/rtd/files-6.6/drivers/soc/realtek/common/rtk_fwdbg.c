// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Realtek FW Debug driver
 *
 * Copyright (c) 2017 Realtek Semiconductor Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */


#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/uaccess.h>
#include <soc/realtek/rtk_media_heap.h>
#include <soc/realtek/memory.h>


#define FWDBG_IOC_MAGIC 'k'
#define FWDBG_IOCTRGETDBGREG_A _IOWR(FWDBG_IOC_MAGIC, 0x10, struct dbg_flag)
#define FWDBG_IOCTRGETDBGREG_V _IOWR(FWDBG_IOC_MAGIC, 0x11, struct dbg_flag)
#define FWDBG_IOCTRGETDBGPRINT_V _IOWR(FWDBG_IOC_MAGIC, 0x12, struct dbg_flag)

#define FWDBG_DBGREG_GET	0
#define FWDBG_DBGREG_SET	1

struct device *fw_dbg_dev;


struct dbg_flag {
	uint32_t op;
	uint32_t flagValue;
	uint32_t flagAddr;
};

struct fw_debug_flag {
	unsigned int acpu;
	unsigned int reserve_acpu[127];
	unsigned int vcpu;
	unsigned int reserve_vcpu[127];
};

struct fw_debug_flag_memory {
	struct fw_debug_flag *debug_flag;
	phys_addr_t debug_phys;
	size_t debug_size;
	void *vaddr;
};

struct fw_debug_print_memory {
	void *debug_hdr;
	phys_addr_t debug_phys;
	size_t debug_size;
	int32_t fd;
	void *vaddr;
	struct dma_buf *dmabuf;
	struct device *dev;
};

static struct fw_debug_flag_memory *mDebugFlagMemory;
static struct fw_debug_print_memory *mDebugPrintMemory;


static int fw_debug_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct fw_debug_print_memory *dbm = dmabuf->priv;
	struct device *dev = dbm->dev;
	size_t size = vma->vm_end - vma->vm_start;
	dev_err(dev, "%s \n", __func__);
	return dma_mmap_coherent(dev, vma, dbm->vaddr, dbm->debug_phys, size);
}
static void fw_debug_release(struct dma_buf *dmabuf)
{
	struct fw_debug_print_memory *dbm = dmabuf->priv;
	struct device *dev = dbm->dev;
	dev_err(dev, "%s \n", __func__);
	dma_free_coherent(dev, dbm->debug_size, dbm->vaddr, dbm->debug_phys);
}


static const struct dma_buf_ops fw_debug_dma_buf_ops = {
	.mmap = fw_debug_mmap,
	.release = fw_debug_release,
};


static struct fw_debug_flag_memory *get_debug_flag_memory(struct device *dev)
{
	struct fw_debug_flag_memory *tmp;
	unsigned int flag_mask = 0;
	dma_addr_t daddr;
	void *vaddr;

	if (mDebugFlagMemory)
		return mDebugFlagMemory;

	tmp = kzalloc(sizeof(struct fw_debug_flag_memory), GFP_KERNEL);
	if (tmp == NULL)
		goto alloc_err;

	flag_mask |= RTK_FLAG_SCPUACC | RTK_FLAG_ACPUACC | RTK_FLAG_VCPU_FWACC;
	mutex_lock(&dev->mutex);
	rheap_setup_dma_pools(dev, NULL, flag_mask, __func__);
	vaddr = dma_alloc_coherent(dev, sizeof(struct fw_debug_flag),
					 &daddr, GFP_KERNEL);
	mutex_unlock(&dev->mutex);
	if (!vaddr)
		goto rheap_err;

	tmp->debug_flag = vaddr;
	tmp->debug_phys = daddr;
	mDebugFlagMemory = tmp;

	return mDebugFlagMemory;

rheap_err:
	kfree(tmp);
alloc_err:
	return NULL;

}

static struct fw_debug_print_memory *get_debug_print_memory(struct device *dev)
{
	struct fw_debug_print_memory *tmp;
	unsigned int flag_mask = 0;
	dma_addr_t daddr;
	void *vaddr;
	size_t size = sizeof(struct fw_debug_flag);
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	if (mDebugPrintMemory)
		return mDebugPrintMemory;

	tmp = kzalloc(sizeof(struct fw_debug_print_memory), GFP_KERNEL);

	if (tmp == NULL)
		goto alloc_err;

	flag_mask |= RTK_FLAG_SCPUACC | RTK_FLAG_ACPUACC | RTK_FLAG_VCPU_FWACC;

	mutex_lock(&dev->mutex);

	rheap_setup_dma_pools(dev, NULL, flag_mask, __func__);
	vaddr = dma_alloc_attrs(dev, size, &daddr, GFP_KERNEL,
						DMA_ATTR_NO_KERNEL_MAPPING);
	mutex_unlock(&dev->mutex);

	if (!vaddr)
		goto rheap_err;

	exp_info.ops = &fw_debug_dma_buf_ops;
	exp_info.size = size;
	exp_info.flags = O_RDWR;
	exp_info.priv = tmp;
	tmp->dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(tmp->dmabuf))
		goto dmabuf_err;
	tmp->fd = dma_buf_fd(tmp->dmabuf, O_CLOEXEC);
	tmp->debug_phys = daddr;
	tmp->dev = dev;
	mDebugPrintMemory = tmp;

	return mDebugPrintMemory;


dmabuf_err:
	dma_free_coherent(dev, size, vaddr, daddr);
rheap_err:
	kfree(tmp);
alloc_err:
	return NULL;

}


static struct fw_debug_flag *get_debug_flag(struct device *dev)
{
	struct fw_debug_flag_memory *debug_memory = get_debug_flag_memory(dev);

	return (debug_memory) ? debug_memory->debug_flag : NULL;
}

static phys_addr_t get_debug_flag_phyAddr(struct device *dev)
{
	struct fw_debug_flag_memory *debug_memory = get_debug_flag_memory(dev);

	return (debug_memory) ? debug_memory->debug_phys : -1UL;
}

static struct fw_debug_print_memory *get_debug_print(struct device *dev)
{
	struct fw_debug_print_memory *debug_print = get_debug_print_memory(dev);

	return (debug_print) ? debug_print : NULL;
}

static phys_addr_t get_debug_print_phyAddr(struct device *dev)
{
	struct fw_debug_print_memory *debug_print = get_debug_print_memory(dev);

	return (debug_print) ? debug_print->debug_phys : -1UL;
}


long rtk_fwdbg_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct dbg_flag dFlag;
	unsigned int *puDebugFlag = NULL;
	struct fw_debug_flag *debug_flag;
	phys_addr_t debug_flag_phyAddr;
	struct fw_debug_print_memory *debug_print;
	phys_addr_t debug_print_phyAddr;
	struct device *dev = filp->private_data;

	switch (cmd) {
	case FWDBG_IOCTRGETDBGREG_A:
	case FWDBG_IOCTRGETDBGREG_V:
		if (copy_from_user(&dFlag, (void __user *)arg, sizeof(dFlag)))
			return -EFAULT;
		debug_flag = get_debug_flag(dev);
		debug_flag_phyAddr = get_debug_flag_phyAddr(dev);

		if (debug_flag == NULL || -1 == debug_flag_phyAddr)
			return -EFAULT;

		if (cmd == FWDBG_IOCTRGETDBGREG_V) {
			puDebugFlag = &debug_flag->vcpu;
			debug_flag_phyAddr = debug_flag_phyAddr + offsetof(struct fw_debug_flag, vcpu);
		} else {
			puDebugFlag = &debug_flag->acpu;
			debug_flag_phyAddr = debug_flag_phyAddr + offsetof(struct fw_debug_flag, acpu);
		}

		if (dFlag.op == FWDBG_DBGREG_SET) {
			*puDebugFlag = dFlag.flagValue;
		} else {
			dFlag.flagValue = (unsigned int)*puDebugFlag;
			dFlag.flagAddr = (uint32_t) debug_flag_phyAddr & -1U;
			if (copy_to_user((void __user *)arg, &dFlag, sizeof(dFlag)))
				return -EFAULT;
		}

		pr_debug("FWDBG cmd=%s op=%s phyAddr=0x%08llx flag=0x%08x",
			(cmd == FWDBG_IOCTRGETDBGREG_V) ? "FWDBG_IOCTRGETDBGREG_V" : "FWDBG_IOCTRGETDBGREG_A",
			(dFlag.op == FWDBG_DBGREG_SET) ? "SET" : "GET",
			debug_flag_phyAddr, *puDebugFlag);
		break;
	case FWDBG_IOCTRGETDBGPRINT_V:
		if (copy_from_user(&dFlag, (void __user *)arg, sizeof(dFlag)))
			return -EFAULT;

		debug_print = get_debug_print(dev);
		debug_print_phyAddr = get_debug_print_phyAddr(dev);

		if (debug_print == NULL || -1 == debug_print_phyAddr)
			return -EFAULT;

		dFlag.flagAddr = (uint32_t) debug_print_phyAddr & -1U;
		dFlag.flagValue = debug_print->fd;

		if (copy_to_user((void __user *)arg, &dFlag, sizeof(dFlag)))
			return -EFAULT;
		break;
	default:
		pr_warn("[FWDBG]: error ioctl command...\n");
		break;
	}

	return 0;
}

int rtk_fwdbg_open(struct inode *inode, struct file *filp)
{

	filp->private_data = fw_dbg_dev;

	return 0;
}


static const struct file_operations rtk_fwdbg_fops = {
	.owner      = THIS_MODULE,
	.open       = rtk_fwdbg_open,
	.unlocked_ioctl = rtk_fwdbg_ioctl,
	.compat_ioctl = rtk_fwdbg_ioctl,
};


static char *fwdbg_devnode(const struct device *dev, umode_t *mode)
{
	*mode = 0660;
	return NULL;
}


static int rtk_fwdbg_probe(struct platform_device *pdev)
{
	struct class *fwdbg_class = NULL;
	int ret = 0;
	struct cdev *cdev;
	struct device *dev;
	dev_t devno;

	ret = alloc_chrdev_region(&devno, 0, 1, "rtk_fwdbg");
	if (ret < 0) {
		dev_err(&pdev->dev, "alloc_chrdev_region failed");
		return ret;
	}

	cdev = cdev_alloc();
	if (IS_ERR(cdev)) {
		ret = PTR_ERR(cdev);
		goto unregister_devno;
	}

	cdev_init(cdev, &rtk_fwdbg_fops);
	cdev->owner = THIS_MODULE;
	ret = cdev_add(cdev, devno, 1);
	if (ret)
		goto free_cdev;

	fwdbg_class = class_create("rtk_fwdbg");
	if (IS_ERR(fwdbg_class)) {
		ret = PTR_ERR(fwdbg_class);
		goto unregister_cdev;
	}

	fwdbg_class->devnode = fwdbg_devnode;
	dev = device_create(fwdbg_class, NULL, devno, NULL, "rtk_fwdbg");
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto delete_class;
	}

	mDebugFlagMemory = NULL;
	mDebugPrintMemory = NULL;

	fw_dbg_dev = &pdev->dev;

	fw_dbg_dev->coherent_dma_mask = DMA_BIT_MASK(32);
	fw_dbg_dev->dma_mask = (u64 *)&fw_dbg_dev->coherent_dma_mask;

	dev_info(&pdev->dev, "probe\n");

	return 0;

delete_class:
	class_destroy(fwdbg_class);

unregister_cdev:
	cdev_del(cdev);

free_cdev:
	kfree(cdev);

unregister_devno:
	unregister_chrdev_region(devno, 1);

	return ret;

}


static const struct of_device_id rtk_fwdbg_of_match[] = {
	{ .compatible = "realtek, rtk-fwdbg"},
	{},
};
MODULE_DEVICE_TABLE(of, rtk_fwdbg_of_match);

static struct platform_driver rtk_fwdbg_driver = {
	.probe = rtk_fwdbg_probe,
	.driver = {
		.name = "rtk-fwdbg",
		.of_match_table = rtk_fwdbg_of_match,
	},
};

static int __init rtk_fwdbg_init(void)
{
	return platform_driver_register(&rtk_fwdbg_driver);
}
module_init(rtk_fwdbg_init);

static void __exit rtk_fwdbg_exit(void)
{
	platform_driver_register(&rtk_fwdbg_driver);
}
module_exit(rtk_fwdbg_exit);

MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL");
