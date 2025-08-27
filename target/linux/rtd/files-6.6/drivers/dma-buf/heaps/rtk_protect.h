// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 <cy.huang@realtek.com>
 */

#ifndef __RTK_PROTECT_H
#define __RTK_PROTECT_H

#include <linux/types.h>
#include <linux/list.h>

enum e_notifier_protect_type {
	NOTIFIER_PROTECT_TYPE_NONE = 0,
	NOTIFIER_PROTECT_TYPE_1,
	NOTIFIER_PROTECT_TYPE_2,
	NOTIFIER_PROTECT_TYPE_3,
	NOTIFIER_PROTECT_TYPE_4,
	NOTIFIER_PROTECT_TYPE_5,
	NOTIFIER_PROTECT_TYPE_6,
	NOTIFIER_PROTECT_TYPE_7,
	NOTIFIER_PROTECT_TYPE_8,
	NOTIFIER_PROTECT_TYPE_9,
	NOTIFIER_PROTECT_TYPE_10,
	NOTIFIER_PROTECT_TYPE_11,
	NOTIFIER_PROTECT_TYPE_12,
	NOTIFIER_PROTECT_TYPE_13,
	NOTIFIER_PROTECT_TYPE_14,
	NOTIFIER_PROTECT_TYPE_15,
	NOTIFIER_PROTECT_TYPE_MAX,
};

enum e_notifier_protect_ext {
	NOTIFIER_PROTECTED_EXT_NONE = 0,
	NOTIFIER_PROTECTED_EXT_1,
	NOTIFIER_PROTECTED_EXT_2,
	NOTIFIER_PROTECTED_EXT_3,
	NOTIFIER_PROTECTED_EXT_4,
	NOTIFIER_PROTECTED_EXT_5,
	NOTIFIER_PROTECTED_EXT_6,
	NOTIFIER_PROTECTED_EXT_7,
	NOTIFIER_PROTECTED_EXT_MAX,
};

struct protect_region {
	enum e_notifier_protect_type type;
	unsigned long base;
	size_t size;
};

struct protect_ext_region {
	enum e_notifier_protect_ext ext;
	unsigned long base;
	size_t size;
	void *priv;
};


struct rtk_protect_create_info {
	struct protect_region mem;
	void *priv_virt;
};

struct rtk_protect_change_info {
	struct protect_region mem;
	void *priv_virt;
};

struct rtk_protect_destroy_info {
	void *priv_virt;
};

struct rtk_protect_info {
	struct list_head list;
	struct rtk_protect_create_info create_info;
};

struct rtk_protect_ext_set {
	struct protect_ext_region mem;
	void *priv_virt;
};

struct rtk_protect_ext_unset {
	void *priv_virt;
};

struct rtk_protect_ext_info {
	struct list_head list;
	struct rtk_protect_ext_set create_info;
};


int rtk_protect_create(struct rtk_protect_create_info *info);

int rtk_protect_change(struct rtk_protect_change_info *info);

int rtk_protect_destroy(struct rtk_protect_destroy_info *info);

int rtk_protect_ext_set(struct rtk_protect_ext_set * config);

int rtk_protect_ext_unset(struct rtk_protect_ext_unset * config);

bool rtk_protect_handler_ready(void);

#endif
