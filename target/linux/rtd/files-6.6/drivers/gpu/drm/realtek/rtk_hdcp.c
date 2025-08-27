// SPDX-License-Identifier: GPL-2.0-only
/*
 * Realtek HDCP driver
 *
 * Copyright (c) 2022 Realtek Semiconductor Corp.
 */

#include <drm/display/drm_hdcp_helper.h>
#include <drm/drm_print.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/random.h>

#include "rtk_hdmi.h"

#define HDCP2_AUTH_RETRY_CNT  3
#define HDCP2_LC_RETRY_CNT  1024

static struct rtk_hdcp_err_table hdcp_err_table[] = {
	{ HDCP_NO_ERR, "no error" },
	{ HDCP_PLUGOUT_ERR, "HDMI cable plugout" },
	{ HDCP_WAIT_TIMEOUT_ERR, "wait timeout" },
	{ HDCP_DDC_RD_TRANS_ERR, "i2c read transfer failed" },
	{ HDCP_DDC_WR_NOMEM_ERR, "hdcp ddc allocate memory failed" },
	{ HDCP_DDC_WR_TRANS_ERR, "i2c write transfer failed" },
	{ HDCP1_NOTSUPPORT_ERR, "sink device doesn't support hdcp" },
	{ HDCP1_INVALID_AKSV_ERR, "AKSV of key is invalid" },
	{ HDCP1_INVALID_BKSV_ERR, "BKSV of sink device is invalid" },
	{ HDCP1_REVOKED_BKSV_ERR, "BKSV of sink device is revoked" },
	{ HDCP1_REVOKED_KSVLIST_ERR, "KSV list of downstream is revoked" },
	{ HDCP1_KSVLIST_TIMEOUT_ERR,
		"getting KSV list from repeater device timeout" },
	{ HDCP1_MAX_CASCADE_ERR,
		"hdcp1 max topology limit exceeded, more than seven levels" },
	{ HDCP1_MAX_DEVICE_ERR,
		"hdcp1 max topology limit exceeded, more than 127 downstream devices" },
	{ HDCP1_DDC_BCAPS_ERR,
		"read Bcaps from sink device over i2c(offset=0x40) failed" },
	{ HDCP1_DDC_BSTATUS_ERR,
		"read Bstatus from sink device over i2c(offset=0x41) failed" },
	{ HDCP1_DDC_AN_ERR,
		"write An to sink device over i2c(offset=0x18) failed" },
	{ HDCP1_DDC_AKSV_ERR,
		"write AKSV to sink device over i2c(offset=0x10) failed" },
	{ HDCP1_DDC_BKSV_ERR,
		"read BKSV from sink device over i2c(offset=0x00) failed" },
	{ HDCP1_DDC_RI_PRIME_ERR,
		"read Ri' from sink device over i2c(offset=0x08) failed" },
	{ HDCP1_DDC_KSV_FIFO_ERR,
		"read KSV list form repeater device over i2c(offset=0x43) failed" },
	{ HDCP1_DDC_V_PRIME_ERR,
		"hdcp1 read V' from repeater device over i2c(offset=0x20) failed" },
	{ HDCP1_TEE_INIT_OPENCONTEXT_ERR,
		"invoke tee_client_open_context func failed when init hdcp1 tee" },
	{ HDCP1_TEE_INIT_OPENSESSION_ERR,
		"invoke tee_client_open_session func failed when init hdcp1 tee" },
	{ HDCP1_TEE_NOINIT_ERR, "hdcp1 tee is not initialized" },
	{ HDCP1_TEE_NOMEM_ERR, "hdcp1 ca allocate memory failed" },
	{ HDCP1_TEE_GEN_AN_ERR,
		"invoke TA_TEE_HDCP14_GenAn command failed" },
	{ HDCP1_TEE_READAKSV_ERR,
		"invoke TA_TEE_HDCP14_GetAKSV command failed" },
	{ HDCP1_TEE_REPEATER_BIT_ERR,
		"invoke TA_TEE_HDCP14_SetRepeaterBitInTx command failed" },
	{ HDCP1_TEE_WEITE_BKSV_ERR,
		"invoke TA_TEE_HDCP14_WriteBKSV command failed" },
	{ HDCP1_TEE_CHECK_RI_ERR,
		"invoke TA_TEE_HDCP14_CheckR0 command failed" },
	{ HDCP1_TEE_INCORRECT_RI_ERR, "computed Ri does not equals to Ri prime" },
	{ HDCP1_TEE_SET_ENC_ERR,
		"invoke TA_TEE_HDCP14_SetEnc command failed" },
	{ HDCP1_TEE_SET_WINDER_ERR,
		"invoke TA_TEE_HDCP14_SetWinderWin command failed" },
	{ HDCP1_TEE_SHA_APPEND_ERR,
		"invoke TA_TEE_HDCP14_SHAAppend command failed" },
	{ HDCP1_TEE_COMPUTE_V_ERR,
		"invoke TA_TEE_HDCP14_ComputeV command failed" },
	{ HDCP1_TEE_WAIT_V_READY_ERR, "compute V failed" },
	{ HDCP1_TEE_VERIFY_V_ERR,
		"invoke TA_TEE_HDCP14_VerifyV command failed" },
	{ HDCP1_TEE_V_MATCH_ERR, "hdcp1 computed V does not equals to V prime" },
	{ HDCP1_TEE_SET_KEY_ERR,
		"invoke TA_TEE_HDCP14_SetParamKey command failed" },
	{ HDCP1_TEE_FIX_480P_ERR,
		"invoke TA_TEE_HDCP14_Fix480P command failed" },
	{ HDCP1_TEE_SET_KEEPOUTWIN_ERR,
		"invoke TA_TEE_HDCP14_SetKeepoutwin command failed" },
	{ HDCP2_NOTSUPPORT_ERR, "sink device doesn't support hdcp 2.x" },
	{ HDCP2_NOMEM_ERR, "hdcp2 allocate memory failed" },
	{ HDCP2_REVOKED_RECEIVER_ID_ERR, "hdcp2 receiver id is revoked" },
	{ HDCP2_REVOKED_ID_LIST_ERR, "hdcp2 id list of downstream is revoked" },
	{ HDCP2_VERIFY_RX_CERT_INVALID_ARG_ERR,
		"invalid argument when trying to verify rx_cert" },
	{ HDCP2_MAX_CASCADE_ERR,
		"hdcp2 max topology limit exceeded, more than four levels" },
	{ HDCP2_MAX_DEVICE_ERR,
		"hdcp2 max topology limit exceeded, more than 31 downstream devices" },
	{ HDCP2_SEQ_NUM_M_ROLL_OVER_ERR, "seq_num_M rolls over" },
	{ HDCP2_REAUTH_REQUEST_ERR, "hdcp2 reauth request" },
	{ HDCP2_TOPOLOGY_CHANGE_ERR, "hdcp2 topology is changed"},
	{ HDCP2_GET_TIMEOUT_VAL_ERR, "hdcp2 get msg timeout value failed" },
	{ HDCP2_WAIT_MSG_TIMEOUT_ERR, "hdcp2 wait msg timeout" },
	{ HDCP2_READ_MSG_SIZE_ERR, "message size is more than expect size" },
	{ HDCP2_READ_AKE_SEND_CERT_ERR, "Read AKE_Send_Cert message failed" },
	{ HDCP2_READ_AKE_SEND_HPRIME_ERR, "Read AKE_Send_H_prime message failed" },
	{ HDCP2_READ_AKE_SEND_PAIRING_INFO_ERR,
		"Read AKE_Send_Pairing_Info message failed" },
	{ HDCP2_READ_LC_SEND_LPRIME_ERR, "Read LC_Send_L_prime message failed" },
	{ HDCP2_READ_REP_SEND_RECVID_LIST_ERR,
		"Read RepeaterAuth_Send_ReceiverID_List message failed" },
	{ HDCP2_READ_REP_STREAM_READY_ERR,
		"Read RepeaterAuth_Stream_Ready message failed" },
	{ HDCP2_DDC_HDCP_VER_ERR,
		"read hdcp version from sink device over i2c(offset=0x50) failed" },
	{ HDCP2_DDC_WR_MSG_ERR,
		"hdcp2 write message to sink device over i2c(offset=0x60) failed" },
	{ HDCP2_DDC_RXSTATUS_ERR,
		"hdcp2 read RxStatus from sink device over i2c(offset=0x70) failed" },
	{ HDCP2_DDC_RD_MSG_ERR,
		"hdcp2 read message from sink device over i2c(offset=0x80) failed" },
	{ HDCP2_TEE_INIT_OPENCONTEXT_ERR,
		"invoke tee_client_open_context func failed when init hdcp2 tee" },
	{ HDCP2_TEE_INIT_OPENSESSION_ERR,
		"invoke tee_client_open_session func failed when init hdcp2 tee" },
	{ HDCP2_TEE_NOINIT_ERR, "hdcp2 tee is not initialized" },
	{ HDCP2_TEE_NOMEM_ERR, "hdcp2 ca allocate memory failed" },
	{ HDCP2_TEE_READ_KEY_ERR, "hdcp2 key is not exist" },
	{ HDCP2_TEE_WRITE_KEY_ERR, "write hdcp2 key failed" },
	{ HDCP2_TEE_AKE_INIT_ERR,
		"invoke TA_TEE_HDCPSendAkeInit command failed" },
	{ HDCP2_TEE_RX_CERT_ID_ERR,
		"wrong msg_id when trying to verify rx_cert in CA" },
	{ HDCP2_TEE_INVOKE_LLC_SIGN_ERR,
		"invoke TA_TEE_HDCPCheckLLCSignature command failed" },
	{ HDCP2_TEE_INVALID_LLC_SIGN_ERR,
		"invalid LLC signature from sink device" },
	{ HDCP2_TEE_INVOKE_GET_RXINFO_ERR,
		"invoke TA_TEE_HDCPGetRxInfo command failed" },
	{ HDCP2_TEE_AKE_NO_STOREDKM_ERR,
		"invoke TA_TEE_HDCPSendAkeNoStoredKm command failed" },
	{ HDCP2_TEE_RX_PRIME_ID_ERR,
		"wrong msg_id when trying to verify hprime in CA" },
	{ HDCP2_TEE_COMPUTE_H_ERR, "hdcp2 compute H failed" },
	{ HDCP2_TEE_VERIFY_H_PRIME_ERR,
		"calculated H does not equals to H prime" },
	{ HDCP2_TEE_PAIRING_INFO_ID_ERR,
		"wrong msg_id when store pairing_info" },
	{ HDCP2_TEE_SAVE_RXINFO_ERR,
		"invoke TA_TEE_HDCPSaveRxInfo command failed" },
	{ HDCP2_TEE_LC_INIT_ERR,
		"invoke TA_TEE_HDCPSendLCInit command failed" },
	{ HDCP2_TEE_RX_LPRIME_ID_ERR,
		"wrong msg_id when trying to verify L prime in CA" },
	{ HDCP2_TEE_COMPUTE_L_ERR,
		"invoke TA_TEE_HDCPComputeL command failed" },
	{ HDCP2_TEE_VERIFY_LPRIME_ERR,
		"computed L does not equals to L prime" },
	{ HDCP2_TEE_GET_SKEY_ERR,
		"invoke TA_TEE_HDCPSendSke command failed" },
	{ HDCP2_TEE_RECVID_LIST_ID_ERR,
		"wrong msg_id when trying to verify repeater topology" },
	{ HDCP2_TEE_INVOKE_RECVID_LIST_ERR,
		"invoke TA_TEE_HDCPReceiverIDList command failed" },
	{ HDCP2_TEE_RLIST_V_SIZE_ERR,
		"invalid V prime half size in ta" },
	{ HDCP2_TEE_RLIST_MSG_ID_ERR,
		"wrong msg_id when trying to verify repeater topology in ta" },
	{ HDCP2_TEE_RLIST_MAX_EXCEED_ERR, "hdcp2 max topology limit exceeded" },
	{ HDCP2_TEE_RLIST_MSG_SIZE_ERR,
		"computed message size is incorrect when process receiver id list in ta" },
	{ HDCP2_TEE_RLIST_INCORRECT_SEQ_V_ERR,
		"incorrect seq_num_V, repeater device should initializes seq_num_V" },
	{ HDCP2_TEE_RLIST_V_ROLL_OVER_ERR, "seq_num_V rolls over" },
	{ HDCP2_TEE_RLIST_COMPUTE_V_ERR, "hdcp2 compute V failed" },
	{ HDCP2_TEE_RLIST_V_COMPARE_ERR,
		"hdcp2 computed V does not equals to V prime" },
	{ HDCP2_TEE_STREAM_READY_ID_ERR,
		"wrong msg_id when trying to verify M prime" },
	{ HDCP2_TEE_INVOKE_COMPUTE_M_ERR,
		"invoke TA_TEE_HDCPComputeM command failed" },
	{ HDCP2_TEE_STREAM_M_COMPARE_ERR,
		"hdcp2 computed M does not equals to M prime" },
	{ HDCP2_TEE_ENABLE_CIPHER_ERR,
		"invoke TA_TEE_HDCPLC128Cipher_2 command failed" }
};

