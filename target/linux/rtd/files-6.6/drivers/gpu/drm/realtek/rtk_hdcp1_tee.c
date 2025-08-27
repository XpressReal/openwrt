// SPDX-License-Identifier: GPL-2.0-only
/*
 * Realtek HDCP 1.4 CA driver
 *
 * Copyright (c) 2022 Realtek Semiconductor Corp.
 */
#include <linux/uuid.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "rtk_hdmi.h"
#include "rtk_hdcp.h"

#define TEEC_SUCCESS            0x0
#define TEE_ERROR_NOT_SUPPORTED           0xFFFF000A
#define TEE_IOCTL_UUID_LEN      16

#define MAX_TEE_SHA_DATA_SIZE       645
#define MAX_TEE_SHA_VPRIME_SIZE     20
#define MAX_TA_INIT_RETRY_CNT       2

static const uuid_t ta_hdcptx14_uuid = UUID_INIT(0x87ef28e8, 0xf581, 0x4e3d,
						 0xb2, 0xb2, 0xd7, 0xe3, 0xd4, 0x8b, 0x23, 0x21);

enum HDCP14_CMD_FOR_TA {
	TA_TEE_HDCP14_GenAn                     = 0x1,
	TA_TEE_HDCP14_WriteBKSV                 = 0x2,
	TA_TEE_HDCP14_SetRepeaterBitInTx        = 0x3,
	TA_TEE_HDCP14_CheckRepeaterBitInTx      = 0x4,
	TA_TEE_HDCP14_SetEnc                    = 0x5,
	TA_TEE_HDCP14_SetWinderWin              = 0x6,
	TA_TEE_HDCP14_EnRi                      = 0x7,
	TA_TEE_HDCP14_SetAVMute                 = 0x8,
	TA_TEE_HDCP14_SHAAppend                 = 0x9,
	TA_TEE_HDCP14_ComputeV                  = 0xa,
	TA_TEE_HDCP14_VerifyV                   = 0xb,
	TA_TEE_HDCP14_CheckR0                   = 0xc,
	TA_TEE_HDCP14_GetAKSV                   = 0xd,
	TA_TEE_HDCP14_GetCtrlState              = 0xe,
	TA_TEE_HDCP14_SetParamKey               = 0xf,
	TA_TEE_HDCP14_Fix480P                   = 0x10,
	TA_TEE_HDCP14_SetKeepoutwin             = 0x11,
	TA_TEE_HDCP14_SetRekeyWin               = 0x12,
};

static int hdcp1_optee_match(struct tee_ioctl_version_data *data,
			    const void *vers)
{
	return 1;
}

static int rtk_hdmi_hdcp1_tee_api_init(struct rtk_hdcp1_tee *hdcp1_tee)
{
	int ret;
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct tee_param  invoke_param[4];
	struct tee_ioctl_version_data vers = {
		.impl_id = TEE_OPTEE_CAP_TZ,
		.impl_caps = TEE_IMPL_ID_OPTEE,
		.gen_caps = TEE_GEN_CAP_GP,
	};

	if (hdcp1_tee->init_hdcp1_ta_flag == 1)
		return HDCP_NO_ERR;

	if (hdcp1_tee->init_ta_count >= MAX_TA_INIT_RETRY_CNT)
		return -HDCP1_TEE_NOINIT_ERR;

	hdcp1_tee->init_ta_count++;

	dev_info(hdmi->dev, "hdcp1_tee open context");
	hdcp1_tee->hdcp1_ctx = tee_client_open_context(NULL, hdcp1_optee_match, NULL, &vers);
	if (IS_ERR(hdcp1_tee->hdcp1_ctx)) {
		dev_err(hdmi->dev, "hdcp1_tee open context fail");
		return -HDCP1_TEE_INIT_OPENCONTEXT_ERR;
	}

	memcpy(hdcp1_tee->hdcp1_arg.uuid, ta_hdcptx14_uuid.b, TEE_IOCTL_UUID_LEN);

	hdcp1_tee->hdcp1_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	hdcp1_tee->hdcp1_arg.num_params = 4;

	memset(&invoke_param, 0, sizeof(invoke_param));

	invoke_param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	dev_info(hdmi->dev, "hdcp1_tee open session");
	ret = tee_client_open_session(hdcp1_tee->hdcp1_ctx, &hdcp1_tee->hdcp1_arg, invoke_param);
	if ((ret < 0) || (hdcp1_tee->hdcp1_arg.ret != TEEC_SUCCESS)) {
		dev_err(hdmi->dev, "hdcp1_tee open session fail, ret=0x%x, hdcp1_arg.ret=0x%x",
			ret, hdcp1_tee->hdcp1_arg.ret);
		return -HDCP1_TEE_INIT_OPENSESSION_ERR;
	}
	hdcp1_tee->init_hdcp1_ta_flag = 1;

	dev_info(hdmi->dev, "hdcp1_tee init done");
	return HDCP_NO_ERR;
}

