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
#include <linux/ioctl.h>
#include <linux/syscalls.h>
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
#include "snd-hifi-realtek.h"

static int krpc_hifi_cb(struct rtk_krpc_ept_info *krpc_ept_info, char *buf)
{
	u32 *tmp;
	struct rpc_struct *rpc = (struct rpc_struct *)buf;

	if (rpc->programID == REPLYID) {
		tmp = (u32 *)(buf + sizeof(struct rpc_struct));
		*(krpc_ept_info->retval) = *(tmp + 1);

		complete(&krpc_ept_info->ack);
	}

	return 0;
}

int snd_ept_init(struct rtk_krpc_ept_info *krpc_ept_info)
{
	int ret = 0;

	ret = krpc_info_init(krpc_ept_info, "snd", krpc_hifi_cb);

	return ret;
}

static char *prepare_rpc_data(struct rtk_krpc_ept_info *krpc_ept_info,
			      u32 command, u32 param1,
			      u32 param2, int *len)
{
	struct rpc_struct *rpc;
	u32 *tmp;
	char *buf;

	*len = sizeof(struct rpc_struct) + 3 * sizeof(u32);
	buf = kmalloc(sizeof(struct rpc_struct) + 3 * sizeof(u32), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	rpc = (struct rpc_struct *)buf;
	rpc->programID = KERNELID;
	rpc->versionID = KERNELID;
	rpc->procedureID = 0;
	rpc->taskID = krpc_ept_info->id;
	rpc->sysTID = 0;
	rpc->sysPID = 0;
	rpc->parameterSize = 3 * sizeof(u32);
	rpc->mycontext = 0;
	tmp = (u32 *)(buf + sizeof(struct rpc_struct));
	*tmp = command;
	*(tmp + 1) = param1;
	*(tmp + 2) = param2;

	return buf;
}

static int snd_send_rpc(struct rtk_krpc_ept_info *krpc_ept_info, char *buf, int len, u32 *retval)
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
		pr_err("ALSA HIFI: kernel rpc timeout: %s...\n", krpc_ept_info->name);
		rtk_krpc_dump_ringbuf_info(krpc_ept_info);
		WARN_ON(1);
		mutex_unlock(&krpc_ept_info->send_mutex);
		return -EINVAL;
	}
	mutex_unlock(&krpc_ept_info->send_mutex);

	return 0;
}

static int send_rpc(struct rtk_krpc_ept_info *krpc_ept_info,
		    u32 command, u32 param1,
		    u32 param2, u32 *retval)
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

int rpc_create_ao_agent(phys_addr_t paddr, void *vaddr, int *ao_id,
			int pin_id)
{
	struct rpc_create_ao_agent_t *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct rpc_create_ao_agent_t *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct rpc_create_ao_agent_t));
	rpc->info.instance_id = 0;
	rpc->info.type = pin_id;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_CREATE_AGENT,
		     dat,
		     dat + get_rpc_alignment_offset(sizeof(rpc->info)),
		     &rpc->ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc->retval.result != S_OK || rpc->ret != S_OK) {
		pr_err("[ALSA %x %x %s %d RPC fail]\n",
		       rpc->retval.result, rpc->ret, __func__, __LINE__);
		goto exit;
	}

	*ao_id = rpc->retval.data;
	ret = 0;
	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
exit:
	return ret;
}

int rpc_put_share_memory_latency(phys_addr_t paddr, void *vaddr,
				 void *p, void *p2,
				 int dec_id, int ao_id, int type)
{
	struct audio_rpc_privateinfo_parameters *cmd = NULL;
	struct audio_rpc_privateinfo_returnval *res;
	int magic_num = 2379;
	u32 RPC_ret;
	phys_addr_t dat;
	unsigned long offset;

	pr_info("[%s ion_alloc p1 %x p2 %x dec_id %d ao_id %d type %d\n", __func__,
		(u32)((long)p), (u32)((long)p2), dec_id, ao_id, type);

	cmd = (struct audio_rpc_privateinfo_parameters *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_privateinfo_parameters));
	offset = ALIGN(offset, 128); /* HIFI 128 align */

	res = (struct audio_rpc_privateinfo_returnval *)((unsigned long)cmd + offset);
	memset(cmd, 0, sizeof(struct audio_rpc_privateinfo_parameters));

	cmd->type = type;
	cmd->private_info[0] = (u32)(long)p;
	cmd->private_info[1] = magic_num;
	cmd->private_info[2] = (u32)(long)p2;
	cmd->private_info[3] = (u32)dec_id;
	cmd->private_info[4] = (u32)ao_id;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_PRIVATEINFO,
		     dat,
		     dat + offset,
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

	return 0;
}