#define SCDC_DDC_ADDR 0x54
static ssize_t rtk_scdc_read(struct i2c_adapter *adapter, u8 offset, void *buffer,
		      size_t size)
{
	int ret;
	struct i2c_msg msgs[2] = {
		{
			.addr = SCDC_DDC_ADDR,
			.flags = 0,
			.len = 1,
			.buf = &offset,
		}, {
			.addr = SCDC_DDC_ADDR,
			.flags = I2C_M_RD,
			.len = size,
			.buf = buffer,
		}
	};

	ret = i2c_transfer(adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		return ret;
	if (ret != ARRAY_SIZE(msgs))
		return -EPROTO;

	return 0;
}

static int rtk_hdmi_hdcp_read(struct rtk_hdmi *hdmi,
				unsigned int offset, void *buffer, size_t size)
{
	struct i2c_adapter *adapter = hdmi->ddc;
	int ret;
	u8 start = offset & 0xff;
	int hpd;

	struct i2c_msg msgs[] = {
		{
			.addr = DRM_HDCP_DDC_ADDR,
			.flags = 0,
			.len = 1,
			.buf = &start,
		},
		{
			.addr = DRM_HDCP_DDC_ADDR,
			.flags = I2C_M_RD,
			.len = size,
			.buf = buffer
		}
	};

	hpd = gpiod_get_value(hdmi->hpd_gpio);
	if (!hpd) {
		ret = -HDCP_PLUGOUT_ERR;
		goto exit;
	}

	ret = i2c_transfer(adapter, msgs, ARRAY_SIZE(msgs));
	if (ret == ARRAY_SIZE(msgs)) {
		ret = HDCP_NO_ERR;
	} else {
		switch (offset) {
		case DRM_HDCP_DDC_BCAPS:
			ret = -HDCP1_DDC_BCAPS_ERR;
			break;
		case DRM_HDCP_DDC_BSTATUS:
			ret = -HDCP1_DDC_BSTATUS_ERR;
			break;
		case DRM_HDCP_DDC_BKSV:
			ret = -HDCP1_DDC_BKSV_ERR;
			break;
		case DRM_HDCP_DDC_RI_PRIME:
			ret = -HDCP1_DDC_RI_PRIME_ERR;
			break;
		case DRM_HDCP_DDC_KSV_FIFO:
			ret = -HDCP1_DDC_KSV_FIFO_ERR;
			break;
		case DRM_HDCP_DDC_V_PRIME(0):
		case DRM_HDCP_DDC_V_PRIME(1):
		case DRM_HDCP_DDC_V_PRIME(2):
		case DRM_HDCP_DDC_V_PRIME(3):
		case DRM_HDCP_DDC_V_PRIME(4):
			ret = -HDCP1_DDC_V_PRIME_ERR;
			break;
		case HDCP_2_2_HDMI_REG_VER_OFFSET:
			ret = -HDCP2_DDC_HDCP_VER_ERR;
			break;
		case HDCP_2_2_HDMI_REG_RXSTATUS_OFFSET:
			ret = -HDCP2_DDC_RXSTATUS_ERR;
			break;
		case HDCP_2_2_HDMI_REG_RD_MSG_OFFSET:
			ret = -HDCP2_DDC_RD_MSG_ERR;
			break;
		default:
			ret = -HDCP_DDC_RD_TRANS_ERR;
		}
	}

	hpd = gpiod_get_value(hdmi->hpd_gpio);
	if (!hpd)
		ret = -HDCP_PLUGOUT_ERR;

exit:
	return ret;
}

static int rtk_hdmi_hdcp_write(struct rtk_hdmi *hdmi,
				unsigned int offset, void *buffer, size_t size)
{
	struct i2c_adapter *adapter = hdmi->ddc;
	int ret;
	u8 *write_buf;
	struct i2c_msg msg;
	int hpd;

	write_buf = kzalloc(size + 1, GFP_KERNEL);
	if (!write_buf)
		return -HDCP_DDC_WR_NOMEM_ERR;

	write_buf[0] = offset & 0xff;
	memcpy(&write_buf[1], buffer, size);

	msg.addr = DRM_HDCP_DDC_ADDR;
	msg.flags = 0,
	msg.len = size + 1,
	msg.buf = write_buf;

	ret = i2c_transfer(adapter, &msg, 1);
	if (ret == 1) {
		ret = HDCP_NO_ERR;
	} else {
		switch (offset) {
		case DRM_HDCP_DDC_AN:
			ret = -HDCP1_DDC_AN_ERR;
			break;
		case DRM_HDCP_DDC_AKSV:
			ret = -HDCP1_DDC_AKSV_ERR;
			break;
		case HDCP_2_2_HDMI_REG_WR_MSG_OFFSET:
			ret = -HDCP2_DDC_WR_MSG_ERR;
			break;
		default:
			ret = -HDCP_DDC_WR_TRANS_ERR;
		}
	}

	hpd = gpiod_get_value(hdmi->hpd_gpio);
	if (!hpd)
		ret = -HDCP_PLUGOUT_ERR;

	kfree(write_buf);
	return ret;
}

static const char *hdcp_state_get_name(enum rtk_hdcp_state state)
{
	switch (state) {
	case HDCP_HDMI_DISCONNECT:
		return "CABLE_DISCONNECTED";
	case HDCP_HDMI_DISABLED:
		return "HDMI_DISABLED";
	case HDCP_UNAUTH:
		return "HDCP_UNAUTH";
	case HDCP_1_IN_AUTH:
		return "HDCP1_IN_AUTH";
	case HDCP_2_IN_AUTH:
		return "HDCP2_IN_AUTH";
	case HDCP_1_SUCCESS:
		return "HDCP1_SUCCESS";
	case HDCP_2_SUCCESS:
		return "HDCP2_SUCCESS";
	case HDCP_1_FAILURE:
		return "HDCP1_FAILURE";
	case HDCP_2_FAILURE:
		return "HDCP2_FAILURE";
	}
	return "Invalid";
}

static const char *hdcp_err_get_description(int err_code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hdcp_err_table); i++)
		if (err_code == hdcp_err_table[i].err_code)
			return hdcp_err_table[i].description;

	DRM_DEBUG_DRIVER("Undefined err_code=%d", err_code);

	return "Undefined error";
}

void rtk_hdcp_set_state(struct rtk_hdmi *hdmi,
		enum rtk_hdcp_state state, int err_code)
{
	struct kobject *kobj = &hdmi->dev->kobj;
	char state_str[30];
	char err_str[30];
	char err_desc[200];
	char *envp[4] = {state_str, err_str, err_desc, NULL};

	if ((hdmi->connector.status == connector_status_disconnected) &&
		(hdmi->hdcp.hdcp_state == HDCP_HDMI_DISCONNECT) &&
		(state == HDCP_UNAUTH))
		return;

	if (hdmi->hdcp.hdcp_state == state)
		return;

	hdmi->hdcp.hdcp_state = state;
	hdmi->hdcp.hdcp_err_code = err_code;

	snprintf(state_str, sizeof(state_str), "HDCP_STATE=%s",
		hdcp_state_get_name(state));
	snprintf(err_str, sizeof(err_str), "HDCP_ERR=%d",
		err_code);
	snprintf(err_desc, sizeof(err_desc), "HDCP_ERR_DES=%s",
		hdcp_err_get_description(err_code));

	if (!hdmi->in_suspend)
		kobject_uevent_env(kobj, KOBJ_CHANGE, envp);
	else
		dev_info(hdmi->dev, "Skip send uevent in suspend\n");

	dev_info(hdmi->dev, "%s, err_code=%d, %s\n",
		state_str, err_code, err_desc);
}

static
int rtk_hdmi_hdcp_capable(struct rtk_hdmi *hdmi,
		bool *hdcp_capable)
{
	int ret;
	u8 val[2];

	*hdcp_capable = false;

	ret = rtk_hdmi_hdcp_read(hdmi, DRM_HDCP_DDC_BCAPS, &val, 1);
	if (ret)
		return ret;

	ret = rtk_hdmi_hdcp_read(hdmi, DRM_HDCP_DDC_BSTATUS,
					&val, DRM_HDCP_BSTATUS_LEN);
	if (ret)
		return ret;

	*hdcp_capable = true;
	return HDCP_NO_ERR;

}

static
int rtk_hdmi_hdcp_write_an_aksv(struct rtk_hdmi *hdmi,
				u8 *an, u8 *aksv)
{
	int ret;

	ret = rtk_hdmi_hdcp_write(hdmi, DRM_HDCP_DDC_AN, an,
					DRM_HDCP_AN_LEN);
	if (ret)
		return ret;

	ret = rtk_hdmi_hdcp_write(hdmi, DRM_HDCP_DDC_AKSV,
				  aksv, DRM_HDCP_KSV_LEN);

	return ret;
}

static int rtk_hdmi_hdcp_read_bksv(struct rtk_hdmi *hdmi, u8 *bksv)
{
	int ret;

	ret = rtk_hdmi_hdcp_read(hdmi, DRM_HDCP_DDC_BKSV, bksv,
					DRM_HDCP_KSV_LEN);

	return ret;
}

static
int rtk_hdmi_hdcp_read_bstatus(struct rtk_hdmi *hdmi, u8 *bstatus)
{
	int ret;

	ret = rtk_hdmi_hdcp_read(hdmi, DRM_HDCP_DDC_BSTATUS,
				bstatus, DRM_HDCP_BSTATUS_LEN);

	return ret;
}

static
int rtk_hdmi_hdcp_repeater_present(struct rtk_hdmi *hdmi, bool *repeater_present)
{
	int ret;
	u8 val;

	ret = rtk_hdmi_hdcp_read(hdmi, DRM_HDCP_DDC_BCAPS, &val, 1);
	if (ret) {
		DRM_DEBUG_DRIVER("Read bcaps over DDC failed, ret=%d", ret);
		return ret;
	}
	*repeater_present = val & DRM_HDCP_DDC_BCAPS_REPEATER_PRESENT;
	return 0;
}

static
int rtk_hdmi_hdcp_read_ri_prime(struct rtk_hdmi *hdmi, u8 *ri_prime)
{
	int ret;

	ret = rtk_hdmi_hdcp_read(hdmi, DRM_HDCP_DDC_RI_PRIME,
					ri_prime, DRM_HDCP_RI_LEN);

	return ret;
}

static
int rtk_hdmi_hdcp_read_ksv_ready(struct rtk_hdmi *hdmi, bool *ksv_ready)
{
	int ret;
	u8 val;

	ret = rtk_hdmi_hdcp_read(hdmi, DRM_HDCP_DDC_BCAPS, &val, 1);
	if (ret) {
		DRM_DEBUG_DRIVER("Read bcaps over DDC failed, ret=%d", ret);
		return ret;
	}
	*ksv_ready = val & DRM_HDCP_DDC_BCAPS_KSV_FIFO_READY;
	return ret;
}

static
int rtk_hdmi_hdcp_read_ksv_fifo(struct rtk_hdmi *hdmi,
				int num_downstream, u8 *ksv_fifo)
{
	int ret;

	ret = rtk_hdmi_hdcp_read(hdmi, DRM_HDCP_DDC_KSV_FIFO,
				ksv_fifo, num_downstream * DRM_HDCP_KSV_LEN);

	return ret;
}

static
int rtk_hdmi_hdcp_read_v_prime_part(struct rtk_hdmi *hdmi, int i, u32 *part)
{
	int ret;

	if (i >= DRM_HDCP_V_PRIME_NUM_PARTS)
		return -EINVAL;

	ret = rtk_hdmi_hdcp_read(hdmi, DRM_HDCP_DDC_V_PRIME(i),
				part, DRM_HDCP_V_PRIME_PART_LEN);
	if (ret)
		DRM_DEBUG_DRIVER("Read V'[%d] over DDC failed, ret=%d", i, ret);
	return ret;
}

