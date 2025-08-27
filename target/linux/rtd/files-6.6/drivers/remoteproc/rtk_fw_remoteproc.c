// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * Realtek Remote Processor driver
 *
 * Copyright (c) 2017-2023 Realtek Semiconductor Corp.
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>
#include <linux/firmware.h>
#include <linux/dma-map-ops.h>
#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <soc/realtek/rtk_tee.h>
#include <soc/realtek/memory.h>
#include <soc/realtek/rtk_ipc_shm.h>
#include <soc/realtek/rtk-rpmsg.h>
#include <soc/realtek/rtk_ipc_shm.h>

#define FIT_IMAGES "images"
#define MAX_DELIVER_SIZE (3 * 1024 * 1024)
#define AFW       0xff02
#define VFW       0xff04
#define HIFI_FW   0xff0a
#define AFW_CERT  21
#define TOT_NR_FWS 8

struct rtk_fw_rproc {
	struct rproc *rproc;
	struct device_node *node;
	unsigned int cert_type;
	const char *fw_name;
};

static uint32_t log_addr[TOT_NR_FWS] = { 0 };
static uint32_t log_size[TOT_NR_FWS] = { 0 };
static uint32_t log_level[TOT_NR_FWS] = { 0 };

static char log_dev_name[TOT_NR_FWS][6] = {
	[AUDIO_ID] = "alog",
	[VIDEO_ID] = "vlog",
	[HIFI_ID] = "hlog0",
};

static void fdt_find_log_shm(unsigned int id)
{
	struct device_node *log_dev;
	int len;
	const __be32 *prop;

	switch (id) {
	case AUDIO_ID:
		log_dev = of_find_node_by_path("/reserved-memory/alog");
		break;
	case VIDEO_ID:
		log_dev = of_find_node_by_path("/reserved-memory/vlog");
		break;
	case HIFI_ID:
		log_dev = of_find_node_by_path("/reserved-memory/hlog0");
		break;
	default:
		return;
	}

	if (log_dev) {
		prop = of_get_property(log_dev, "reg", &len);
		if (prop) {
			if (len != (2 * sizeof(__be32)))
				pr_info("Invalid %s property setting.\n",
					log_dev_name[id]);
			else {
				log_addr[id] = cpu_to_be32(*prop);
				log_size[id] = cpu_to_be32(*(++prop));
				pr_info("Found %s buffer at 0x%x, size:0x%x.\n",
					log_dev_name[id], log_addr[id],
					log_size[id]);
			}
		}
	}
	of_node_put(log_dev);
}

static void fdt_find_log_level(unsigned int id)
{
	struct device_node *log_dev;
	int len;
	const __be32 *prop;

	switch (id) {
	case AUDIO_ID:
		log_dev = of_find_node_by_path("/rtk_avcpu/alog");
		break;
	case VIDEO_ID:
		log_dev = of_find_node_by_path("/rtk_avcpu/vlog");
		break;
	case HIFI_ID:
		log_dev = of_find_node_by_path("/rtk_avcpu/hlog");
		break;
	default:
		return;
	}

	if (log_dev) {
		prop = of_get_property(log_dev, "lvl", &len);
		if (prop) {
			if (len != sizeof(__be32))
				pr_info("Invalid %s level setting.\n",
					log_dev_name[id]);
			else {
				log_level[id] = cpu_to_be32(*prop);
				pr_info("Found %s level setting:0x%x.\n",
					log_dev_name[id], log_level[id]);
			}
		}
	}
	of_node_put(log_dev);
}

