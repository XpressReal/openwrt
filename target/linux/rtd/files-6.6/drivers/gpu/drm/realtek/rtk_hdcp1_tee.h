/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Realtek Semiconductor Corp.
 */
#ifndef _HDCP1_TEE_H_INCLUDED_
#define _HDCP1_TEE_H_INCLUDED_

#include <drm/display/drm_hdcp.h>
#include <linux/tee_drv.h>
#include <uapi/linux/tee.h>

#define HDCP14_KEY_SIZE		288

struct rtk_hdcp1_tee;

struct rtk_hdcp1_tee_ops {
	int (*hdcp1_tee_api_init)(struct rtk_hdcp1_tee *hdcp1_tee);
	void (*hdcp1_tee_api_deinit)(struct rtk_hdcp1_tee *hdcp1_tee);
	int (*generate_an)(struct rtk_hdcp1_tee *hdcp1_tee, u8 *an);
	int (*read_aksv)(struct rtk_hdcp1_tee *hdcp1_tee, u8 *aksv);
	int (*set_hdcp1_repeater_bit)(struct rtk_hdcp1_tee *hdcp1_tee,
				       u8 is_repeater);
	int (*write_bksv)(struct rtk_hdcp1_tee *hdcp1_tee, u8 *bksv);
	int (*check_ri_prime)(struct rtk_hdcp1_tee *hdcp1_tee, u8 *ri);
	int (*hdcp1_set_encryption)(struct rtk_hdcp1_tee *hdcp1_tee,
				    u8 enc_state);
	int (*sha_append_bstatus_m0)(struct rtk_hdcp1_tee *hdcp1_tee,
				      u8 *ksv_fifo,
				      int *byte_cnt,
				      u8 *bstatus);
	int (*compute_V)(struct rtk_hdcp1_tee *hdcp1_tee,
			 u8 *ksv_fifo,
			 int *byte_cnt);
	int (*verify_V)(struct rtk_hdcp1_tee *hdcp1_tee, u8 *vprime);
	int (*set_wider_window)(struct rtk_hdcp1_tee *hdcp1_tee);
	int (*write_hdcp1_key)(struct rtk_hdcp1_tee *hdcp1_tee,
			       unsigned char *key);
	int (*fix480p)(struct rtk_hdcp1_tee *hdcp1_tee);
	int (*set_keepout_win)(struct rtk_hdcp1_tee *hdcp1_tee);
	int (*set_rekey_win)(struct rtk_hdcp1_tee *hdcp1_tee,
			       unsigned char rekey_win);
};

struct rtk_hdcp1_tee {
	const struct rtk_hdcp1_tee_ops *hdcp1_tee_ops;

	int init_hdcp1_ta_flag;
	unsigned int init_ta_count;
	struct tee_ioctl_open_session_arg hdcp1_arg;
	struct tee_context *hdcp1_ctx;
};

#if IS_ENABLED(CONFIG_REALTEK_TEE)
int rtk_hdcp1_tee_init(struct rtk_hdcp1_tee *hdcp1_tee);
#else
static inline int rtk_hdcp1_tee_init(struct rtk_hdcp1_tee *hdcp1_tee)
{
	return -ESRCH;
}
#endif

#endif