static
int rtk_hdmi_hdcp_check_link(struct rtk_hdmi *hdmi)
{
	int ret = 0;
	u8 ri_prime[DRM_HDCP_RI_LEN];
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	struct rtk_hdcp1_tee *hdcp1_tee = &hdcp->hdcp1_tee;
	const struct rtk_hdcp1_tee_ops *hdcp1_tee_ops = hdcp1_tee->hdcp1_tee_ops;

	ret = rtk_hdmi_hdcp_read_ri_prime(hdmi, ri_prime);
	if (ret)
		return ret;

	ret = hdcp1_tee_ops->check_ri_prime(hdcp1_tee, ri_prime);

	return ret;
}

static
int rtk_hdmi_hdcp_set_rekey_win(struct rtk_hdmi *hdmi, u8 rekey_win)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	struct rtk_hdcp1_tee *hdcp1_tee = &hdcp->hdcp1_tee;
	int ret;

	ret = hdcp1_tee->hdcp1_tee_ops->hdcp1_tee_api_init(hdcp1_tee);
	if (ret)
		return ret;

	ret = hdcp1_tee->hdcp1_tee_ops->set_rekey_win(hdcp1_tee, rekey_win);

	return ret;
}

struct hdcp2_message_data {
	u8 msg_id;
	u32 timeout;
	u32 timeout2;
};

static const struct hdcp2_message_data hdcp2_msg_data[] = {
	{ HDCP_2_2_AKE_INIT, 0, 0 },
	{ HDCP_2_2_AKE_SEND_CERT, HDCP_2_2_CERT_TIMEOUT_MS + 200, 0 },
	{ HDCP_2_2_AKE_NO_STORED_KM, 0, 0 },
	{ HDCP_2_2_AKE_STORED_KM, 0, 0 },
	{ HDCP_2_2_AKE_SEND_HPRIME, HDCP_2_2_HPRIME_PAIRED_TIMEOUT_MS,
		HDCP_2_2_HPRIME_NO_PAIRED_TIMEOUT_MS },
	{ HDCP_2_2_AKE_SEND_PAIRING_INFO, HDCP_2_2_PAIRING_TIMEOUT_MS, 0 },
	{ HDCP_2_2_LC_INIT, 0, 0 },
	{ HDCP_2_2_LC_SEND_LPRIME, HDCP_2_2_HDMI_LPRIME_TIMEOUT_MS, 0 },
	{ HDCP_2_2_SKE_SEND_EKS, 0, 0 },
	{ HDCP_2_2_REP_SEND_RECVID_LIST, HDCP_2_2_RECVID_LIST_TIMEOUT_MS + 500, 0 },
	{ HDCP_2_2_REP_SEND_ACK, 0, 0 },
	{ HDCP_2_2_REP_STREAM_MANAGE, 0, 0 },
	{ HDCP_2_2_REP_STREAM_READY, HDCP_2_2_STREAM_READY_TIMEOUT_MS, 0 },
};

static
int rtk_hdmi_hdcp2_capable(struct rtk_hdmi *hdmi, bool *capable)
{
	u8 hdcp2_version;
	int ret;

	*capable = false;
	ret = rtk_hdmi_hdcp_read(hdmi, HDCP_2_2_HDMI_REG_VER_OFFSET,
				&hdcp2_version, sizeof(hdcp2_version));
	if (!ret && hdcp2_version & HDCP_2_2_HDMI_SUPPORT_MASK)
		*capable = true;

	return ret;
}

static
int rtk_hdmi_hdcp2_read_rx_status(struct rtk_hdmi *hdmi, u8 *rx_status)
{
	return rtk_hdmi_hdcp_read(hdmi,
			     HDCP_2_2_HDMI_REG_RXSTATUS_OFFSET,
			     rx_status,
			     HDCP_2_2_HDMI_RXSTATUS_LEN);
}

static inline
int hdcp2_detect_msg_availability(struct rtk_hdmi *hdmi,
				u8 msg_id, bool *msg_ready,
				ssize_t *msg_sz)
{
	u8 rx_status[HDCP_2_2_HDMI_RXSTATUS_LEN];
	int ret;

	ret = rtk_hdmi_hdcp2_read_rx_status(hdmi, rx_status);
	if (ret < 0) {
		DRM_DEBUG_DRIVER("Read rx_status failed, ret=%d", ret);
		return ret;
	}

	*msg_sz = ((HDCP_2_2_HDMI_RXSTATUS_MSG_SZ_HI(rx_status[1]) << 8) |
		rx_status[0]);

	if (msg_id == HDCP_2_2_REP_SEND_RECVID_LIST)
		*msg_ready = (HDCP_2_2_HDMI_RXSTATUS_READY(rx_status[1]) &&
				*msg_sz);
	else
		*msg_ready = *msg_sz;

	return HDCP_NO_ERR;
}

static int get_hdcp2_msg_timeout(u8 msg_id, bool is_paired)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hdcp2_msg_data); i++) {
		if (hdcp2_msg_data[i].msg_id == msg_id &&
			(msg_id != HDCP_2_2_AKE_SEND_HPRIME || is_paired))
			return hdcp2_msg_data[i].timeout;
		else if (hdcp2_msg_data[i].msg_id == msg_id)
			return hdcp2_msg_data[i].timeout2;
	}

	return -EINVAL;
}

static int
rtk_hdmi_hdcp2_wait_for_msg(struct rtk_hdmi *hdmi,
			u8 msg_id, bool paired)
{
	bool msg_ready = false;
	int timeout, ret;
	ssize_t msg_sz = 0;

	timeout = get_hdcp2_msg_timeout(msg_id, paired);
	if (timeout < 0)
		return -HDCP2_GET_TIMEOUT_VAL_ERR;

	ret = __wait_for(ret = hdcp2_detect_msg_availability(hdmi,
							     msg_id, &msg_ready,
							     &msg_sz),
			ret == -HDCP_PLUGOUT_ERR,
			!ret && msg_ready && msg_sz, timeout * 1000,
			1000, 5 * 1000);
	if (ret)
		dev_info(hdmi->dev, "msg_id: %u, ret: %d, timeout: %d, msg_ready: %u, msg_sz: %ld\n",
			msg_id, ret, timeout, msg_ready, msg_sz);

	if (ret == -HDCP_PLUGOUT_ERR)
		return ret;

	if (ret) {
		switch (msg_id) {
		case HDCP_2_2_AKE_SEND_CERT:
			ret = -HDCP2_READ_AKE_SEND_CERT_ERR;
			break;
		case HDCP_2_2_AKE_SEND_HPRIME:
			ret = -HDCP2_READ_AKE_SEND_HPRIME_ERR;
			break;
		case HDCP_2_2_AKE_SEND_PAIRING_INFO:
			ret = -HDCP2_READ_AKE_SEND_PAIRING_INFO_ERR;
			break;
		case HDCP_2_2_LC_SEND_LPRIME:
			ret = -HDCP2_READ_LC_SEND_LPRIME_ERR;
			break;
		case HDCP_2_2_REP_SEND_RECVID_LIST:
			ret = -HDCP2_READ_REP_SEND_RECVID_LIST_ERR;
			break;
		case HDCP_2_2_REP_STREAM_READY:
			ret = -HDCP2_READ_REP_STREAM_READY_ERR;
			break;
		default:
			ret = -HDCP2_WAIT_MSG_TIMEOUT_ERR;
		}
		return ret;
	}

	return (int)msg_sz;
}

static
int rtk_hdmi_hdcp2_write_msg(struct rtk_hdmi *hdmi,
				void *buf, size_t size)
{
	unsigned int offset;
	unsigned char *msg;

	msg = (unsigned char *)buf;
	DRM_DEBUG_DRIVER("HDCP2 send id(%u) message", msg[0]);

	offset = HDCP_2_2_HDMI_REG_WR_MSG_OFFSET;
	return rtk_hdmi_hdcp_write(hdmi, offset, buf, size);
}

static
int rtk_hdmi_hdcp2_read_msg(struct rtk_hdmi *hdmi,
			u8 msg_id, void *buf, size_t size)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	unsigned int offset;
	int ret;
	unsigned long start_time;
	unsigned int wait_ms;

	DRM_DEBUG_DRIVER("HDCP2 wait id(%u) message", msg_id);

	start_time = jiffies;
	ret = rtk_hdmi_hdcp2_wait_for_msg(hdmi, msg_id,
					  hdcp->is_paired);

	if (ret < 0)
		return ret;

	/*
	 * Available msg size should be equal to or lesser than the
	 * available buffer.
	 */
	if (ret > size) {
		DRM_DEBUG_DRIVER("HDCP2 msg_sz(%d) is more than exp size(%zu)",
				ret, size);
		return -HDCP2_READ_MSG_SIZE_ERR;
	}

	offset = HDCP_2_2_HDMI_REG_RD_MSG_OFFSET;
	ret = rtk_hdmi_hdcp_read(hdmi, offset, buf, ret);

	wait_ms = jiffies_to_msecs(jiffies - start_time);
	DRM_DEBUG_DRIVER("HDCP2 message id(%u) costs %ums", msg_id, wait_ms);

	return ret;
}

static
int rtk_hdmi_hdcp2_check_link(struct rtk_hdmi *hdmi)
{
	u8 rx_status[HDCP_2_2_HDMI_RXSTATUS_LEN];
	int ret;

	ret = rtk_hdmi_hdcp2_read_rx_status(hdmi, rx_status);
	if (ret)
		return ret;

	/*
	 * Re-auth request and Link Integrity Failures are represented by
	 * same bit. i.e reauth_req.
	 */
	if (HDCP_2_2_HDMI_RXSTATUS_REAUTH_REQ(rx_status[1]))
		ret = -HDCP2_REAUTH_REQUEST_ERR;
	else if (HDCP_2_2_HDMI_RXSTATUS_READY(rx_status[1]))
		ret = -HDCP2_TOPOLOGY_CHANGE_ERR;

	return ret;
}

static
int rtk_hdmi_hdcp_check_ced(struct rtk_hdmi *hdmi)
{
	struct drm_scdc *scdc = &hdmi->connector.display_info.hdmi.scdc;
	u8 ced_data[6];
	unsigned int ch0_err, ch1_err, ch2_err;
	unsigned int err_code = 0;
	int ret;

	if (!scdc->supported)
		return -HDCP_CED_NOTSUPPORT;

	if (!hdmi->hpd_state)
		return -HDCP_PLUGOUT_ERR;

	ret = rtk_scdc_read(hdmi->ddc, SCDC_ERR_DET_0_L,
			ced_data, sizeof(ced_data));
	if (ret)
		return -HDCP_DDC_RD_TRANS_ERR;

	if (!(ced_data[1] & SCDC_CHANNEL_VALID) ||
		!(ced_data[3] & SCDC_CHANNEL_VALID) ||
		!(ced_data[5] & SCDC_CHANNEL_VALID))
		return -HDCP_CED_INVALID_ERR;

	ch0_err = ((ced_data[1]&0x7f) << 8) | ced_data[0];
	ch1_err = ((ced_data[3]&0x7f) << 8) | ced_data[2];
	ch2_err = ((ced_data[5]&0x7f) << 8) | ced_data[4];

	if ((ch0_err == 0) && (ch1_err == 0) && (ch2_err == 0))
		return HDCP_NO_ERR;

	DRM_DEBUG_DRIVER("CED err count ch0=%u ch1=%u ch2=%u",
		ch0_err, ch1_err, ch2_err);

	if (ch0_err)
		err_code |= HDCP_CED_CH0_ERR;

	if (ch1_err)
		err_code |= HDCP_CED_CH1_ERR;

	if (ch2_err)
		err_code |= HDCP_CED_CH2_ERR;

	ret = -err_code;

	return ret;
}