int rpc_get_ao_flash_pin(phys_addr_t paddr, void *vaddr, int ao_agent_id)
{
	struct audio_rpc_privateinfo_parameters *cmd = NULL;
	struct audio_rpc_privateinfo_returnval *res;
	u32 RPC_ret;
	int ret = -1;
	phys_addr_t dat;
	unsigned long offset;

	cmd = (struct audio_rpc_privateinfo_parameters *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_privateinfo_parameters));
	offset = ALIGN(offset, 128); /* HIFI 128 align */

	res = (struct audio_rpc_privateinfo_returnval *)((unsigned long)cmd + offset);
	memset(cmd, 0, sizeof(struct audio_rpc_privateinfo_parameters));
	memset(res, 0, sizeof(struct audio_rpc_privateinfo_returnval));
	cmd->instance_id = ao_agent_id;
	cmd->type = ENUM_PRIVATEINFO_AUDIO_GET_FLASH_PIN;
	cmd->private_info[0] = 0xFF;
	cmd->private_info[1] = 0xFF;
	cmd->private_info[2] = 0xFF;
	cmd->private_info[3] = 0xFF;
	cmd->private_info[4] = 0xFF;
	cmd->private_info[5] = 0xFF;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_PRIVATEINFO,
		     dat,
		     dat + offset,
		     &RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	ret = res->private_info[0];

	if (ret < FLASH_AUDIO_PIN_1 || ret > FLASH_AUDIO_PIN_3) {
		pr_err("[ALSA %s %d RPC %d fail]\n", __func__, __LINE__, ret);
		ret = -1;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
exit:
	return ret;
}

int rpc_set_ao_flash_volume(phys_addr_t paddr, void *vaddr,
			    struct snd_rtk_pcm *dpcm)
{
	struct audio_rpc_privateinfo_parameters *cmd = NULL;
	struct audio_rpc_privateinfo_returnval *res;
	u32 rpc_ret;
	int ret = -1;
	phys_addr_t dat;
	unsigned long offset;

	cmd = (struct audio_rpc_privateinfo_parameters *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_privateinfo_parameters));
	res = (struct audio_rpc_privateinfo_returnval *)((unsigned long)cmd + offset);

	memset(cmd, 0, sizeof(struct audio_rpc_privateinfo_parameters));
	cmd->instance_id = dpcm->ao_agent_id;
	cmd->type = ENUM_PRIVATEINFO_AUDIO_CONTROL_FLASH_VOLUME;
	cmd->private_info[0] = dpcm->ao_pin_id;
	cmd->private_info[1] = 31 - dpcm->volume;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_PRIVATEINFO,
		     dat,
		     dat + offset,
		     &rpc_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[ALSA set AO_pin %d volume %d]\n", dpcm->ao_pin_id, dpcm->volume);
	ret = 0;
exit:
	return ret;
}

int rpc_create_decoder_agent(phys_addr_t paddr, void *vaddr, int *dec_id,
			     int *pin_id)
{
	struct rpc_create_pcm_decoder_ctrl_t *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct rpc_create_pcm_decoder_ctrl_t *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct rpc_create_pcm_decoder_ctrl_t));
	rpc->instance.type = AUDIO_DECODER;
	rpc->instance.instance_id = -1;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_CREATE_AGENT,
		     dat,
		     dat + get_rpc_alignment_offset(sizeof(rpc->instance)),
		     &rpc->ret)) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc->ret != S_OK) {
		pr_err("[%s %d fail]\n", __func__, __LINE__);
		goto exit;
	}

	*dec_id = rpc->res.data;
	*pin_id = BASE_BS_IN;

	pr_info("[ALSA Create Decoder instance %d]\n", *dec_id);
	ret = 0;
