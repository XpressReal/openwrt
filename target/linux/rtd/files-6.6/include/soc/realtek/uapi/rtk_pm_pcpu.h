/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright (c) 2023 Realtek Semiconductor Corp.
 */

#ifndef __UAPI_RTK_PM_PCPU_H
#define __UAPI_RTK_PM_PCPU_H

#include "rtk_ir.h"  // struct ipc_shm_irda
#include "rtk_cec.h" // struct ipc_shm_cec

#define GPIO_MAX_SIZE 86

struct pm_pcpu_param {
	unsigned int wakeup_source;
	unsigned int timerout_val;
	char wu_gpio_en[GPIO_MAX_SIZE];
	char wu_gpio_act[GPIO_MAX_SIZE];
	struct ipc_shm_irda irda_info;
	struct ipc_shm_cec cec_info;
	unsigned int bt;
} __packed;


enum rtk_wakeup_event {
	LAN_EVENT = 0,
	IR_EVENT,
	GPIO_EVENT,
	ALARM_EVENT,
	TIMER_EVENT,
	CEC_EVENT,
	USB_EVENT,
	HIFI_EVENT,
	VTC_EVENT,
	PON_EVENT,
	MAX_EVENT,
};

#define PCPU_WAKEUP_EVENT_MASK   0xfff
#define PCPU_FLAGS_DCO_ENABLED   0x1000 /* notify dco is enabled */
#define PCPU_FLAGS_IGNORE_PD_PIN 0x2000 /* pcpu should ignore setting pd pin */

#define DCO_ENABLE        PCPU_FLAGS_DCO_ENABLED


#endif /* __UAPI_RTK_PM_PCPU_H */
