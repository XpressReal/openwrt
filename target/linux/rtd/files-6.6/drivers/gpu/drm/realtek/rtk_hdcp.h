/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Realtek Semiconductor Corp.
 */
#ifndef _HDCP_H_INCLUDED_
#define _HDCP_H_INCLUDED_

#include "rtk_hdcp1_tee.h"
#include "rtk_hdcp2_tee.h"

#define to_rtk_hdcp(x) container_of(x, struct rtk_hdcp, x)

/*No HDCP supported, no secure data path*/
#define WV_HDCP_NONE			0
/*HDCP version 1.0*/
#define WV_HDCP_V1			1
/*HDCP version 2.0 Type 1*/
#define WV_HDCP_V2			2
/*HDCP version 2.1 Type 1.*/
#define WV_HDCP_V2_1			3
/*HDCP version 2.2 Type 1.*/
#define WV_HDCP_V2_2			4
/*HDCP version 2.3 Type 1.*/
#define WV_HDCP_V2_3			5
/*No digital output*/
#define WV_HDCP_NO_DIGITAL_OUTPUT	0xff

#define HDCP_ENC_OFF 0
#define HDCP_ENC_ON 1

#define MAX_SHA_DATA_SIZE       645
#define MAX_SHA_VPRIME_SIZE     20

#define HDCP2_AKE_INIT_DELAY_MS 10
#define HDCP2_MSG_DELAY_MS      30

#define HDCP2_STREAM_MANAGE_TIMEOUT_MS  120

#define HDCP_CED_CHECK_MIN_MS  100
#define HDCP_CED_CHECK_MAX_MS  1500
#define	HDCP_CED_SLEEP_MS	   30
#define HDCP_CED_PASS_COND     3

/* hdcp error code */
#define HDCP_NO_ERR             0
#define HDCP_PLUGOUT_ERR        1
#define HDCP_WAIT_TIMEOUT_ERR   2
#define HDCP_DDC_RD_TRANS_ERR   3
#define HDCP_DDC_WR_NOMEM_ERR   4
#define HDCP_DDC_WR_TRANS_ERR   5

#define HDCP_CANCELED_ERR      10

#define HDCP_CED_NOTSUPPORT    16
#define HDCP_CED_CH0_ERR       0x11 /* 17 */
#define HDCP_CED_CH1_ERR       0x12 /* 18 */
#define HDCP_CED_CH01_ERR      0x13 /* 19 */
#define HDCP_CED_CH2_ERR       0x14 /* 20 */
#define HDCP_CED_CH02_ERR      0x15 /* 21 */
#define HDCP_CED_CH12_ERR      0x16 /* 22 */
#define HDCP_CED_CH012_ERR     0x17 /* 23 */
#define HDCP_CED_INVALID_ERR   24

#define HDCP1_NOTSUPPORT_ERR        100
#define HDCP1_INVALID_AKSV_ERR      101
#define HDCP1_INVALID_BKSV_ERR      102
#define HDCP1_REVOKED_BKSV_ERR      103
#define HDCP1_REVOKED_KSVLIST_ERR   104
#define HDCP1_KSVLIST_TIMEOUT_ERR   105
#define HDCP1_MAX_CASCADE_ERR       106
#define HDCP1_MAX_DEVICE_ERR        107

#define HDCP1_DDC_BCAPS_ERR     130
#define HDCP1_DDC_BSTATUS_ERR   131
#define HDCP1_DDC_AN_ERR        132
#define HDCP1_DDC_AKSV_ERR      133
#define HDCP1_DDC_BKSV_ERR      134
#define HDCP1_DDC_RI_PRIME_ERR  135
#define HDCP1_DDC_KSV_FIFO_ERR  136
#define HDCP1_DDC_V_PRIME_ERR   137
#define HDCP1_TEE_INIT_OPENCONTEXT_ERR 150
#define HDCP1_TEE_INIT_OPENSESSION_ERR 151
#define HDCP1_TEE_NOINIT_ERR           152
#define HDCP1_TEE_NOMEM_ERR            153
#define HDCP1_TEE_GEN_AN_ERR           154
#define HDCP1_TEE_READAKSV_ERR         155
#define HDCP1_TEE_REPEATER_BIT_ERR     156
#define HDCP1_TEE_WEITE_BKSV_ERR       157
#define HDCP1_TEE_CHECK_RI_ERR         158
#define HDCP1_TEE_INCORRECT_RI_ERR     159
#define HDCP1_TEE_SET_ENC_ERR          160
#define HDCP1_TEE_SET_WINDER_ERR       161
#define HDCP1_TEE_SHA_APPEND_ERR       162
#define HDCP1_TEE_COMPUTE_V_ERR        163
#define HDCP1_TEE_WAIT_V_READY_ERR     164
#define HDCP1_TEE_VERIFY_V_ERR         165
#define HDCP1_TEE_V_MATCH_ERR          166
#define HDCP1_TEE_SET_KEY_ERR          167
#define HDCP1_TEE_FIX_480P_ERR         168
#define HDCP1_TEE_SET_KEEPOUTWIN_ERR   169
#define HDCP1_TEE_SET_REKEYWIN_ERR     170

