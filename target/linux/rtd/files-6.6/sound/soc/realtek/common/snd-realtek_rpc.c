// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2017-2020 Realtek Semiconductor Corp.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/ioctl.h> /* needed for the _IOW etc stuff used later */
#include <linux/syscalls.h> /* needed for the _IOW etc stuff used later */
#include <linux/mpage.h>
#include <linux/dcache.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/fdtable.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/asound.h>
#include <asm/cacheflush.h>
#include <soc/realtek/memory.h>
#include <soc/realtek/rtk_media_heap.h>
#include "snd-realtek.h"

static int krpc_acpu_cb(struct rtk_krpc_ept_info *krpc_ept_info, char *buf)
{
	uint32_t *tmp;
	struct rpc_struct *rpc = (struct rpc_struct *)buf;

	if (rpc->programID == REPLYID) {
		tmp = (uint32_t *)(buf + sizeof(struct rpc_struct));
		*(krpc_ept_info->retval) =*(tmp + 1);

		complete(&krpc_ept_info->ack);
	}

	return 0;
}

int snd_afw_ept_init(struct rtk_krpc_ept_info *krpc_ept_info)
{
	int ret = 0;

	ret = krpc_info_init(krpc_ept_info, "snd", krpc_acpu_cb);

	return ret;
}

static char *prepare_rpc_data(struct rtk_krpc_ept_info *krpc_ept_info,
			uint32_t command, uint32_t param1,
			uint32_t param2, int *len)
{
	struct rpc_struct *rpc;
	uint32_t *tmp;
	char *buf;

	*len = sizeof(struct rpc_struct) + 3 * sizeof(uint32_t);
	buf = kmalloc(sizeof(struct rpc_struct) + 3 * sizeof(uint32_t), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	rpc = (struct rpc_struct *)buf;
	rpc->programID = KERNELID;
	rpc->versionID = KERNELID;
	rpc->procedureID = 0;
	rpc->taskID = krpc_ept_info->id;
	rpc->sysTID = 0;
	rpc->sysPID = 0;
	rpc->parameterSize = 3*sizeof(uint32_t);
	rpc->mycontext = 0;
	tmp = (uint32_t *)(buf+sizeof(struct rpc_struct));
	*tmp = command;
	*(tmp+1) = param1;
	*(tmp+2) = param2;

	return buf;
}

static int snd_send_rpc(struct rtk_krpc_ept_info *krpc_ept_info, char *buf, int len, uint32_t *retval)
{
	int ret = 0;

	mutex_lock(&krpc_ept_info->send_mutex);

	krpc_ept_info->retval = retval;
	ret = rtk_send_rpc(krpc_ept_info, buf, len);
	if (ret < 0) {
		pr_err("[%s] send rpc failed\n", krpc_ept_info->name);
		mutex_unlock(&krpc_ept_info->send_mutex);
		return ret;
	}
	if (!wait_for_completion_timeout(&krpc_ept_info->ack, RPC_TIMEOUT)) {
		pr_err("ALSA AFW: kernel rpc timeout: %s...\n", krpc_ept_info->name);
		rtk_krpc_dump_ringbuf_info(krpc_ept_info);
		mutex_unlock(&krpc_ept_info->send_mutex);
		return -EINVAL;
	}
	mutex_unlock(&krpc_ept_info->send_mutex);

	return 0;
}

static int send_rpc(struct rtk_krpc_ept_info *krpc_ept_info,
			uint32_t command, uint32_t param1,
			uint32_t param2, uint32_t *retval)
{
	int ret = 0;
	int len;
	char *buf;

	buf = prepare_rpc_data(krpc_ept_info, command, param1, param2, &len);
	if (!IS_ERR(buf)) {
		ret = snd_send_rpc(krpc_ept_info, buf, len, retval);
		kfree(buf);
	}

	return ret;
}

int RPC_TOAGENT_CHECK_AUDIO_READY(phys_addr_t paddr, void *vaddr)
{
	struct RPC_DEFAULT_INPUT_T *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct RPC_DEFAULT_INPUT_T *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct RPC_DEFAULT_INPUT_T));

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_CHECK_READY,
		CONVERT_FOR_AVCPU(dat), //rpc->info address
		CONVERT_FOR_AVCPU(dat + get_rpc_alignment_offset(sizeof(rpc->info))), //rpc->retval address
		&rpc->ret)) {
		pr_err("[ALSA %s RPC fail]\n", __func__);
		goto exit;
	}

	if (rpc->ret != S_OK) {
		pr_err("[ALSA %s RPC fail]\n", __func__);
		goto exit;
	}

	if (rpc->info == 0) {
		pr_err("[ALSA Audio is not ready]\n");
		goto exit;
	}

	// successful
	ret = 0;
	pr_info("[%s %s %d ] success\n", __FILE__, __func__, __LINE__);