exit:

	return ret;
}

int rpc_initringbuffer_header(phys_addr_t paddr, void *vaddr,
			      struct audio_rpc_ringbuffer_header *header,
			      int buffer_count)
{
	struct rpc_initringbuffer_header_t *rpc = NULL;
	int ch;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct rpc_initringbuffer_header_t *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct rpc_initringbuffer_header_t));
	rpc->header.instance_id = header->instance_id;
	rpc->header.pin_id = header->pin_id;
	rpc->header.read_idx = header->read_idx;
	rpc->header.list_size = header->list_size;

	pr_info(" header instance ID %d\n", header->instance_id);
	pr_info(" header pin_id       %d\n", header->pin_id);
	pr_info(" header read_idx     %d\n", header->read_idx);
	pr_info(" header list_size    %d\n", header->list_size);

	for (ch = 0; ch < buffer_count; ch++)
		rpc->header.ringbuffer_header_list[ch] = header->ringbuffer_header_list[ch];

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_INIT_RINGBUF,
		     dat,
		     dat + get_rpc_alignment_offset(sizeof(rpc->header)),
		     &rpc->res)) {
		pr_err("[%s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc->res != S_OK || rpc->ret.result != S_OK) {
		pr_err("[%s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int rpc_connect_svc(phys_addr_t paddr, void *vaddr,
		    struct audio_rpc_connection *pconnection)
{
	struct rpc_connection_t *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct rpc_connection_t *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct rpc_connection_t));
	rpc->out.src_instance_id = pconnection->src_instance_id;
	rpc->out.src_pin_id = pconnection->src_pin_id;
	rpc->out.des_instance_id = pconnection->des_instance_id;
	rpc->out.des_pin_id = pconnection->des_pin_id;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_CONNECT,
		     dat,
		     dat + get_rpc_alignment_offset(sizeof(rpc->out)),
		     &rpc->res)) {
		pr_err("[%s RPC fail %d]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc->res != S_OK || rpc->ret.result != S_OK) {
		pr_err("[%s RPC fail %d]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int rpc_pause_svc(phys_addr_t paddr, void *vaddr, int instance_id)
{
	struct rpc_pause_t *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct rpc_pause_t *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct rpc_pause_t));
	rpc->inst_id = instance_id;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_PAUSE,
		     dat,
		     dat +
		     sizeof(rpc->inst_id) +
		     sizeof(rpc->reserved),
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

int rpc_destroy_ai_flow_svc(phys_addr_t paddr, void *vaddr,
			    int instance_id, int hw_configed)
{
	struct audio_rpc_aio_privateinfo_parameters *rpc = NULL;
	int offset, ret = -1;
	u32 res;
	phys_addr_t dat;

	rpc = (struct audio_rpc_aio_privateinfo_parameters *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_aio_privateinfo_parameters));
	offset = ALIGN(offset, 128); /* HIFI 128 Align */

	memset(rpc, 0, sizeof(struct audio_rpc_aio_privateinfo_parameters));
	rpc->type = ENUM_PRIVATEINFO_AIO_ALSA_DESTROY_AI_FLOW;
	rpc->instance_id = instance_id;

	/* if hw didn't config, only destroy AI */
	if (hw_configed == 0)
		rpc->argate_info[0] = 0x23792379;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
		     dat,
		     dat + offset,
		     &res)) {
		pr_err("[%s RPC fail\n]", __func__);
		goto exit;
	}

	ret = 0;
exit:

	return ret;
}