static void log_shm_setup(unsigned int id)
{
	struct avcpu_syslog_struct __iomem *avlog_p;

	switch (id) {
	case AUDIO_ID:
		avlog_p =
			(struct avcpu_syslog_struct
				 *)(IPC_SHM_VIRT + offsetof(struct rtk_ipc_shm,
							    printk_buffer));
		break;
	case VIDEO_ID:
		avlog_p = (struct avcpu_syslog_struct
				   *)(IPC_SHM_VIRT +
				      offsetof(struct rtk_ipc_shm,
					       video_printk_buffer));
		break;
	case HIFI_ID:
		avlog_p = (struct avcpu_syslog_struct
				   *)(IPC_SHM_VIRT +
				      offsetof(struct rtk_ipc_shm,
					       hifi_printk_buffer));
		break;
	default:
		avlog_p = NULL;
		break;
	}
	if (avlog_p) {
		/* Check if buffer info is valid */
		if ((log_addr[id] && (!log_size[id])) ||
		    ((!log_addr[id]) && log_size[id]))
			pr_err("Invalid %s setting (addr:0x%x, size:0x%x)",
			       log_dev_name[id], log_addr[id], log_size[id]);
		else {
			avlog_p->log_buf_addr = log_addr[id];
			avlog_p->log_buf_len = log_size[id];
			avlog_p->con_start = log_level[id];
		}
	}
}

static int trust_fw_load(struct rproc *rproc, const struct firmware *fw)
{
	struct device *dev = &rproc->dev;
	struct rtk_fw_rproc *rtk_rproc = rproc->priv;
	int ret;
	unsigned int size;
	dma_addr_t dma;
	void *vaddr = NULL;
	struct arm_smccc_res res;

	dev_info(dev->parent, "Find FW Name: %s\n", rtk_rproc->fw_name);
	dev_info(dev->parent, "size 0x%x\n", (unsigned int)fw->size);

	/* Prepare for dma alloc */
	size = PAGE_ALIGN(fw->size);

	/* alloc uncached memory */
	vaddr = dma_alloc_coherent(dev->parent, size, &dma, GFP_KERNEL);
	if (!vaddr)
		return -ENOMEM;

	memcpy(vaddr, fw->data, fw->size);

	arm_smccc_smc(0x8400ff39, dma, fw->size,
		rtk_rproc->cert_type, 0, 0, 0, 0, &res);
	ret = (unsigned int)res.a0;
	if (ret) {
		dev_err(dev, "process fwtype: %d fail\n", rtk_rproc->cert_type);
		goto err;
	}

	ret = 0;
err:
	dma_free_coherent(dev->parent, size, vaddr, dma);

	return ret;
};

static int avcert_start(struct rproc *rproc)
{
	int ret;
	struct rtk_fw_rproc *rtk_rproc = rproc->priv;
	struct device *dev = &rproc->dev;

	/* Process secure fw after cert finished */
	ret = of_platform_populate(rtk_rproc->node, NULL, NULL, dev->parent);
	if (ret) {
		dev_err(dev->parent, "%s failed to add subnode device\n", __func__);
		return -ENODEV;
	}

	return 0;
};

static int avcert_stop(struct rproc *rproc)
{
	return 0;
};

#define SYS_PLL_ACPU1		0x9800010C
#define SYS_PLL_ACPU2		0x98000110
#define SYS_PLL_SSC_DIG_ACPU0	0x980005C0
#define SYS_PLL_SSC_DIG_ACPU1	0x980005C4

#define SYS_CLOCK_ENABLE3	0x98000058
#define SYS_SOFT_RESET6		0x98000014

#define ACPU_START_ADDR		0x9801A078

#define ACPU_STARTUP_FLAG	0x9801a360
#define ACPU_MAGIC1		0xacd1

static void pll_acpu1_lpf_rs_set(int v)
{
	void __iomem *map_bit;
	unsigned int val;

	map_bit = ioremap(SYS_PLL_ACPU1, 0x120);
	val = readl(map_bit);
	val = (val & ~0x0003c000) | ((v & 0xf) << 14);
	writel(val, map_bit);

	iounmap(map_bit);
}

static void pll_acpu1_pi_bps_set(int v)
{
	void __iomem *map_bit;
	unsigned int val;

	map_bit = ioremap(SYS_PLL_ACPU1, 0x120);
	val = readl(map_bit);
	val = (val & ~0x00000010) | ((v & 0x1) << 4);
	writel(val, map_bit);

	iounmap(map_bit);
}