exit:

	return ret;
}

int RPC_TOAGENT_CREATE_AO_AGENT_AFW(phys_addr_t paddr, void *vaddr, int *aoId,
				 int pinId)
{
	struct RPC_CREATE_AO_AGENT_T *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct RPC_CREATE_AO_AGENT_T *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct RPC_CREATE_AO_AGENT_T));
	rpc->info.instanceID = htonl(0);
	rpc->info.type = htonl(pinId);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_CREATE_AGENT,
		CONVERT_FOR_AVCPU(dat), //rpc->info address
		CONVERT_FOR_AVCPU(dat + get_rpc_alignment_offset(sizeof(rpc->info))),//rpc->retval address
		&rpc->ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (ntohl(rpc->retval.result) != S_OK || rpc->ret != S_OK) {
		pr_err("[ALSA %x %x %s %d RPC fail]\n", rpc->retval.result, rpc->ret, __func__, __LINE__);
		goto exit;
	}

	*aoId = ntohl(rpc->retval.data);
	ret = 0;
	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
exit:
	return ret;
}

int RPC_TOAGENT_PUT_SHARE_MEMORY_LATENCY_AFW(phys_addr_t paddr, void *vaddr,
			void *p, void *p2, int decID, int aoID, int type)
{
	struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *cmd = NULL;
	struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *res;
	int ret = 0;
	int magic_num = 2379;
	uint32_t RPC_ret;
	phys_addr_t dat;
	unsigned long offset;

	pr_info("[%s %d ion_alloc p1 %x p2 %x decID %d aoID %d type %d\n", __FUNCTION__, __LINE__,
			(uint32_t)((long)p), (uint32_t)((long)p2), decID, aoID, type);

	cmd = (struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	res = (struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *)((unsigned long)cmd + offset);
	memset(cmd, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));

	cmd->type = htonl(type);
	cmd->privateInfo[0] = (uint32_t)htonl((uint32_t)((long)p));
	cmd->privateInfo[1] = htonl(magic_num);
	cmd->privateInfo[2] = (uint32_t)htonl((uint32_t)((long)p2));
	cmd->privateInfo[3] = (uint32_t)htonl((uint32_t)(decID));
	cmd->privateInfo[4] = (uint32_t)htonl((uint32_t)(aoID));

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
exit:

	return ret;
}

int RPC_TOAGENT_GET_AO_FLASH_PIN_AFW(phys_addr_t paddr, void *vaddr, int AOAgentID)
{
	struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *cmd = NULL;
	struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *res;
	uint32_t RPC_ret;
	int ret = -1;
	phys_addr_t dat;
	unsigned long offset;

	cmd = (struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	res = (struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *)((unsigned long)cmd + offset);
	memset(cmd, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	memset(res, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_RETURNVAL));
	cmd->instanceID = htonl(AOAgentID);
	cmd->type = htonl(ENUM_PRIVATEINFO_AUDIO_GET_FLASH_PIN);
	cmd->privateInfo[0] = 0xFF;
	cmd->privateInfo[1] = 0xFF;
	cmd->privateInfo[2] = 0xFF;
	cmd->privateInfo[3] = 0xFF;
	cmd->privateInfo[4] = 0xFF;
	cmd->privateInfo[5] = 0xFF;

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat), //cmd address
		CONVERT_FOR_AVCPU(dat + offset),//res address
		&RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	ret = ntohl(res->privateInfo[0]);

	if (ret < FLASH_AUDIO_PIN_1 || ret > FLASH_AUDIO_PIN_3) {
		pr_err("[ALSA %s %d RPC %d fail]\n", __func__, __LINE__, ret);
		ret = -1;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
exit:
	return ret;
}

int RPC_TOAGENT_SET_AO_FLASH_VOLUME_AFW(phys_addr_t paddr, void *vaddr,
			struct snd_card_RTK_pcm *dpcm)
{
	struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *cmd = NULL;
	struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *res;
	uint32_t rpc_ret;
	int ret = -1;
	phys_addr_t dat;
	unsigned long offset;

	cmd = (struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	res = (struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *)((unsigned long)cmd + offset);

	memset(cmd, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	cmd->instanceID = htonl(dpcm->AOAgentID);
	cmd->type = htonl(ENUM_PRIVATEINFO_AUDIO_CONTROL_FLASH_VOLUME);
	cmd->privateInfo[0] = htonl(dpcm->AOpinID);
	cmd->privateInfo[1] = htonl((31-dpcm->volume));

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&rpc_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[ALSA set AO_pin %d volume %d]\n", dpcm->AOpinID, dpcm->volume);
	ret = 0;
exit:
	return ret;
}

int RPC_TOAGENT_CREATE_DECODER_AGENT_AFW(phys_addr_t paddr, void *vaddr, int *decID,
				     int *pinID)
{
	struct RPC_CREATE_PCM_DECODER_CTRL_T *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct RPC_CREATE_PCM_DECODER_CTRL_T *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct RPC_CREATE_PCM_DECODER_CTRL_T));
	rpc->instance.type = htonl(AUDIO_DECODER);
	rpc->instance.instanceID = htonl(-1);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_CREATE_AGENT,
		CONVERT_FOR_AVCPU(dat), //rpc->instance address
		CONVERT_FOR_AVCPU(dat + get_rpc_alignment_offset(sizeof(rpc->instance))), //rpc->res address
		&rpc->ret)) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc->ret != S_OK) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		goto exit;
	}

	*decID = ntohl(rpc->res.data);
	*pinID = BASE_BS_IN;

	pr_info("[ALSA Create Decoder instance %d]\n", *decID/*dpcm->DECAgentID*/);
	ret = 0;
