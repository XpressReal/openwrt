/* SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause) */
/*
 * Realtek DHC SoC family power management driver
 *
 * Copyright (c) 2020 Realtek Semiconductor Corp.
 */

#ifndef _RTK_PM_H_
#define _RTK_PM_H_

#include <linux/suspend.h>
#include <soc/realtek/rtk_ipc_shm.h>
#include <soc/realtek/uapi/rtk_pm_pcpu.h>

#define DISABLE_IRQMUX 665
#define ENABLE_IRQMUX 786

#define MEM_VERIFIED_CNT 100

struct clk;

enum rtk_pm_driver_id {
	PM = 0,
	LAN,
	IRDA,
	GPIO,
	ALARM_TIMER,
	TIMER,
	CEC,
	USB,
};

struct pm_dev_param {
	struct device *dev;
	struct list_head list;
	unsigned int dev_type;
	void *data;
};

struct pm_private {
	struct device *dev;
	struct pm_dev_param *device_param;
	struct pm_pcpu_param *pcpu_param;
	dma_addr_t pcpu_param_pa;
	struct regmap *syscon_iso;
	unsigned int pm_dbg;
	unsigned int reboot_reasons;
	unsigned int wakeup_reason;
	unsigned int suspend_context;
	struct clk *dco;
};

struct mem_check {
	unsigned char *mem_addr;
	size_t mem_byte;
};

extern void rtk_pm_init_list(void);
extern void rtk_pm_add_list(struct pm_dev_param *pm_node);
extern struct pm_dev_param *rtk_pm_get_param(unsigned int id);
extern unsigned int rtk_pm_get_param_mask(void);
extern int rtk_pm_create_sysfs(void);
extern void rtk_pm_set_pcpu_param(struct device *dev);
extern void rtk_pm_del_list(struct pm_dev_param *pm_node);
extern int rtk_pm_get_wakeup_reason(void);

/**
 * rtk_pm_wakeup_source_alarm_set - set wakeup_source alarm
 * @pm_dev: pm device
 * @enable: 1 to enable, 0 to disable
 */
static inline void rtk_pm_wakeup_source_alarm_set(struct pm_private *pm_dev, int enable)
{
	if (enable)
		pm_dev->pcpu_param->wakeup_source |= BIT(ALARM_EVENT);
	else
		pm_dev->pcpu_param->wakeup_source &= ~BIT(ALARM_EVENT);
};

/**
 * rtk_pm_wakeup_source_hifi_enabled - check if wakeup_source hifi is enabled
 * @pm_dev: pm device
 *
 * return 1 if wakeup_source hifi is enabled
 */
static inline int rtk_pm_wakeup_source_hifi_enabled(struct pm_private *pm_dev)
{
	if (pm_dev->pcpu_param->wakeup_source & BIT(HIFI_EVENT))
		return 1;
	return 0;
}

/**
 * rtk_pm_ignore_pd_pin - don't set pd pin in pm mode
 * @pm_dev: pm device
 */
static inline void rtk_pm_ignore_pd_pin(struct pm_private *pm_dev)
{
	pm_dev->pcpu_param->wakeup_source |= PCPU_FLAGS_IGNORE_PD_PIN;
}

#endif
