// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016-2020 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#define pr_fmt(fmt) "rtk-efuse: " fmt

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/hwspinlock.h>
#include <linux/regmap.h>
#include <soc/realtek/rtk_clk_det.h>

#define OTP_CTRL              0x000
#define OTP_CTRL_ST           0x004
#define OTP_CRC               0x008
#define OTP_TM                0x00c
#define OTP_DBG               0x010
#define OTP_TM_ST             0x014
#define OTP_DUMMY             0x018
#define OTP_CFG               0x020
#define OTP_RINGOSC           0x024
#define OTP_CLK_DET           0x028

struct rtk_efuse_desc {
	int size;                /* data size */
	int dat_reg_sel;         /* use an alternative data register */
	int ctl_offset;          /* ctl register offset */
	u32 enable_icg : 1;      /* setup icg */
	u32 writable : 1;        /* data is writable */
	u32 recalibrate_dco : 1; /* recalibrate dco when use dco */
	u32 has_clk_det : 1;     /* hw contains clk_det */
};

struct rtk_efuse_device {
	struct nvmem_config config;
	struct device *dev;
	struct list_head list;
	void *base;
	void *ctl_base;
	struct nvmem_device *nvmem;
	struct mutex lock;
	struct hwspinlock *hwlock;
	const struct rtk_efuse_desc *desc;
	struct clk *clk;
	struct clk *dco;
};

static unsigned long rtk_efuse_lock(struct rtk_efuse_device *edev)
{
	unsigned long flags = 0;

	mutex_lock(&edev->lock);
	if (edev->hwlock)
		hwspin_lock_timeout_raw(edev->hwlock, UINT_MAX);
	return flags;
}

static void rtk_efuse_unlock(struct rtk_efuse_device *edev, unsigned long flags)
{
	if (edev->hwlock)
		hwspin_unlock_raw(edev->hwlock);
	mutex_unlock(&edev->lock);
}

static int rtk_efuse_ctrl_st_wait_done(struct rtk_efuse_device *edev, int timeout_us)
{
	unsigned int val;

	return readl_poll_timeout(edev->ctl_base + OTP_CTRL_ST, val, !(val & BIT(16)), 0, timeout_us);
}

static void rtk_efuse_ctrl_set(struct rtk_efuse_device *edev, unsigned int val)
{
	dev_dbg(edev->dev, "%s: val=%08x\n", __func__, val);
	writel(val, edev->ctl_base + OTP_CTRL);
}

static unsigned int rtk_efuse_ctrl_get(struct rtk_efuse_device *edev)
{
	return readl(edev->ctl_base + OTP_CTRL);
}

static void rtk_efuse_tm_set(struct rtk_efuse_device *edev, unsigned int val)
{
	writel(val, edev->ctl_base + OTP_TM);
}

static unsigned int rtk_efuse_tm_st_get(struct rtk_efuse_device *edev)
{
	return readl(edev->ctl_base + OTP_TM_ST);
}

static unsigned int rtk_efuse_read_otp(struct rtk_efuse_device *edev, unsigned int offset)
{
	return readb(edev->base + offset);
}

static unsigned char rtk_efuse_read_otp_clk_sel(struct rtk_efuse_device *edev)
{
	return rtk_efuse_read_otp(edev, 0x457) >> 5;
}

static int rtk_efuse_reg_read(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct rtk_efuse_device *edev = priv;
	unsigned long flags;
	int i;
	unsigned char *p = val;

	dev_dbg(edev->dev, "%s: offset=%03x, size=%zd\n", __func__, offset, bytes);
	might_sleep();

	flags = rtk_efuse_lock(edev);

	for (i = 0; i < bytes; i++)
		p[i] = rtk_efuse_read_otp(edev, offset + i);

	rtk_efuse_unlock(edev, flags);

	return 0;
}

static int rtk_efuse_set_clk(struct rtk_efuse_device *edev)
{
	unsigned long freq;
	unsigned int cmd;
	unsigned int clk_val;

	if (!edev->clk)
		return -EINVAL;

	freq = clk_get_rate(edev->clk);

	clk_val = DIV_ROUND_CLOSEST(freq, 1000000) - 1;
	dev_dbg(edev->dev, "%s: clk_val=%02x\n", __func__, clk_val);
	if (clk_val > 0xff)
		return -EINVAL;

	cmd = 0x100080d | (clk_val << 16);

	rtk_efuse_ctrl_set(edev, cmd);

	rtk_efuse_tm_set(edev, 0x300);

	return rtk_efuse_ctrl_st_wait_done(edev, 100);
}