int rpc_run_svc(phys_addr_t paddr, void *vaddr, int instance_id)
{
	struct rpc_t *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct rpc_t *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct rpc_t));
	rpc->inst_id = instance_id;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_RUN,
		     dat,
		     dat +
		     sizeof(rpc->inst_id) +
		     sizeof(rpc->reserved),
		&rpc->res)) {
		pr_err("[%s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc->res != S_OK || rpc->retval.result != S_OK) {
		pr_err("[%s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int rpc_flush_svc(phys_addr_t paddr, void *vaddr,
		  struct audio_rpc_sendio *sendio)
{
	struct rpc_flash_t *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct rpc_flash_t *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct rpc_flash_t));
	rpc->sendio.instance_id = sendio->instance_id;
	rpc->sendio.pin_id = sendio->pin_id;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_FLUSH,
		     dat,
		     dat + sizeof(rpc->sendio),
		     &rpc->res)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc->res != S_OK || rpc->retval.result != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int rpc_release_ao_flash_pin(phys_addr_t paddr, void *vaddr,
			     int ao_agent_id, int ao_pin_id)
{
	struct audio_rpc_privateinfo_parameters *cmd = NULL;
	struct audio_rpc_privateinfo_returnval *res;
	u32 rpc_ret = 0;
	int ret = -1;
	phys_addr_t dat;
	unsigned long offset;

	cmd = (struct audio_rpc_privateinfo_parameters *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_privateinfo_parameters));
	offset = ALIGN(offset, 128); /* HIFI 128 Align */

	res = (struct audio_rpc_privateinfo_returnval *)((unsigned long)cmd + offset);
	memset(cmd, 0, sizeof(struct audio_rpc_privateinfo_parameters));

	cmd->instance_id = ao_agent_id;
	cmd->type = ENUM_PRIVATEINFO_AUDIO_RELEASE_FLASH_PIN;
	cmd->private_info[0] = ao_pin_id;
	cmd->private_info[1] = 0xFF;
	cmd->private_info[2] = 0xFF;
	cmd->private_info[3] = 0xFF;
	cmd->private_info[4] = 0xFF;
	cmd->private_info[5] = 0xFF;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_PRIVATEINFO,
		     dat,
		     dat + offset,
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