exit:

	return ret;
}

/* data of AUDIO_RPC_RINGBUFFER_HEADER is "hose side" */
int RPC_TOAGENT_INITRINGBUFFER_HEADER_SVC_AFW(phys_addr_t paddr, void *vaddr,
			struct AUDIO_RPC_RINGBUFFER_HEADER *header, int buffer_count)
{
	struct RPC_INITRINGBUFFER_HEADER_T *rpc = NULL;
	int ch;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct RPC_INITRINGBUFFER_HEADER_T *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct RPC_INITRINGBUFFER_HEADER_T));
	rpc->header.instanceID = htonl(header->instanceID);
	rpc->header.pinID = htonl(header->pinID);
	rpc->header.readIdx = htonl(header->readIdx);
	rpc->header.listSize = htonl(header->listSize);

	pr_info(" header instance ID %d\n", header->instanceID);
	pr_info(" header pinID       %d\n", header->pinID);
	pr_info(" header readIdx     %d\n", header->readIdx);
	pr_info(" header listSize    %d\n", header->listSize);

	for (ch = 0; ch < buffer_count; ch++)
		rpc->header.pRingBufferHeaderList[ch] = htonl((unsigned int) header->pRingBufferHeaderList[ch]);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_INIT_RINGBUF,
		CONVERT_FOR_AVCPU(dat + get_rpc_alignment_offset(sizeof(rpc->ret) + sizeof(rpc->res))), //rpc->header address
		CONVERT_FOR_AVCPU(dat), //rpc->ret address
		&rpc->res)) {
		pr_err("[%s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc->res != S_OK || ntohl(rpc->ret.result) != S_OK) {
		pr_err("[%s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_CONNECT_SVC_AFW(phys_addr_t paddr, void *vaddr,
			struct AUDIO_RPC_CONNECTION *pconnection)
{
	struct RPC_CONNECTION_T *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct RPC_CONNECTION_T *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct RPC_CONNECTION_T));
	rpc->out.srcInstanceID = htonl(pconnection->srcInstanceID);
	rpc->out.srcPinID = htonl(pconnection->srcPinID);
	rpc->out.desInstanceID = htonl(pconnection->desInstanceID);
	rpc->out.desPinID = htonl(pconnection->desPinID);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_CONNECT,
		CONVERT_FOR_AVCPU(dat), //rpc->out address
		CONVERT_FOR_AVCPU(dat + get_rpc_alignment_offset(sizeof(rpc->out))), //rpc->ret
		&rpc->res)) {
		pr_err("[%s RPC fail %d]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc->res != S_OK || ntohl(rpc->ret.result) != S_OK) {
		pr_err("[%s RPC fail %d]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_PAUSE_SVC_AFW(phys_addr_t paddr, void *vaddr, int instance_id)
{
	struct RPC_TOAGENT_PAUSE_T *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct RPC_TOAGENT_PAUSE_T *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct RPC_TOAGENT_PAUSE_T));
	rpc->inst_id = htonl(instance_id);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_PAUSE,
		CONVERT_FOR_AVCPU(dat), //rpc->inst_id address
		CONVERT_FOR_AVCPU(dat + get_rpc_alignment_offset(sizeof(rpc->inst_id))),//rpc->retval address
		&rpc->res)) {
		pr_err("[%s %d RPC fail\n]", __func__, __LINE__);
		goto exit;
	}

	if (rpc->res != S_OK) {
		pr_err("[%s %d RPC fail\n]", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_DESTROY_AI_FLOW_SVC_AFW(phys_addr_t paddr, void *vaddr, int instance_id)
{
	struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS *rpc = NULL;
	int ret = -1;
	uint32_t res;
	phys_addr_t dat;

	rpc = (struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS));
	rpc->type = htonl(ENUM_PRIVATEINFO_AIO_ALSA_DESTROY_AI_FLOW);
	rpc->instanceID = htonl(instance_id);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat), //rpc->inst_id address
		CONVERT_FOR_AVCPU(dat + get_rpc_alignment_offset(sizeof(*rpc))),//rpc->retval address
		&res)) {
		pr_err("[%s %d RPC fail\n]", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_RUN_SVC_AFW(phys_addr_t paddr, void *vaddr, int instance_id)
{
	struct RPC_TOAGENT_T *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct RPC_TOAGENT_T *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct RPC_TOAGENT_T));
	rpc->inst_id = htonl(instance_id);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_RUN,
		CONVERT_FOR_AVCPU(dat), //rpc->inst_id address
		CONVERT_FOR_AVCPU(dat + get_rpc_alignment_offset(sizeof(rpc->inst_id))), //rpc->retval address
		&rpc->res)) {
		pr_err("[%s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc->res != S_OK || ntohl(rpc->retval.result) != S_OK) {
		pr_err("[%s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_FLUSH_SVC_AFW(phys_addr_t paddr, void *vaddr,
			struct AUDIO_RPC_SENDIO *sendio)
{
	struct RPC_TOAGENT_FLASH_T *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct RPC_TOAGENT_FLASH_T *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct RPC_TOAGENT_FLASH_T));
	rpc->sendio.instanceID = htonl(sendio->instanceID);
	rpc->sendio.pinID = htonl(sendio->pinID);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_FLUSH,
		CONVERT_FOR_AVCPU(dat + get_rpc_alignment_offset(sizeof(rpc->retval) + sizeof(rpc->res))), //rpc->sendio address
		CONVERT_FOR_AVCPU(dat), //rpc->retval address
		&rpc->res)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc->res != S_OK || ntohl(rpc->retval.result) != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_RELEASE_AO_FLASH_PIN_AFW(phys_addr_t paddr, void *vaddr,
			int AOAgentID, int AOpinID)
{
	struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *cmd = NULL;
	struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *res;
	uint32_t rpc_ret = 0;
	int ret = -1;
	phys_addr_t dat;
	unsigned long offset;

	cmd = (struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	res = (struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *)((unsigned long)cmd + offset);

	memset(cmd, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	cmd->instanceID = htonl(AOAgentID);
	cmd->type = htonl(ENUM_PRIVATEINFO_AUDIO_RELEASE_FLASH_PIN);
	cmd->privateInfo[0] = htonl(AOpinID);
	cmd->privateInfo[1] = 0xFF;
	cmd->privateInfo[2] = 0xFF;
	cmd->privateInfo[3] = 0xFF;
	cmd->privateInfo[4] = 0xFF;
	cmd->privateInfo[5] = 0xFF;

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat), //cmd address
		CONVERT_FOR_AVCPU(dat + offset), //res address
		&rpc_ret)) {
		pr_err("[ALSA %s %d fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc_ret != S_OK) {
		pr_err("[ALSA %s %d fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_AO_CONFIG_WITHOUT_DECODER_AFW(phys_addr_t paddr, void *vaddr,
			struct snd_pcm_runtime *runtime)
{
	struct snd_card_RTK_pcm *dpcm = runtime->private_data;
	struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS *cmd = NULL;
	struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *res;
	int ret = -1;
	int offset, tmp, ch;
	char *p;
	uint32_t RPC_ret;
	phys_addr_t dat;

	cmd = (struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS));

	p = (char *)&cmd->argateInfo[3];
	memset(cmd, 0, sizeof(struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS));
	res = (struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *)((unsigned long)cmd + offset);

	cmd->type = htonl(ENUM_PRIVATEINFO_AIO_AO_FLASH_LPCM);
	cmd->instanceID = htonl(dpcm->AOAgentID);

	tmp = dpcm->AOpinID & 0xff;
	cmd->argateInfo[1] |= tmp;
	tmp = ((runtime->sample_bits >> 3) << 8);
	cmd->argateInfo[1] |= tmp;
	tmp = AUDIO_LITTLE_ENDIAN << 16;
	cmd->argateInfo[1] |= tmp;

	cmd->argateInfo[1] = htonl(cmd->argateInfo[1]);
	cmd->argateInfo[2] = htonl(runtime->rate);
	for (ch = 0; ch < runtime->channels; ++ch)
		p[ch] = ch + 1;

	// config ao lpcm out delay and ao hw buffer delay
	cmd->argateInfo[5] = (15 << 16);
	cmd->argateInfo[5] |= 25;
	cmd->argateInfo[5] = htonl(cmd->argateInfo[5]);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&RPC_ret)) {
		pr_err("[%s %d RPC fail\n]", __FUNCTION__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __FUNCTION__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __FUNCTION__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_STOP_SVC_AFW(phys_addr_t paddr, void *vaddr, int instanceID)
{
	struct RPC_TOAGENT_STOP_T *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct RPC_TOAGENT_STOP_T *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct RPC_TOAGENT_STOP_T));
	rpc->instanceID = htonl(instanceID);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_STOP,
		CONVERT_FOR_AVCPU(dat + get_rpc_alignment_offset(sizeof(rpc->retval) + sizeof(rpc->res))), //rpc->instanceID address
		CONVERT_FOR_AVCPU(dat),//rpc->retval address
		&rpc->res)) {
		pr_err("[ALSA %s %d fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc->res != S_OK || ntohl(rpc->retval.result) != S_OK) {
		pr_err("[ALSA %s %d fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_DESTROY_SVC_AFW(phys_addr_t paddr, void *vaddr, int instanceID)
{
	struct RPC_TOAGENT_DESTROY_T *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct RPC_TOAGENT_DESTROY_T *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct RPC_TOAGENT_DESTROY_T));
	rpc->instanceID = htonl(instanceID);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_DESTROY,
		CONVERT_FOR_AVCPU(dat + get_rpc_alignment_offset(sizeof(rpc->retval) + sizeof(rpc->res))), //rpc->instanceID address
		CONVERT_FOR_AVCPU(dat), //rpc->retval address
		&rpc->res)) {
		pr_err("%s %d RPC fail\n", __FILE__, __LINE__);
		goto exit;
	}

	if (rpc->res != S_OK || ntohl(rpc->retval.result) != S_OK) {
		pr_err("%s %d RPC fail\n", __FILE__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_INBAND_EOS_SVC_AFW(struct snd_card_RTK_pcm *dpcm)
{
	struct AUDIO_DEC_EOS cmd;

	cmd.header.type = htonl(AUDIO_DEC_INBAND_CMD_TYPE_EOS);
	cmd.header.size = htonl(sizeof(struct AUDIO_DEC_EOS));
	cmd.EOSID = 0;
	cmd.wPtr = htonl(dpcm->decInRing_LE[0].writePtr);
	writeInbandCmd_afw(dpcm, &cmd, sizeof(struct AUDIO_DEC_EOS));

	return 0;
}

/* set AO volume */
int RPC_TOAGENT_SET_VOLUME_AFW(struct device *dev, int volume)
{
	struct AUDIO_CONFIG_COMMAND *config = NULL;
	unsigned int *res;
	uint32_t ret = 0;
	phys_addr_t dat;
	void *vaddr;
	size_t size = SZ_4K;
	unsigned long offset;

	vaddr = dma_alloc_coherent(dev, size, &dat, GFP_KERNEL);
	if (!vaddr) {
		dev_err(dev, "%s dma_alloc fail \n", __func__);
		ret = -1;
		goto exit;
	}
	config = (struct AUDIO_CONFIG_COMMAND *)vaddr;

	memset(config, 0, sizeof(struct AUDIO_CONFIG_COMMAND));
	config->msgID = htonl(AUDIO_CONFIG_CMD_VOLUME);
	config->value[0] = htonl(31 - volume);

	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_CONFIG_COMMAND));
	res = (unsigned int *)((unsigned long)config + offset);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_VOLUME_CONTROL,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&ret)) {
		pr_err("[ALSA %s %d fail]\n", __func__, __LINE__);
		ret = -1;
		goto exit;
	}

	if (ret != S_OK) {
		pr_err("[ALSA %s %d fail]\n", __func__, __LINE__);
		ret = -1;
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:
	if (vaddr)
		dma_free_coherent(dev, size, vaddr, dat);

	return ret;
}