static int acpu_start(struct rproc *rproc)
{
	struct arm_smccc_res res;
	void __iomem *map_bit;

	log_shm_setup(AUDIO_ID);
	dev_info(&rproc->dev, "afw bring up\n");

	pll_acpu1_lpf_rs_set(8);

	/* ACPU PLL setting */
	map_bit = ioremap(SYS_PLL_ACPU2, 0x120);
	writel(0x00000005, map_bit); /* OEB=1, RSTB=0, POW=1 */
	writel(0x00000007, map_bit); /* OEB=1, RSTB=1, POW=1 */

	map_bit = ioremap(SYS_PLL_SSC_DIG_ACPU0, 0x120);
	writel(0x0000000c, map_bit); /* turn off OC_EN_ACPU */

	map_bit = ioremap(SYS_PLL_SSC_DIG_ACPU1, 0x120);
	writel(0x00012ded, map_bit); /* 550MHz(Default) */

	map_bit = ioremap(SYS_PLL_SSC_DIG_ACPU0, 0x120);
	writel(0x0000000d, map_bit); /* turn on OC_EN_ACPU */

	udelay(150);
	pll_acpu1_pi_bps_set(0);

	map_bit = ioremap(SYS_PLL_ACPU2, 0x120);
	writel(0x00000003, map_bit); /* enable ACPU PLL OEB */

	arm_smccc_smc(0x8400ffff, ACPU_STARTUP_FLAG,
		ACPU_MAGIC1, 0, 0, 0, 0, 0, &res);

	/* Disable acpu clock using bit 6 and 7 */
	map_bit = ioremap(SYS_CLOCK_ENABLE3, 0x120);
	writel(0x80, map_bit);

	/* Disable reset using bit 4 and 5 */
	arm_smccc_smc(0x8400ffff, SYS_SOFT_RESET6,
		0x20, 0, 0, 0, 0, 0, &res);

	/* Enable reset using bit 4 and 5 */
	arm_smccc_smc(0x8400ffff, SYS_SOFT_RESET6,
		0x30, 0, 0, 0, 0, 0, &res);

	map_bit = ioremap(SYS_CLOCK_ENABLE3, 0x120);
	writel(0xc0, map_bit);

	iounmap(map_bit);

	return 0;
};

static int acpu_stop(struct rproc *rproc)
{
	struct arm_smccc_res res;
	void __iomem *map_bit;

	dev_info(&rproc->dev, "afw bring down\n");

	/* Disable acpu clock using bit 6 and 7 */
	map_bit = ioremap(SYS_CLOCK_ENABLE3, 0x120);
	writel(0x80, map_bit);

	/* Disable reset using bit 4 and 5 */
	arm_smccc_smc(0x8400ffff, SYS_SOFT_RESET6,
		0x20, 0, 0, 0, 0, 0, &res);

	return 0;
};

#define SYS_VE_CKSEL	0x9800004C
#define PLL_VE_FREQ_432 0x088011d0
#define PLL_VE_FREQ_540 0x08801250
#define PLL_VE_FREQ_553 0x08801260
#define PLL_VE_FREQ_621 0x088012b0
#define PLL_VE_FREQ_648 0x088012d0
#define SYS_PLL_VE2_1	0x980001D0
#define SYS_PLL_VE2_2	0x980001D4

#define VCPU_STARTUP_FLAG	0x9801b7f0
#define VCPU_MAGIC1		0xacd1

static void pll_ve2_set_analog(unsigned int val)
{
	void __iomem *map_bit;

	map_bit = ioremap(SYS_PLL_VE2_1, 0x120);
	writel(val, map_bit);
	iounmap(map_bit);
}

static void clk_ve2_sel_set(void)
{
	void __iomem *map_bit;
	unsigned int val;

	map_bit = ioremap(SYS_VE_CKSEL, 0x120);
	val = readl(map_bit);
	val = (val & ~0x38) | 0x18;
	writel(val, map_bit);
	iounmap(map_bit);
}

#define SYS_SOFT_RESET1		0x98000000
#define RESET_VE2_REGISTER	0x98000464
#define SYS_CLOCK_ENABLE1	0x98000050

