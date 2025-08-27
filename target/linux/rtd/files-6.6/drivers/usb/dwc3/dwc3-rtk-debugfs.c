// SPDX-License-Identifier: GPL-2.0
/**
 * dwc3-rtk-debugfs.c - Reltek DesignWare USB3 DRD Controller DebugFS file
 *
 * Copyright (C) 2023 Realtek Semiconductor Corporation
 *
 */

#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/of_address.h>
#include <linux/module.h>

#include "core.h"
#include "io.h"

struct dwc3_rtk_debug {
	struct device *dev;

	struct device *wrap;
	u64 regs_addr;
	size_t regs_size;
	u64 regs_pm_addr;
	size_t regs_pm_size;

	u64 regs_dwc;
	size_t regs_dwc_size;

	struct dentry *debug_dir;

	void *dwc3_regs_dump;
	void *wrap_regs_dump;
	void *wrap_pm_regs_dump;
};

#ifdef CONFIG_DEBUG_FS

struct rtk_debugfs_reg32 {
	char *name;
	unsigned long offset;
	int default_val;
};

#define dump_register(nm)				\
{							\
	.name	= __stringify(nm) " [" __stringify(DWC3_ ##nm) "]", \
	.offset	= DWC3_ ##nm,				\
}

#define dump_ep_register_set_DEPCMDPAR2(n)	\
	{					\
		.name = "DEPCMDPAR2("__stringify(n)") ["	\
			    __stringify(DWC3_DEP_BASE(n)) " + " \
			    __stringify(DWC3_DEPCMDPAR2) "]",	\
		.offset = DWC3_DEP_BASE(n) +	\
			DWC3_DEPCMDPAR2,	\
	},
#define dump_ep_register_set_DEPCMDPAR1(n)	\
	{					\
		.name = "DEPCMDPAR1("__stringify(n)") ["		\
			    __stringify(DWC3_DEP_BASE(n)) " + " \
			    __stringify(DWC3_DEPCMDPAR1) "]",	\
		.offset = DWC3_DEP_BASE(n) +	\
			DWC3_DEPCMDPAR1,	\
	},
#define dump_ep_register_set_DEPCMDPAR0(n)	\
	{					\
		.name = "DEPCMDPAR0("__stringify(n)") ["	\
			    __stringify(DWC3_DEP_BASE(n)) " + " \
			    __stringify(DWC3_DEPCMDPAR0) "]",	\
		.offset = DWC3_DEP_BASE(n) +	\
			DWC3_DEPCMDPAR0,	\
	},
#define dump_ep_register_set_CMD(n)		\
	{					\
		.name = "DEPCMD("__stringify(n)") [" \
			    __stringify(DWC3_DEP_BASE(n)) " + " \
			    __stringify(DWC3_DEPCMD) "]",	\
		.offset = DWC3_DEP_BASE(n) +	\
			DWC3_DEPCMD,		\
	}

#define dump_ep_register_set(n)				\
	dump_ep_register_set_DEPCMDPAR2(n)		\
	dump_ep_register_set_DEPCMDPAR1(n)		\
	dump_ep_register_set_DEPCMDPAR0(n)		\
	dump_ep_register_set_CMD(n)			\

static const struct rtk_debugfs_reg32 dwc3_regs[] = {
	dump_register(GSBUSCFG0),
	dump_register(GSBUSCFG1),
	dump_register(GTXTHRCFG),
	dump_register(GRXTHRCFG),
	dump_register(GCTL),
	dump_register(GEVTEN),
	dump_register(GSTS),
	dump_register(GUCTL1),
	dump_register(GSNPSID),
	dump_register(GGPIO),
	dump_register(GUID),
	dump_register(GUCTL),
	dump_register(GBUSERRADDR0),
	dump_register(GBUSERRADDR1),
	dump_register(GPRTBIMAP0),
	dump_register(GPRTBIMAP1),
	dump_register(GHWPARAMS0),
	dump_register(GHWPARAMS1),
	dump_register(GHWPARAMS2),
	dump_register(GHWPARAMS3),
	dump_register(GHWPARAMS4),
	dump_register(GHWPARAMS5),
	dump_register(GHWPARAMS6),
	dump_register(GHWPARAMS7),
	dump_register(GDBGFIFOSPACE),
	dump_register(GDBGLTSSM),
	dump_register(GPRTBIMAP_HS0),
	dump_register(GPRTBIMAP_HS1),
	dump_register(GPRTBIMAP_FS0),
	dump_register(GPRTBIMAP_FS1),

	dump_register(GUSB2PHYCFG(0)),
	dump_register(GUSB2PHYCFG(1)),
	dump_register(GUSB2PHYCFG(2)),
	dump_register(GUSB2PHYCFG(3)),
	dump_register(GUSB2PHYCFG(4)),
	dump_register(GUSB2PHYCFG(5)),
	dump_register(GUSB2PHYCFG(6)),
	dump_register(GUSB2PHYCFG(7)),
	dump_register(GUSB2PHYCFG(8)),
	dump_register(GUSB2PHYCFG(9)),
	dump_register(GUSB2PHYCFG(10)),
	dump_register(GUSB2PHYCFG(11)),
	dump_register(GUSB2PHYCFG(12)),
	dump_register(GUSB2PHYCFG(13)),
	dump_register(GUSB2PHYCFG(14)),
	dump_register(GUSB2PHYCFG(15)),

	dump_register(GUSB2I2CCTL(0)),
	dump_register(GUSB2I2CCTL(1)),
	dump_register(GUSB2I2CCTL(2)),
	dump_register(GUSB2I2CCTL(3)),
	dump_register(GUSB2I2CCTL(4)),
	dump_register(GUSB2I2CCTL(5)),
	dump_register(GUSB2I2CCTL(6)),
	dump_register(GUSB2I2CCTL(7)),
	dump_register(GUSB2I2CCTL(8)),
	dump_register(GUSB2I2CCTL(9)),
	dump_register(GUSB2I2CCTL(10)),
	dump_register(GUSB2I2CCTL(11)),
	dump_register(GUSB2I2CCTL(12)),
	dump_register(GUSB2I2CCTL(13)),
	dump_register(GUSB2I2CCTL(14)),
	dump_register(GUSB2I2CCTL(15)),

	dump_register(GUSB2PHYACC(0)),
	dump_register(GUSB2PHYACC(1)),
	dump_register(GUSB2PHYACC(2)),
	dump_register(GUSB2PHYACC(3)),
	dump_register(GUSB2PHYACC(4)),
	dump_register(GUSB2PHYACC(5)),
	dump_register(GUSB2PHYACC(6)),
	dump_register(GUSB2PHYACC(7)),
	dump_register(GUSB2PHYACC(8)),
	dump_register(GUSB2PHYACC(9)),
	dump_register(GUSB2PHYACC(10)),
	dump_register(GUSB2PHYACC(11)),
	dump_register(GUSB2PHYACC(12)),
	dump_register(GUSB2PHYACC(13)),
	dump_register(GUSB2PHYACC(14)),
	dump_register(GUSB2PHYACC(15)),

	dump_register(GUSB3PIPECTL(0)),
	dump_register(GUSB3PIPECTL(1)),
	dump_register(GUSB3PIPECTL(2)),
	dump_register(GUSB3PIPECTL(3)),
	dump_register(GUSB3PIPECTL(4)),
	dump_register(GUSB3PIPECTL(5)),
	dump_register(GUSB3PIPECTL(6)),
	dump_register(GUSB3PIPECTL(7)),
	dump_register(GUSB3PIPECTL(8)),
	dump_register(GUSB3PIPECTL(9)),
	dump_register(GUSB3PIPECTL(10)),
	dump_register(GUSB3PIPECTL(11)),
	dump_register(GUSB3PIPECTL(12)),
	dump_register(GUSB3PIPECTL(13)),
	dump_register(GUSB3PIPECTL(14)),
	dump_register(GUSB3PIPECTL(15)),

	dump_register(GTXFIFOSIZ(0)),
	dump_register(GTXFIFOSIZ(1)),
	dump_register(GTXFIFOSIZ(2)),
	dump_register(GTXFIFOSIZ(3)),
	dump_register(GTXFIFOSIZ(4)),
	dump_register(GTXFIFOSIZ(5)),
	dump_register(GTXFIFOSIZ(6)),
	dump_register(GTXFIFOSIZ(7)),
	dump_register(GTXFIFOSIZ(8)),
	dump_register(GTXFIFOSIZ(9)),
	dump_register(GTXFIFOSIZ(10)),
	dump_register(GTXFIFOSIZ(11)),
	dump_register(GTXFIFOSIZ(12)),
	dump_register(GTXFIFOSIZ(13)),
	dump_register(GTXFIFOSIZ(14)),
	dump_register(GTXFIFOSIZ(15)),
	dump_register(GTXFIFOSIZ(16)),
	dump_register(GTXFIFOSIZ(17)),
	dump_register(GTXFIFOSIZ(18)),
	dump_register(GTXFIFOSIZ(19)),
	dump_register(GTXFIFOSIZ(20)),
	dump_register(GTXFIFOSIZ(21)),
	dump_register(GTXFIFOSIZ(22)),
	dump_register(GTXFIFOSIZ(23)),
	dump_register(GTXFIFOSIZ(24)),
	dump_register(GTXFIFOSIZ(25)),
	dump_register(GTXFIFOSIZ(26)),
	dump_register(GTXFIFOSIZ(27)),
	dump_register(GTXFIFOSIZ(28)),
	dump_register(GTXFIFOSIZ(29)),
	dump_register(GTXFIFOSIZ(30)),
	dump_register(GTXFIFOSIZ(31)),

	dump_register(GRXFIFOSIZ(0)),
	dump_register(GRXFIFOSIZ(1)),
	dump_register(GRXFIFOSIZ(2)),
	dump_register(GRXFIFOSIZ(3)),
	dump_register(GRXFIFOSIZ(4)),
	dump_register(GRXFIFOSIZ(5)),
	dump_register(GRXFIFOSIZ(6)),
	dump_register(GRXFIFOSIZ(7)),
	dump_register(GRXFIFOSIZ(8)),
	dump_register(GRXFIFOSIZ(9)),
	dump_register(GRXFIFOSIZ(10)),
	dump_register(GRXFIFOSIZ(11)),
	dump_register(GRXFIFOSIZ(12)),
	dump_register(GRXFIFOSIZ(13)),
	dump_register(GRXFIFOSIZ(14)),
	dump_register(GRXFIFOSIZ(15)),
	dump_register(GRXFIFOSIZ(16)),
	dump_register(GRXFIFOSIZ(17)),
	dump_register(GRXFIFOSIZ(18)),
	dump_register(GRXFIFOSIZ(19)),
	dump_register(GRXFIFOSIZ(20)),
	dump_register(GRXFIFOSIZ(21)),
	dump_register(GRXFIFOSIZ(22)),
	dump_register(GRXFIFOSIZ(23)),
	dump_register(GRXFIFOSIZ(24)),
	dump_register(GRXFIFOSIZ(25)),
	dump_register(GRXFIFOSIZ(26)),
	dump_register(GRXFIFOSIZ(27)),
	dump_register(GRXFIFOSIZ(28)),
	dump_register(GRXFIFOSIZ(29)),
	dump_register(GRXFIFOSIZ(30)),
	dump_register(GRXFIFOSIZ(31)),

	dump_register(GEVNTADRLO(0)),
	dump_register(GEVNTADRHI(0)),
	dump_register(GEVNTSIZ(0)),
	dump_register(GEVNTCOUNT(0)),

	dump_register(GHWPARAMS8),
	dump_register(DCFG),
	dump_register(DCTL),
	dump_register(DEVTEN),
	dump_register(DSTS),
	dump_register(DGCMDPAR),
	dump_register(DGCMD),
	dump_register(DALEPENA),

	dump_ep_register_set(0),
	dump_ep_register_set(1),
	dump_ep_register_set(2),
	dump_ep_register_set(3),
	dump_ep_register_set(4),
	dump_ep_register_set(5),
	dump_ep_register_set(6),
	dump_ep_register_set(7),
	dump_ep_register_set(8),
	dump_ep_register_set(9),
	dump_ep_register_set(10),
	dump_ep_register_set(11),
	dump_ep_register_set(12),
	dump_ep_register_set(13),
	dump_ep_register_set(14),
	dump_ep_register_set(15),
	dump_ep_register_set(16),
	dump_ep_register_set(17),
	dump_ep_register_set(18),
	dump_ep_register_set(19),
	dump_ep_register_set(20),
	dump_ep_register_set(21),
	dump_ep_register_set(22),
	dump_ep_register_set(23),
	dump_ep_register_set(24),
	dump_ep_register_set(25),
	dump_ep_register_set(26),
	dump_ep_register_set(27),
	dump_ep_register_set(28),
	dump_ep_register_set(29),
	dump_ep_register_set(30),
	dump_ep_register_set(31),

	dump_register(OCFG),
	dump_register(OCTL),
	dump_register(OEVT),
	dump_register(OEVTEN),
	dump_register(OSTS),
};

#define dump_wrap_register(nm)				\
{							\
	.name	= __stringify(nm) " [" __stringify(WRAP_ ##nm) "]", \
	.offset	= WRAP_ ##nm,				\
}

#define dump_wrap_pm_register(nm)				\
{							\
	.name	= __stringify(nm) " [" __stringify(WRAP_PM_ ##nm) "]", \
	.offset	= WRAP_PM_ ##nm,				\
}

#define WRAP_CTR_REG  0x0
#define WRAP_USB2_PHY_UTMI_REG 0x8
#define WRAP_PHY_PIPE_REG 0xC
#define WRAP_USB_HMAC_CTR0_REG 0x60
#define WRAP_USB2_PHY_REG  0x70
#define WRAP_APHY_REG 0x128
#define WRAP_PM_USB_DBUS_PWR_CTRL_REG 0x0

static const struct rtk_debugfs_reg32 wrap_regs[] = {
	dump_wrap_register(CTR_REG),
	dump_wrap_register(USB2_PHY_UTMI_REG),
	dump_wrap_register(USB_HMAC_CTR0_REG),
	dump_wrap_register(USB2_PHY_REG),
	dump_wrap_register(APHY_REG),
};

static const struct rtk_debugfs_reg32 wrap_pm_regs[] = {
	dump_wrap_pm_register(USB_DBUS_PWR_CTRL_REG),
};

static int dwc3_rtk_dwc_regdump_show(struct seq_file *s, void *unused)
{
	struct dwc3_rtk_debug *rtk = s->private;
	struct rtk_debugfs_reg32 *regs = (struct rtk_debugfs_reg32 *)(rtk->dwc3_regs_dump);
	u32 nregs = ARRAY_SIZE(dwc3_regs);
	void __iomem *base;
	int i;

	base = ioremap(rtk->regs_dwc, rtk->regs_dwc_size);

	for (i = 0; i < nregs; i++, regs++) {
		u32 val = dwc3_readl(base, regs->offset);

		seq_printf(s, "%s = 0x%08x -> 0x%08x (diff=0x%08x)\n",
			    regs->name,
			    regs->default_val, val, regs->default_val ^ val);
		if (seq_has_overflowed(s))
			break;
	}

	iounmap(base);

	return 0;
}

static int dwc3_rtk_dwc_regdump_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwc3_rtk_dwc_regdump_show, inode->i_private);
}

static const struct file_operations dwc3_rtk_dwc_regdump_fops = {
	.open			= dwc3_rtk_dwc_regdump_open,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int dwc3_rtk_wrap_regdump_show(struct seq_file *s, void *unused)
{
	struct dwc3_rtk_debug *rtk = s->private;
	struct rtk_debugfs_reg32 *regs;
	u32 nregs;
	void __iomem *base;
	int i;

	base = ioremap(rtk->regs_addr, rtk->regs_size);

	regs = (struct rtk_debugfs_reg32 *)(rtk->wrap_regs_dump);
	nregs = ARRAY_SIZE(wrap_regs);
	for (i = 0; i < nregs; i++, regs++) {
		u32 val = readl(base + regs->offset);

		seq_printf(s, "%s = 0x%08x -> 0x%08x (diff=0x%08x)\n",
			    regs->name,
			    regs->default_val, val, regs->default_val ^ val);
		if (seq_has_overflowed(s))
			break;
	}
	iounmap(base);

	base = ioremap(rtk->regs_pm_addr, rtk->regs_pm_size);
	regs = (struct rtk_debugfs_reg32 *)(rtk->wrap_pm_regs_dump);
	nregs = ARRAY_SIZE(wrap_pm_regs);
	for (i = 0; i < nregs; i++, regs++) {
		u32 val = readl(base + regs->offset);

		seq_printf(s, "%s = 0x%08x -> 0x%08x (diff=0x%08x)\n",
			    regs->name,
			    regs->default_val, val, regs->default_val ^ val);
		if (seq_has_overflowed(s))
			break;
	}
	iounmap(base);

	return 0;
}

static int dwc3_rtk_wrap_regdump_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwc3_rtk_wrap_regdump_show, inode->i_private);
}