/* get AI ID */
int RPC_TOAGENT_GET_AI_AGENT_AFW(phys_addr_t paddr, void *vaddr,
		struct snd_card_RTK_capture_pcm *dpcm)
{
	struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *in = NULL;
	struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *out;
	int offset;
	uint32_t ret = 0;
	phys_addr_t dat;

	in = (struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	memset(in, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	out = (struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *)((unsigned long)in + offset);
	in->type = htonl(ENUM_PRIVATEINFO_AIO_GET_AUDIO_PROCESSING_AI);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&ret)) {
		pr_err("[fail %s %d]\n", __func__, __LINE__);
		goto exit;
	}

	dpcm->AIAgentID = ntohl(out->privateInfo[0]);
	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

/* get AO volume */
int RPC_TOAGENT_GET_VOLUME_AFW(phys_addr_t paddr, void *vaddr)
{
	int volume = 0;
	struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *pArg = NULL;
	struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *pRet;
	int offset;
	uint32_t rc;
	phys_addr_t dat;

	pArg = (struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	memset(pArg, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	pRet = (struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *)((unsigned long)pArg + offset);
	pArg->type = htonl(ENUM_PRIVATEINFO_AUDIO_GET_MUTE_N_VOLUME);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_GET_MUTE_N_VOLUME,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&rc)) {
		pr_err("[fail %s %d]\n", __func__, __LINE__);
		volume = -1;
		goto exit;
	}

	volume = ntohl(pRet->privateInfo[1]);
	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
exit:
	return volume;
}

int RPC_TOAGENT_AI_CONFIG_HDMI_RX_IN_AFW(phys_addr_t paddr, void *vaddr,
			struct snd_card_RTK_capture_pcm *dpcm)
{
	struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS *rpc = NULL;
	uint32_t RPC_ret;
	unsigned int offset;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS));
	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS));
	rpc->instanceID = htonl(dpcm->AIAgentID);
	rpc->type = htonl(ENUM_PRIVATEINFO_AIO_AI_PRIVATEINFO);
	rpc->argateInfo[0] = htonl(ENUM_AI_PRIVATE_HDMI_RX);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_AI_CONFIG_I2S_IN_AFW(phys_addr_t paddr, void *vaddr,
			struct snd_card_RTK_capture_pcm *dpcm)
{
	struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *rpc = NULL;
	uint32_t RPC_ret;
	unsigned int offset;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	rpc->instanceID = htonl(dpcm->AIAgentID);
	rpc->type = htonl(ENUM_PRIVATEINFO_AUDIO_AI_PAD_IN);
	rpc->privateInfo[0] = htonl(48000);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:
	return ret;
}