static int rtk_efuse_prepare_write_cmd(struct rtk_efuse_device *edev)
{
	int ret = 0;

	if (!edev->desc->recalibrate_dco)
		return 0;
	if (rtk_efuse_read_otp_clk_sel(edev) <= 1)
		return 0;

	clk_prepare_enable(edev->dco);
	ret = rtk_efuse_set_clk(edev);
	if (ret)
		clk_disable_unprepare(edev->dco);
	return ret;
}

static void rtk_efuse_unprepare_write_cmd(struct rtk_efuse_device *edev)
{
	if (!edev->desc->recalibrate_dco)
		return;
	if (rtk_efuse_read_otp_clk_sel(edev) <= 1)
		return;
	clk_disable_unprepare(edev->dco);
}

static unsigned int generate_write_cmd(struct rtk_efuse_device *edev, int addr, unsigned char val)
{
	unsigned int cmd = 0x31000800;

	if (addr >= edev->desc->size)
		return 0;

	cmd |= (val << 16) | (addr & 0x7ff);

	if (edev->desc->size == 0x1000)
		cmd |= 0x2000 | ((addr & 0x800) << 1);

	return cmd;
}

static int __rtk_efuse_program(struct rtk_efuse_device *edev, int addr, unsigned char val)
{
	unsigned int cmd;
	int ret;

	if (rtk_efuse_ctrl_st_wait_done(edev, 20))
		return -EBUSY;

	ret = rtk_efuse_prepare_write_cmd(edev);
	if (ret)
		return ret;

	cmd = generate_write_cmd(edev, addr, val);
	if (!cmd) {
		ret = -EINVAL;
		goto done;
	}

	rtk_efuse_ctrl_set(edev, cmd);

	ret = rtk_efuse_ctrl_st_wait_done(edev, 100);
	if (ret)
		goto done;
	udelay(250);

	if ((rtk_efuse_tm_st_get(edev) & 0x300) != 0x100)
		ret = -EIO;
done:
	rtk_efuse_unprepare_write_cmd(edev);
	return ret;
}

static int rtk_efuse_program(struct rtk_efuse_device *edev, int addr, unsigned char val)
{
	unsigned long flags;
	unsigned char val_before, val_after;
	int ret;

	dev_dbg(edev->dev, "%s: addr=%03x, val=%02x\n", __func__, addr, val);

	flags = rtk_efuse_lock(edev);

	val_before = rtk_efuse_read_otp(edev, addr);
	if (val_before & ~val) {
		rtk_efuse_unlock(edev, flags);
		return -EINVAL;
	}

	val = val_before | val;
	ret = __rtk_efuse_program(edev, addr, val);

	rtk_efuse_unlock(edev, flags);

	val_after = rtk_efuse_read_otp(edev, addr);

	if (ret || val != val_after) {
		dev_err(edev->dev, "%s: addr=%03x, val: excepted=%02x, before=%02x, after=%02x, ret=%d\n",
			__func__, addr, val, val_before, val_after, ret);
		dev_err(edev->dev, "%s: OTP_CTRL=%08x, OTP_TM_ST=%08x\n",
			__func__, rtk_efuse_ctrl_get(edev), rtk_efuse_tm_st_get(edev));
	}

	return ret;
}

static int rtk_efuse_write_byte(struct rtk_efuse_device *edev, int addr, unsigned char val)
{
	int retry = 20;
	int ret;

	if (val == 0)
		return 0;

again:
	dev_dbg(edev->dev, "%s: addr=%03x, val=%02x\n", __func__, addr, val);
	ret = rtk_efuse_program(edev, addr, val);

	if (ret == -EBUSY && retry-- >= 0)
		goto again;
	return ret;
}