static void rtk_hdmi_hdcp1_tee_api_deinit(struct rtk_hdcp1_tee *hdcp1_tee)
{
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);

	if (hdcp1_tee->init_hdcp1_ta_flag == 0)
		return;

	if (hdcp1_tee->hdcp1_ctx == NULL) {
		dev_info(hdmi->dev, "hdcp1_tee deint fail, hdcp1_ctx is NULL");
		return;
	}

	dev_info(hdmi->dev, "hdcp1_tee close session");
	tee_client_close_session(hdcp1_tee->hdcp1_ctx, hdcp1_tee->hdcp1_arg.session);

	dev_info(hdmi->dev, "hdcp1_tee close context");
	tee_client_close_context(hdcp1_tee->hdcp1_ctx);

	hdcp1_tee->hdcp1_ctx = NULL;
	hdcp1_tee->init_hdcp1_ta_flag = 0;

	dev_info(hdmi->dev, "hdcp1_tee deinit done");
}

static int
rtk_hdmi_hdcp1_tee_generate_an(struct rtk_hdcp1_tee *hdcp1_tee, u8 *an)
{
	int ret = -HDCP1_TEE_NOMEM_ERR;
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct tee_param  invoke_param[4];
	struct tee_ioctl_invoke_arg arg_in;
	struct tee_shm *shm;

	if (hdcp1_tee->init_hdcp1_ta_flag != 1)
		return -HDCP1_TEE_NOINIT_ERR;

	memset(&invoke_param, 0, sizeof(invoke_param));
	memset(&arg_in, 0, sizeof(arg_in));

	arg_in.func = TA_TEE_HDCP14_GenAn;
	arg_in.session = hdcp1_tee->hdcp1_arg.session;
	arg_in.num_params = 4;

	shm = tee_shm_alloc_kernel_buf(hdcp1_tee->hdcp1_ctx, DRM_HDCP_AN_LEN);
	if (IS_ERR(shm))
		goto exit;

	invoke_param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	invoke_param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	invoke_param[0].u.memref.size = DRM_HDCP_AN_LEN;
	invoke_param[0].u.memref.shm = shm;
	memcpy(invoke_param[0].u.memref.shm->kaddr, an, DRM_HDCP_AN_LEN);

	ret = tee_client_invoke_func(hdcp1_tee->hdcp1_ctx, &arg_in, invoke_param);
	if ((ret < 0) || (arg_in.ret != TEEC_SUCCESS)) {
		dev_err(hdmi->dev, "hdcp1_tee generate_an fail, ret=0x%x, arg_in.ret=0x%x",
			ret, arg_in.ret);
		ret = -HDCP1_TEE_GEN_AN_ERR;
		goto free_shm;
	}
	memcpy(an, invoke_param[0].u.memref.shm->kaddr, DRM_HDCP_AN_LEN);

	ret = HDCP_NO_ERR;
free_shm:
	tee_shm_free(shm);
exit:
	return ret;
}

/*
 * ta_hdcp_lib_get_aksv - Get AKSV from RPMB
 */
static int
rtk_hdmi_hdcp1_tee_read_aksv(struct rtk_hdcp1_tee *hdcp1_tee, u8 *aksv)
{
	int ret = -HDCP1_TEE_NOMEM_ERR;
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct tee_param  invoke_param[4];
	struct tee_ioctl_invoke_arg arg_in;
	struct tee_shm *shm;

	if (hdcp1_tee->init_hdcp1_ta_flag != 1)
		return -HDCP1_TEE_NOINIT_ERR;

	memset(&invoke_param, 0, sizeof(invoke_param));
	memset(&arg_in, 0, sizeof(arg_in));

	arg_in.func = TA_TEE_HDCP14_GetAKSV;
	arg_in.session = hdcp1_tee->hdcp1_arg.session;
	arg_in.num_params = 4;

	shm = tee_shm_alloc_kernel_buf(hdcp1_tee->hdcp1_ctx, DRM_HDCP_KSV_LEN);
	if (IS_ERR(shm))
		goto exit;

	invoke_param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	invoke_param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	invoke_param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	invoke_param[0].u.memref.size = DRM_HDCP_KSV_LEN;
	invoke_param[0].u.memref.shm = shm;
	memcpy(invoke_param[0].u.memref.shm->kaddr, aksv, DRM_HDCP_KSV_LEN);

	ret = tee_client_invoke_func(hdcp1_tee->hdcp1_ctx, &arg_in, invoke_param);
	if ((ret < 0) || (arg_in.ret != TEEC_SUCCESS)) {
		dev_err(hdmi->dev, "hdcp1_tee read_aksv fail, ret=0x%x, arg_in.ret=0x%x",
			ret, arg_in.ret);
		ret = -HDCP1_TEE_READAKSV_ERR;
		goto free_shm;
	}
	memcpy(aksv, invoke_param[0].u.memref.shm->kaddr, DRM_HDCP_KSV_LEN);

	ret = HDCP_NO_ERR;
free_shm:
	tee_shm_free(shm);
exit:
	return ret;
}