int rpc_ao_config_without_decoder(phys_addr_t paddr, void *vaddr,
				  struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_pcm *dpcm = runtime->private_data;
	struct audio_rpc_aio_privateinfo_parameters *cmd = NULL;
	struct audio_rpc_privateinfo_returnval *res;
	int ret = -1;
	int offset, tmp, ch;
	char *p;
	u32 RPC_ret;
	phys_addr_t dat;

	cmd = (struct audio_rpc_aio_privateinfo_parameters *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_aio_privateinfo_parameters));
	offset = ALIGN(offset, 128); /* HIFI 128 Align */

	p = (char *)&cmd->argate_info[3];
	memset(cmd, 0, sizeof(struct audio_rpc_aio_privateinfo_parameters));
	res = (struct audio_rpc_privateinfo_returnval *)((unsigned long)cmd + offset);

	cmd->type = ENUM_PRIVATEINFO_AIO_AO_FLASH_LPCM;
	cmd->instance_id = dpcm->ao_agent_id;

	tmp = dpcm->ao_pin_id & 0xff;
	cmd->argate_info[1] |= tmp;
	tmp = ((runtime->sample_bits >> 3) << 8);
	cmd->argate_info[1] |= tmp;
	tmp = AUDIO_LITTLE_ENDIAN << 16;
	cmd->argate_info[1] |= tmp;

	cmd->argate_info[2] = runtime->rate;
	for (ch = 0; ch < runtime->channels; ++ch)
		p[ch] = ch + 1;

	/* config ao lpcm out delay and ao hw buffer delay */
	cmd->argate_info[5] = (15 << 16);
	cmd->argate_info[5] |= 20;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
		     dat,
		     dat + offset,
		     &RPC_ret)) {
		pr_err("[%s %d RPC fail\n]", __func__, __LINE__);
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

int rpc_stop_svc(phys_addr_t paddr, void *vaddr, int instance_id)
{
	struct rpc_stop_t *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct rpc_stop_t *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct rpc_stop_t));
	rpc->instance_id = instance_id;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_STOP,
		     dat,
		     dat +
		     sizeof(rpc->instance_id) +
		     sizeof(rpc->reserved),
		&rpc->res)) {
		pr_err("[ALSA %s %d fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc->res != S_OK || rpc->retval.result != S_OK) {
		pr_err("[ALSA %s %d fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int rpc_destroy_svc(phys_addr_t paddr, void *vaddr, int instance_id)
{
	struct rpc_destroy_t *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct rpc_destroy_t *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct rpc_destroy_t));
	rpc->instance_id = instance_id;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_DESTROY,
		     dat,
		     dat +
		     sizeof(rpc->instance_id) +
		     sizeof(rpc->reserved),
		&rpc->res)) {
		pr_err("%s %d RPC fail\n", __FILE__, __LINE__);
		goto exit;
	}

	if (rpc->res != S_OK || rpc->retval.result != S_OK) {
		pr_err("%s %d RPC fail\n", __FILE__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

/* set AO volume */
int rpc_set_volume(struct device *dev, int volume)
{
	struct audio_config_command *config = NULL;
	unsigned int *res;
	u32 ret = 0;
	phys_addr_t dat;
	unsigned long offset;
	void *vaddr;
	size_t size = SZ_4K;

	mutex_lock(&dev->mutex);
	rheap_setup_dma_pools(dev, "rtk_media_heap",
			      RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC |
			      RTK_FLAG_ACPUACC, __func__);

	vaddr = dma_alloc_coherent(dev, size, &dat, GFP_KERNEL);
	if (!vaddr) {
		ret = -1;
		mutex_unlock(&dev->mutex);
		goto exit;
	}
	mutex_unlock(&dev->mutex);
	config = (struct audio_config_command *)vaddr;

	memset(config, 0, sizeof(struct audio_config_command));
	config->msd_id = AUDIO_CONFIG_CMD_VOLUME;
	config->value[0] = 31 - volume;

	offset = get_rpc_alignment_offset(sizeof(struct audio_config_command));
	offset = ALIGN(offset, 128); /* HIFI 128 align */

	res = (unsigned int *)((unsigned long)config + offset);

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_VOLUME_CONTROL,
		     dat,
		     dat + offset,
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

/* get AO volume */
int rpc_get_volume(phys_addr_t paddr, void *vaddr)
{
	int volume = 0;
	struct audio_rpc_privateinfo_parameters *p_arg = NULL;
	struct audio_rpc_privateinfo_returnval *p_ret;
	int offset;
	u32 rc;
	phys_addr_t dat;

	p_arg = (struct audio_rpc_privateinfo_parameters *)vaddr;
	dat = paddr;

	memset(p_arg, 0, sizeof(struct audio_rpc_privateinfo_parameters));
	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_privateinfo_parameters));
	offset = ALIGN(offset, 128); /* HIFI 128 Align */

	p_ret = (struct audio_rpc_privateinfo_returnval *)((unsigned long)p_arg + offset);
	p_arg->type = ENUM_PRIVATEINFO_AUDIO_GET_MUTE_N_VOLUME;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_GET_MUTE_N_VOLUME,
		     dat,
		     dat + offset,
		     &rc)) {
		pr_err("[fail %s %d]\n", __func__, __LINE__);
		volume = -1;
		goto exit;
	}

	volume = p_ret->private_info[1];
	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
exit:
	return volume;
}

int rpc_ai_config_hdmi_rx_in(phys_addr_t paddr, void *vaddr,
			     struct snd_rtk_cap_pcm *dpcm)
{
	struct audio_rpc_aio_privateinfo_parameters *rpc = NULL;
	u32 RPC_ret;
	unsigned int offset;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct audio_rpc_aio_privateinfo_parameters *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct audio_rpc_aio_privateinfo_parameters));
	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_aio_privateinfo_parameters));
	offset = ALIGN(offset, 128); /* HIFI 128 align */

	rpc->instance_id = dpcm->ai_agent_id;
	rpc->type = ENUM_PRIVATEINFO_AIO_AI_PRIVATEINFO;
	rpc->argate_info[0] = ENUM_AI_PRIVATE_HDMI_RX;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
		     dat,
		     dat + offset,
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