static int vcpu_start(struct rproc *rproc)
{
	void __iomem *map_bit;
	struct rtk_ipc_shm __iomem *ipc = (void __iomem *)IPC_SHM_VIRT;
	int timeout = 50;

	ipc->video_rpc_flag = 0xffffffff;

	log_shm_setup(VIDEO_ID);
	dev_info(&rproc->dev, "vfw bring up\n");

	map_bit = ioremap(VCPU_STARTUP_FLAG, 0x120);
	writel(VCPU_MAGIC1, map_bit);

	map_bit = ioremap(SYS_SOFT_RESET1, 0x120);
	writel(0xC000, map_bit); /* reset ve2 bist */

	map_bit = ioremap(RESET_VE2_REGISTER, 0x120);
	writel(0x1, map_bit); /* reset ve2 bit */

	map_bit = ioremap(SYS_CLOCK_ENABLE1, 0x120);
	writel(0x00C00000, map_bit); /* clock enable for ve2 H256 */

	clk_ve2_sel_set(); /* VE_CKSEL */
	pll_ve2_set_analog(PLL_VE_FREQ_621);

	map_bit = ioremap(SYS_PLL_VE2_2, 0x120);
	writel(0x00000003, map_bit); /* pllvcpu2 reset control active and power control on */

	iounmap(map_bit);

	while (ipc->video_rpc_flag && timeout) {
		timeout--;
		mdelay(10);
	}
	if (!timeout) {
		dev_info(&rproc->dev, "vfw boot timeout\n");
		ipc->video_rpc_flag = 0;
		return -EINVAL;
	}

	return 0;
};

static int vcpu_stop(struct rproc *rproc)
{
	void __iomem *map_bit;

	dev_info(&rproc->dev, "vfw bring down\n");

	map_bit = ioremap(SYS_PLL_VE2_2, 0x120);
	writel(0x00000004, map_bit); /* pllvcpu2 power control off */

	map_bit = ioremap(SYS_CLOCK_ENABLE1, 0x120);
	writel(0x00800000, map_bit); /* clock disable for ve2 H256 */

	map_bit = ioremap(SYS_SOFT_RESET1, 0x120);
	writel(0x8000, map_bit); /* reset ve2 bist */

	map_bit = ioremap(RESET_VE2_REGISTER, 0x120);
	writel(0x0, map_bit); /* reset ve2 bit */

	iounmap(map_bit);

	return 0;
};

#define SYS_PLL_VE1_1 0x98000114
#define SYS_PLL_VE1_2 0x98000118
#define SYS_CLOCK_ENABLE4 0x9800005c
#define ISO_SRAM_CTRL 0x98007FD8
#define SYS_SOFT_RESET6_S 0x98000014
#define VE3_A_ENTRY   0x04200000
#define VE3_MEM_START 0x04201000

static int ve3_fw_load(struct rproc *rproc, const struct firmware *fw)
{
	struct device *dev = &rproc->dev;
	struct device_node *node;
	struct rtk_fw_rproc *rtk_rproc = rproc->priv;
	unsigned int size;
	struct reserved_mem *rmem;
	phys_addr_t paddr;
	void __iomem *vaddr;
	const struct firmware *ve3_entry_fw;
	unsigned int ve3_entry_size;
	int ret;

	dev_info(dev->parent, "Find FW Name: %s\n", rtk_rproc->fw_name);
	dev_info(dev->parent, "size 0x%x\n", (unsigned int)fw->size);

	ret = request_firmware(&ve3_entry_fw, "ve3_entry.img", dev);
	if (ret < 0) {
		dev_err(dev, "request_firmware failed: %d\n", ret);
		return ret;
	}

	ve3_entry_size = PAGE_ALIGN(ve3_entry_fw->size);

	size = PAGE_ALIGN(fw->size);

	node = of_parse_phandle(rtk_rproc->node, "memory-region", 0);
	rmem = of_reserved_mem_lookup(node);
	if (!rmem) {
		dev_err(dev->parent, "Failed to find reserved memory region for VE3FW\n");
		return -ENOMEM;
	}

	paddr = rmem->base;

	vaddr = ioremap(paddr, rmem->size);
	if (!vaddr) {
		dev_err(dev->parent, "Failed to ioremap reserved memory\n");
		return -ENOMEM;
	}

	memcpy(vaddr, ve3_entry_fw->data, ve3_entry_size);
	memcpy(vaddr + 0x1000, fw->data, size);

	release_firmware(ve3_entry_fw);
	return 0;
}

