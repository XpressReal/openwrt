#include <linux/rpmsg.h>
#include <linux/of.h>
#include <soc/realtek/rtk-krpc-agent.h>
#include "common.h"

static int krpc_notify_cb(struct rtk_krpc_ept_info *krpc_ept_info, char *buf)
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

static int snd_notify_ept_init(struct rtk_krpc_ept_info *krpc_ept_info)
{
	int ret = 0;

	ret = krpc_info_init(krpc_ept_info, "snd_notify", krpc_notify_cb);

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
		pr_err("SND Notify: kernel rpc timeout: %s...\n", krpc_ept_info->name);
		rtk_krpc_dump_ringbuf_info(krpc_ept_info);
		WARN_ON(1);
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

static inline u32 remote_align(u32 size, int remote_cpu)
{
	/* HIFI needs 128 align */
	/* CPU 1: HIFI, CPU 0: AFW */
	if (remote_cpu == 1)
		return ALIGN(get_rpc_alignment_offset(size), 128);
	else
		return get_rpc_alignment_offset(size);
}


int rtk_aio_ctrl_rpc_setup(struct device *dev, struct rtk_aio_ctrl_rpc *rpc)
{
	struct device_node *np = dev->of_node;
	int ret;
	struct rtk_krpc_ept_info *ept_info = NULL;


	/* Get destionation of remote cpu */
	ret = of_property_read_u32(np, "remote-cpu", &rpc->remote_cpu);
	if (ret) {
		dev_err(dev, "[%s] failed to get remote cpu info\n", __func__);
		rpc->remote_cpu = 0;
	}

	/* Init for RPMSG AFW/HIFI */
	if (IS_ENABLED(CONFIG_RPMSG_RTK_RPC)) {
		ept_info = of_krpc_ept_info_get(np, rpc->remote_cpu);
		if (IS_ERR(ept_info))
			return dev_err_probe(dev, PTR_ERR(ept_info),
					"failed to get krpc ept info: 0x%lx\n", PTR_ERR(ept_info));

		snd_notify_ept_init(ept_info);
	}
	rpc->notify_ept_info = ept_info;

	return 0;
}

int rtk_aio_ctrl_rpc_send_msg(struct rtk_aio_ctrl_rpc *rpc, u32 command, dma_addr_t addr, u32 size)
{
	u32 off = remote_align(size, rpc->remote_cpu);
	int rpc_ret;

	if (send_rpc(rpc->notify_ept_info, command, addr, addr + off, &rpc_ret))
		return -EINVAL;

	return rpc_ret != S_OK ? -EINVAL : 0;
}

u32 rtk_aio_ctrl_rpc_to_remote32(struct rtk_aio_ctrl_rpc *rpc, u32 val)
{
	return cpu_to_rpmsg32(rpc->notify_ept_info->rpdev, val);
}