static int
rtk_hdmi_hdcp1_tee_set_hdcp1_repeater_bit(struct rtk_hdcp1_tee *hdcp1_tee,
					  u8 is_repeater)
{
	int ret;
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct tee_param  invoke_param[4];
	struct tee_ioctl_invoke_arg arg_in;

	if (hdcp1_tee->init_hdcp1_ta_flag != 1)
		return -HDCP1_TEE_NOINIT_ERR;

	memset(&invoke_param, 0, sizeof(invoke_param));
	memset(&arg_in, 0, sizeof(arg_in));

	arg_in.func = TA_TEE_HDCP14_SetRepeaterBitInTx;
	arg_in.session = hdcp1_tee->hdcp1_arg.session;
	arg_in.num_params = 4;

	invoke_param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	invoke_param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	invoke_param[0].u.value.a = is_repeater;

	ret = tee_client_invoke_func(hdcp1_tee->hdcp1_ctx, &arg_in, invoke_param);
	if ((ret < 0) || (arg_in.ret != TEEC_SUCCESS)) {
		dev_err(hdmi->dev, "hdcp1_tee set repeater_bit fail, ret=0x%x, arg_in.ret=0x%x",
			ret, arg_in.ret);
		return -HDCP1_TEE_REPEATER_BIT_ERR;
	}

	return HDCP_NO_ERR;
}

static int
rtk_hdmi_hdcp1_tee_write_bksv(struct rtk_hdcp1_tee *hdcp1_tee, u8 *bksv)
{
	int ret = -HDCP1_TEE_NOMEM_ERR;
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct tee_param  invoke_param[4];
	struct tee_ioctl_invoke_arg arg_in;
	struct tee_shm *shm;

	if (hdcp1_tee->init_hdcp1_ta_flag != 1)
		return -HDCP1_TEE_NOINIT_ERR;

	memset(&invoke_param, 0, sizeof(invoke_param));
	memset(&arg_in, 0, sizeof(arg_in));

	arg_in.func = TA_TEE_HDCP14_WriteBKSV;
	arg_in.session = hdcp1_tee->hdcp1_arg.session;
	arg_in.num_params = 4;

	shm = tee_shm_alloc_kernel_buf(hdcp1_tee->hdcp1_ctx, DRM_HDCP_KSV_LEN);
	if (IS_ERR(shm))
		goto exit;

	invoke_param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	invoke_param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	invoke_param[0].u.memref.size = DRM_HDCP_KSV_LEN;
	invoke_param[0].u.memref.shm = shm;
	memcpy(invoke_param[0].u.memref.shm->kaddr, bksv, DRM_HDCP_KSV_LEN);

	ret = tee_client_invoke_func(hdcp1_tee->hdcp1_ctx, &arg_in, invoke_param);
	if ((ret < 0) || (arg_in.ret != TEEC_SUCCESS)) {
		dev_err(hdmi->dev, "hdcp1_tee write_bksv fail, ret=0x%x, arg_in.ret=0x%x",
			ret, arg_in.ret);
		ret = -HDCP1_TEE_WEITE_BKSV_ERR;
		goto free_shm;
	}

	ret = HDCP_NO_ERR;
free_shm:
	tee_shm_free(shm);
exit:
	return ret;
}