/* hdcp2 error code */
#define HDCP2_NOTSUPPORT_ERR                  200
#define HDCP2_NOMEM_ERR                       201
#define HDCP2_REVOKED_RECEIVER_ID_ERR         202
#define HDCP2_REVOKED_ID_LIST_ERR             203
#define HDCP2_VERIFY_RX_CERT_INVALID_ARG_ERR  204
#define HDCP2_MAX_CASCADE_ERR                 205
#define HDCP2_MAX_DEVICE_ERR                  206
#define HDCP2_SEQ_NUM_M_ROLL_OVER_ERR         207
#define HDCP2_REAUTH_REQUEST_ERR              208
#define HDCP2_TOPOLOGY_CHANGE_ERR             209
#define HDCP2_GET_TIMEOUT_VAL_ERR             210
#define HDCP2_WAIT_MSG_TIMEOUT_ERR            211
#define HDCP2_READ_MSG_SIZE_ERR               212
#define HDCP2_READ_AKE_SEND_CERT_ERR          213
#define HDCP2_READ_AKE_SEND_HPRIME_ERR        214
#define HDCP2_READ_AKE_SEND_PAIRING_INFO_ERR  215
#define HDCP2_READ_LC_SEND_LPRIME_ERR         216
#define HDCP2_READ_REP_SEND_RECVID_LIST_ERR   217
#define HDCP2_READ_REP_STREAM_READY_ERR       218

#define HDCP2_DDC_HDCP_VER_ERR         230
#define HDCP2_DDC_WR_MSG_ERR           231
#define HDCP2_DDC_RXSTATUS_ERR         232
#define HDCP2_DDC_RD_MSG_ERR           233

#define HDCP2_TEE_INIT_OPENCONTEXT_ERR      250
#define HDCP2_TEE_INIT_OPENSESSION_ERR      251
#define HDCP2_TEE_NOINIT_ERR                252
#define HDCP2_TEE_NOMEM_ERR                 253
#define HDCP2_TEE_READ_KEY_ERR              254
#define HDCP2_TEE_WRITE_KEY_ERR             255
#define HDCP2_TEE_AKE_INIT_ERR              256
#define HDCP2_TEE_RX_CERT_ID_ERR            257
#define HDCP2_TEE_INVOKE_LLC_SIGN_ERR       258
#define HDCP2_TEE_INVALID_LLC_SIGN_ERR      259
#define HDCP2_TEE_INVOKE_GET_RXINFO_ERR     260
#define HDCP2_TEE_AKE_NO_STOREDKM_ERR       261
#define HDCP2_TEE_RX_PRIME_ID_ERR           262
#define HDCP2_TEE_COMPUTE_H_ERR             263
#define HDCP2_TEE_VERIFY_H_PRIME_ERR        264
#define HDCP2_TEE_PAIRING_INFO_ID_ERR       265
#define HDCP2_TEE_SAVE_RXINFO_ERR           266
#define HDCP2_TEE_LC_INIT_ERR               267
#define HDCP2_TEE_RX_LPRIME_ID_ERR          268
#define HDCP2_TEE_COMPUTE_L_ERR             269
#define HDCP2_TEE_VERIFY_LPRIME_ERR         270
#define HDCP2_TEE_GET_SKEY_ERR              271
#define HDCP2_TEE_RECVID_LIST_ID_ERR        272
#define HDCP2_TEE_INVOKE_RECVID_LIST_ERR    273
#define HDCP2_TEE_RLIST_V_SIZE_ERR          274
#define HDCP2_TEE_RLIST_MSG_ID_ERR          275
#define HDCP2_TEE_RLIST_MAX_EXCEED_ERR      276
#define HDCP2_TEE_RLIST_MSG_SIZE_ERR        277
#define HDCP2_TEE_RLIST_INCORRECT_SEQ_V_ERR 278
#define HDCP2_TEE_RLIST_V_ROLL_OVER_ERR     279
#define HDCP2_TEE_RLIST_COMPUTE_V_ERR       280
#define HDCP2_TEE_RLIST_V_COMPARE_ERR       281
#define HDCP2_TEE_STREAM_READY_ID_ERR       282
#define HDCP2_TEE_INVOKE_COMPUTE_M_ERR      283
#define HDCP2_TEE_STREAM_M_COMPARE_ERR      284
#define HDCP2_TEE_ENABLE_CIPHER_ERR         285