static const struct rtk_hdcp_ops rtk_hdmi_hdcp_ops = {
	.hdcp_capable = rtk_hdmi_hdcp_capable,
	.write_an_aksv = rtk_hdmi_hdcp_write_an_aksv,
	.read_bksv = rtk_hdmi_hdcp_read_bksv,
	.read_bstatus = rtk_hdmi_hdcp_read_bstatus,
	.repeater_present = rtk_hdmi_hdcp_repeater_present,
	.read_ri_prime = rtk_hdmi_hdcp_read_ri_prime,
	.read_ksv_ready = rtk_hdmi_hdcp_read_ksv_ready,
	.read_ksv_fifo = rtk_hdmi_hdcp_read_ksv_fifo,
	.read_v_prime_part = rtk_hdmi_hdcp_read_v_prime_part,
	.check_link = rtk_hdmi_hdcp_check_link,
	.set_rekey_win = rtk_hdmi_hdcp_set_rekey_win,
	.hdcp2_capable = rtk_hdmi_hdcp2_capable,
	.write_2_2_msg = rtk_hdmi_hdcp2_write_msg,
	.read_2_2_msg = rtk_hdmi_hdcp2_read_msg,
	.check_2_2_link = rtk_hdmi_hdcp2_check_link,
	.check_ced_error = rtk_hdmi_hdcp_check_ced,
};

static
bool rtk_hdcp_is_ksv_valid(u8 *ksv)
{
	int i, ones = 0;
	/* KSV has 20 1's and 20 0's */
	for (i = 0; i < DRM_HDCP_KSV_LEN; i++)
		ones += hweight8(ksv[i]);
	if (ones != 20)
		return false;

	return true;
}

static
int rtk_hdcp_read_valid_bksv(struct rtk_hdmi *hdmi,
			     const struct rtk_hdcp_ops *hdcp_ops,
			     u8 *bksv)
{
	int ret, i, tries = 2;

	/* HDCP spec states that we must retry the bksv if it is invalid */
	for (i = 0; i < tries; i++) {
		ret = hdcp_ops->read_bksv(hdmi, bksv);
		if (ret)
			return ret;

		DRM_DEBUG_DRIVER("Bksv %02x %02x %02x %02x %02x",
			bksv[0], bksv[1], bksv[2], bksv[3], bksv[4]);

		if (rtk_hdcp_is_ksv_valid(bksv))
			break;
	}

	if (i == tries)
		return -HDCP1_INVALID_BKSV_ERR;

	return ret;
}

static int
hdcp2_prepare_ake_init(struct rtk_hdcp *hdcp,
		       struct hdcp2_ake_init *ake_data)
{
	int ret;
	struct rtk_hdcp2_tee *hdcp2_tee = &hdcp->hdcp2_tee;

	ake_data->msg_id = HDCP_2_2_AKE_INIT;
	/*
	 * According to the HDCP 2.2 Spec.,
	 * version must be 0x2 and tx_cap_mask read as 0
	 */
	ake_data->tx_caps.version = 0x2;
	ake_data->tx_caps.tx_cap_mask[0] = 0x0;
	ake_data->tx_caps.tx_cap_mask[1] = 0x0;

	ret = hdcp2_tee->hdcp2_tee_ops->generate_random_rtx(hdcp2_tee, ake_data);
	return ret;
}

/*
 * We always use the no stored km data structure
 * to hold the store_km and nostore_km data
 */
static int
hdcp2_verify_rx_cert_prepare_km(struct rtk_hdcp *hdcp,
				struct hdcp2_ake_send_cert *rx_cert,
				bool *km_stored,
				struct hdcp2_ake_no_stored_km *ek_pub_km,
				size_t *msg_sz)
{
	int ret;
	struct rtk_hdcp2_tee *hdcp2_tee = &hdcp->hdcp2_tee;
	u8 receiver_id[HDCP_2_2_RECEIVER_ID_LEN];

	if (!rx_cert || !km_stored || !ek_pub_km || !msg_sz)
		return -HDCP2_VERIFY_RX_CERT_INVALID_ARG_ERR;

	ret = hdcp2_tee->hdcp2_tee_ops->verify_rx_cert(hdcp2_tee, rx_cert);
	if (ret) {
		DRM_DEBUG_DRIVER("Verify rx_cert failed ret=%d", ret);
		return ret;
	}

	memcpy(receiver_id, rx_cert->cert_rx.receiver_id,
	       HDCP_2_2_RECEIVER_ID_LEN);

	// TODO: Implement stored km
	/*
	 *	hdcp2_tee->hdcp2_tee_ops->check_stored_km(hdcp2_tee, receiver_id,
	 *					    ek_pub_km, km_stored);
	 */

	*km_stored = false;

	if (*km_stored) {
		DRM_DEBUG_DRIVER("Stored Km");
		ek_pub_km->msg_id = HDCP_2_2_AKE_STORED_KM;
		*msg_sz = sizeof(struct hdcp2_ake_stored_km);
		/*
		 * We do not need to setup stored km data because
		 * that data has been already prepared in check_stored_km if success,
		 * However, we add a callback function here
		 */
		ret = hdcp2_tee->hdcp2_tee_ops->prepare_stored_km(hdcp2_tee, ek_pub_km);
	} else {
		DRM_DEBUG_DRIVER("Non-Stored Km");
		ek_pub_km->msg_id = HDCP_2_2_AKE_NO_STORED_KM;
		*msg_sz = sizeof(struct hdcp2_ake_no_stored_km);

		ret = hdcp2_tee->hdcp2_tee_ops->prepare_no_stored_km(hdcp2_tee, ek_pub_km);
	}

	return ret;
}

static int hdcp2_verify_hprime(struct rtk_hdcp *hdcp,
			       struct hdcp2_ake_send_hprime *rx_hprime)
{
	int ret = 0;
	struct rtk_hdcp2_tee *hdcp2_tee = &hdcp->hdcp2_tee;
	/*
	 * Sum of HDCP_2_2_RRX_LEN, HDCP_2_2_RTX_LEN,
	 * HDCP_2_2_RXCAPS_LEN, and sizeof(struct hdcp2_tx_caps)
	 */
	u8 verified_src[22];
	u8 h[HDCP_2_2_H_PRIME_LEN] = {0};
	int offset = 0;

	memcpy(verified_src, hdcp->r_rx, HDCP_2_2_RRX_LEN);
	offset += HDCP_2_2_RRX_LEN;
	memcpy(verified_src+offset, hdcp->r_tx, HDCP_2_2_RTX_LEN);
	offset += HDCP_2_2_RTX_LEN;
	memcpy(verified_src+offset, hdcp->rx_caps, HDCP_2_2_RXCAPS_LEN);
	offset += HDCP_2_2_RXCAPS_LEN;
	memcpy(verified_src+offset, &hdcp->tx_caps, sizeof(struct hdcp2_tx_caps));

	ret = hdcp2_tee->hdcp2_tee_ops->verify_hprime(hdcp2_tee, rx_hprime,
							verified_src, h);
	if (ret)
		DRM_DEBUG_DRIVER("Verify hprime failed, ret=%d", ret);

	return ret;
}

static int
hdcp2_store_pairing_info(struct rtk_hdcp *hdcp,
			 struct hdcp2_ake_send_pairing_info *pairing_info)
{
	// TODO: Implement stored km

	/*
	 * struct rtk_hdcp2_tee *hdcp2_tee = &hdcp->hdcp2_tee;
	 * ret = hdcp2_tee->hdcp2_tee_ops->store_pairing_info(hdcp2_tee, pairing_info);
	 */

	return 0;
}

static int
hdcp2_prepare_lc_init(struct rtk_hdcp *hdcp,
		      struct hdcp2_lc_init *lc_init)
{
	int ret;
	struct rtk_hdcp2_tee *hdcp2_tee = &hdcp->hdcp2_tee;

	lc_init->msg_id = HDCP_2_2_LC_INIT;

	ret = hdcp2_tee->hdcp2_tee_ops->initiate_locality_check(hdcp2_tee, lc_init);

	return ret;
}

static int
hdcp2_verify_lprime(struct rtk_hdcp *hdcp,
		    struct hdcp2_lc_send_lprime *rx_lprime)
{
	int ret;
	u8 lprime[HDCP_2_2_L_PRIME_LEN] = {0};
	struct rtk_hdcp2_tee *hdcp2_tee = &hdcp->hdcp2_tee;

	ret = hdcp2_tee->hdcp2_tee_ops->verify_lprime(hdcp2_tee, rx_lprime, lprime);

	if (ret)
		DRM_DEBUG_DRIVER("Verify L_Prime failed, ret=%d", ret);

	return ret;
}

static int hdcp2_prepare_skey(struct rtk_hdcp *hdcp,
			       struct hdcp2_ske_send_eks *ske_data)
{
	int ret;
	struct rtk_hdcp2_tee *hdcp2_tee = &hdcp->hdcp2_tee;

	ske_data->msg_id = HDCP_2_2_SKE_SEND_EKS;

	ret = hdcp2_tee->hdcp2_tee_ops->get_session_key(hdcp2_tee, ske_data);
	return ret;
}

static int
hdcp2_verify_rep_topology_prepare_ack(struct rtk_hdcp *hdcp,
				      u8 *buf, int msg_size,
				      u8 *mV)
{
	int ret;
	struct rtk_hdcp2_tee *hdcp2_tee = &hdcp->hdcp2_tee;

	ret = hdcp2_tee->hdcp2_tee_ops->repeater_check_flow_prepare_ack(hdcp2_tee,
									buf, msg_size, mV);
	if (ret)
		DRM_DEBUG_DRIVER("Verify rep topology failed, ret=%d", ret);

	return ret;
}

static int hdcp2_verify_mprime(struct rtk_hdcp *hdcp,
		    struct hdcp2_rep_stream_ready *stream_ready,
		    u8 *input, int input_size)
{
	int ret;
	struct rtk_hdcp2_tee *hdcp2_tee = &hdcp->hdcp2_tee;

	ret = hdcp2_tee->hdcp2_tee_ops->verify_mprime(hdcp2_tee,
				stream_ready, input, input_size);

	if (ret)
		DRM_DEBUG_DRIVER("Verify mprime failed, ret=%d", ret);

	return ret;
}

static int hdcp2_authentication_key_exchange(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	struct drm_device *dev = hdmi->connector.dev;
	union {
		struct hdcp2_ake_init ake_init;
		struct hdcp2_ake_send_cert send_cert;
		struct hdcp2_ake_no_stored_km no_stored_km;
		struct hdcp2_ake_send_hprime send_hprime;
		struct hdcp2_ake_send_pairing_info pairing_info;
	} msgs = {0};
	const struct rtk_hdcp_ops *hdcp_ops = hdcp->hdcp_ops;
	size_t size;
	int ret;

	hdcp->hdcp1_device_downstream = 0;
	hdcp->seq_num_m = 0;

	ret = hdcp2_prepare_ake_init(hdcp, &msgs.ake_init);
	if (ret)
		return ret;

	memcpy(&hdcp->tx_caps, &msgs.ake_init.tx_caps, sizeof(struct hdcp2_tx_caps));
	memcpy(hdcp->r_tx, msgs.ake_init.r_tx, HDCP_2_2_RTX_LEN);

	ret = hdcp_ops->write_2_2_msg(hdmi, &msgs.ake_init,
				      sizeof(msgs.ake_init));
	if (ret)
		return ret;

	msleep(HDCP2_AKE_INIT_DELAY_MS); /* For DELL monitor reply wrong msg_id */

	ret = hdcp_ops->read_2_2_msg(hdmi, HDCP_2_2_AKE_SEND_CERT,
				     &msgs.send_cert, sizeof(msgs.send_cert));
	if (ret)
		return ret;

	if (msgs.send_cert.rx_caps[0] != HDCP_2_2_RX_CAPS_VERSION_VAL) {
		DRM_DEBUG_DRIVER("cert.rx_caps dont claim HDCP2.2");
		return -HDCP2_NOTSUPPORT_ERR;
	}

	memcpy(hdcp->r_rx, msgs.send_cert.r_rx, HDCP_2_2_RRX_LEN);
	memcpy(hdcp->rx_caps, msgs.send_cert.rx_caps, HDCP_2_2_RXCAPS_LEN);
	hdcp->is_repeater = HDCP_2_2_RX_REPEATER(msgs.send_cert.rx_caps[2]);

	if (drm_hdcp_check_ksvs_revoked(dev, msgs.send_cert.cert_rx.receiver_id, 1)) {
		DRM_DEBUG_DRIVER("Receiver ID is revoked");
		return -HDCP2_REVOKED_RECEIVER_ID_ERR;
	}

	/*
	 * Here msgs.no_stored_km will hold msgs corresponding to the km
	 * stored also.
	 */
	ret = hdcp2_verify_rx_cert_prepare_km(hdcp, &msgs.send_cert,
					      &hdcp->is_paired,
					      &msgs.no_stored_km, &size);
	if (ret)
		return ret;

	ret = hdcp_ops->write_2_2_msg(hdmi, &msgs.no_stored_km, size);
	if (ret < 0)
		return ret;

	ret = hdcp_ops->read_2_2_msg(hdmi, HDCP_2_2_AKE_SEND_HPRIME,
				     &msgs.send_hprime, sizeof(msgs.send_hprime));
	if (ret)
		return ret;

	ret = hdcp2_verify_hprime(hdcp, &msgs.send_hprime);
	if (ret != 0)
		return ret;

	if (!hdcp->is_paired) {
		/* Pairing is required */
		ret = hdcp_ops->read_2_2_msg(hdmi,
					     HDCP_2_2_AKE_SEND_PAIRING_INFO,
					     &msgs.pairing_info,
					     sizeof(msgs.pairing_info));
		if (ret)
			return ret;

		ret = hdcp2_store_pairing_info(hdcp, &msgs.pairing_info);
		if (ret)
			return ret;

		hdcp->is_paired = true;
	}

	return HDCP_NO_ERR;
}

