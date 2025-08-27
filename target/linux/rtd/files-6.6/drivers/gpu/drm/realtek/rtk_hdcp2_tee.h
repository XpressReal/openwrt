/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Realtek Semiconductor Corp.
 */

#ifndef _HDCP2_TEE_H_INCLUDED_
#define _HDCP2_TEE_H_INCLUDED_

#include <drm/display/drm_hdcp.h>
#include <linux/tee_drv.h>
#include <uapi/linux/tee.h>

struct rtk_hdcp2_tee;

struct rtk_hdcp2_tee_ops {
	int (*hdcp2_tee_api_init)(struct rtk_hdcp2_tee *hdcp2_tee);
	void (*hdcp2_tee_api_deinit)(struct rtk_hdcp2_tee *hdcp2_tee);
	int (*generate_random_rtx)(struct rtk_hdcp2_tee *hdcp2_tee,
				    struct hdcp2_ake_init *ake_data);
	int (*verify_rx_cert)(struct rtk_hdcp2_tee *hdcp2_tee,
			      struct hdcp2_ake_send_cert *rx_cert);
	int (*prepare_stored_km)(struct rtk_hdcp2_tee *hdcp2_tee,
				  struct hdcp2_ake_no_stored_km *ek_pub_km);
	int (*prepare_no_stored_km)(struct rtk_hdcp2_tee *hdcp2_tee,
				     struct hdcp2_ake_no_stored_km *ek_pub_km);
	int (*verify_hprime)(struct rtk_hdcp2_tee *hdcp2_tee,
			     struct hdcp2_ake_send_hprime *rx_hprime,
			     u8 *verified_src, u8 *h);
	int (*check_stored_km)(struct rtk_hdcp2_tee *hdcp2_tee,
				u8 *receiver_id,
				struct hdcp2_ake_no_stored_km *e_kh_km_m,
				bool *km_stored);
	int (*store_pairing_info)(struct rtk_hdcp2_tee *hdcp2_tee,
				  struct hdcp2_ake_send_pairing_info *pairing_info);
	int (*initiate_locality_check)(struct rtk_hdcp2_tee *hdcp2_tee,
					struct hdcp2_lc_init *lc_init);
	int (*verify_lprime)(struct rtk_hdcp2_tee *hdcp2_tee,
			     struct hdcp2_lc_send_lprime *rx_lprime,
			     u8 *lprime);
	int (*get_session_key)(struct rtk_hdcp2_tee *hdcp2_tee,
				struct hdcp2_ske_send_eks *ske_data);
	int (*repeater_check_flow_prepare_ack)(struct rtk_hdcp2_tee *hdcp2_tee,
					       u8 *buf,
					       int msg_size,
					       u8 *mV);
	int (*verify_mprime)(struct rtk_hdcp2_tee *hdcp2_tee,
			     struct hdcp2_rep_stream_ready *stream_ready,
			     u8 *input, int input_size);
	/* Enables HDCP signalling on the port */
	int (*enable_hdcp2_cipher)(struct rtk_hdcp2_tee *hdcp2_tee,
				    u8 *mCap);
	void (*disable_hdcp2_cipher)(struct rtk_hdcp2_tee *hdcp2_tee);
	void (*update_mCap)(struct rtk_hdcp2_tee *hdcp2_tee,
			    u8 *mCap);
	int (*read_hdcp2_key)(struct rtk_hdcp2_tee *hdcp2_tee);
	int (*write_hdcp2_key)(struct rtk_hdcp2_tee *hdcp2_tee,
			       char *key, unsigned int keyLength);
	void (*clear_hdcp2_cipher_setting)(struct rtk_hdcp2_tee *hdcp2_tee);
};

struct rtk_hdcp2_tee {
	const struct rtk_hdcp2_tee_ops *hdcp2_tee_ops;

	int init_hdcp2_ta_flag;
	struct tee_ioctl_open_session_arg hdcp2_arg;
	struct tee_context *hdcp2_ctx;
};

#if IS_ENABLED(CONFIG_REALTEK_TEE)
int rtk_hdcp2_tee_init(struct rtk_hdcp2_tee *hdcp2_tee);
#else
static inline int rtk_hdcp2_tee_init(struct rtk_hdcp2_tee *hdcp2_tee)
{
	return -ESRCH;
}
#endif

#endif