int RPC_TOAGENT_AI_CONFIG_AUDIO_IN_AFW(phys_addr_t paddr, void *vaddr,
			struct snd_card_RTK_capture_pcm *dpcm)
{
	struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS *rpc = NULL;
	uint32_t RPC_ret;
	unsigned int offset;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS));
	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS));
	rpc->instanceID = htonl(dpcm->AIAgentID);
	rpc->type = htonl(ENUM_PRIVATEINFO_AIO_AI_PRIVATEINFO);

	if (dpcm->source_in == ENUM_AIN_AUDIO_V2)
		rpc->argateInfo[0] = htonl(ENUM_AI_PRIVATE_DUAL_DMIC_AND_LOOPBACK);
	else if (dpcm->source_in == ENUM_AIN_AUDIO_V3) {
		rpc->argateInfo[0] = htonl(ENUM_AI_PRIVATE_SPEECH_RECOGNITION_FROM_DMIC);
		rpc->argateInfo[1] = htonl(ENUM_AIN_AUDIO_PROCESSING_DMIC);
	} else if (dpcm->source_in == ENUM_AIN_AUDIO_V4) {
		rpc->argateInfo[0] = htonl(ENUM_AI_PRIVATE_SPEECH_RECOGNITION_FROM_DMIC);
		rpc->argateInfo[1] = htonl(ENUM_AIN_AUDIO_PROCESSING_I2S);
	}

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_AI_CONFIG_I2S_LOOPBACK_IN_AFW(phys_addr_t paddr, void *vaddr,
			struct snd_card_RTK_capture_pcm *dpcm)
{
	struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS *rpc = NULL;
	uint32_t RPC_ret;
	unsigned int offset;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS));
	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS));
	rpc->instanceID = htonl(dpcm->AIAgentID);
	rpc->type = htonl(ENUM_PRIVATEINFO_AIO_AI_LOOPBACK_AO);
	rpc->argateInfo[0] |= (1 << ENUM_RPC_AI_LOOPBACK_FROM_AO_I2S);
	rpc->argateInfo[0] = htonl(rpc->argateInfo[0]);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:
	return ret;
}