static int
rtk_hdmi_hdcp1_tee_check_ri_prime(struct rtk_hdcp1_tee *hdcp1_tee,
				  u8 *ri_prime)
{
	int ret = -HDCP1_TEE_NOMEM_ERR;
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct tee_param  invoke_param[4];
	struct tee_ioctl_invoke_arg arg_in;
	struct tee_shm *shm;
	u8 ri[DRM_HDCP_RI_LEN];

	if (hdcp1_tee->init_hdcp1_ta_flag != 1) {
		ret = -HDCP1_TEE_NOINIT_ERR;
		goto exit;
	}

	memset(&invoke_param, 0, sizeof(invoke_param));
	memset(&arg_in, 0, sizeof(arg_in));

	arg_in.func = TA_TEE_HDCP14_CheckR0;
	arg_in.session = hdcp1_tee->hdcp1_arg.session;
	arg_in.num_params = 4;

	shm = tee_shm_alloc_kernel_buf(hdcp1_tee->hdcp1_ctx, DRM_HDCP_RI_LEN);
	if (IS_ERR(shm))
		goto exit;

	invoke_param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	invoke_param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	invoke_param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	invoke_param[0].u.memref.size = DRM_HDCP_RI_LEN;
	invoke_param[0].u.memref.shm = shm;
	memcpy(invoke_param[0].u.memref.shm->kaddr, ri_prime, DRM_HDCP_RI_LEN);

	ret = tee_client_invoke_func(hdcp1_tee->hdcp1_ctx, &arg_in, invoke_param);
	if ((ret < 0) || (arg_in.ret != TEEC_SUCCESS)) {
		dev_err(hdmi->dev, "hdcp1_tee check_ri_prime fail, ret=0x%x, arg_in.ret=0x%x",
			ret, arg_in.ret);
		ret = -HDCP1_TEE_CHECK_RI_ERR;
		goto free_shm;
	}
	memcpy(ri, invoke_param[0].u.memref.shm->kaddr, DRM_HDCP_RI_LEN);

	if (invoke_param[1].u.value.a == HDCP_NO_ERR) {
		ret = HDCP_NO_ERR;
	} else {
		dev_info(hdmi->dev, "hdcp1 Ri:%02x%02x != Ri':%02x%02x\n",
			ri[0], ri[1], ri_prime[0], ri_prime[1]);
		ret = -HDCP1_TEE_INCORRECT_RI_ERR;
	}

free_shm:
	tee_shm_free(shm);
exit:
	return ret;
}

static int
rtk_hdmi_hdcp1_tee_set_encryption(struct rtk_hdcp1_tee *hdcp1_tee,
				  u8 enc_state)
{
	int ret;
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct tee_param  invoke_param[4];
	struct tee_ioctl_invoke_arg arg_in;

	if (hdcp1_tee->init_hdcp1_ta_flag != 1)
		return -HDCP1_TEE_NOINIT_ERR;

	memset(&invoke_param, 0, sizeof(invoke_param));
	memset(&arg_in, 0, sizeof(arg_in));

	arg_in.func = TA_TEE_HDCP14_SetEnc;
	arg_in.session = hdcp1_tee->hdcp1_arg.session;
	arg_in.num_params = 4;

	invoke_param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	invoke_param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	invoke_param[0].u.value.a = enc_state;

	ret = tee_client_invoke_func(hdcp1_tee->hdcp1_ctx, &arg_in, invoke_param);
	if ((ret < 0) || (arg_in.ret != TEEC_SUCCESS)) {
		dev_err(hdmi->dev, "hdcp1_tee set_encryption fail, ret=0x%x, arg_in.ret=0x%x",
			ret, arg_in.ret);
		return -HDCP1_TEE_SET_ENC_ERR;
	}

	return HDCP_NO_ERR;
}