static int hdcp2_locality_check(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	union {
		struct hdcp2_lc_init lc_init;
		struct hdcp2_lc_send_lprime send_lprime;
	} msgs = {0};
	const struct rtk_hdcp_ops *hdcp_ops = hdcp->hdcp_ops;
	int tries = HDCP2_LC_RETRY_CNT, ret, i;

	for (i = 0; i < tries; i++) {
		ret = hdcp2_prepare_lc_init(hdcp, &msgs.lc_init);
		if (ret)
			break;

		ret = hdcp_ops->write_2_2_msg(hdmi, &msgs.lc_init,
					      sizeof(msgs.lc_init));
		if (ret)
			break;

		ret = hdcp_ops->read_2_2_msg(hdmi,
					     HDCP_2_2_LC_SEND_LPRIME,
					     &msgs.send_lprime,
					     sizeof(msgs.send_lprime));
		if (ret == -HDCP_PLUGOUT_ERR)
			break;

		if (ret)
			continue;

		ret = hdcp2_verify_lprime(hdcp, &msgs.send_lprime);
		if (ret == HDCP_NO_ERR)
			break;
	}

	return ret;
}

static int hdcp2_session_key_exchange(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	struct hdcp2_ske_send_eks send_eks = {0};
	const struct rtk_hdcp_ops *hdcp_ops = hdcp->hdcp_ops;
	int ret;

	ret = hdcp2_prepare_skey(hdcp, &send_eks);
	if (ret)
		return ret;

	ret = hdcp_ops->write_2_2_msg(hdmi, &send_eks,
				      sizeof(send_eks));
	return ret;
}

static
int hdcp2_authenticate_repeater_topology(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	struct drm_device *dev = hdmi->connector.dev;
	union {
		struct hdcp2_rep_send_receiverid_list recvid_list;
		struct hdcp2_rep_send_ack rep_ack;
	} msgs;
	const struct rtk_hdcp_ops *hdcp_ops = hdcp->hdcp_ops;
	u32 device_cnt;
	u8 *rx_info, *buf;
	u8 mV[HDCP_2_2_V_PRIME_HALF_LEN];
	int ret;
	int msg_sz;

	ret = rtk_hdmi_hdcp2_wait_for_msg(hdmi,
					     HDCP_2_2_REP_SEND_RECVID_LIST,
					     hdcp->is_paired);
	if (ret < 0)
		goto exit;

	msg_sz = ret;
	if (msg_sz > sizeof(struct hdcp2_rep_send_receiverid_list)) {
		ret = -HDCP2_READ_MSG_SIZE_ERR;
		goto exit;
	}

	DRM_DEBUG_DRIVER("wait for ReceiverList ready and msg_sz=%d",
			msg_sz);

	buf = kzalloc(msg_sz, GFP_KERNEL);
	if (!buf) {
		ret = -HDCP2_NOMEM_ERR;
		goto exit;
	}

	ret = hdcp_ops->read_2_2_msg(hdmi, HDCP_2_2_REP_SEND_RECVID_LIST,
				     buf, msg_sz);
	if (ret)
		goto free_buf;

	ret = hdcp2_verify_rep_topology_prepare_ack(hdcp,
						    buf,
						    msg_sz,
						    mV);
	if (ret)
		goto free_buf;

	memcpy(&msgs.recvid_list, buf, msg_sz);

	rx_info = msgs.recvid_list.rx_info;

	if (HDCP_2_2_MAX_CASCADE_EXCEEDED(rx_info[1])) {
		ret = -HDCP2_MAX_CASCADE_ERR;
		goto free_buf;
	}

	if (HDCP_2_2_MAX_DEVS_EXCEEDED(rx_info[1])) {
		ret = -HDCP2_MAX_DEVICE_ERR;
		goto free_buf;
	}

	device_cnt = (HDCP_2_2_DEV_COUNT_HI(rx_info[0]) << 4 |
		      HDCP_2_2_DEV_COUNT_LO(rx_info[1]));
	if (drm_hdcp_check_ksvs_revoked(dev, msgs.recvid_list.receiver_ids,
					device_cnt)) {
		dev_info(hdmi->dev, "Revoked receiver ID(s) is in list");
		ret = -HDCP2_REVOKED_ID_LIST_ERR;
		goto free_buf;
	}

	hdcp->hdcp1_device_downstream = (rx_info[1] >> 0) & 0x1;

	msgs.rep_ack.msg_id = HDCP_2_2_REP_SEND_ACK;
	memcpy(msgs.rep_ack.v, mV, HDCP_2_2_V_PRIME_HALF_LEN);

	ret = hdcp_ops->write_2_2_msg(hdmi, &msgs.rep_ack,
				      sizeof(msgs.rep_ack));

free_buf:
	kfree(buf);
exit:
	return ret;
}

static int
hdcp2_propagate_stream_management_info(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	union {
		struct hdcp2_rep_stream_manage stream_manage;
		struct hdcp2_rep_stream_ready stream_ready;
	} msgs;
	const struct rtk_hdcp_ops *hdcp_ops = hdcp->hdcp_ops;
	int ret, streams_size_delta;
	u8 seq_num_m[HDCP_2_2_SEQ_NUM_LEN];
	u8 streamIDtype[2];
	u8 input[5];

	/* Prepare RepeaterAuth_Stream_Manage msg */
	msgs.stream_manage.msg_id = HDCP_2_2_REP_STREAM_MANAGE;
	drm_hdcp_cpu_to_be24(msgs.stream_manage.seq_num_m, hdcp->seq_num_m);
	memcpy(seq_num_m, msgs.stream_manage.seq_num_m, HDCP_2_2_SEQ_NUM_LEN);

	/* K no of streams is fixed as 1. Stored as big-endian. */
	msgs.stream_manage.k = cpu_to_be16(1);

	/* For HDMI this is forced to be 0x0. For DP SST also this is 0x0. */
	msgs.stream_manage.streams[0].stream_id = 0;

	/*We set type 0 content if there is an HDCP 1.4 downstream device*/
	if (hdcp->hdcp1_device_downstream == 1)
		msgs.stream_manage.streams[0].stream_type =
					DRM_MODE_HDCP_CONTENT_TYPE0;
	else
		msgs.stream_manage.streams[0].stream_type =
					DRM_MODE_HDCP_CONTENT_TYPE1;

	streamIDtype[0] = msgs.stream_manage.streams[0].stream_id;
	streamIDtype[1] = msgs.stream_manage.streams[0].stream_type;

	memcpy(input, streamIDtype, 2);
	memcpy(input+2, seq_num_m, HDCP_2_2_SEQ_NUM_LEN);

	streams_size_delta = (HDCP_2_2_MAX_CONTENT_STREAMS_CNT - 1) *
					sizeof(struct hdcp2_streamid_type);

	/* Send it to Repeater */
	ret = hdcp_ops->write_2_2_msg(hdmi, &msgs.stream_manage,
				      sizeof(msgs.stream_manage) - streams_size_delta);
	if (ret)
		return ret;

	hdcp->seq_num_m++;

	ret = hdcp_ops->read_2_2_msg(hdmi, HDCP_2_2_REP_STREAM_READY,
				     &msgs.stream_ready, sizeof(msgs.stream_ready));
	if (ret)
		return ret;

	ret = hdcp2_verify_mprime(hdcp, &msgs.stream_ready,
				  input, 2+HDCP_2_2_SEQ_NUM_LEN);
	if (ret)
		return ret;

	if (hdcp->seq_num_m > HDCP_2_2_SEQ_NUM_MAX) {
		DRM_DEBUG_DRIVER("seq_num_m roll over");
		return -HDCP2_SEQ_NUM_M_ROLL_OVER_ERR;
	}

	return HDCP_NO_ERR;
}

static int hdcp2_authenticate_repeater(struct rtk_hdmi *hdmi)
{
	int ret;
	unsigned long start_time;
	unsigned long duration_ms;

	ret = hdcp2_authenticate_repeater_topology(hdmi);
	if (ret)
		return ret;

	usleep_range(HDCP2_MSG_DELAY_MS * 1000, (HDCP2_MSG_DELAY_MS + 1) * 1000);

	start_time = jiffies;
	for (;;) {
		ret = hdcp2_propagate_stream_management_info(hdmi);
		if ((ret == HDCP_NO_ERR) || (ret == -HDCP_PLUGOUT_ERR))
			break;

		duration_ms = jiffies_to_msecs(jiffies - start_time);
		if (duration_ms > HDCP2_STREAM_MANAGE_TIMEOUT_MS)
			break;

		usleep_range(HDCP2_MSG_DELAY_MS * 1000, (HDCP2_MSG_DELAY_MS + 1) * 1000);
	}

	return ret;
}

static int hdcp2_authenticate_sink(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	int ret;

	hdcp->mCap[0] = WV_HDCP_NONE;

	DRM_DEBUG_DRIVER("AKE Init");
	ret = hdcp2_authentication_key_exchange(hdmi);
	if (ret) {
		DRM_DEBUG_DRIVER("AKE Init failed, ret=%d", ret);
		return ret;
	}

	DRM_DEBUG_DRIVER("Locality Check");
	ret = hdcp2_locality_check(hdmi);
	if (ret) {
		DRM_DEBUG_DRIVER("Locality Check failed, ret=%d", ret);
		return ret;
	}

	DRM_DEBUG_DRIVER("SKE Init");
	ret = hdcp2_session_key_exchange(hdmi);
	if (ret) {
		DRM_DEBUG_DRIVER("SKE Init failed, ret=%d", ret);
		return ret;
	}

	if (hdcp->is_repeater) {
		DRM_DEBUG_DRIVER("Auth Repeater");
		ret = hdcp2_authenticate_repeater(hdmi);
		if (ret) {
			DRM_DEBUG_DRIVER("Repeater Auth failed, ret=%d", ret);
			return ret;
		}

		if (hdcp->hdcp1_device_downstream == 1)
			hdcp->mCap[0]  = WV_HDCP_V1;
		else
			hdcp->mCap[0] = WV_HDCP_V2_2;
	} else {
		hdcp->mCap[0] = WV_HDCP_V2_2;
	}

	return ret;
}

static int hdcp2_enable_encryption(struct rtk_hdmi *hdmi)
{
	int ret;
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	struct rtk_hdcp2_tee *hdcp2_tee = &hdcp->hdcp2_tee;

	ret = hdcp2_tee->hdcp2_tee_ops->enable_hdcp2_cipher(hdcp2_tee,
							      hdcp->mCap);
	return ret;
}

static void hdcp2_disable_encryption(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	struct rtk_hdcp2_tee *hdcp2_tee = &hdcp->hdcp2_tee;

	if (hdcp2_tee->hdcp2_tee_ops &&
	   hdcp2_tee->hdcp2_tee_ops->disable_hdcp2_cipher)
		hdcp2_tee->hdcp2_tee_ops->disable_hdcp2_cipher(hdcp2_tee);

	/*
	 * Disable hdcp 2.2 cipher encryption will take effect in the next frame,
	 * However, clear HDCP 2.2 cipher setting will take effect immediately,
	 * The result is that use the incorrect cipher parameters to encrypt the current frame
	 */

	/* Make sure clearing hdcp 2.2 cipher setting is in the next frame */
	msleep(50);

	if (hdcp2_tee->hdcp2_tee_ops &&
	   hdcp2_tee->hdcp2_tee_ops->clear_hdcp2_cipher_setting)
		hdcp2_tee->hdcp2_tee_ops->clear_hdcp2_cipher_setting(hdcp2_tee);

	if (hdmi->hpd_state)
		hdcp->mCap[0] = WV_HDCP_NONE;
	else
		hdcp->mCap[0] = WV_HDCP_NO_DIGITAL_OUTPUT;

	if (hdcp2_tee->hdcp2_tee_ops &&
	   hdcp2_tee->hdcp2_tee_ops->update_mCap)
		hdcp2_tee->hdcp2_tee_ops->update_mCap(hdcp2_tee,
						      hdcp->mCap);
}