/* tee error code */
#define TEE_ERROR_HDCP2_ERR_DOWNSTREAM_EXCEED  0x100
#define TEE_ERROR_HDCP2_ERR_LIST_MSG_SIZE      0x101
#define TEE_ERROR_HDCP2_ERR_INCORRECT_SEQ_V    0x102
#define TEE_ERROR_HDCP2_ERR_V_ROLL_OVER        0x103
#define TEE_ERROR_HDCP2_ERR_V_COMPARE          0x105
#define TEE_ERROR_BAD_FORMAT              0xFFFF0005
#define TEE_ERROR_COMMUNICATION           0xFFFF000E
#define TEE_ERROR_SIGNATURE_INVALID       0xFFFF3072


struct rtk_hdcp_err_table {
	int err_code;
	char *description;
};

enum rtk_hdcp_version {
	HDCP_NONE,
	HDCP_1x,
	HDCP_2x
};

enum rtk_hdcp_state {
	HDCP_HDMI_DISCONNECT,
	HDCP_HDMI_DISABLED,
	HDCP_UNAUTH,
	HDCP_1_IN_AUTH,
	HDCP_2_IN_AUTH,
	HDCP_1_SUCCESS,
	HDCP_2_SUCCESS,
	HDCP_1_FAILURE,
	HDCP_2_FAILURE
};

#define __wait_for(OP, COND1, COND2, US, Wmin, Wmax) ({ \
	const ktime_t end__ = ktime_add_ns(ktime_get_raw(), 1000ll * (US)); \
	long wait__ = (Wmin); /* recommended min for usleep is 10 us */ \
	int ret__;                                                      \
	might_sleep();                                                  \
	for (;;) {                                                      \
		const bool expired__ = ktime_after(ktime_get_raw(), end__); \
		OP;                                                     \
		/* Guarantee COND check prior to timeout */             \
		barrier();                                              \
		if (COND1) {						\
			ret__ = -HDCP_PLUGOUT_ERR;			\
			break;						\
		}							\
		if (COND2) {                                             \
			ret__ = HDCP_NO_ERR;                                      \
			break;                                          \
		}                                                       \
		if (expired__) {                                        \
			ret__ = -HDCP_WAIT_TIMEOUT_ERR;                             \
			break;                                          \
		}                                                       \
		usleep_range(wait__, wait__ * 2);                       \
		if (wait__ < (Wmax))                                    \
			wait__ <<= 1;                                   \
	}                                                               \
	ret__;                                                          \
})

struct rtk_hdmi;

struct rtk_hdcp_ops {
	 /* Outputs the transmitter's An and Aksv values to the receiver. */
	int (*write_an_aksv)(struct rtk_hdmi *hdmi,
			     u8 *an, u8 *aksv);

	/* Reads the receiver's key selection vector */
	int (*read_bksv)(struct rtk_hdmi *hdmi, u8 *bksv);

	/*
	 * Reads BINFO from DP receivers and BSTATUS from HDMI receivers. The
	 * definitions are the same in the respective specs, but the names are
	 * different. Call it BSTATUS since that's the name the HDMI spec
	 * uses and it was there first.
	 */
	int (*read_bstatus)(struct rtk_hdmi *hdmi,
				u8 *bstatus);