static int
rtk_hdmi_hdcp1_tee_sha_append_bstatus_m0(struct rtk_hdcp1_tee *hdcp1_tee,
					 u8 *ksv_fifo, int *byte_cnt,
					 u8 *bstatus)
{
	int ret = -HDCP1_TEE_NOMEM_ERR;
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct tee_param  invoke_param[4];
	struct tee_ioctl_invoke_arg arg_in;
	struct tee_shm *shm, *shm2;

	if (hdcp1_tee->init_hdcp1_ta_flag != 1) {
		ret = -HDCP1_TEE_NOINIT_ERR;
		goto exit;
	}

	memset(&invoke_param, 0, sizeof(invoke_param));
	memset(&arg_in, 0, sizeof(arg_in));

	arg_in.func = TA_TEE_HDCP14_SHAAppend;
	arg_in.session = hdcp1_tee->hdcp1_arg.session;
	arg_in.num_params = 4;

	shm = tee_shm_alloc_kernel_buf(hdcp1_tee->hdcp1_ctx, MAX_TEE_SHA_DATA_SIZE);
	if (IS_ERR(shm))
		goto exit;

	shm2 = tee_shm_alloc_kernel_buf(hdcp1_tee->hdcp1_ctx, DRM_HDCP_BSTATUS_LEN);
	if (IS_ERR(shm2))
		goto free_shm;

	invoke_param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	invoke_param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	invoke_param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	invoke_param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	invoke_param[0].u.memref.size = MAX_TEE_SHA_DATA_SIZE;
	invoke_param[0].u.memref.shm = shm;
	memcpy(invoke_param[0].u.memref.shm->kaddr, ksv_fifo,
		MAX_TEE_SHA_DATA_SIZE);

	invoke_param[1].u.memref.size = DRM_HDCP_BSTATUS_LEN;
	invoke_param[1].u.memref.shm = shm2;
	memcpy(invoke_param[1].u.memref.shm->kaddr, bstatus,
		DRM_HDCP_BSTATUS_LEN);

	invoke_param[2].u.value.a = (*byte_cnt);

	ret = tee_client_invoke_func(hdcp1_tee->hdcp1_ctx, &arg_in, invoke_param);
	if ((ret < 0) || (arg_in.ret != TEEC_SUCCESS)) {
		dev_err(hdmi->dev, "hdcp1_tee sha_append fail, ret=0x%x, arg_in.ret=0x%x",
			ret, arg_in.ret);
		ret = -HDCP1_TEE_SHA_APPEND_ERR;
		goto free_shm2;
	}

	(*byte_cnt) = invoke_param[2].u.value.a;
	memcpy(ksv_fifo, invoke_param[0].u.memref.shm->kaddr,
		MAX_TEE_SHA_DATA_SIZE);

	ret = HDCP_NO_ERR;
free_shm2:
	tee_shm_free(shm2);
free_shm:
	tee_shm_free(shm);
exit:
	return ret;
}

static int
rtk_hdmi_hdcp1_tee_compute_V(struct rtk_hdcp1_tee *hdcp1_tee,
			     u8 *ksv_fifo, int *byte_cnt)
{
	int ret = -HDCP1_TEE_NOMEM_ERR;
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct tee_param  invoke_param[4];
	struct tee_ioctl_invoke_arg arg_in;
	struct tee_shm *shm;

	if (hdcp1_tee->init_hdcp1_ta_flag != 1) {
		ret = -HDCP1_TEE_NOINIT_ERR;
		goto exit;
	}

	memset(&invoke_param, 0, sizeof(invoke_param));
	memset(&arg_in, 0, sizeof(arg_in));

	arg_in.func = TA_TEE_HDCP14_ComputeV;
	arg_in.session = hdcp1_tee->hdcp1_arg.session;
	arg_in.num_params = 4;

	shm = tee_shm_alloc_kernel_buf(hdcp1_tee->hdcp1_ctx, MAX_TEE_SHA_DATA_SIZE);
	if (IS_ERR(shm))
		goto exit;

	invoke_param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	invoke_param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	invoke_param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	invoke_param[0].u.memref.size = MAX_TEE_SHA_DATA_SIZE;
	invoke_param[0].u.memref.shm = shm;
	memcpy(invoke_param[0].u.memref.shm->kaddr, ksv_fifo,
		MAX_TEE_SHA_DATA_SIZE);

	invoke_param[1].u.value.a = (*byte_cnt);
	invoke_param[1].u.value.b = HDCP1_TEE_COMPUTE_V_ERR;

	ret = tee_client_invoke_func(hdcp1_tee->hdcp1_ctx, &arg_in, invoke_param);
	if ((ret < 0) || (arg_in.ret != TEEC_SUCCESS)) {
		dev_err(hdmi->dev, "hdcp1_tee compute_V fail, ret=0x%x, arg_in.ret=0x%x",
			ret, arg_in.ret);
		ret = -HDCP1_TEE_COMPUTE_V_ERR;
		goto free_shm;
	}

	if (invoke_param[1].u.value.b != HDCP_NO_ERR) {
		ret = -HDCP1_TEE_WAIT_V_READY_ERR;
		goto free_shm;
	}

	(*byte_cnt) = invoke_param[1].u.value.a;
	memcpy(ksv_fifo, invoke_param[0].u.memref.shm->kaddr,
		MAX_TEE_SHA_DATA_SIZE);

	ret = HDCP_NO_ERR;
free_shm:
	tee_shm_free(shm);
exit:
	return ret;
}