static int hdcp2_authenticate_and_encrypt(struct rtk_hdmi *hdmi)
{
	int ret;
	int i;
	int hpd;

	for (i = 0; i < HDCP2_AUTH_RETRY_CNT; i++) {
		ret = hdcp2_authenticate_sink(hdmi);
		if (!ret)
			break;

		/* Clearing the mei hdcp session */
		DRM_DEBUG_DRIVER("HDCP2 Auth %d of %d failed, ret=%d",
				i + 1, HDCP2_AUTH_RETRY_CNT, ret);

		if (ret == -HDCP_PLUGOUT_ERR)
			return ret;
	}

	if (i != HDCP2_AUTH_RETRY_CNT) {
		/*
		 * Ensuring the required 200mSec min time interval between
		 * Session Key Exchange and encryption. During this period,
		 * we also detect if there is any plugout events.
		 */
		for (i = 0; i < 20; i++) {
			hpd = gpiod_get_value(hdmi->hpd_gpio);
			if (!hpd)
				return -HDCP_PLUGOUT_ERR;
			msleep(HDCP_2_2_DELAY_BEFORE_ENCRYPTION_EN/20);
		}

		ret = hdcp2_enable_encryption(hdmi);
	}

	return ret;
}

static int rtk_hdcp_poll_ksv_fifo(struct rtk_hdmi *hdmi,
				  const struct rtk_hdcp_ops *hdcp_ops)
{
	int ret, read_ret;
	bool ksv_ready = false;

	/* Poll for ksv list ready (spec says max time allowed is 5s) */
	ret = __wait_for(read_ret = hdcp_ops->read_ksv_ready(hdmi, &ksv_ready),
				 read_ret == -HDCP_PLUGOUT_ERR,
				 read_ret || ksv_ready, 5 * 1000 * 1000, 1000,
				 100 * 1000);
	if (ret == -HDCP_PLUGOUT_ERR)
		return ret;
	if (read_ret)
		return read_ret;
	if (!ksv_ready)
		read_ret = -HDCP1_KSVLIST_TIMEOUT_ERR;

	return read_ret;
}

static int
rtk_hdcp_sha_append_bstatus_m0(struct rtk_hdcp *hdcp, u8 *ksv_fifo,
			       int *byte_cnt, u8 *bstatus)
{
	int ret;
	struct rtk_hdcp1_tee *hdcp1_tee = &hdcp->hdcp1_tee;

	ret = hdcp1_tee->hdcp1_tee_ops->sha_append_bstatus_m0(hdcp1_tee,
					ksv_fifo, byte_cnt, bstatus);

	return ret;
}

static int
rtk_hdcp_compute_and_validate_V(struct rtk_hdcp *hdcp, u8 *ksv_fifo,
				int *byte_cnt, u8 *vprime)
{
	int ret;
	struct rtk_hdcp1_tee *hdcp1_tee = &hdcp->hdcp1_tee;

	DRM_DEBUG_DRIVER("Compute V");

	ret = hdcp1_tee->hdcp1_tee_ops->compute_V(hdcp1_tee, ksv_fifo, byte_cnt);
	if (ret)
		return ret;

	DRM_DEBUG_DRIVER("Verify V");

	ret = hdcp1_tee->hdcp1_tee_ops->verify_V(hdcp1_tee, vprime);
	if (ret)
		return ret;

	DRM_DEBUG_DRIVER("Verify V Pass");

	return ret;
}

static int rtk_hdcp_auth_downstream(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	struct drm_device *dev = hdmi->connector.dev;
	const struct rtk_hdcp_ops *hdcp_ops = hdcp->hdcp_ops;
	u8 bstatus[DRM_HDCP_BSTATUS_LEN];
	int num_downstream, byte_cnt;
	u8 ksv_fifo[MAX_SHA_DATA_SIZE];
	u32 vprime_part;
	u8 vprime[MAX_SHA_VPRIME_SIZE];
	int ret = -1, i;

	DRM_DEBUG_DRIVER("Wait KSV list ready");
	ret = rtk_hdcp_poll_ksv_fifo(hdmi, hdcp_ops);
	if (ret) {
		DRM_DEBUG_DRIVER("KSV list failed to become ready, ret=%d", ret);
		return ret;
	}

	ret = hdcp_ops->read_bstatus(hdmi, bstatus);
	if (ret)
		return ret;

	if (DRM_HDCP_MAX_DEVICE_EXCEEDED(bstatus[0])) {
		DRM_DEBUG_DRIVER("Max Topology Limit Exceeded, bstatus[0][1]=0x%x 0x%x",
				bstatus[0], bstatus[1]);
		return -HDCP1_MAX_CASCADE_ERR;
	}

	if (DRM_HDCP_MAX_CASCADE_EXCEEDED(bstatus[1])) {
		DRM_DEBUG_DRIVER("Max Topology Limit Exceeded, bstatus[0][1]=0x%x 0x%x",
				bstatus[0], bstatus[1]);
		return -HDCP1_MAX_DEVICE_ERR;
	}

	DRM_DEBUG_DRIVER("Read KSV list");

	memset(ksv_fifo, 0, MAX_SHA_DATA_SIZE);

	num_downstream = DRM_HDCP_NUM_DOWNSTREAM(bstatus[0]);
	byte_cnt = num_downstream * DRM_HDCP_KSV_LEN;

	if (num_downstream) {
		ret = hdcp_ops->read_ksv_fifo(hdmi, num_downstream, ksv_fifo);
		if (ret)
			return ret;
	}

	if (num_downstream &&
	    drm_hdcp_check_ksvs_revoked(dev, ksv_fifo, num_downstream)) {
		DRM_DEBUG_DRIVER("Revoked Ksv(s) in ksv_fifo");
		return -HDCP1_REVOKED_KSVLIST_ERR;
	}

	/* Read and add Bstatus */
	ret = rtk_hdcp_sha_append_bstatus_m0(hdcp, ksv_fifo, &byte_cnt, bstatus);
	if (ret)
		return ret;

	DRM_DEBUG_DRIVER("Read V prime");
	memset(vprime, 0, MAX_SHA_VPRIME_SIZE);
	for (i = 0; i < DRM_HDCP_V_PRIME_NUM_PARTS; i++) {
		ret = hdcp_ops->read_v_prime_part(hdmi, i, &vprime_part);
		if (ret)
			return ret;
		memcpy(vprime + i*DRM_HDCP_V_PRIME_PART_LEN,
			&vprime_part, DRM_HDCP_V_PRIME_PART_LEN);
	}

	ret = rtk_hdcp_compute_and_validate_V(hdcp, ksv_fifo,
					      &byte_cnt, vprime);
	if (ret == HDCP_NO_ERR)
		DRM_DEBUG_DRIVER("%d downstream devices is verified",
			num_downstream);

	return ret;
}

/* Implements Part 1 of the HDCP authorization procedure */
static int rtk_hdcp_auth(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	struct drm_device *dev = hdmi->connector.dev;
	const struct rtk_hdcp_ops *hdcp_ops = hdcp->hdcp_ops;
	struct rtk_hdcp1_tee *hdcp1_tee = &hdcp->hdcp1_tee;
	const struct rtk_hdcp1_tee_ops *hdcp1_tee_ops = hdcp1_tee->hdcp1_tee_ops;
	int ret;
	int hpd;
	u8 an[DRM_HDCP_AN_LEN] = {0};
	u8 aksv[DRM_HDCP_KSV_LEN] = {0};
	u8 bksv[DRM_HDCP_KSV_LEN] = {0};
	u8 ri_prime[DRM_HDCP_RI_LEN] = {0};
	unsigned long r0_prime_gen_start;
	unsigned int wait_ms;

	/* Generate An */
	DRM_DEBUG_DRIVER("Generate An");
	ret = hdcp1_tee_ops->generate_an(hdcp1_tee, an);
	if (ret)
		return ret;

	DRM_DEBUG_DRIVER("Read Aksv");
	ret = hdcp1_tee_ops->read_aksv(hdcp1_tee, aksv);
	if (ret)
		return ret;

	if (!rtk_hdcp_is_ksv_valid(aksv))
		return -HDCP1_INVALID_AKSV_ERR;

	DRM_DEBUG_DRIVER("Aksv %02x %02x %02x %02x %02x",
			aksv[0], aksv[1], aksv[2], aksv[3], aksv[4]);
	ret = hdcp_ops->write_an_aksv(hdmi, an, aksv);
	if (ret)
		return ret;

	r0_prime_gen_start = jiffies;

	memset(bksv, 0, DRM_HDCP_KSV_LEN);
	DRM_DEBUG_DRIVER("Read Bksv");
	ret = rtk_hdcp_read_valid_bksv(hdmi, hdcp_ops, bksv);
	if (ret)
		return ret;

	if (drm_hdcp_check_ksvs_revoked(dev, bksv, 1)) {
		DRM_DEBUG_DRIVER("BKSV is revoked");
		return -HDCP1_REVOKED_BKSV_ERR;
	}

	ret = hdcp_ops->repeater_present(hdmi, &hdcp->is_repeater);
	if (ret)
		return ret;

	ret = hdcp1_tee_ops->set_hdcp1_repeater_bit(hdcp1_tee,
			hdcp->is_repeater ? 1:0);
	if (ret)
		return ret;

	DRM_DEBUG_DRIVER("Generate Km");
	ret = hdcp1_tee_ops->write_bksv(hdcp1_tee, bksv);
	if (ret)
		return ret;

	/*
	 * Wait for R0' to become available. The spec says 100ms from Aksv, but
	 * some monitors can take longer than this. We'll set the timeout at
	 * 300ms just to be sure.
	 *
	 */
	for (;;) {
		hpd = gpiod_get_value(hdmi->hpd_gpio);
		if (hpd == 0)
			return -HDCP_PLUGOUT_ERR;

		wait_ms = jiffies_to_msecs(jiffies - r0_prime_gen_start);
		if (wait_ms > 300)
			break;

		usleep_range(10 * 1000, 15 * 1000);
	}

	DRM_DEBUG_DRIVER("%u ms, read r0'", wait_ms);
	ret = hdcp_ops->read_ri_prime(hdmi, ri_prime);
	if (ret)
		return ret;

	DRM_DEBUG_DRIVER("Check r0'");
	ret = hdcp1_tee_ops->check_ri_prime(hdcp1_tee, ri_prime);
	if (ret)
		return ret;

	if (hdcp->is_repeater) {
		DRM_DEBUG_DRIVER("Auth Repeater");
		ret = rtk_hdcp_auth_downstream(hdmi);
		if (ret)
			return ret;
	}

	ret = hdcp1_tee_ops->set_wider_window(hdcp1_tee);
	if (ret)
		return ret;

	DRM_DEBUG_DRIVER("Enable hdcp 1.4 cipher");
	ret = hdcp1_tee_ops->hdcp1_set_encryption(hdcp1_tee, HDCP_ENC_ON);

	return ret;
}

static void _rtk_hdcp_disable(struct rtk_hdmi *hdmi)
{
	int ret;
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	struct rtk_hdcp1_tee *hdcp1_tee = &hdcp->hdcp1_tee;
	const struct rtk_hdcp1_tee_ops *hdcp1_tee_ops = hdcp1_tee->hdcp1_tee_ops;

	DRM_DEBUG_DRIVER("HDCP1.4 is being disabled");

	ret = hdcp1_tee_ops->hdcp1_set_encryption(hdcp1_tee, HDCP_ENC_OFF);

	if (ret)
		DRM_DEBUG_DRIVER("Disable HDCP1.4 failed, ret=%d", ret);
	else
		DRM_DEBUG_DRIVER("HDCP1.4 is disabled");

	hdcp->hdcp_encrypted = false;
}