	/* Determines whether a repeater is present downstream */
	int (*repeater_present)(struct rtk_hdmi *hdmi,
				bool *repeater_present);

	/* Reads the receiver's Ri' value */
	int (*read_ri_prime)(struct rtk_hdmi *hdmi, u8 *ri);

	/* Determines if the receiver's KSV FIFO is ready for consumption */
	int (*read_ksv_ready)(struct rtk_hdmi *hdmi,
				bool *ksv_ready);

	/* Reads the ksv fifo for num_downstream devices */
	int (*read_ksv_fifo)(struct rtk_hdmi *hdmi,
				int num_downstream, u8 *ksv_fifo);

	/* Reads a 32-bit part of V' from the receiver */
	int (*read_v_prime_part)(struct rtk_hdmi *hdmi,
				int i, u32 *part);

	/* Ensures the link is still protected */
	int (*check_link)(struct rtk_hdmi *hdmi);

	/* Set rekey win */
	int (*set_rekey_win)(struct rtk_hdmi *hdmi, u8 rekey_win);

	/* Detects panel's hdcp capability. */
	int (*hdcp_capable)(struct rtk_hdmi *hdmi,
				bool *hdcp_capable);

	/* Detects panel's hdcp2 capability. */
	int (*hdcp2_capable)(struct rtk_hdmi *hdmi,
				bool *hdcp2_capable);

	/* Write HDCP2.2 messages */
	int (*write_2_2_msg)(struct rtk_hdmi *hdmi,
				void *buf, size_t size);

	/* Read HDCP2.2 messages */
	int (*read_2_2_msg)(struct rtk_hdmi *hdmi,
				u8 msg_id, void *buf, size_t size);

	/* HDCP2.2 Link Integrity Check */
	int (*check_2_2_link)(struct rtk_hdmi *hdmi);

	/* SCDC character error detection */
	int (*check_ced_error)(struct rtk_hdmi *hdmi);
};

struct rtk_hdcp {
	const struct rtk_hdcp_ops *hdcp_ops;
	/* Mutex for hdcp state of the connector */
	struct mutex mutex;
	u64 value;
	struct delayed_work commit_work;
	struct delayed_work check_work;
	struct work_struct prop_work;

	bool do_cancel_hdcp;

	/* HDCP1.4 Encryption status */
	bool hdcp_encrypted;

	/* HDCP2.2 related definitions */
	/* Flag indicates whether this connector supports HDCP2.2 or not. */
	bool hdcp2_supported;

	/* HDCP2.2 Encryption status */
	bool hdcp2_encrypted;

	enum rtk_hdcp_version sink_hdcp_ver;
	enum rtk_hdcp_state hdcp_state;
	int hdcp_err_code;

	bool is_paired;
	bool is_repeater;

	/*
	 * Count of RepeaterAuth_Stream_Manage msg propagated.
	 * Initialized to 0 on AKE_INIT. Incremented after every successful
	 * transmission of RepeaterAuth_Stream_Manage message. When it rolls
	 * over re-Auth has to be triggered.
	 */
	u32 seq_num_m;
	u8 hdcp1_device_downstream;
	u8 mCap[1];
	u8 r_rx[HDCP_2_2_RRX_LEN];
	u8 r_tx[HDCP_2_2_RTX_LEN];
	u8 rx_caps[HDCP_2_2_RXCAPS_LEN];


	struct hdcp2_tx_caps    tx_caps;

	/* force use hdcp14 auth regardless of sink support hdcp2 */
	u32 force_hdcp14;
	struct drm_property *force_hdcp14_property;

	struct rtk_hdcp1_tee hdcp1_tee;
	struct rtk_hdcp2_tee hdcp2_tee;
};

int rtk_hdcp_init(struct rtk_hdmi *hdmi, u32 hdcp_support);
int rtk_hdcp_enable(struct rtk_hdmi *hdmi);
int rtk_hdcp_disable(struct rtk_hdmi *hdmi);
void rtk_hdcp_update_cap(struct rtk_hdmi *hdmi, u8 mCap);
void rtk_hdcp_set_state(struct rtk_hdmi *hdmi,
		enum rtk_hdcp_state state, int err_code);
void rtk_hdcp_commit_state(struct drm_connector *connector,
		unsigned int old_cp, unsigned int new_cp);
#endif