static int
rtk_hdmi_hdcp1_tee_verify_V(struct rtk_hdcp1_tee *hdcp1_tee,
			    u8 *vprime)
{
	int ret = -HDCP1_TEE_NOMEM_ERR;
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct tee_param  invoke_param[4];
	struct tee_ioctl_invoke_arg arg_in;
	struct tee_shm *shm;

	if (hdcp1_tee->init_hdcp1_ta_flag != 1) {
		ret = -HDCP1_TEE_NOINIT_ERR;
		goto exit;
	}

	memset(&invoke_param, 0, sizeof(invoke_param));
	memset(&arg_in, 0, sizeof(arg_in));

	arg_in.func = TA_TEE_HDCP14_VerifyV;
	arg_in.session = hdcp1_tee->hdcp1_arg.session;
	arg_in.num_params = 4;

	shm = tee_shm_alloc_kernel_buf(hdcp1_tee->hdcp1_ctx, MAX_TEE_SHA_VPRIME_SIZE);
	if (IS_ERR(shm))
		goto exit;

	invoke_param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	invoke_param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	invoke_param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	/* MAX_TEE_SHA_DATA_SIZE is a Bug, it should be MAX_TEE_SHA_VPRIME_SIZE,
	 * However, the TA also check this value MAX_TEE_SHA_DATA_SIZE.
	 * it will return bad parameter error from TA
	 * if we only just modify this CA file.
	 */
	invoke_param[0].u.memref.size = MAX_TEE_SHA_DATA_SIZE;
	invoke_param[0].u.memref.shm = shm;
	memcpy(invoke_param[0].u.memref.shm->kaddr, vprime,
		MAX_TEE_SHA_VPRIME_SIZE);

	invoke_param[1].u.value.a = HDCP1_TEE_VERIFY_V_ERR;

	ret = tee_client_invoke_func(hdcp1_tee->hdcp1_ctx, &arg_in, invoke_param);
	if ((ret < 0) || (arg_in.ret != TEEC_SUCCESS)) {
		dev_err(hdmi->dev, "hdcp1_tee verify_V fail, ret=0x%x, arg_in.ret=0x%x",
			ret, arg_in.ret);
		ret = -HDCP1_TEE_VERIFY_V_ERR;
		goto free_shm;
	}
	memcpy(vprime, invoke_param[0].u.memref.shm->kaddr,
		MAX_TEE_SHA_VPRIME_SIZE);

	if (invoke_param[1].u.value.a != HDCP_NO_ERR) {
		dev_info(hdmi->dev, "hdcp1_tee verify V != V'");
		ret = -HDCP1_TEE_V_MATCH_ERR;
		goto free_shm;
	}

	ret = HDCP_NO_ERR;
free_shm:
	tee_shm_free(shm);
exit:
	return ret;
}

static int
rtk_hdmi_hdcp1_tee_set_wider_window(struct rtk_hdcp1_tee *hdcp1_tee)
{
	int ret;
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct tee_param  invoke_param[4];
	struct tee_ioctl_invoke_arg arg_in;

	if (hdcp1_tee->init_hdcp1_ta_flag != 1)
		return -HDCP1_TEE_NOINIT_ERR;

	memset(&invoke_param, 0, sizeof(invoke_param));
	memset(&arg_in, 0, sizeof(arg_in));

	arg_in.func = TA_TEE_HDCP14_SetWinderWin;
	arg_in.session = hdcp1_tee->hdcp1_arg.session;
	arg_in.num_params = 4;

	invoke_param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	invoke_param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	invoke_param[0].u.value.a = 1;

	ret = tee_client_invoke_func(hdcp1_tee->hdcp1_ctx, &arg_in, invoke_param);
	if (((ret < 0) || (arg_in.ret != TEEC_SUCCESS)) &&
			(arg_in.ret != TEE_ERROR_NOT_SUPPORTED)) {
		dev_err(hdmi->dev, "hdcp1_tee set_wider_window fail, ret=0x%x, arg_in.ret=0x%x",
			ret, arg_in.ret);
		return -HDCP1_TEE_SET_WINDER_ERR;
	}

	return HDCP_NO_ERR;
}

