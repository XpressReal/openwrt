// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017,2023 Realtek Semiconductor Corp.
 */

#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/hwspinlock.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <soc/realtek/uapi/rtk_mcp.h>
#include "rtk_mcp.h"

#define MCP_DESC_ENTRY_COUNT   64

#define MCP_HWLOCK_SLEEP_US    100

/* MCP registers */
#define MCP_CTRL            0x100
#define MCP_STATUS          0x104
#define MCP_EN              0x108
#define MCP_BASE            0x10c
#define MCP_LIMIT           0x110
#define MCP_RDPTR           0x114
#define MCP_WRPTR           0x118
#define MCP_DES_INI_KEY     0x11c
#define MCP_AES_INI_KEY     0x124
#define MCP_DES_COUNT       0x134
#define MCP_DES_COMPARE     0x138
#define MCP_CTRL1           0x198

#define MCP_CTRL_GO            BIT(1)
#define MCP_STATUS_RING_EMPTY  BIT(1)
#define MCP_STATUS_COMPARE     BIT(2)
#define MCP_STATUS_K_KL_DONE   BIT(13)
#define MCP_STATUS_KL_DONE     BIT(20)

struct mcp_device_data {
	void __iomem *base;
	struct device *dev;
	struct hwspinlock *hwlock;
	struct mutex lock;
};

static unsigned int mcp_reg_read(struct mcp_device_data *data, int offset)
{
	return readl(data->base + offset);
}

static void mcp_reg_write(struct mcp_device_data *data, int offset, unsigned int val)
{
	writel(val, data->base + offset);
}

static void mcp_reg_update_bits(struct mcp_device_data *data, int offset, unsigned int mask,
	unsigned int val)
{
	unsigned int v;

	v = mcp_reg_read(data, offset);
	v = (v & ~mask) | (val & mask);
	mcp_reg_write(data, offset, v);
}

void mcp_set_auto_padding(struct mcp_device_data *data, int endis)
{
	mcp_reg_update_bits(data, MCP_CTRL1, BIT(11), endis ? 0 : BIT(11));
}

static void mcp_hw_set_desc_buf(struct mcp_device_data *data,
	unsigned long base, unsigned long limit, unsigned long rp, unsigned long wp)
{
	mcp_reg_write(data, MCP_BASE, base);
	mcp_reg_write(data, MCP_LIMIT, limit);
	mcp_reg_write(data, MCP_RDPTR, rp);
	mcp_reg_write(data, MCP_WRPTR, wp);
}

static int mcp_hw_setup(struct mcp_device_data *data)
{
	mcp_reg_write(data, MCP_CTRL, MCP_CTRL_GO);
	mcp_reg_write(data, MCP_EN, ~1);
	mcp_reg_write(data, MCP_STATUS, ~1);
	mcp_hw_set_desc_buf(data, 0, 0, 0, 0);

	return 0;
}

static void mcp_hw_teardown(struct mcp_device_data *data)
{
	mcp_reg_write(data, MCP_CTRL, MCP_CTRL_GO);
	mcp_reg_write(data, MCP_EN, 0xFE);
	usleep_range(10000, 10001); // wait for hw stop
	mcp_hw_set_desc_buf(data, 0, 0, 0, 0);
}

static int mcp_start_xfer(struct mcp_device_data *data)
{
	int ret;
	unsigned int val;

	mcp_reg_write(data, MCP_CTRL, MCP_CTRL_GO | 1);

	ret = readl_poll_timeout(data->base + MCP_STATUS, val,
		(val & 0x6) || (mcp_reg_read(data, MCP_CTRL) & MCP_CTRL_GO) == 0, 1000, 4000000);

	if (ret != -ETIMEDOUT)
		ret = (val & ~(MCP_STATUS_RING_EMPTY | MCP_STATUS_COMPARE | MCP_STATUS_KL_DONE |
			MCP_STATUS_K_KL_DONE)) ? -EINVAL : 0;

	mcp_reg_write(data, MCP_CTRL, MCP_CTRL_GO);
	mcp_reg_write(data, MCP_STATUS, ~1);

	return ret;
}

static int mcp_lock_hw_interruptible(struct mcp_device_data *data)
{
	int ret;

	ret = mutex_lock_interruptible(&data->lock);
	if (ret)
		return ret;

	if (!data->hwlock)
		return 0;

	for (;;) {
		ret = hwspin_trylock_raw(data->hwlock);
		if (ret == 0)
			break;

		if (signal_pending(current)) {
			mutex_unlock(&data->lock);
			return -EINTR;
		}

		usleep_range(MCP_HWLOCK_SLEEP_US, MCP_HWLOCK_SLEEP_US + 1);
	}
	return 0;
}