static void ve3_reset_ctrl_set(unsigned int rstval)
{
	void __iomem *map_bit;
	unsigned int val;

	map_bit = ioremap(ISO_SRAM_CTRL, 0x120);
	val = readl(map_bit);
	val = (val & ~0x00e00000) | ((rstval & 0x7) << 21);
	writel(val, map_bit);
	iounmap(map_bit);
}

static void rstn_ve3_set(unsigned int rstval)
{
	struct arm_smccc_res res;
	unsigned int val;

	val = ((rstval ? 1 : 0) << 10) | 0x800;
	arm_smccc_smc(0x8400ffff, SYS_SOFT_RESET6_S,
		val, 0, 0, 0, 0, 0, &res);

	mdelay(1);
}

static void clk_en_ve3_set(unsigned int clkval)
{
	void __iomem *map_bit;
	unsigned int val;

	map_bit = ioremap(SYS_CLOCK_ENABLE4, 0x120);
	val = ((clkval ? 1 : 0) << 26) | 0x08000000;
	writel(val, map_bit);
	iounmap(map_bit);
}

static void clk_ve3_sel_set(int cksel)
{
	void __iomem *map_bit;
	unsigned int val;

	map_bit = ioremap(SYS_VE_CKSEL, 0x120);
	val = readl(map_bit);
	val = (val & ~0x000001c0) | ((cksel & 0x7) << 6);
	writel(val, map_bit);
	iounmap(map_bit);
}

static void pll_ve1_set_freq(unsigned int val)
{
	void __iomem *map_bit;

	map_bit = ioremap(SYS_PLL_VE1_1, 0x120);
	writel(val, map_bit);
	iounmap(map_bit);
}

static void pll_ve1_power_on(void)
{
	void __iomem *map_bit;

	map_bit = ioremap(SYS_PLL_VE1_2, 0x120);
	writel(0x00000003, map_bit);
	iounmap(map_bit);
}


static int ve3_start(struct rproc *rproc)
{
	void __iomem *map_bit;
	unsigned int val;
	struct arm_smccc_res res;
	int ret;

	dev_info(&rproc->dev, "ve3fw bring up\n");

	clk_ve3_sel_set(2);
	pll_ve1_set_freq(PLL_VE_FREQ_540);
	pll_ve1_power_on();

	// ignore sram power on

	clk_en_ve3_set(0);
	ve3_reset_ctrl_set(3);
	rstn_ve3_set(1);
	clk_en_ve3_set(1);

	mdelay(10);
	/* re-mapping */
	map_bit = ioremap(0x98048c8c, 0x120);
	val = readl(map_bit);
	val &= ~0x00100000;
	writel(val, map_bit);

	val = readl(map_bit);
	val = (val & ~0x000fffff) | (VE3_A_ENTRY >> 12);
	writel(val, map_bit);

	/* vde MMU setting */
	arm_smccc_smc(0x8400ffff, 0x9804A230,
		0x22110040, 0, 0, 0, 0, 0, &res);

	clk_en_ve3_set(0);
	ve3_reset_ctrl_set(7);
	clk_en_ve3_set(1);

	iounmap(map_bit);

	ret = of_platform_populate(rproc->dev.parent->of_node, NULL, NULL, &rproc->dev);
	if (ret) {
		dev_err(&rproc->dev, "populate child device failed\n");
	}

	return 0;
};

static int ve3_stop(struct rproc *rproc)
{
	return 0;
};


#define SYS_PLL_HIFI1		0x980001D8
#define SYS_PLL_HIFI2		0x980001DC
#define SYS_PLL_SSC_DIG_HIFI0	0x980006E0
#define SYS_PLL_SSC_DIG_HIFI1	0x980006E4