int rpc_ai_config_i2s_in(phys_addr_t paddr, void *vaddr,
			 struct snd_rtk_cap_pcm *dpcm)
{
	struct audio_rpc_privateinfo_parameters *rpc = NULL;
	u32 RPC_ret;
	unsigned int offset;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct audio_rpc_privateinfo_parameters *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct audio_rpc_privateinfo_parameters));
	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_privateinfo_parameters));
	offset = ALIGN(offset, 128); /* HIFI 128 align */

	rpc->instance_id = dpcm->ai_agent_id;
	rpc->type = ENUM_PRIVATEINFO_AUDIO_AI_PAD_IN;
	rpc->private_info[0] = 48000;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_PRIVATEINFO,
		     dat,
		     dat + offset,
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

int rpc_ai_config_audio_in(phys_addr_t paddr, void *vaddr,
			   struct snd_rtk_cap_pcm *dpcm)
{
	struct audio_rpc_aio_privateinfo_parameters *rpc = NULL;
	u32 RPC_ret;
	unsigned int offset;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct audio_rpc_aio_privateinfo_parameters *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct audio_rpc_aio_privateinfo_parameters));
	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_aio_privateinfo_parameters));
	offset = ALIGN(offset, 128); /* HIFI 128 align */

	rpc->instance_id = dpcm->ai_agent_id;
	rpc->type = ENUM_PRIVATEINFO_AIO_AI_PRIVATEINFO;

	if (dpcm->source_in == ENUM_AIN_AUDIO_V2) {
		rpc->argate_info[0] = ENUM_AI_PRIVATE_DUAL_DMIC_AND_LOOPBACK;
	} else if (dpcm->source_in == ENUM_AIN_AUDIO_V3) {
		rpc->argate_info[0] = ENUM_AI_PRIVATE_SPEECH_RECOGNITION_FROM_DMIC;
		rpc->argate_info[1] = ENUM_AIN_AUDIO_PROCESSING_DMIC;
	} else if (dpcm->source_in == ENUM_AIN_AUDIO_V4) {
		rpc->argate_info[0] = ENUM_AI_PRIVATE_SPEECH_RECOGNITION_FROM_DMIC;
		rpc->argate_info[1] = ENUM_AIN_AUDIO_PROCESSING_I2S;
	}

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
		     dat,
		     dat + offset,
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

int rpc_ai_config_i2s_loopback_in(phys_addr_t paddr, void *vaddr,
				  struct snd_rtk_cap_pcm *dpcm)
{
	struct audio_rpc_aio_privateinfo_parameters *rpc = NULL;
	u32 RPC_ret;
	unsigned int offset;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct audio_rpc_aio_privateinfo_parameters *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct audio_rpc_aio_privateinfo_parameters));
	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_aio_privateinfo_parameters));
	offset = ALIGN(offset, 128); /* HIFI 128 align */

	rpc->instance_id = dpcm->ai_agent_id;
	rpc->type = ENUM_PRIVATEINFO_AIO_AI_LOOPBACK_AO;
	rpc->argate_info[0] |= (1 << ENUM_RPC_AI_LOOPBACK_FROM_AO_I2S);

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
		     dat,
		     dat + offset,
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

int rpc_ai_config_dmic_in(phys_addr_t paddr, void *vaddr,
			  struct snd_rtk_cap_pcm *dpcm)
{
	struct audio_rpc_aio_privateinfo_parameters *rpc = NULL;
	u32 RPC_ret;
	unsigned int offset;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct audio_rpc_aio_privateinfo_parameters *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct audio_rpc_aio_privateinfo_parameters));
	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_aio_privateinfo_parameters));
	offset = ALIGN(offset, 128); /* HIFI 128 align */

	rpc->instance_id = dpcm->ai_agent_id;
	rpc->type = ENUM_PRIVATEINFO_AIO_AI_PRIVATEINFO;
	rpc->argate_info[0] = ENUM_AI_PRIVATE_ADC_DMIC;
	rpc->argate_info[1] = 16000;

	if (dpcm->dmic_volume[0] == 0 && dpcm->dmic_volume[1] == 0) {
		/* Set max volume */
		rpc->argate_info[4] = 0x303;
	} else {
		if (dpcm->dmic_volume[0] < 0)
			dpcm->dmic_volume[0] = 0;
		else if (dpcm->dmic_volume[0] > 3)
			dpcm->dmic_volume[0] = 3;

		if (dpcm->dmic_volume[1] < 0)
			dpcm->dmic_volume[1] = 0;
		else if (dpcm->dmic_volume[1] > 3)
			dpcm->dmic_volume[1] = 3;

		rpc->argate_info[4] = (dpcm->dmic_volume[0] << 8) + dpcm->dmic_volume[1];
	}

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
		     dat,
		     dat + offset,
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