int RPC_TOAGENT_AI_CONFIG_DMIC_PASSTHROUGH_IN_AFW(phys_addr_t paddr, void *vaddr,
			struct snd_card_RTK_capture_pcm *dpcm)
{
	struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS *rpc = NULL;
	uint32_t RPC_ret;
	unsigned int offset;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS));
	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS));
	rpc->instanceID = htonl(dpcm->AIAgentID);
	rpc->type = htonl(ENUM_PRIVATEINFO_AIO_AI_PRIVATEINFO);
	rpc->argateInfo[0] = htonl(ENUM_AI_PRIVATE_ADC_DMIC);
	rpc->argateInfo[1] = htonl(16000);

	if (dpcm->dmic_volume[0] == 0 && dpcm->dmic_volume[1] == 0) {
		/* Set max volume */
		rpc->argateInfo[4] = htonl(0x303);
	} else {
		if (dpcm->dmic_volume[0] < 0)
			dpcm->dmic_volume[0] = 0;
		else if (dpcm->dmic_volume[0] > 3)
			dpcm->dmic_volume[0] = 3;

		if (dpcm->dmic_volume[1] < 0)
			dpcm->dmic_volume[1] = 0;
		else if (dpcm->dmic_volume[1] > 3)
			dpcm->dmic_volume[1] = 3;

		rpc->argateInfo[4] = (dpcm->dmic_volume[0] << 8) + dpcm->dmic_volume[1];
		rpc->argateInfo[4] = htonl(rpc->argateInfo[4]);
	}

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_CREATE_GLOBAL_AO_AFW(phys_addr_t paddr, void *vaddr, int *aoId)
{
	struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *rpc = NULL;
	struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *res;
	uint32_t RPC_ret;
	unsigned int offset;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	res = (struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *)((unsigned long)rpc + offset);
	rpc->type = htonl(ENUM_PRIVATEINFO_AUDIO_GET_GLOBAL_AO_INSTANCEID);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	*aoId = ntohl(res->privateInfo[0]);
	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_CREATE_AI_AGENT_AFW(phys_addr_t paddr, void *vaddr,
			struct snd_card_RTK_capture_pcm *dpcm)
{
	struct RPC_CREATE_AO_AGENT_T *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct RPC_CREATE_AO_AGENT_T *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct RPC_CREATE_AO_AGENT_T));
	rpc->info.instanceID = htonl(-1);
	rpc->info.type = htonl(AUDIO_IN);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_CREATE_AGENT,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + get_rpc_alignment_offset(sizeof(rpc->info))),
		&rpc->ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (ntohl(rpc->retval.result) != S_OK || rpc->ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	dpcm->AIAgentID = ntohl(rpc->retval.data);
	pr_info("[%s %s %d] [ALSA Create AI instance %d]\n", __FILE__, __func__, __LINE__, dpcm->AIAgentID);
	// success
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_AI_DISCONNECT_ALSA_AUDIO_AFW(phys_addr_t paddr, void *vaddr,
			struct snd_pcm_runtime *runtime)
{
	struct snd_card_RTK_capture_pcm *dpcm = runtime->private_data;
	struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *cmd = NULL;
	struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *res;
	uint32_t RPC_ret;
	int ret = -1;
	phys_addr_t dat;
	unsigned int offset;

	cmd = (struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	res = (struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *)((unsigned long)cmd + offset);

	memset(cmd, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	cmd->instanceID = htonl(dpcm->AIAgentID);
	cmd->type = htonl(ENUM_PRIVATEINFO_AUDIO_AI_CONNECT_ALSA);
	cmd->privateInfo[0] = htonl(AUDIO_ALSA_FORMAT_NONE);
	cmd->privateInfo[1] = htonl(runtime->rate);

	if (dpcm->source_in == ENUM_AIN_AUDIO)
		cmd->privateInfo[2] = htonl(1);
	else
		cmd->privateInfo[2] = htonl(0);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_AI_CONNECT_ALSA_AFW(phys_addr_t paddr, void *vaddr,
			struct snd_pcm_runtime *runtime)
{
	struct snd_card_RTK_capture_pcm *dpcm = runtime->private_data;
	struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *cmd = NULL;
	struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *res;
	uint32_t RPC_ret;
	int ret = -1;
	phys_addr_t dat;
	unsigned int offset;

	cmd = (struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	res = (struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *)((unsigned long)cmd + offset);

	memset(cmd, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	cmd->instanceID = htonl(dpcm->AIAgentID);
	cmd->type = htonl(ENUM_PRIVATEINFO_AUDIO_AI_CONNECT_ALSA);
	cmd->privateInfo[0] = htonl(dpcm->nAIFormat);
	cmd->privateInfo[1] = htonl(runtime->rate);

	switch (dpcm->source_in) {
	case ENUM_AIN_AUDIO:
		cmd->privateInfo[2] = htonl(1);
		break;
	default:
		cmd->privateInfo[2] = htonl(0);
		break;
	}

	switch (dpcm->source_in) {
	case ENUM_AIN_AUDIO_V2:
	case ENUM_AIN_AUDIO_V3:
		cmd->privateInfo[3] = htonl(1); //1 channel
		break;
	default:
		cmd->privateInfo[3] = htonl(0); //channels
		break;
	}

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_AI_CONNECT_AO_AFW(phys_addr_t paddr, void *vaddr,
			struct snd_card_RTK_capture_pcm *dpcm)
{
	struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *cmd = NULL;
	struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *res;
	uint32_t RPC_ret;
	int ret = -1;
	phys_addr_t dat;
	unsigned int offset;

	cmd = (struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	res = (struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *)((unsigned long)cmd + offset);

	memset(cmd, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	cmd->instanceID = htonl(dpcm->AIAgentID);
	cmd->type = htonl(ENUM_PRIVATEINFO_AUDIO_AI_SET_AO_FLASH_PIN);
	cmd->privateInfo[0] = htonl(dpcm->AOpinID);

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_SET_LOW_WATER_LEVEL(phys_addr_t paddr, void *vaddr, bool isLowWater)
{
	struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *cmd = NULL;
	struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *res;
	uint32_t RPC_ret;
	int ret = -1;
	phys_addr_t dat;
	unsigned long offset;

	cmd = (struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	res = (struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *)((unsigned long)cmd + offset);
	memset(cmd, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	memset(res, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_RETURNVAL));
	cmd->type = htonl(ENUM_PRIVATEINFO_AUDIO_SET_LOW_WATERLEVEL);
	if (isLowWater) {
		cmd->privateInfo[0] = htonl(0x1);
		pr_info("Enable pp ao low waterlevel mode\n");
	} else {
		cmd->privateInfo[0] = 0;
		pr_info("Disable pp ao low waterlevel mode\n");
	}
	cmd->privateInfo[1] = 0;
	cmd->privateInfo[2] = 0;
	cmd->privateInfo[3] = 0;

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat), //cmd address
		CONVERT_FOR_AVCPU(dat + offset),//res address
		&RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	// successful
	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int RPC_TOAGENT_SET_MAX_LATENCY_AFW(phys_addr_t paddr, void *vaddr,
			struct snd_card_RTK_pcm *dpcm)
{
	struct AUDIO_RPC_DEC_PRIVATEINFO_PARAMETERS *cmd;
	struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *res;
	uint32_t RPC_ret;
	int ret = 0;
	phys_addr_t dat;
	unsigned long offset;

	cmd = (struct AUDIO_RPC_DEC_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct AUDIO_RPC_DEC_PRIVATEINFO_PARAMETERS));
	res = (struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *)((unsigned long)cmd + offset);
	memset(cmd, 0, sizeof(struct AUDIO_RPC_DEC_PRIVATEINFO_PARAMETERS));
	memset(res, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_RETURNVAL));
	cmd->instanceID = htonl(dpcm->DECAgentID);
	cmd->type = htonl(ENUM_PRIVATEINFO_DEC_ALSA_CONFIG);
	cmd->privateInfo[0] = htonl(11); /* max latency of dec out(ms) */
	cmd->privateInfo[1] = htonl(30); /* max latency of ao out(ms) */

	if (send_rpc(acpu_ept_info,
		ENUM_KERNEL_RPC_DEC_PRIVATEINFO,
		CONVERT_FOR_AVCPU(dat), //cmd address
		CONVERT_FOR_AVCPU(dat + offset),//res address
		&RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __FUNCTION__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __FUNCTION__, __LINE__);
		goto exit;
	}

exit:

	return ret;
}

MODULE_LICENSE("GPL v2");