#define HIFI_PLL_486MHZ 0x00010800
#define HIFI_PLL_796MHZ 0x0001C000

void set_hifi_pll_and_ssc_control(uint32_t freq_setting)
{
	void __iomem *map_bit;

	map_bit = ioremap(SYS_PLL_HIFI2, 0x120);
	if (readl(map_bit) == 0x3)
		return;

	/* Set AUCPU PLL & SSC control:
	 * Set 0x9800_01DC 0x5, PLL OEB=1, RSTB=0, POW=1
	 * Set 0x9800_01DC 0x7, PLL OEB=1, RSTB=1, POW=1
	 * Set 0x9800_06E0 0xC, CKSSC_INV=1, SSC_DIG_RSTB=1, OC_EN=0, SSC_EN=0
	 * Set 0x9800_01D8 (Electrical specification, no need setting in simulation)
	 * Set 0x9800_06E4 0x1C800, 810Mz, SSC_NCODE_T=39, SSC_FCODE=0
	 * Set 0x9800_06E0 0xD, CKSSC_INV=1, SSC_DIG_RSTB=1, OC_EN=1, SSC_EN=0
	 * Set 0x9800_01DC 0x3, PLL OEB=0, RSTB=1, POW=1
	 * Need wait 200us for PLL oscillating
	 */
	writel(0x00000005, map_bit);
	writel(0x00000007, map_bit);

	map_bit = ioremap(SYS_PLL_SSC_DIG_HIFI0, 0x120);
	writel(0x0000000c, map_bit);
	/* Set RS value of PLL to increase performance, please refer to Note 2 of
	 * https://wiki.realtek.com/pages/viewpage.action?pageId=136516076
	 */
	map_bit = ioremap(SYS_PLL_HIFI1, 0x120);
	writel(0x02060000, map_bit);

	/* Set frequency to 796.5Mhz */
	map_bit = ioremap(SYS_PLL_SSC_DIG_HIFI1, 0x120);
	writel(freq_setting, map_bit);

	map_bit = ioremap(SYS_PLL_SSC_DIG_HIFI0, 0x120);
	writel(0x0000000d, map_bit);
	udelay(200);

	map_bit = ioremap(SYS_PLL_HIFI2, 0x120);
	writel(0x00000003, map_bit);

	iounmap(map_bit);
}

#define ISO_POWER_CTRL		0x98007FD0
#define ISO_HIFI0_SRAM_PWR4	0x98007248
#define ISO_HIFI0_SRAM_PWR5	0x9800724C

void hifi_poweroff(void)
{
	void __iomem *map_bit;
	int timeout = 500;

	/* Set iso_hifi0 bits of ISO_power_ctrl(0x9812_9300[13]) register to 1 */
	map_bit = ioremap(ISO_POWER_CTRL, 0x120);
	writel(readl(map_bit) | 0x00002000, map_bit);

	/* Set bit 0 and bit1 of ISO_hifi0_sram_pwr4(0x9800_7248[1:0]) register to 1;
	 * To turn off aucpu power
	 */
	map_bit = ioremap(ISO_HIFI0_SRAM_PWR4, 0x120);
	writel(readl(map_bit) | 0x00000003, map_bit);

	map_bit = ioremap(ISO_HIFI0_SRAM_PWR5, 0x120);
	while ((readl(map_bit) & 0x4) != 0x4) {
		udelay(1000);
		if (timeout-- < 0) {
			pr_err("[HIFI] ERROR: failed to power on HiFi: polling sram_pwr5 timeout!\n");
			break;
		}
	}

	iounmap(map_bit);
}