int rpc_create_global_ao(phys_addr_t paddr, void *vaddr, int *ao_id)
{
	struct audio_rpc_privateinfo_parameters *rpc = NULL;
	struct audio_rpc_privateinfo_returnval *res;
	u32 RPC_ret;
	unsigned int offset;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct audio_rpc_privateinfo_parameters *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct audio_rpc_privateinfo_parameters));
	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_privateinfo_parameters));
	offset = ALIGN(offset, 128); /* HIFI 128 align */

	res = (struct audio_rpc_privateinfo_returnval *)((unsigned long)rpc + offset);
	rpc->type = ENUM_PRIVATEINFO_AUDIO_GET_GLOBAL_AO_INSTANCEID;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_PRIVATEINFO,
		     dat,
		     dat + offset,
		     &RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	*ao_id = res->private_info[0];
	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int rpc_set_ai_flash_volume(phys_addr_t paddr, void *vaddr,
			    struct snd_rtk_cap_pcm *dpcm,
			    unsigned int volume)
{
	struct audio_rpc_aio_privateinfo_parameters *rpc = NULL;
	struct audio_rpc_privateinfo_returnval *res;
	u32 rpc_ret;
	unsigned int offset;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct audio_rpc_aio_privateinfo_parameters *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct audio_rpc_aio_privateinfo_parameters));
	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_aio_privateinfo_parameters));
	res = (struct audio_rpc_privateinfo_returnval *)((unsigned long)rpc + offset);
	rpc->instance_id = dpcm->ai_agent_id;
	rpc->type = ENUM_PRIVATEINFO_AIO_AI_PRIVATEINFO;
	rpc->argate_info[0] = ENUM_AI_PRIVATE_ADC_SET_VOLUME;

	/* ADC left channel digital volume in 0.5 dB step, -33.5dB~30dB */
	rpc->argate_info[1] = volume;
	/* ADC right channel digital volume in 0.5 dB step, -33.5dB~30dB */
	rpc->argate_info[2] = volume;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
		     dat,
		     dat + offset,
		     &rpc_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	pr_info("[%s %s %d] success\n", __FILE__, __func__, __LINE__);
	ret = 0;
exit:

	return ret;
}

int rpc_ai_config_nonpcm_in(phys_addr_t paddr, void *vaddr,
			    struct snd_rtk_cap_pcm *dpcm)
{
	struct audio_rpc_privateinfo_parameters *rpc = NULL;
	u32 RPC_ret;
	unsigned int offset;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct audio_rpc_privateinfo_parameters *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct audio_rpc_privateinfo_parameters));
	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_privateinfo_parameters));
	rpc->instance_id = dpcm->ai_agent_id;
	rpc->type = ENUM_PRIVATEINFO_AUDIO_AI_NON_PCM_IN;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_PRIVATEINFO,
		     dat,
		     dat + offset,
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

int rpc_create_ai_agent(phys_addr_t paddr, void *vaddr,
			struct snd_rtk_cap_pcm *dpcm)
{
	struct rpc_create_ao_agent_t *rpc = NULL;
	int ret = -1;
	phys_addr_t dat;

	rpc = (struct rpc_create_ao_agent_t *)vaddr;
	dat = paddr;

	memset(rpc, 0, sizeof(struct rpc_create_ao_agent_t));
	rpc->info.instance_id = -1;
	rpc->info.type = AUDIO_IN;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_CREATE_AGENT,
		     dat,
		     dat + get_rpc_alignment_offset(sizeof(rpc->info)),
		     &rpc->ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (rpc->retval.result != S_OK || rpc->ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	dpcm->ai_agent_id = rpc->retval.data;
	pr_info("[%s] [ALSA Create AI instance %d]\n", __func__, dpcm->ai_agent_id);

	ret = 0;
exit:

	return ret;
}

int rpc_ai_connect_alsa(phys_addr_t paddr, void *vaddr,
			struct snd_pcm_runtime *runtime)
{
	struct snd_rtk_cap_pcm *dpcm = runtime->private_data;
	struct audio_rpc_privateinfo_parameters *cmd = NULL;
	struct audio_rpc_privateinfo_returnval *res;
	u32 RPC_ret;
	int ret = -1;
	phys_addr_t dat;
	unsigned int offset;

