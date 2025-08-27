// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek DHC SoC family power management driver
 * Copyright (c) 2020-2021 Realtek Semiconductor Corp.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/arm-smccc.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/psci.h>

#include <soc/realtek/rtk_pm.h>

static LIST_HEAD(rtk_pm_param_list);

void rtk_pm_add_list(struct pm_dev_param *pm_node)
{
	list_add(&pm_node->list, &rtk_pm_param_list);
}
EXPORT_SYMBOL(rtk_pm_add_list);

void rtk_pm_del_list(struct pm_dev_param *pm_node)
{
	list_del(&pm_node->list);
}
EXPORT_SYMBOL(rtk_pm_del_list);

struct pm_dev_param *rtk_pm_get_param(unsigned int id)
{
	struct pm_dev_param *node;

	list_for_each_entry(node, &rtk_pm_param_list, list) {
		if (node->dev_type == id)
			return node;
	}

	return NULL;
}
EXPORT_SYMBOL(rtk_pm_get_param);

unsigned int rtk_pm_get_param_mask(void)
{
	/* CONFIG_R8169SOC, CONFIG_IR_RTK, CONFIG_RTK_HDMI_CEC */
	unsigned long param_mask = ~(BIT(LAN_EVENT) | BIT(IR_EVENT) | BIT(CEC_EVENT));
	struct pm_dev_param *node;

	list_for_each_entry(node, &rtk_pm_param_list, list) {
		if (node->dev_type)
			param_mask |= BIT(node->dev_type - 1);
	}

	return (unsigned int)param_mask;
}
EXPORT_SYMBOL(rtk_pm_get_param_mask);

void rtk_pm_set_pcpu_param(struct device *dev)
{
	struct pm_private *dev_pm = dev_get_drvdata(dev);
	struct pm_pcpu_param *pcpu_param = dev_pm->pcpu_param;
	struct arm_smccc_res res;
	struct pm_dev_param *node;
	struct ipc_shm_irda *irda = &pcpu_param->irda_info;
	struct ipc_shm_cec *cec = &pcpu_param->cec_info;

	pcpu_param->wakeup_source &= rtk_pm_get_param_mask();
	pcpu_param->wakeup_source = htonl(pcpu_param->wakeup_source);
	pcpu_param->timerout_val = htonl(pcpu_param->timerout_val);

	list_for_each_entry(node, &rtk_pm_param_list, list) {
		switch (node->dev_type) {
		case LAN:
			break;
		case IRDA:
			memcpy(irda, node->data, sizeof(*irda));
			break;
		case GPIO:
			break;
		case ALARM_TIMER:
			break;
		case TIMER:
			break;
		case CEC:
			memcpy(cec, node->data, sizeof(*cec));
			break;
		case USB:
			break;
		default:
			break;
		}
	}

	arm_smccc_smc(0x8400ff04, dev_pm->pcpu_param_pa, 0, 0, 0, 0, 0, 0, &res);
}
EXPORT_SYMBOL(rtk_pm_set_pcpu_param);

int rtk_pm_get_wakeup_reason(void)
{
	struct pm_dev_param *pm_param = rtk_pm_get_param(PM);
	struct pm_private *dev_pm = dev_get_drvdata(pm_param->dev);

	return dev_pm->wakeup_reason;
}
EXPORT_SYMBOL(rtk_pm_get_wakeup_reason);

MODULE_LICENSE("GPL v2");