static void mcp_hw_unlock(struct mcp_device_data *data)
{
	if (data->hwlock)
		hwspin_unlock_raw(data->hwlock);
	mutex_unlock(&data->lock);
}

int mcp_do_command(struct mcp_device_data *data, struct mcp_desc *descs, int n)
{
	struct device *dev = data->dev;
	size_t limit = sizeof(struct mcp_desc) * (n + 1);
	size_t len = sizeof(struct mcp_desc) * n;
	dma_addr_t desc_dma;
	void *p;
	int ret;

	if (!n)
		return 0;

	p = dma_alloc_coherent(dev, limit, &desc_dma, GFP_ATOMIC | GFP_DMA);
	if (!p)
		return -ENOMEM;

	print_hex_dump_debug("raw_desc: ", DUMP_PREFIX_NONE, 16, 4, descs, len, false);
	memcpy(p, descs, len);

	ret = mcp_lock_hw_interruptible(data);
	if (ret)
		goto free_dma;

	mcp_hw_set_desc_buf(data, desc_dma, desc_dma + limit, desc_dma, desc_dma + len);
	ret = mcp_start_xfer(data);

	mcp_hw_unlock(data);

free_dma:
	dma_free_coherent(dev, len, p, desc_dma);
	return ret;
}

int mcp_import_buf(struct mcp_device_data *data, struct mcp_dma_buf *mcp_buf, int fd)
{
	struct device *dev = data->dev;
	struct dma_buf *buf = dma_buf_get(fd);
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	dma_addr_t dma_addr;
	int ret = 0;

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	attach = dma_buf_attach(buf, dev);
	if (IS_ERR(attach)) {
		ret = PTR_ERR(attach);
		dev_err(dev, "%s: cannot attach\n", __func__);
		goto put_dma_buf;
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		dev_err(dev, "%s: cannot map attachment\n", __func__);
		goto detach_dma_buf;
	}

	if (sgt->nents != 1) {
		ret = -EINVAL;
		dev_err(dev, "%s: scatter list not supportted\n", __func__);
		goto detach_dma_buf;
	}

	dma_addr = sg_dma_address(sgt->sgl);
	if (!dma_addr) {
		ret = -EINVAL;
		dev_err(dev, "%s: invalid dma address\n", __func__);
		goto detach_dma_buf;
	}

	mcp_buf->attachment = attach;
	mcp_buf->sgt        = sgt;
	mcp_buf->dma_addr   = dma_addr;
	mcp_buf->dma_len    = sg_dma_len(sgt->sgl);
	return 0;

detach_dma_buf:
	dma_buf_detach(buf, attach);
put_dma_buf:
	dma_buf_put(buf);
	return ret;
}

void mcp_release_buf(struct mcp_dma_buf *mcp_buf)
{
	struct dma_buf_attachment *attach = mcp_buf->attachment;
	struct dma_buf *buf;

	if (mcp_buf->sgt)
		dma_buf_unmap_attachment(attach, mcp_buf->sgt, DMA_BIDIRECTIONAL);

	buf = attach->dmabuf;
	dma_buf_detach(buf, attach);
	dma_buf_put(buf);
}

static int mcp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mcp_device_data *data;
	int lock_id;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -EINVAL;

	data->dev = dev;
	mutex_init(&data->lock);

	data->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->base)) {
		ret = PTR_ERR(data->base);
		dev_err(dev, "failed to remap cp register: %d\n", ret);
		return ret;
	}

	ret = mcp_hw_setup(data);
	if (ret) {
		dev_err(dev, "failed to init mcp hw: %d\n", ret);
		return ret;
	}

	lock_id = of_hwspin_lock_get_id(dev->of_node, 0);
	if (lock_id > 0 || (IS_ENABLED(CONFIG_HWSPINLOCK) && lock_id == 0)) {
		struct hwspinlock *lock = devm_hwspin_lock_request_specific(&pdev->dev, lock_id);

		if (lock) {
			dev_info(&pdev->dev, "use hwlock%d\n", lock_id);
			data->hwlock = lock;
		}
	} else {
		if (lock_id != -ENOENT)
			dev_warn(dev, "failed to get hwlock: %pe\n", ERR_PTR(lock_id));
	}

	platform_set_drvdata(pdev, data);

	return 0;
}

static int mcp_remove(struct platform_device *pdev)
{
	struct mcp_device_data *data = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	data->hwlock = NULL;
	mcp_hw_teardown(data);

	return 0;
}

static const struct of_device_id rtk_mcp_ids[] = {
	{ .compatible = "realtek,mcp" },
	{ /* Sentinel */ }
};

struct platform_driver rtk_mcp_driver = {
	.probe = mcp_probe,
	.remove = mcp_remove,
	.driver = {
		.name = "rtk-mcp",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(rtk_mcp_ids),
	},
};
