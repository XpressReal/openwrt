/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright (c) 2017 Realtek Semiconductor Corp.
 */

#ifndef __UAPI_RTK_IR_H
#define __UAPI_RTK_IR_H

#define MAX_WAKEUP_CODE	16
#define MAX_KEY_TBL	2

struct irda_wake_up_key {
	unsigned int protocol;
	unsigned int scancode_mask;
	unsigned int wakeup_keynum;
	unsigned int wakeup_scancode[MAX_WAKEUP_CODE];
	unsigned int cus_mask;
	unsigned int cus_code;
};

struct ipc_shm_irda {
	unsigned int ipc_shm_ir_magic;
	unsigned int dev_count;
	struct irda_wake_up_key key_tbl[MAX_KEY_TBL];
};

#endif /* __UAPI_RTK_IR_H */