static int rtk_efuse_reg_write(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct rtk_efuse_device *edev = priv;
	unsigned char *p = val;
	int i;
	int ret;

	dev_dbg(edev->dev, "%s: offset=%03x, size=%zu\n", __func__, offset, bytes);
	might_sleep();

	for (i = 0; i < bytes; i++) {
		ret = rtk_efuse_write_byte(edev, offset + i, p[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct rtk_efuse_desc rtd1295_efuse_desc = {
	.size = 0x400,
	.ctl_offset = 0x400,
	.enable_icg = 0,
	.writable = 0,
};

static const struct rtk_efuse_desc rtd1619_efuse_desc = {
	.size = 0x800,
	.ctl_offset = 0x800,
	.enable_icg = 1,
	.writable = 1,
};

static const struct rtk_efuse_desc rtd1619b_efuse_desc = {
	.size = 0x1000,
	.dat_reg_sel = 1,
	.ctl_offset = 0x800,
	.enable_icg = 1,
	.writable = 1,
	.recalibrate_dco = 1,
	.has_clk_det = 1,
};

static const struct rtk_efuse_desc rtd1319d_efuse_desc = {
	.size = 0x1000,
	.dat_reg_sel = 1,
	.ctl_offset = 0x800,
	.enable_icg = 0,
	.writable = 1,
	.recalibrate_dco = 1,
	.has_clk_det = 1,
};

static int rtk_efuse_enable_powersaving(struct rtk_efuse_device *edev)
{
	if (!edev->desc->enable_icg)
		return 0;

	writel(0x0C00C000, edev->ctl_base + OTP_CTRL);
	return 0;
}

static int rtk_efuse_probe(struct platform_device *pdev)
{
	struct rtk_efuse_device *edev;
	struct device *dev = &pdev->dev;
	struct resource *res;
	const struct rtk_efuse_desc *desc;
	int lock_id;
	int ret;

	desc = of_device_get_match_data(dev);
	if (!desc)
		desc = &rtd1295_efuse_desc;

	edev = devm_kzalloc(dev, sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, desc->dat_reg_sel);
	edev->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(edev->base)) {
		dev_err(dev, "failed to get base\n");
		return PTR_ERR(edev->base);
	}

	if (desc->dat_reg_sel != 0) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		edev->ctl_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(edev->ctl_base)) {
			dev_err(dev, "failed to get ctl_base\n");
			return PTR_ERR(edev->ctl_base);
		}
	} else
		edev->ctl_base = edev->base;

	edev->ctl_base += desc->ctl_offset;

	edev->dev = dev;
	edev->desc = desc;
	mutex_init(&edev->lock);

	lock_id = of_hwspin_lock_get_id(dev->of_node, 0);
	if (lock_id > 0 || (IS_ENABLED(CONFIG_HWSPINLOCK) && lock_id == 0)) {
		struct hwspinlock *lock = devm_hwspin_lock_request_specific(dev, lock_id);

		if (lock) {
			dev_info(dev, "use hwlock%d\n", lock_id);
			edev->hwlock = lock;
		}
       } else {
	       if (lock_id != -ENOENT)
		       dev_err(dev, "failed to get hwlock: %pe\n", ERR_PTR(lock_id));
	}

	edev->config.owner     = THIS_MODULE;
	edev->config.name      = "rtk-efuse";
	edev->config.stride    = 1;
	edev->config.word_size = 1;
	edev->config.reg_read  = rtk_efuse_reg_read;
	edev->config.reg_write = edev->desc->writable ? rtk_efuse_reg_write : NULL;
	edev->config.dev       = dev;
	edev->config.size      = desc->size;
	edev->config.priv      = edev;
	edev->config.add_legacy_fixed_of_cells = 1;

	edev->nvmem = devm_nvmem_register(dev, &edev->config);
	if (IS_ERR(edev->nvmem))
		return PTR_ERR(edev->nvmem);

	rtk_efuse_enable_powersaving(edev);

	if (desc->has_clk_det && IS_ENABLED(CONFIG_RTK_CLK_DET)) {
		struct clk_det_initdata initdata = {0};

		initdata.name = "ref_clk_otp";
		initdata.base = edev->ctl_base + OTP_CLK_DET;
		initdata.type = CLK_DET_TYPE_GENERIC;

		edev->clk = devm_clk_det_register(dev, &initdata);
		if (IS_ERR(edev->clk)) {
			ret = PTR_ERR(edev->clk);
			dev_err(dev, "failed to add clk_det: %d\n",ret);
			return ret;
		}
	}

	if (desc->recalibrate_dco) {
		edev->dco = devm_clk_get(dev, NULL);
		if (IS_ERR(edev->dco)) {
			ret = PTR_ERR(edev->dco);
			dev_warn(dev, "failed to get dco: %d\n", ret);
			edev->dco = NULL;
		}
	}

	platform_set_drvdata(pdev, edev);

	return 0;
}

static const struct of_device_id rtk_efuse_of_match[] = {
	{.compatible = "realtek,efuse",       .data = &rtd1295_efuse_desc, },
	{.compatible = "realtek,rtd1619-otp", .data = &rtd1619_efuse_desc, },
	{.compatible = "realtek,rtd1619b-otp", .data = &rtd1619b_efuse_desc, },
	{.compatible = "realtek,rtd1319d-otp", .data = &rtd1319d_efuse_desc, },
	{},
};

static struct platform_driver rtk_efuse_drv = {
	.probe = rtk_efuse_probe,
	.driver = {
		.name = "rtk-efuse",
		.owner = THIS_MODULE,
		.of_match_table = rtk_efuse_of_match,
	},
};

static __init int rtk_efuse_init(void)
{
	return platform_driver_register(&rtk_efuse_drv);
}
subsys_initcall(rtk_efuse_init);

MODULE_DESCRIPTION("Realtek eFuse driver");
MODULE_ALIAS("platform:rtk-efuse");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