	cmd = (struct audio_rpc_privateinfo_parameters *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_privateinfo_parameters));
	offset = ALIGN(offset, 128); /* HIFI 128 align */

	res = (struct audio_rpc_privateinfo_returnval *)((unsigned long)cmd + offset);
	memset(cmd, 0, sizeof(struct audio_rpc_privateinfo_parameters));

	cmd->instance_id = dpcm->ai_agent_id;
	cmd->type = ENUM_PRIVATEINFO_AUDIO_AI_CONNECT_ALSA;
	cmd->private_info[0] = dpcm->ai_format;
	cmd->private_info[1] = runtime->rate;
	cmd->private_info[2] = 0;

	switch (dpcm->source_in) {
	case ENUM_AIN_AUDIO_V2:
	case ENUM_AIN_AUDIO_V3:
		/* 1 channel */
		cmd->private_info[3] = 1;
		break;
	default:
		/* channels */
		cmd->private_info[3] = 0;
		break;
	}

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_PRIVATEINFO,
		     dat,
		     dat + offset,
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

int rpc_ai_connect_ao(phys_addr_t paddr, void *vaddr,
		      struct snd_rtk_cap_pcm *dpcm)
{
	struct audio_rpc_privateinfo_parameters *cmd = NULL;
	struct audio_rpc_privateinfo_returnval *res;
	u32 RPC_ret;
	int ret = -1;
	phys_addr_t dat;
	unsigned int offset;

	cmd = (struct audio_rpc_privateinfo_parameters *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_privateinfo_parameters));
	offset = ALIGN(offset, 128); /* HIFI 128 align */

	res = (struct audio_rpc_privateinfo_returnval *)((unsigned long)cmd + offset);
	memset(cmd, 0, sizeof(struct audio_rpc_privateinfo_parameters));

	cmd->instance_id = dpcm->ai_agent_id;
	cmd->type = ENUM_PRIVATEINFO_AUDIO_AI_SET_AO_FLASH_PIN;
	cmd->private_info[0] = dpcm->ao_pin_id;

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_PRIVATEINFO,
		     dat,
		     dat + offset,
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

int rpc_set_max_latency(phys_addr_t paddr, void *vaddr,
			struct snd_rtk_pcm *dpcm)
{
	struct audio_rpc_dec_privateinfo_parameters *cmd;
	struct audio_rpc_privateinfo_returnval *res;
	u32 RPC_ret;
	phys_addr_t dat;
	unsigned long offset;

	cmd = (struct audio_rpc_dec_privateinfo_parameters *)vaddr;
	dat = paddr;

	offset = get_rpc_alignment_offset(sizeof(struct audio_rpc_dec_privateinfo_parameters));
	res = (struct audio_rpc_privateinfo_returnval *)((unsigned long)cmd + offset);
	memset(cmd, 0, sizeof(struct audio_rpc_dec_privateinfo_parameters));
	memset(res, 0, sizeof(struct audio_rpc_privateinfo_returnval));
	cmd->instance_id = dpcm->dec_agent_id;
	cmd->type = ENUM_PRIVATEINFO_DEC_ALSA_CONFIG;
	cmd->private_info[0] = 11; /* max latency of dec out(ms) */
	cmd->private_info[1] = 30; /* max latency of ao out(ms) */

	if (send_rpc(hifi_ept_info,
		     ENUM_KERNEL_RPC_DEC_PRIVATEINFO,
		     dat,
		     dat + offset,
		     &RPC_ret)) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

	if (RPC_ret != S_OK) {
		pr_err("[ALSA %s %d RPC fail]\n", __func__, __LINE__);
		goto exit;
	}

exit:

	return 0;
}

MODULE_LICENSE("GPL v2");