void hifi_poweron(void)
{
	void __iomem *map_bit;
	int timeout = 500;

	/* Set bit 0 and bit1 of ISO_hifi0_sram_pwr4(0x9800_7248[1:0]) register to 0;
	 * To turn on aucpu power
	 */
	map_bit = ioremap(ISO_HIFI0_SRAM_PWR4, 0x120);
	writel(readl(map_bit) & 0xfffffffc, map_bit);

	/* Set iso_hifi0 bits of ISO_power_ctrl(0x9800_7FD0[13]) register to 0. */
	map_bit = ioremap(ISO_POWER_CTRL, 0x120);
	writel(readl(map_bit) & 0xffffdfff, map_bit);

	map_bit = ioremap(ISO_HIFI0_SRAM_PWR5, 0x120);
	while ((readl(map_bit) & 0x4) != 0x4) {
		udelay(1000);
		if (timeout-- < 0) {
			pr_err("[HIFI] ERROR: failed to power on HiFi: polling sram_pwr5 timeout!\n");
			break;
		}
	}

	iounmap(map_bit);
}

static int hifi_prepare(struct rproc *rproc)
{
	dev_info(&rproc->dev, "hifi prepare\n");

	hifi_poweroff();

	return 0;
};

static int hifi_start(struct rproc *rproc)
{
	struct arm_smccc_res res;
	int ret;

	log_shm_setup(HIFI_ID);
	dev_info(&rproc->dev, "hifi bring up\n");

	hifi_poweron();
	set_hifi_pll_and_ssc_control(HIFI_PLL_796MHZ);

	arm_smccc_smc(0x8400ff36, 0, 0, 0, 0, 0, 0, 0, &res);
	ret = (unsigned int)res.a0;
	dev_info(&rproc->dev, "[%s] smc ret: 0x%x\n", __func__, ret);

	return ret;
};

static int hifi_stop(struct rproc *rproc)
{
	return 0;
};

static const struct rproc_ops avcert_ops = {
	.start = avcert_start,
	.load = trust_fw_load,
	.stop = avcert_stop,
};

static const struct rproc_ops acpu_ops = {
	.start = acpu_start,
	.load = trust_fw_load,
	.stop = acpu_stop,
};

static const struct rproc_ops vcpu_ops = {
	.start = vcpu_start,
	.load = trust_fw_load,
	.stop = vcpu_stop,
};

static const struct rproc_ops hifi_ops = {
	.prepare = hifi_prepare,
	.start = hifi_start,
	.load = trust_fw_load,
	.stop = hifi_stop,
};

static const struct rproc_ops ve3_ops = {
	.start = ve3_start,
	.load = ve3_fw_load,
	.stop = ve3_stop,
};

static int rtk_register_rproc(struct device *dev, struct device_node *node)
{
	struct rtk_fw_rproc *rtk_rproc;
	struct rproc *rproc;
	int ret = 0;
	const char *fw_name, *fw_type;
	const struct rproc_ops *ops;

	ret = of_property_read_string(node, "type-firmware",
				      &fw_type);
	if (ret) {
		dev_err(dev, "No firmware type given\n");
		return -ENODEV;
	}
	dev_info(dev, "Register rproc type %s\n", fw_type);

	ret = of_property_read_string(node, "name-firmware",
				      &fw_name);
	if (ret) {
		dev_err(dev, "No firmware filename given\n");
		return -ENODEV;
	}

	if (!strcmp(fw_type, "acpu")) {
		ops = &acpu_ops;
	} else if (!strcmp(fw_type, "vcpu")) {
		ops = &vcpu_ops;
	} else if (!strcmp(fw_type, "hifi")) {
		ops = &hifi_ops;
	} else if (!strcmp(fw_type, "avcert")) {
		ops = &avcert_ops;
	} else if (!strcmp(fw_type, "ve3")) {
		ops = &ve3_ops;
	} else {
		dev_err(dev, "No matching firmware type\n");
		return -ENODEV;
	}

	/* rproc_alloc will allocate dev for each rproc, only input parent dev */
	rproc = rproc_alloc(dev, node->name, ops, fw_name, sizeof(*rtk_rproc));
	if (!rproc) {
		ret = -ENOMEM;
		goto err;
	}

	rproc->auto_boot = false;
	rtk_rproc = rproc->priv;
	rtk_rproc->rproc = rproc;
	rtk_rproc->fw_name = fw_name;
	rtk_rproc->node = node;

	if (!strcmp(fw_type, "acpu"))
		rtk_rproc->cert_type = AFW;
	else if (!strcmp(fw_type, "vcpu"))
		rtk_rproc->cert_type = VFW;
	else if (!strcmp(fw_type, "hifi"))
		rtk_rproc->cert_type = HIFI_FW;
	else if (!strcmp(fw_type, "avcert"))
		rtk_rproc->cert_type = AFW_CERT;

	dev_set_drvdata(dev, rproc);

	ret = rproc_add(rproc);
	if (ret) {
		dev_err(&rproc->dev, "rproc_add failed\n");
		goto err_put_rproc;
	}

	ret = rproc_boot(rproc);
	if (ret) {
		if (ret == -ENOENT)
			ret = -EPROBE_DEFER;
		dev_err(&rproc->dev, "rproc_boot failed\n");
		goto err_put_rproc;
	}

	return 0;

err_put_rproc:
	rproc_del(rproc);
	rproc_free(rproc);
err:
	return ret;
}

