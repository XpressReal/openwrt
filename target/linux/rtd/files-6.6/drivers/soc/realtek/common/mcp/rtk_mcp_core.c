// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017,2023 Realtek Semiconductor Corp.
 */

#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <soc/realtek/uapi/rtk_mcp.h>
#include "rtk_mcp.h"

#define MCP_DESC_ENTRY_COUNT   64
#define MCP_DESC_ENTRY_COUNT_MAX  1024

struct mcp_file_data {
	struct mcp_device_data *data;
	struct mcp_desc        descs[MCP_DESC_ENTRY_COUNT];
	struct rb_root         va_root;
	struct mutex           va_lock;
};

static void mcp_buf_rbtree_remove(struct rb_node *node, struct rb_root *tree);

static int mcp_open(struct inode *inode, struct file *file)
{
	struct mcp_file_data *file_data;
	struct device *dev;

	file_data = kzalloc(sizeof(*file_data), GFP_KERNEL);
	if (!file_data)
		return -EINVAL;

	dev = driver_find_device(&rtk_mcp_driver.driver, NULL, NULL, device_match_any);
	if (dev)
		file_data->data = dev_get_drvdata(dev);
	file_data->va_root = RB_ROOT;
	mutex_init(&file_data->va_lock);
	file->private_data = file_data;

	return 0;
}

static int mcp_release(struct inode *inode, struct file *file)
{
	struct mcp_file_data *file_data = file->private_data;

	struct rb_node *node;
	struct mcp_dma_buf *mcp_buf;

	mutex_lock(&file_data->va_lock);
	for (node = rb_first(&file_data->va_root); node; node = rb_first(&file_data->va_root)) {
		mcp_buf_rbtree_remove(node, &file_data->va_root);
		mutex_unlock(&file_data->va_lock);

	        mcp_buf = rb_entry(node, struct mcp_dma_buf, rb_node);

		pr_info("%s: %pad not freed\n", __func__, &mcp_buf->dma_addr);
		mcp_release_buf(mcp_buf);
	        kfree(mcp_buf);

		mutex_lock(&file_data->va_lock);
	}
	mutex_unlock(&file_data->va_lock);

	kfree(file_data);
	return 0;
}

static inline bool mcp_key_in_cw(struct mcp_desc *desc)
{
	return (desc->ctrl & MCP_KEY_SEL_MASK) == MCP_KEY_SEL_CW;
}

static inline bool mcp_mode_is_aes_192(struct mcp_desc *desc)
{
	return (desc->ctrl & MCP_MODE_MASK) == MCP_MODE_AES_192;
}

static inline bool mcp_mode_is_aes_256(struct mcp_desc *desc)
{
	return (desc->ctrl & MCP_MODE_MASK) == MCP_MODE_AES_256;
}

static int mcp_ioctl_do_command(struct mcp_file_data *file_data, unsigned long arg)
{
	struct mcp_device_data *data = file_data->data;
	struct mcp_desc *descs = file_data->descs;
	void __user *p;
	struct mcp_desc_set set = {};
	u32 n_desc;
	int ret;

	p = in_compat_syscall() ? compat_ptr(arg) : (void __user *)arg;
	if (copy_from_user(&set, p, sizeof(set)))
		return -EFAULT;

	n_desc = set.n_desc;
	if (n_desc > MCP_DESC_ENTRY_COUNT_MAX)
		return -EINVAL;

	p = in_compat_syscall() ? compat_ptr((unsigned long)set.p_desc) : set.p_desc;

	while (n_desc > 0) {
		int n = min(n_desc, (u32)MCP_DESC_ENTRY_COUNT);

		if (copy_from_user(descs, p, sizeof(*descs) * n))
			return -EFAULT;

		ret = mcp_do_command(data, descs, n);
		if (ret)
			return ret;

		p += n * sizeof(*descs);
		n_desc -= n;
	}
	return 0;
}

static int mcp_buf_va_less(struct rb_node *_a, struct rb_node *_b)
{
	struct mcp_dma_buf *a = rb_entry(_a, struct mcp_dma_buf, rb_node);
	struct mcp_dma_buf *b = rb_entry(_b, struct mcp_dma_buf, rb_node);

	return a->dma_addr < b->dma_addr;
}

static void mcp_buf_rbtree_add(struct rb_node *node, struct rb_root *tree)
{
	struct rb_node **link = &tree->rb_node;
	struct rb_node *parent = NULL;

	while (*link) {
		parent = *link;
		if (mcp_buf_va_less(node, parent))
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}

	rb_link_node(node, parent, link);
	rb_insert_color(node, tree);
}

static struct rb_node *mcp_buf_rbtree_find_va(struct rb_root *tree, u32 mcp_va)
{
	struct rb_node *node = tree->rb_node;

	while (node) {
		u32 node_va = rb_entry(node, struct mcp_dma_buf, rb_node)->dma_addr;

		if (node_va == mcp_va)
			return node;
		else if (node_va < mcp_va)
			node = node->rb_right;
		else
			node = node->rb_left;
	}

	return NULL;
}

static void mcp_buf_rbtree_remove(struct rb_node *node, struct rb_root *tree)
{
	rb_erase(node, tree);
}