static int rtk_hdmi_hdcp1_tee_write_key(struct rtk_hdcp1_tee *hdcp1_tee,
					unsigned char *key)
{
	int ret = -HDCP1_TEE_NOMEM_ERR;
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct tee_param  invoke_param[4];
	struct tee_ioctl_invoke_arg arg_in;
	struct tee_shm *shm;

	if (hdcp1_tee->init_hdcp1_ta_flag != 1) {
		ret = -HDCP1_TEE_NOINIT_ERR;
		goto exit;
	}

	memset(&invoke_param, 0, sizeof(invoke_param));
	memset(&arg_in, 0, sizeof(arg_in));

	arg_in.func = TA_TEE_HDCP14_SetParamKey;
	arg_in.session = hdcp1_tee->hdcp1_arg.session;
	arg_in.num_params = 4;

	shm = tee_shm_alloc_kernel_buf(hdcp1_tee->hdcp1_ctx, HDCP14_KEY_SIZE);
	if (IS_ERR(shm))
		goto exit;

	invoke_param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	invoke_param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT;
	invoke_param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	invoke_param[0].u.memref.size = HDCP14_KEY_SIZE;
	invoke_param[0].u.memref.shm = shm;
	memcpy(invoke_param[0].u.memref.shm->kaddr, key, HDCP14_KEY_SIZE);

	invoke_param[1].u.value.a = 1;

	ret = tee_client_invoke_func(hdcp1_tee->hdcp1_ctx, &arg_in, invoke_param);
	if ((ret < 0) || (arg_in.ret != TEEC_SUCCESS)) {
		dev_err(hdmi->dev, "hdcp1_tee write_key fail, ret=0x%x, arg_in.ret=0x%x",
			ret, arg_in.ret);
		ret = -HDCP1_TEE_SET_KEY_ERR;
		goto free_shm;
	}

	ret = invoke_param[1].u.value.a;
	if (ret != HDCP_NO_ERR) {
		dev_err(hdmi->dev, "hdcp1_tee write_key fail, ret=%u", ret);
		ret = -HDCP1_TEE_SET_KEY_ERR;
		goto free_shm;
	}

	ret = HDCP_NO_ERR;
free_shm:
	tee_shm_free(shm);
exit:
	return ret;
}

static int rtk_hdmi_hdcp1_tee_fix480p(struct rtk_hdcp1_tee *hdcp1_tee)
{
	int ret;
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct tee_param  invoke_param[4];
	struct tee_ioctl_invoke_arg arg_in;

	if (hdcp1_tee->init_hdcp1_ta_flag != 1)
		return -HDCP1_TEE_NOINIT_ERR;

	memset(&invoke_param, 0, sizeof(invoke_param));
	memset(&arg_in, 0, sizeof(arg_in));

	arg_in.func = TA_TEE_HDCP14_Fix480P;
	arg_in.session = hdcp1_tee->hdcp1_arg.session;
	arg_in.num_params = 4;

	invoke_param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	invoke_param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	invoke_param[0].u.value.a = 0;

	ret = tee_client_invoke_func(hdcp1_tee->hdcp1_ctx, &arg_in, invoke_param);
	if ((ret < 0) || (arg_in.ret != TEEC_SUCCESS)) {
		dev_err(hdmi->dev, "hdcp1_tee fix480p fail, ret=0x%x, arg_in.ret=0x%x",
			ret, arg_in.ret);
		return -HDCP1_TEE_FIX_480P_ERR;
	}

	return HDCP_NO_ERR;
}

static int rtk_hdmi_hdcp1_tee_set_keepout_win(struct rtk_hdcp1_tee *hdcp1_tee)
{
	int ret;
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct tee_param  invoke_param[4];
	struct tee_ioctl_invoke_arg arg_in;

	if (hdcp1_tee->init_hdcp1_ta_flag != 1)
		return -HDCP1_TEE_NOINIT_ERR;

	memset(&invoke_param, 0, sizeof(invoke_param));
	memset(&arg_in, 0, sizeof(arg_in));

	arg_in.func = TA_TEE_HDCP14_SetKeepoutwin;
	arg_in.session = hdcp1_tee->hdcp1_arg.session;
	arg_in.num_params = 4;

	invoke_param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	invoke_param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	invoke_param[0].u.value.a = 0x1fa; /* keepoutwinstart */
	invoke_param[0].u.value.b = 0x288; /* keepoutwinend */

	ret = tee_client_invoke_func(hdcp1_tee->hdcp1_ctx, &arg_in, invoke_param);
	if ((ret < 0) || (arg_in.ret != TEEC_SUCCESS)) {
		dev_err(hdmi->dev, "hdcp1_tee fix480p fail, ret=0x%x, arg_in.ret=0x%x",
			ret, arg_in.ret);
		return -HDCP1_TEE_SET_KEEPOUTWIN_ERR;
	}

	return HDCP_NO_ERR;
}