static int _rtk_hdcp_enable(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	struct rtk_hdcp1_tee *hdcp1_tee = &hdcp->hdcp1_tee;
	int ret;

	DRM_DEBUG_DRIVER("HDCP1.4 is being enabled");

	ret = hdcp1_tee->hdcp1_tee_ops->hdcp1_tee_api_init(hdcp1_tee);
	if (ret)
		goto err_exit;

	ret = rtk_hdcp_auth(hdmi);
	if (!ret) {
		hdcp->hdcp_encrypted = true;
		rtk_hdcp_set_state(hdmi, HDCP_1_SUCCESS, HDCP_NO_ERR);
		DRM_DEBUG_DRIVER("HDCP1.4 is enabled");
		return HDCP_NO_ERR;
	}

	DRM_DEBUG_DRIVER("HDCP Auth failure, ret=%d", ret);

	/* Ensuring HDCP encryption and signalling are stopped */
	_rtk_hdcp_disable(hdmi);

err_exit:
	rtk_hdcp_set_state(hdmi, HDCP_1_FAILURE, -ret);
	DRM_DEBUG_DRIVER("HDCP1.4 authentication failed, ret=%d", ret);

	return ret;
}

static int _rtk_hdcp2_enable(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	int ret;

	DRM_DEBUG_DRIVER("HDCP2 authentication start");

	ret = hdcp2_authenticate_and_encrypt(hdmi);
	if (ret) {
		rtk_hdcp_set_state(hdmi, HDCP_2_FAILURE, -ret);
		DRM_DEBUG_DRIVER("HDCP2 authentication failed, ret=%d", ret);
		return ret;
	}

	DRM_DEBUG_DRIVER("HDCP2 authentication done, cipher is enabled");

	hdcp->hdcp2_encrypted = true;
	rtk_hdcp_set_state(hdmi, HDCP_2_SUCCESS, HDCP_NO_ERR);
	return 0;
}

static void _rtk_hdcp2_disable(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;

	DRM_DEBUG_DRIVER("HDCP2 is being disabled");

	hdcp2_disable_encryption(hdmi);

	DRM_DEBUG_DRIVER("HDCP2 is disabled");

	hdcp->hdcp2_encrypted = false;
}

void rtk_hdcp_update_cap(struct rtk_hdmi *hdmi, u8 mCap)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	struct rtk_hdcp2_tee *hdcp2_tee = &hdcp->hdcp2_tee;
	bool capable;

	/*mCap is only available for HDCP 2.2*/
	if (hdcp->hdcp2_supported) {
		mutex_lock(&hdcp->mutex);

		hdcp->mCap[0] = mCap;

		if (hdcp2_tee->hdcp2_tee_ops &&
		   hdcp2_tee->hdcp2_tee_ops->update_mCap)
			hdcp2_tee->hdcp2_tee_ops->update_mCap(hdcp2_tee,
							      hdcp->mCap);

		mutex_unlock(&hdcp->mutex);
	}

	if (mCap == WV_HDCP_NONE) {
		mutex_lock(&hdcp->mutex);
		hdcp->hdcp_ops->hdcp2_capable(hdmi, &capable);
		mutex_unlock(&hdcp->mutex);
		if (capable) {
			hdmi->hdcp.sink_hdcp_ver = HDCP_2x;
			return;
		}

		mutex_lock(&hdcp->mutex);
		hdcp->hdcp_ops->hdcp_capable(hdmi, &capable);
		mutex_unlock(&hdcp->mutex);
		if (capable) {
			hdmi->hdcp.sink_hdcp_ver = HDCP_1x;
			return;
		}

		hdmi->hdcp.sink_hdcp_ver = HDCP_NONE;
	} else if (mCap == WV_HDCP_NO_DIGITAL_OUTPUT) {
		hdmi->hdcp.sink_hdcp_ver = HDCP_NONE;
	}
}

static int rtk_hdcp_check_link(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	const struct rtk_hdcp_ops *hdcp_ops = hdcp->hdcp_ops;
	int ret = -1;

	mutex_lock(&hdcp->mutex);

	/* Check_link valid only when HDCP1.4 is enabled */
	if (hdcp->value != DRM_MODE_CONTENT_PROTECTION_ENABLED ||
	    !hdcp->hdcp_encrypted) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = hdcp_ops->check_link(hdmi);
	if (ret == HDCP_NO_ERR) {
		DRM_DEBUG_DRIVER("HDCP1.4 integirty check ri = ri'");

		goto unlock;
	}

	_rtk_hdcp_disable(hdmi);
	hdcp->value = DRM_MODE_CONTENT_PROTECTION_DESIRED;

	rtk_hdcp_set_state(hdmi, HDCP_1_FAILURE, -ret);
	DRM_DEBUG_DRIVER("HDCP1.4 link failed, ret=%d", ret);

unlock:
	mutex_unlock(&hdcp->mutex);

	if (ret != HDCP_NO_ERR)
		schedule_work(&hdcp->prop_work);

	return ret;
}

/* Implements the Link Integrity Check for HDCP2.2 */
static int rtk_hdcp2_check_link(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	const struct rtk_hdcp_ops *hdcp_ops = hdcp->hdcp_ops;
	struct rtk_hdcp2_tee *hdcp2_tee = &hdcp->hdcp2_tee;
	int ret = 0;

	mutex_lock(&hdcp->mutex);

	/* hdcp2_check_link is expected only when HDCP2.2 is Enabled */
	if (hdcp->value != DRM_MODE_CONTENT_PROTECTION_ENABLED ||
	    !hdcp->hdcp2_encrypted) {
		ret = -EINVAL;
		goto unlock;
	}

	if (hdcp_ops && hdcp_ops->check_2_2_link)
		ret = hdcp_ops->check_2_2_link(hdmi);

	if (ret == HDCP_NO_ERR) {
		DRM_DEBUG_DRIVER("HDCP2 polling rx status");
		goto unlock;
	}

	if (ret == -HDCP2_TOPOLOGY_CHANGE_ERR) {
		DRM_DEBUG_DRIVER("HDCP2 Downstream topology change");
		ret = hdcp2_authenticate_repeater_topology(hdmi);
		if (!ret) {
			if (hdcp->hdcp1_device_downstream == 1)
				hdcp->mCap[0]  = WV_HDCP_V1;
			else
				hdcp->mCap[0] = WV_HDCP_V2_2;

			if (hdcp2_tee->hdcp2_tee_ops &&
			   hdcp2_tee->hdcp2_tee_ops->update_mCap)
				hdcp2_tee->hdcp2_tee_ops->update_mCap(hdcp2_tee,
								      hdcp->mCap);

			goto unlock;
		}
		DRM_DEBUG_DRIVER("HDCP2 Repeater topology auth failed, ret=%d", ret);
	}

	_rtk_hdcp2_disable(hdmi);
	hdcp->value = DRM_MODE_CONTENT_PROTECTION_DESIRED;

	rtk_hdcp_set_state(hdmi, HDCP_2_FAILURE, -ret);
	DRM_DEBUG_DRIVER("HDCP2 link failed, ret=%d", ret);

unlock:
	mutex_unlock(&hdcp->mutex);

	if (ret != HDCP_NO_ERR)
		schedule_work(&hdcp->prop_work);

	return ret;
}

static void rtk_hdcp_wait_ced_stable(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	const struct rtk_hdcp_ops *hdcp_ops = hdcp->hdcp_ops;
	unsigned long start_time;
	unsigned int wait_ms;
	unsigned int stable_count;
	int ret;

	start_time = jiffies;
	msleep(HDCP_CED_CHECK_MIN_MS);

	stable_count = 0;
	while (stable_count < HDCP_CED_PASS_COND) {
		ret = hdcp_ops->check_ced_error(hdmi);

		if ((ret == -HDCP_PLUGOUT_ERR) ||
			(ret == -HDCP_CED_NOTSUPPORT) ||
			(ret ==  -HDCP_DDC_RD_TRANS_ERR))
			break;

		if (ret == HDCP_NO_ERR)
			stable_count++;
		else
			stable_count = 0;

		wait_ms = jiffies_to_msecs(jiffies - start_time);
		if ((wait_ms > HDCP_CED_CHECK_MAX_MS) ||
			(hdcp->do_cancel_hdcp))
			break;

		usleep_range(HDCP_CED_SLEEP_MS * 1000, (HDCP_CED_SLEEP_MS + 1) * 1000);
	}

	if (ret)
		dev_info(hdmi->dev, "CED result ret=%d", ret);
	else
		dev_info(hdmi->dev, "CED check consume %ums", wait_ms);
}

int rtk_hdcp_enable(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	const struct rtk_hdcp_ops *hdcp_ops = hdcp->hdcp_ops;
	unsigned long check_link_interval = DRM_HDCP_CHECK_PERIOD_MS;
	int ret = -EINVAL;
	bool capable = false;

	BUG_ON(!hdcp || !hdcp_ops);

	dev_info(hdmi->dev, "enable hdcp, hdcp2_supported=%s force_hdcp14=%s",
		hdcp->hdcp2_supported ? "Y":"N",
		hdcp->force_hdcp14 ? "Y":"N");

	rtk_hdcp_wait_ced_stable(hdmi);

	mutex_lock(&hdcp->mutex);
	WARN_ON(hdcp->value == DRM_MODE_CONTENT_PROTECTION_ENABLED);

	if (hdcp->do_cancel_hdcp) {
		ret = -HDCP_CANCELED_ERR;
		goto unlock;
	}

	/*
	 * Considering that HDCP2.2 is more secure than HDCP1.4, If the setup
	 * is capable of HDCP2.2, it is preferred to use HDCP2.2.
	 */
	if (hdcp->hdcp2_supported && !hdcp->force_hdcp14) {
		struct rtk_hdcp2_tee *hdcp2_tee = &hdcp->hdcp2_tee;

		ret = hdcp_ops->hdcp2_capable(hdmi, &capable);
		if (ret)
			dev_info(hdmi->dev, "sink device doesn't support hdcp2, ret=%d", ret);

		if (!hdcp2_tee->init_hdcp2_ta_flag) {
			ret = hdcp2_tee->hdcp2_tee_ops->hdcp2_tee_api_init(hdcp2_tee);
			if (ret) {
				dev_err(hdmi->dev, "hdcp2_tee init failed, ret=%d", ret);
				goto hdcp14;
			}

			ret = hdcp2_tee->hdcp2_tee_ops->read_hdcp2_key(hdcp2_tee);
			if (ret) {
				dev_err(hdmi->dev, "hdcp2 key doesn't exist");
				goto hdcp14;
			}
		}
	}

	if (hdcp->do_cancel_hdcp) {
		ret = -HDCP_CANCELED_ERR;
		goto unlock;
	}

	if (capable) {
		hdcp->sink_hdcp_ver = HDCP_2x;

		rtk_hdcp_set_state(hdmi, HDCP_2_IN_AUTH, HDCP_NO_ERR);
		ret = _rtk_hdcp2_enable(hdmi);
		if (ret == -HDCP_PLUGOUT_ERR)
			goto unlock;

		if (ret == HDCP_NO_ERR) {
			check_link_interval = DRM_HDCP2_CHECK_PERIOD_MS;
			goto unlock;
		}
	}

hdcp14:
	if (hdcp->do_cancel_hdcp) {
		ret = -HDCP_CANCELED_ERR;
		goto unlock;
	}

	/*
	 * When HDCP2.2 fails, HDCP1.4 will be attempted.
	 */
	ret = hdcp_ops->hdcp_capable(hdmi, &capable);

	if (ret == HDCP_NO_ERR) {
		if (hdcp->sink_hdcp_ver != HDCP_2x)
			hdcp->sink_hdcp_ver = HDCP_1x;

		rtk_hdcp_set_state(hdmi, HDCP_1_IN_AUTH, HDCP_NO_ERR);
		ret = _rtk_hdcp_enable(hdmi);
	} else {
		rtk_hdcp_set_state(hdmi, HDCP_1_FAILURE, -ret);
	}

unlock:
	if (ret == HDCP_NO_ERR)
		hdcp->value = DRM_MODE_CONTENT_PROTECTION_ENABLED;

	mutex_unlock(&hdcp->mutex);

	if (hdcp->do_cancel_hdcp) {
		dev_info(hdmi->dev, "Cancel hdcp authentication");
		ret = -HDCP_CANCELED_ERR;
		return ret;
	}

	if (ret == HDCP_NO_ERR) {
		schedule_delayed_work(&hdcp->check_work,
			msecs_to_jiffies(check_link_interval));
		schedule_work(&hdcp->prop_work);
	} else if ((ret != -HDCP_PLUGOUT_ERR) &&
			(ret != -HDCP2_TEE_READ_KEY_ERR) &&
			(ret != -HDCP1_INVALID_AKSV_ERR) &&
			(hdcp->value == DRM_MODE_CONTENT_PROTECTION_DESIRED)) {
		schedule_delayed_work(&hdmi->hdcp.commit_work, msecs_to_jiffies(100));
	}

	return ret;
}

