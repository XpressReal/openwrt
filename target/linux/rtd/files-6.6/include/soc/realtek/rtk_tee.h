/* SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause) */
/*
 * Realtek DHC SoC family TEE driver
 *
 * Copyright (c) 2020 Realtek Semiconductor Corp.
 */

#ifndef _RTK_TEE_H_
#define _RTK_TEE_H_

#include <linux/tee_drv.h>

struct tee_mem_device;
struct tee_mem_protected_slot;
struct tee_mem_protected_ext_slot;

extern struct tee_mem_device *tee_mem_dev_get_simple(void);

extern struct tee_mem_protected_slot *tee_mem_protected_create(struct tee_mem_device *memdev,
	phys_addr_t base, size_t size, unsigned int type);
extern int tee_mem_protected_destroy(struct tee_mem_device *memdev, struct tee_mem_protected_slot *slot);
extern int tee_mem_protected_change(struct tee_mem_device *memdev, struct tee_mem_protected_slot *slot,
	phys_addr_t base, size_t size, unsigned int type);

extern struct tee_mem_protected_ext_slot *tee_mem_protected_ext_create(struct tee_mem_device *memdev,
	phys_addr_t base, size_t size, unsigned int ext, struct tee_mem_protected_slot *parent);
extern int tee_mem_protected_ext_destroy(struct tee_mem_device *memdev,
	struct tee_mem_protected_ext_slot *slot);

#endif