static int mcp_ioctl_mem_import(struct mcp_file_data *file_data, unsigned long arg)
{
	struct mcp_device_data *data = file_data->data;
	struct mcp_mem_import m;
	struct mcp_dma_buf *mcp_buf;
	int ret;

	if (copy_from_user(&m, (void *)arg, sizeof(m)))
		return -EFAULT;

	mcp_buf = kzalloc(sizeof(*mcp_buf), GFP_KERNEL);
	if (!mcp_buf)
		return -ENOMEM;

	ret = mcp_import_buf(data, mcp_buf, m.fd);
	if (ret)
		goto free_mcp_buf;

	mutex_lock(&file_data->va_lock);
	mcp_buf_rbtree_add(&mcp_buf->rb_node, &file_data->va_root);
	mutex_unlock(&file_data->va_lock);

	m.mcp_va = mcp_buf->dma_addr;

	if (copy_to_user((void *)arg, &m, sizeof(m))){
		ret = -EFAULT;
		goto remove_from_va_tree;
	}

	return 0;

remove_from_va_tree:
	mutex_lock(&file_data->va_lock);
	mcp_buf_rbtree_remove(&mcp_buf->rb_node, &file_data->va_root);
	mutex_unlock(&file_data->va_lock);

	mcp_release_buf(mcp_buf);
free_mcp_buf:
	kfree(mcp_buf);
	return ret;
}

static int mcp_ioctl_mem_free(struct mcp_file_data *file_data, unsigned long arg)
{
	struct mcp_mem_free m;
	struct rb_node *node;
	struct mcp_dma_buf *mcp_buf;

	if (copy_from_user(&m, (void *)arg, sizeof(m)))
                return -EFAULT;

	mutex_lock(&file_data->va_lock);
	node = mcp_buf_rbtree_find_va(&file_data->va_root, m.mcp_va);
	if (!node) {
		mutex_unlock(&file_data->va_lock);
		return -EINVAL;
	}
        mcp_buf_rbtree_remove(node, &file_data->va_root);
	mutex_unlock(&file_data->va_lock);

	mcp_buf = rb_entry(node, struct mcp_dma_buf, rb_node);
	mcp_release_buf(mcp_buf);
	kfree(mcp_buf);
	return 0;
}

static long mcp_ioctl_handle_mcp_dev_cmds(struct mcp_file_data *file_data, unsigned int cmd, unsigned long arg)
{
	struct mcp_device_data *data = file_data->data;

	if (!data)
		return -ENODEV;

	switch (cmd) {
	case MCP_IOCTL_DO_COMMAND_LEGACY:
	case MCP_IOCTL_DO_COMMAND:
		return mcp_ioctl_do_command(file_data, arg);

	case MCP_IOCTL_ENABLE_AUTO_PADDING:
		mcp_set_auto_padding(data, 1);
		break;

	case MCP_IOCTL_DISABLE_AUTO_PADDING:
		mcp_set_auto_padding(data, 0);
		break;

	case MCP_IOCTL_MEM_IMPORT:
		return mcp_ioctl_mem_import(file_data, arg);

	case MCP_IOCTL_MEM_FREE:
		return mcp_ioctl_mem_free(file_data, arg);

	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static long mcp_ioctl_handle_smcp_cmds(unsigned int cmd, unsigned long arg)
{
	void *p = in_compat_syscall() ? compat_ptr(arg) : (void __user *)arg;
	struct smcp_desc_set set;

	if (copy_from_user(&set, p, sizeof(set)))
		return -EFAULT;

	switch (cmd) {
	case SMCP_IOCTL_ENCRYPT_IMAGE_WITH_IK:
		return smcp_encrypt_image_with_ik(set.data_in, set.data_out, set.length);

	case SMCP_IOCTL_DECRYPT_IMAGE_WITH_IK:
		return smcp_decrypt_image_with_ik(set.data_in, set.data_out, set.length);

	default:
		return -ENOIOCTLCMD;
	}
}

static long mcp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct mcp_file_data *file_data = file->private_data;

	switch (cmd) {
	case MCP_IOCTL_DO_COMMAND_LEGACY:
	case MCP_IOCTL_DO_COMMAND:
	case MCP_IOCTL_ENABLE_AUTO_PADDING:
	case MCP_IOCTL_DISABLE_AUTO_PADDING:
	case MCP_IOCTL_MEM_IMPORT:
	case MCP_IOCTL_MEM_FREE:
		return mcp_ioctl_handle_mcp_dev_cmds(file_data, cmd, arg);

	case SMCP_IOCTL_ENCRYPT_IMAGE_WITH_IK:
	case SMCP_IOCTL_DECRYPT_IMAGE_WITH_IK:
		return mcp_ioctl_handle_smcp_cmds(cmd, arg);

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static const struct file_operations mcp_ops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = mcp_ioctl,
	.compat_ioctl   = mcp_ioctl,
	.open           = mcp_open,
	.release        = mcp_release,
};

static struct miscdevice mcp_mdev;

static __init int rtk_mcp_init(void)
{
	int ret;

	mcp_mdev.minor = MISC_DYNAMIC_MINOR;
	mcp_mdev.name  = "mcp_core";
	mcp_mdev.fops  = &mcp_ops;
	ret = misc_register(&mcp_mdev);
	if (ret)
		return ret;

	ret = platform_driver_register(&rtk_mcp_driver);
	if (ret)
		misc_deregister(&mcp_mdev);
	return ret;
}
module_init(rtk_mcp_init);

static __exit void rtk_mcp_exit(void)
{
	platform_driver_unregister(&rtk_mcp_driver);
	misc_deregister(&mcp_mdev);

}
module_exit(rtk_mcp_exit);

MODULE_LICENSE("GPL");