static const struct file_operations dwc3_rtk_wrap_regdump_fops = {
	.open			= dwc3_rtk_wrap_regdump_open,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

void dwc3_rtk_regdump_init(struct dwc3_rtk_debug *rtk)
{
	struct device		*dev;
	struct device_node	*node;
	struct device_node	*dwc3_node;
	u32	global_regs_starting_offset = DWC3_GLOBALS_REGS_START;

	dev = rtk->wrap;
	node = dev->of_node;

	if (of_device_is_compatible(node, "realtek,rtd-dwc3"))
		global_regs_starting_offset = DWC3_RTK_RTD_GLOBALS_REGS_START;

	dwc3_node = of_get_compatible_child(node, "snps,dwc3");
	if (!dwc3_node) {
		dev_info(rtk->dev, "%s: Get dwc3_node at first child node\n",
			    __func__);
		dwc3_node = of_get_next_child(node, NULL);
	}
	if (dwc3_node) {
		const __be32	*addrp;
		u64		addr;
		u64		size;
		unsigned int	flags;
		struct rtk_debugfs_reg32 *regs;
		size_t regs_size = ARRAY_SIZE(dwc3_regs);
		void __iomem *base;
		int i;

		rtk->dwc3_regs_dump = kzalloc(
			    sizeof(struct rtk_debugfs_reg32) * regs_size, GFP_KERNEL);
		regs = (struct rtk_debugfs_reg32 *)(rtk->dwc3_regs_dump);
		memcpy(regs, dwc3_regs, sizeof(struct rtk_debugfs_reg32) * regs_size);

		addrp = of_get_address(dwc3_node, 0, &size, &flags);
		if (addrp == NULL) {
			dev_err(rtk->dev, "%s: Error! Can't get dwc3 reg address!\n", __func__);
			return;
		}
		addr = of_translate_address(dwc3_node, addrp);

		addr += global_regs_starting_offset;
		size -= global_regs_starting_offset;

		rtk->regs_dwc = addr;
		rtk->regs_dwc_size = size;

		dev_dbg(rtk->dev, "dwc3 register start address to 0x%x (size=%x)\n",
			(u32)addr, (u32)size);

		base = ioremap(addr, size);

		for (i = 0; i < regs_size; i++, regs++)
			regs->default_val = dwc3_readl(base, regs->offset);

		iounmap(base);
	}

	if (node) {
		const __be32	*addrp;
		u64		addr;
		u64		size;
		unsigned int	flags;
		void __iomem *base;
		struct rtk_debugfs_reg32 *regs;
		size_t regs_size;
		int i;

		addrp = of_get_address(node, 0, &size, &flags);
		if (addrp == NULL) {
			dev_err(rtk->dev, "%s: Error! Can't get wrap reg address!\n", __func__);
			return;
		}
		addr = of_translate_address(node, addrp);

		dev_dbg(rtk->dev, "wrapper register start address to 0x%x (size=%x)\n",
			(u32)addr, (u32)size);

		rtk->regs_addr = addr;
		rtk->regs_size = size;

		base = ioremap(addr, size);

		regs_size = ARRAY_SIZE(wrap_regs);
		rtk->wrap_regs_dump = kzalloc(
			    sizeof(struct rtk_debugfs_reg32) * regs_size, GFP_KERNEL);
		regs = (struct rtk_debugfs_reg32 *)(rtk->wrap_regs_dump);
		memcpy(regs, wrap_regs, sizeof(struct rtk_debugfs_reg32) * regs_size);

		for (i = 0; i < regs_size; i++, regs++)
			regs->default_val = readl(base + regs->offset);

		iounmap(base);

		addrp = of_get_address(node, 1, &size, &flags);
		if (addrp == NULL) {
			dev_err(rtk->dev, "%s: Error! Can't get wrap pm reg address!\n", __func__);
			return;
		}
		addr = of_translate_address(node, addrp);

		dev_dbg(rtk->dev, "wrapper pm register start address to 0x%x (size=%x)\n",
			(u32)addr, (u32)size);

		rtk->regs_pm_addr = addr;
		rtk->regs_pm_size = size;

		base = ioremap(addr, size);

		regs_size = ARRAY_SIZE(wrap_pm_regs);
		rtk->wrap_pm_regs_dump = kzalloc(
			    sizeof(struct rtk_debugfs_reg32) * regs_size, GFP_KERNEL);
		regs = (struct rtk_debugfs_reg32 *)(rtk->wrap_pm_regs_dump);
		memcpy(regs, wrap_pm_regs, sizeof(struct rtk_debugfs_reg32) * regs_size);

		for (i = 0; i < regs_size; i++, regs++)
			regs->default_val = readl(base + regs->offset);

		iounmap(base);
	}
}

void dwc3_rtk_debugfs_init(struct dwc3_rtk_debug *rtk)
{
	struct dentry *debug_dir;
	struct dentry *file;
	struct device *dev;

	dev = rtk->wrap;

	dev_info(rtk->dev, "%s for dev=%s\n", __func__, dev_name(dev));

	dwc3_rtk_regdump_init(rtk);

	debug_dir = debugfs_create_dir(dev_name(dev), usb_debug_root);
	rtk->debug_dir = debug_dir;

	file = debugfs_create_file("dwc3_regdump", S_IRUGO, debug_dir, rtk,
				   &dwc3_rtk_dwc_regdump_fops);
	file = debugfs_create_file("wrap_regdump", S_IRUGO, debug_dir, rtk,
				   &dwc3_rtk_wrap_regdump_fops);
}

void dwc3_rtk_debugfs_exit(struct dwc3_rtk_debug *rtk)
{
	debugfs_remove_recursive(rtk->debug_dir);

	kfree(rtk->dwc3_regs_dump);
	kfree(rtk->wrap_regs_dump);
	kfree(rtk->wrap_pm_regs_dump);
}

#else
void dwc3_rtk_debugfs_init(struct dwc3_rtk_debug *rtk)
{

}

void dwc3_rtk_debugfs_exit(struct dwc3_rtk_debug *rtk)
{

}

#endif /* CONFIG_DEBUG_FS */

static int dwc3_rtk_debug_probe(struct platform_device *pdev)
{
	struct dwc3_rtk_debug *rtk;
	struct device *dev = &pdev->dev;
	int ret = 0;

	rtk = devm_kzalloc(dev, sizeof(*rtk), GFP_KERNEL);
	if (!rtk) {
		ret = -ENOMEM;
		goto out;
	}

	platform_set_drvdata(pdev, rtk);

	rtk->dev = dev;
	rtk->wrap = dev->parent;

	dwc3_rtk_debugfs_init(rtk);

out:
	return ret;
}

static int dwc3_rtk_debug_remove(struct platform_device *pdev)
{
	struct dwc3_rtk_debug *rtk = platform_get_drvdata(pdev);

	dwc3_rtk_debugfs_exit(rtk);

	return 0;
}

static struct platform_driver dwc3_rtk_debug_driver = {
	.probe		= dwc3_rtk_debug_probe,
	.remove		= dwc3_rtk_debug_remove,
	.driver		= {
		.name	= "rtk-dwc3-debug",
	},
};

module_platform_driver(dwc3_rtk_debug_driver);

MODULE_AUTHOR("Stanley Chang <stanley_chang@realtek.com>");
MODULE_DESCRIPTION("DesignWare USB3 Realtek Debug Driver");
MODULE_ALIAS("platform:rtk-dwc3-debug");
MODULE_LICENSE("GPL");