static int rtk_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node;
	int ret;

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "pm_runtime_get_sync() failed\n");
		goto err;
	}

	node = pdev->dev.of_node;
	if (WARN_ON(!node)) {
		dev_err(dev, "%s can not found device node\n", __func__);
		ret = -ENODEV;
		goto err;
	}

	ret = rtk_register_rproc(&pdev->dev, node);
	if (ret) {
		dev_err(&pdev->dev, "register rproc failed\n");
		goto err;
	}

	return 0;
err:
	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);

	return ret;
}

static int rtk_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);

	rproc_del(rproc);
	rproc_free(rproc);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id rtk_rproc_of_match[] = {
	{ .compatible = "rtk-avcert-rproc" },
	{ .compatible = "acpu-rproc" },
	{ .compatible = "vcpu-rproc" },
	{ .compatible = "hifi-rproc" },
	{ .compatible = "ve3-rproc" },
	{},
};
MODULE_DEVICE_TABLE(of, rtk_rproc_of_match);

#ifdef CONFIG_PM
static int rtk_fw_rpm_suspend(struct device *dev)
{
	struct rproc *rproc = dev_get_drvdata(dev);
	struct rtk_fw_rproc *rtk_rproc = rproc->priv;

	if (rtk_rproc->cert_type == AFW || rtk_rproc->cert_type == VFW)
		if (rproc->ops && rproc->ops->stop)
			rproc->ops->stop(rproc);

	return 0;
}

static int rtk_fw_rpm_resume(struct device *dev)
{
	struct rproc *rproc = dev_get_drvdata(dev);
	struct rtk_fw_rproc *rtk_rproc = rproc->priv;

	if (rtk_rproc->cert_type == AFW || rtk_rproc->cert_type == VFW)
		if (rproc->ops && rproc->ops->start)
			rproc->ops->start(rproc);

	return 0;
}
#endif

#ifdef CONFIG_PM
static const struct dev_pm_ops rtk_fw_rproc_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(rtk_fw_rpm_suspend, rtk_fw_rpm_resume)
};
#else
static const struct dev_pm_ops rtk_fw_rproc_pm_ops = {};
#endif

static struct platform_driver rtk_rproc_driver = {
	.probe = rtk_rproc_probe,
	.remove = rtk_rproc_remove,
	.driver = {
		.name = "rtk-rproc",
		.of_match_table = rtk_rproc_of_match,
		.pm = &rtk_fw_rproc_pm_ops,
	},
};

static int __init rtk_rproc_init(void)
{
	int i;

	for (i = 0; i < TOT_NR_FWS; i++) {
		fdt_find_log_shm(i);
		fdt_find_log_level(i);
	}

	return platform_driver_register(&rtk_rproc_driver);
}
late_initcall(rtk_rproc_init);

static void __exit rtk_rproc_exit(void)
{
	platform_driver_unregister(&rtk_rproc_driver);
}
module_exit(rtk_rproc_exit);

MODULE_AUTHOR("YH_HSIEH <yh_hsieh@realtek.com>");
MODULE_DESCRIPTION("Realtek Rproc Driver");
MODULE_LICENSE("GPL v2");