static int rtk_hdmi_hdcp1_tee_set_rekey_win(struct rtk_hdcp1_tee *hdcp1_tee, unsigned char rekey_win)
{
	int ret;
	struct rtk_hdcp *hdcp = to_rtk_hdcp(hdcp1_tee);
	struct rtk_hdmi *hdmi = to_rtk_hdmi(hdcp);
	struct tee_param  invoke_param[4];
	struct tee_ioctl_invoke_arg arg_in;

	if (hdcp1_tee->init_hdcp1_ta_flag != 1)
		return -HDCP1_TEE_NOINIT_ERR;

	memset(&invoke_param, 0, sizeof(invoke_param));
	memset(&arg_in, 0, sizeof(arg_in));

	arg_in.func = TA_TEE_HDCP14_SetRekeyWin;
	arg_in.session = hdcp1_tee->hdcp1_arg.session;
	arg_in.num_params = 4;

	invoke_param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	invoke_param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	invoke_param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	invoke_param[0].u.value.a = rekey_win;

	ret = tee_client_invoke_func(hdcp1_tee->hdcp1_ctx, &arg_in, invoke_param);
	if ((ret < 0) || (arg_in.ret != TEEC_SUCCESS)) {
		dev_err(hdmi->dev, "hdcp1_tee invoke set_rekey_win fail, ret(0x%x)\n", arg_in.ret);
		return -HDCP1_TEE_SET_REKEYWIN_ERR;
	}

	return HDCP_NO_ERR;
}

static const struct rtk_hdcp1_tee_ops rtk_hdmi_hdcp1_tee_ops = {
	.hdcp1_tee_api_init = rtk_hdmi_hdcp1_tee_api_init,
	.hdcp1_tee_api_deinit = rtk_hdmi_hdcp1_tee_api_deinit,
	.generate_an = rtk_hdmi_hdcp1_tee_generate_an,
	.read_aksv = rtk_hdmi_hdcp1_tee_read_aksv,
	.set_hdcp1_repeater_bit = rtk_hdmi_hdcp1_tee_set_hdcp1_repeater_bit,
	.write_bksv = rtk_hdmi_hdcp1_tee_write_bksv,
	.check_ri_prime = rtk_hdmi_hdcp1_tee_check_ri_prime,
	.hdcp1_set_encryption = rtk_hdmi_hdcp1_tee_set_encryption,
	.sha_append_bstatus_m0 = rtk_hdmi_hdcp1_tee_sha_append_bstatus_m0,
	.compute_V = rtk_hdmi_hdcp1_tee_compute_V,
	.verify_V = rtk_hdmi_hdcp1_tee_verify_V,
	.set_wider_window = rtk_hdmi_hdcp1_tee_set_wider_window,
	.write_hdcp1_key = rtk_hdmi_hdcp1_tee_write_key,
	.fix480p = rtk_hdmi_hdcp1_tee_fix480p,
	.set_keepout_win = rtk_hdmi_hdcp1_tee_set_keepout_win,
	.set_rekey_win = rtk_hdmi_hdcp1_tee_set_rekey_win,
};

int rtk_hdcp1_tee_init(struct rtk_hdcp1_tee *hdcp1_tee)
{
	hdcp1_tee->init_hdcp1_ta_flag = 0;
	hdcp1_tee->init_ta_count = 0;
	hdcp1_tee->hdcp1_tee_ops = &rtk_hdmi_hdcp1_tee_ops;

	if (!hdcp1_tee->hdcp1_tee_ops->hdcp1_tee_api_init ||
		!hdcp1_tee->hdcp1_tee_ops->hdcp1_tee_api_deinit ||
		!hdcp1_tee->hdcp1_tee_ops->generate_an ||
		!hdcp1_tee->hdcp1_tee_ops->read_aksv ||
		!hdcp1_tee->hdcp1_tee_ops->set_hdcp1_repeater_bit ||
		!hdcp1_tee->hdcp1_tee_ops->write_bksv ||
		!hdcp1_tee->hdcp1_tee_ops->check_ri_prime ||
		!hdcp1_tee->hdcp1_tee_ops->hdcp1_set_encryption ||
		!hdcp1_tee->hdcp1_tee_ops->sha_append_bstatus_m0 ||
		!hdcp1_tee->hdcp1_tee_ops->compute_V ||
		!hdcp1_tee->hdcp1_tee_ops->verify_V ||
		!hdcp1_tee->hdcp1_tee_ops->set_wider_window ||
		!hdcp1_tee->hdcp1_tee_ops->write_hdcp1_key ||
		!hdcp1_tee->hdcp1_tee_ops->fix480p ||
		!hdcp1_tee->hdcp1_tee_ops->set_keepout_win ||
		!hdcp1_tee->hdcp1_tee_ops->set_rekey_win)
		return -EFAULT;

	return 0;
}