int rtk_hdcp_disable(struct rtk_hdmi *hdmi)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;

	if (!hdcp || !hdcp->hdcp_ops)
		return -ENOENT;

	dev_info(hdmi->dev, "disable hdcp, current hdcp2_encrypted=%s hdcp_encrypted=%s",
				hdcp->hdcp2_encrypted ? "Y" : "N",
				hdcp->hdcp_encrypted ? "Y" : "N");

	hdcp->do_cancel_hdcp = true;

	cancel_delayed_work_sync(&hdcp->check_work);

	mutex_lock(&hdcp->mutex);

	if (hdcp->hdcp2_encrypted)
		_rtk_hdcp2_disable(hdmi);
	else if (hdcp->hdcp_encrypted)
		_rtk_hdcp_disable(hdmi);

	if (hdcp->value != DRM_MODE_CONTENT_PROTECTION_UNDESIRED)
		hdcp->value = DRM_MODE_CONTENT_PROTECTION_DESIRED;

	mutex_unlock(&hdcp->mutex);

	hdcp->do_cancel_hdcp = false;
	rtk_hdcp_set_state(hdmi, HDCP_UNAUTH, HDCP_NO_ERR);

	return 0;
}

static void rtk_hdcp_commit_work(struct work_struct *work)
{
	struct rtk_hdcp *hdcp = container_of(to_delayed_work(work),
					     struct rtk_hdcp,
					     commit_work);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);

	DRM_DEBUG_DRIVER("enter");

	if (hdmi->hdcp.do_cancel_hdcp) {
		dev_info(hdmi->dev, "do_cancel_hdcp=Y, cancel hdcp_commit_work");
		return;
	}

	if (hdcp->value == DRM_MODE_CONTENT_PROTECTION_DESIRED) {
		if (hdcp->hdcp2_encrypted || hdcp->hdcp_encrypted)
			rtk_hdcp_disable(hdmi);

		if (hdmi->is_hdmi_on)
			rtk_hdcp_enable(hdmi);
		else
			dev_info(hdmi->dev, "Skip enable hdcp when hdmi is off");
	} else if (hdcp->value == DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
		rtk_hdcp_disable(hdmi);
	}

	DRM_DEBUG_DRIVER("exit, hdcp->value=%llu", hdcp->value);
}

static void rtk_hdcp_check_work(struct work_struct *work)
{
	int ret;
	struct rtk_hdcp *hdcp = container_of(to_delayed_work(work),
					     struct rtk_hdcp,
					     check_work);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);

	if (hdcp->hdcp2_encrypted) {
		ret = rtk_hdcp2_check_link(hdmi);
		if (!ret)
			schedule_delayed_work(&hdcp->check_work,
				      msecs_to_jiffies(DRM_HDCP2_CHECK_PERIOD_MS));
	} else if (hdcp->hdcp_encrypted) {
		ret = rtk_hdcp_check_link(hdmi);
		if (!ret)
			schedule_delayed_work(&hdcp->check_work,
				      msecs_to_jiffies(DRM_HDCP_CHECK_PERIOD_MS));
	}

	if ((!hdcp->hdcp2_encrypted) && (!hdcp->hdcp_encrypted) &&
		(hdcp->value == DRM_MODE_CONTENT_PROTECTION_DESIRED))
		schedule_delayed_work(&hdcp->commit_work, msecs_to_jiffies(100));

}

static void rtk_hdcp_prop_work(struct work_struct *work)
{
	struct rtk_hdcp *hdcp = container_of(work, struct rtk_hdcp,
					     prop_work);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct drm_device *dev = hdmi->connector.dev;

	DRM_DEBUG_DRIVER("drm_modeset_lock");
	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	mutex_lock(&hdcp->mutex);

	/*
	 * This worker is only used to flip between ENABLED/DESIRED. Either of
	 * those to UNDESIRED is handled by core. If value == UNDESIRED,
	 * we're running just after hdcp has been disabled, so just exit
	 */
	if (hdcp->value != DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
		DRM_DEBUG_DRIVER("Update content_protection=%u", (u32)hdcp->value);
		drm_hdcp_update_content_protection(&hdmi->connector,
						   hdcp->value);
	}

	mutex_unlock(&hdcp->mutex);
	drm_modeset_unlock(&dev->mode_config.connection_mutex);
	DRM_DEBUG_DRIVER("drm_modeset_unlock");
}

void rtk_hdcp_commit_state(struct drm_connector *connector,
		unsigned int old_cp, unsigned int new_cp)
{
	struct rtk_hdmi *hdmi;

	if (old_cp == new_cp)
		return;

	hdmi = to_rtk_hdmi(connector);

	if ((new_cp == DRM_MODE_CONTENT_PROTECTION_UNDESIRED) ||
		((old_cp == DRM_MODE_CONTENT_PROTECTION_UNDESIRED) &&
		(new_cp == DRM_MODE_CONTENT_PROTECTION_DESIRED))) {
		dev_info(hdmi->dev, "Change content_protection %u -> %u",
			old_cp, new_cp);
		hdmi->hdcp.value = new_cp;
		schedule_delayed_work(&hdmi->hdcp.commit_work, 0);
	}
}

ssize_t hdcp_info_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	ssize_t ret_count = 0;
	struct rtk_hdmi *hdmi = dev_get_drvdata(dev);

	ret_count += sprintf(buf + ret_count, "sink_hdcp_ver: %d\n",
			hdmi->hdcp.sink_hdcp_ver);
	ret_count += sprintf(buf + ret_count, "hdcp2_supported: %.4s\n",
			hdmi->hdcp.hdcp2_supported ? "yes":"no");
	ret_count += sprintf(buf + ret_count, "hdcp_encrypted: %.4s\n",
			hdmi->hdcp.hdcp_encrypted ? "yes":"no");
	ret_count += sprintf(buf + ret_count, "hdcp2_encrypted: %.4s\n",
			hdmi->hdcp.hdcp2_encrypted ? "yes":"no");
	ret_count += sprintf(buf + ret_count, "is_repeater: %.4s\n",
			hdmi->hdcp.is_repeater ? "yes":"no");
	ret_count += sprintf(buf + ret_count, "hdcp1_device_downstream: %.4s\n",
			hdmi->hdcp.hdcp1_device_downstream ? "yes":"no");

	return ret_count;
}

ssize_t hdcp_state_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	ssize_t ret_count = 0;
	struct rtk_hdmi *hdmi = dev_get_drvdata(dev);

	ret_count += sprintf(buf + ret_count, "STATE=%.20s\n",
				hdcp_state_get_name(hdmi->hdcp.hdcp_state));

	return ret_count;
}

static DEVICE_ATTR_RO(hdcp_info);
static DEVICE_ATTR_RO(hdcp_state);

int rtk_hdcp_check_hdcp_ops(const struct rtk_hdcp_ops *hdcp_ops)
{
	if (!hdcp_ops)
		return -EINVAL;

	if (!hdcp_ops->hdcp_capable ||
		!hdcp_ops->write_an_aksv ||
		!hdcp_ops->read_bksv ||
		!hdcp_ops->read_bstatus ||
		!hdcp_ops->repeater_present ||
		!hdcp_ops->read_ri_prime ||
		!hdcp_ops->read_ksv_ready ||
		!hdcp_ops->read_ksv_fifo ||
		!hdcp_ops->read_v_prime_part ||
		!hdcp_ops->check_link ||
		!hdcp_ops->set_rekey_win ||
		!hdcp_ops->hdcp2_capable ||
		!hdcp_ops->write_2_2_msg ||
		!hdcp_ops->read_2_2_msg ||
		!hdcp_ops->check_2_2_link ||
		!hdcp_ops->check_ced_error)
		return -EFAULT;

	return 0;
}

int rtk_hdcp_init(struct rtk_hdmi *hdmi, u32 hdcp_support)
{
	struct rtk_hdcp *hdcp = &hdmi->hdcp;
	struct rtk_hdcp2_tee *hdcp2_tee = &hdcp->hdcp2_tee;
	struct rtk_hdcp1_tee *hdcp1_tee = &hdcp->hdcp1_tee;
	int hpd;
	bool capable = false;
	int ret;

	if (hdcp_support >= 2)
		hdcp->hdcp2_supported = true;
	else
		hdcp->hdcp2_supported = false;

	hdcp->do_cancel_hdcp = false;
	hdcp->hdcp_encrypted = false;
	hdcp->hdcp2_encrypted = false;
	hdcp->hdcp_err_code = HDCP_NO_ERR;

	hdcp->hdcp_ops = &rtk_hdmi_hdcp_ops;
	ret = rtk_hdcp_check_hdcp_ops(hdcp->hdcp_ops);
	if (ret)
		return ret;

	ret = rtk_hdcp1_tee_init(hdcp1_tee);
	if (ret)
		return ret;

	if (hdcp->hdcp2_supported) {
		ret = rtk_hdcp2_tee_init(hdcp2_tee);
		if (ret)
			return ret;

		ret = hdcp2_tee->hdcp2_tee_ops->hdcp2_tee_api_init(hdcp2_tee);
		if (ret)
			return -EFAULT;

		ret = hdcp2_tee->hdcp2_tee_ops->read_hdcp2_key(hdcp2_tee);
		if (ret) {
			dev_err(hdmi->dev, "hdcp2 key doesn't exist, dis support hdcp2");
			hdcp->hdcp2_supported = 0;
		}
	}

	hdcp->value = DRM_MODE_CONTENT_PROTECTION_UNDESIRED;
	ret = drm_connector_attach_content_protection_property(&hdmi->connector,
							       hdcp->hdcp2_supported);
	if (ret) {
		hdcp->hdcp2_supported = false;
		return ret;
	}

	hdcp->force_hdcp14 = 0;
	hdcp->force_hdcp14_property =
		drm_property_create_range(hdmi->drm_dev, 0, "force_hdcp14", 0, 1);
	if (!hdcp->force_hdcp14_property) {
		dev_err(hdmi->dev, "create force_hdcp14_property failed");
		return -ENOMEM;
	}
	drm_object_attach_property(&hdmi->connector.base,
		hdcp->force_hdcp14_property, hdcp->force_hdcp14);

	mutex_init(&hdcp->mutex);
	INIT_DELAYED_WORK(&hdcp->commit_work, rtk_hdcp_commit_work);
	INIT_DELAYED_WORK(&hdcp->check_work, rtk_hdcp_check_work);
	INIT_WORK(&hdcp->prop_work, rtk_hdcp_prop_work);

	hpd = gpiod_get_value(hdmi->hpd_gpio);
	if (hpd) {
		hdcp->hdcp_state = HDCP_UNAUTH;
		hdcp->hdcp_ops->hdcp2_capable(hdmi, &capable);
		if (capable) {
			hdcp->sink_hdcp_ver = HDCP_2x;
		} else {
			hdcp->hdcp_ops->hdcp_capable(hdmi, &capable);
			hdcp->sink_hdcp_ver = capable ? HDCP_1x : HDCP_NONE;
		}
	} else {
		hdcp->sink_hdcp_ver = HDCP_NONE;
		hdcp->hdcp_state = HDCP_HDMI_DISCONNECT;

		if (hdcp->hdcp2_supported) {
			hdcp->mCap[0] = WV_HDCP_NO_DIGITAL_OUTPUT;
			hdcp2_tee->hdcp2_tee_ops->update_mCap(hdcp2_tee, hdcp->mCap);
		}
	}

	ret = device_create_file(hdmi->dev, &dev_attr_hdcp_info);
	if (ret) {
		dev_err(hdmi->dev, "create hdcp_info sysfs failed");
		return -ENOMEM;
	}
	ret = device_create_file(hdmi->dev, &dev_attr_hdcp_state);
	if (ret) {
		dev_err(hdmi->dev, "create hdcp_state sysfs failed");
		return -ENOMEM;
	}

	return 0;
}

