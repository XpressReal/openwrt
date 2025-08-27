// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Realtek IPC Service driver
 *
 * Copyright (c) 2017 Realtek Semiconductor Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <soc/realtek/rtk-krpc-agent.h>
#include <linux/dma-map-ops.h>
#include <soc/realtek/rtk_media_heap.h>
#include <soc/realtek/memory.h>
#include <linux/mutex.h>

DEFINE_MUTEX(urpc_mutex);

#define URPC_IOC_MAGIC 'b'
#define URPC_IOCTGETGPID _IOR(URPC_IOC_MAGIC, 0x1, int)
#define URPC_IOCTGETGTGID _IOR(URPC_IOC_MAGIC, 0x2, int)
#define URPC_IOCSETRCPUSTAT _IOW(URPC_IOC_MAGIC, 0x3, int)
#define URPC_IOCRESETRCPUSTAT _IOW(URPC_IOC_MAGIC, 0x4, int)

#define AUDIO_ION_FLAG \
		(RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC | RTK_FLAG_ACPUACC)

struct rcpu_ept {
	struct rtk_krpc_ept_info *acpu_ept_info;
	struct rtk_krpc_ept_info *vcpu_ept_info;
	struct rtk_krpc_ept_info *hifi_ept_info;
};

struct rcpu_ept self_destroy_epts;

struct device *urpc_dev;

struct proc_rpc_stat {
	pid_t tgid;
	int acpu;
	int vcpu;
	int ve3;
	int hifi;
	struct list_head list;
};

struct proc_rpc_stat proc_lists;
struct mutex proc_mutex;

enum ENUM_AUDIO_KERNEL_RPC_CMD {
	ENUM_KERNEL_RPC_CREATE_AGENT,   // 0
	ENUM_KERNEL_RPC_INIT_RINGBUF,
	ENUM_KERNEL_RPC_PRIVATEINFO,
	ENUM_KERNEL_RPC_RUN,
	ENUM_KERNEL_RPC_PAUSE,
	ENUM_KERNEL_RPC_SWITCH_FOCUS,   // 5
	ENUM_KERNEL_RPC_MALLOC_ADDR,
	ENUM_KERNEL_RPC_VOLUME_CONTROL,      // AUDIO_CONFIG_COMMAND
	ENUM_KERNEL_RPC_FLUSH,               // AUDIO_RPC_SENDIO
	ENUM_KERNEL_RPC_CONNECT,             // AUDIO_RPC_CONNECTION
	ENUM_KERNEL_RPC_SETREFCLOCK,    // 10     // AUDIO_RPC_REFCLOCK
	ENUM_KERNEL_RPC_DAC_I2S_CONFIG,      // AUDIO_CONFIG_DAC_I2S
	ENUM_KERNEL_RPC_DAC_SPDIF_CONFIG,    // AUDIO_CONFIG_DAC_SPDIF
	ENUM_KERNEL_RPC_HDMI_OUT_EDID,       // AUDIO_HDMI_OUT_EDID_DATA
	ENUM_KERNEL_RPC_HDMI_OUT_EDID2,      // AUDIO_HDMI_OUT_EDID_DATA2
	ENUM_KERNEL_RPC_HDMI_SET,       // 15     // AUDIO_HDMI_SET
	ENUM_KERNEL_RPC_HDMI_MUTE,           //AUDIO_HDMI_MUTE_INFO
	ENUM_KERNEL_RPC_ASK_DBG_MEM_ADDR,
	ENUM_KERNEL_RPC_DESTROY,
	ENUM_KERNEL_RPC_STOP,
	ENUM_KERNEL_RPC_CHECK_READY,     // 20    // check if Audio get memory pool from AP
	ENUM_KERNEL_RPC_GET_MUTE_N_VOLUME,   // get mute and volume level
	ENUM_KERNEL_RPC_EOS,
	ENUM_KERNEL_RPC_ADC0_CONFIG,
	ENUM_KERNEL_RPC_ADC1_CONFIG,
	ENUM_KERNEL_RPC_ADC2_CONFIG,    // 25
#if defined(AUDIO_TV_PLATFORM)
	ENUM_KERNEL_RPC_BBADC_CONFIG,
	ENUM_KERNEL_RPC_I2SI_CONFIG,
	ENUM_KERNEL_RPC_SPDIFI_CONFIG,
#endif // AUDIO_TV_PLATFORM
	ENUM_KERNEL_RPC_HDMI_OUT_VSDB,
	ENUM_VIDEO_KERNEL_RPC_CONFIG_TV_SYSTEM,
	ENUM_VIDEO_KERNEL_RPC_CONFIG_HDMI_INFO_FRAME,
	ENUM_VIDEO_KERNEL_RPC_QUERY_DISPLAY_WIN,
	ENUM_VIDEO_KERNEL_RPC_PP_INIT_PIN,
	ENUM_KERNEL_RPC_INIT_RINGBUF_AO, //need check this enum
	ENUM_VIDEO_KERNEL_RPC_VOUT_EDID_DATA,
	ENUM_KERNEL_RPC_AUDIO_POWER_SET,
	ENUM_VIDEO_KERNEL_RPC_VOUT_VDAC_SET,
	ENUM_VIDEO_KERNEL_RPC_QUERY_CONFIG_TV_SYSTEM,
	ENUM_KERNEL_RPC_AUDIO_CONFIG,
	ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
	ENUM_KERNEL_RPC_QUERY_FW_DEBUG_INFO,
	ENUM_KERNEL_RPC_HDMI_RX_LATENCY_MEM,
	ENUM_KERNEL_RPC_EQ_CONFIG,
};

enum AUDIO_ENUM_PRIVAETINFO {
	ENUM_PRIVATEINFO_AUDIO_FORMAT_PARSER_CAPABILITY = 0,
	ENUM_PRIVATEINFO_AUDIO_DECODER_CAPABILITY = 1,
	ENUM_PRIVATEINFO_AUDIO_CONFIG_CMD_BS_INFO = 2,
	ENUM_PRIVATEINFO_AUDIO_CHECK_LPCM_ENDIANESS = 3,
	ENUM_PRIVATEINFO_AUDIO_CONFIG_CMD_AO_DELAY_INFO = 4,
	ENUM_PRIVATEINFO_AUDIO_AO_CHANNEL_VOLUME_LEVEL = 5,
	ENUM_PRIVATEINFO_AUDIO_GET_FLASH_PIN = 6,
	ENUM_PRIVATEINFO_AUDIO_RELEASE_FLASH_PIN = 7,
	ENUM_PRIVATEINFO_AUDIO_GET_MUTE_N_VOLUME = 8,
	ENUM_PRIVATEINFO_AUDIO_AO_MONITOR_FULLNESS = 9,
	ENUM_PRIVATEINFO_AUDIO_CONTROL_FLASH_VOLUME = 10,
	ENUM_PRIVATEINFO_AUDIO_CONTROL_DAC_SWITCH = 11,
	ENUM_PRIVATEINFO_AUDIO_PREPROCESS_CONFIG = 12,
	ENUM_PRIVATEINFO_AUDIO_CHECK_SECURITY_ID = 13,
	ENUM_PRIVATEINFO_AUDIO_LOW_DELAY_PARAMETERS = 14,
	ENUM_PRIVATEINFO_AUDIO_SET_NETWORK_JITTER = 15,
	ENUM_PRIVATEINFO_AUDIO_GET_QUEUE_DATA_SIZE = 16,
	ENUM_PRIVATEINFO_AUDIO_GET_SHARE_MEMORY_FROM_ALSA = 17,
	ENUM_PRIVATEINFO_AUDIO_AI_CONNECT_ALSA = 18,
	ENUM_PRIVATEINFO_AUDIO_SET_PCM_FORMAT = 19,
	ENUM_PRIVATEINFO_AUDIO_DO_SELF_DESTROY_FLOW = 20,
	ENUM_PRIVATEINFO_AUDIO_GET_SAMPLING_RATE = 21,
	ENUM_PRIVATEINFO_AUDIO_SLAVE_TIMEOUT_THRESHOLD = 22,
	ENUM_PRIVATEINFO_AUDIO_GET_GLOBAL_AO_INSTANCEID = 23,
	ENUM_PRIVATEINFO_AUDIO_SET_CEC_PARAMETERS = 24,
	ENUM_PRIVATEINFO_AUDIO_INIT_DBG_DUMP_MEM = 25,
	ENUM_PRIVATEINFO_AUDIO_AI_GET_AO_FLASH_PIN = 26,
	ENUM_PRIVATEINFO_AUDIO_AI_SET_AO_FLASH_PIN = 27,
	ENUM_PRIVATEINFO_AUDIO_GET_PP_FREE_PINID = 28,
	ENUM_PRIVATEINFO_AUDIO_HDMI_RX_CONNECT_TO_BT = 29,
	ENUM_PRIVATEINFO_AUDIO_GET_BS_ERR_RATE = 30,
	ENUM_PRIVATEINFO_AUDIO_SET_RESUME_IR_KEYS = 31,
	ENUM_PRIVATEINFO_SET_GSTREAMER_PTS_ACC_MODE = 32,
	ENUM_PRIVATEINFO_AUDIO_GET_BONDING_TYPE = 33,
	ENUM_PRIVATEINFO_AUDIO_SHARE_MEMORY_FOR_PORTING_FIRMWARE = 34,
	ENUM_PRIVATEINFO_AUDIO_SET_DVDPLAYER_AO_VERSION = 35,
	ENUM_PRIVATEINFO_AUDIO_MS_PP_CERT = 36,
	ENUM_PRIVATEINFO_AUDIO_TRIGGER_EVENT = 37,
	ENUM_PRIVATEINFO_AUDIO_AI_NON_PCM_IN = 38,
	ENUM_PRIVATEINFO_OMX_AUDIO_VERSION = 39,
	ENUM_PRIVATEINFO_AUDIO_AI_PAD_IN = 40,
	ENUM_PRIVATEINFO_AUDIO_MS_MAJOR_DECODER_PIN = 41,
	ENUM_PRIVATEINFO_AUDIO_PROVIDE_RAWOUT_LATENCY = 42,
	ENUM_PRIVATEINFO_AUDIO_MS_MIXER_IGNORE_PIN = 43,
	ENUM_PRIVATEINFO_AUDIO_MS_CERTIFICATION_PLATFORM = 44,
	ENUM_PRIVATEINFO_AUDIO_MS_MIXER_PIN_NEW_SEG = 45,
	ENUM_PRIVATEINFO_AUDIO_MS_DEC_DROP_BY_PTS = 46,
	ENUM_PRIVATEINFO_AUDIO_MS_DEC_INIT_PTS_OFFSET = 47,
	ENUM_PRIVATEINFO_AUDIO_MS_PP_OUTPUT_TYPE = 48,
	ENUM_PRIVATEINFO_AUDIO_DTS_ENCODER_CONFIG = 49,
	ENUM_PRIVATEINFO_AUDIO_GET_FW_VERSION = 50,
	ENUM_PRIVATEINFO_AUDIO_DTS_M8_IN_CONFIG = 51,
	ENUM_PRIVATEINFO_AUDIO_DTS_M8_LA_NUM = 52,
	ENUM_PRIVATEINFO_AUDIO_DTS_M8_SET_OUTPUT_FORMAT = 53,
	ENUM_PRIVATEINFO_AUDIO_SET_DRC_CFG = 54,
	ENUM_PRIVATEINFO_AUDIO_DTS_M8_LA_ERROR_MSG = 55,
	ENUM_PRIVATEINFO_GET_B_VALUE = 56,
	ENUM_PRIVATEINFO_AUDIO_ENTER_SUSPEND = 57,
	ENUM_PRIVATEINFO_AUDIO_MPEGH_IN_CONFIG = 58,
	ENUM_PRIVATEINFO_AUDIO_SET_LOW_WATERLEVEL = 59,
};

struct AUDIO_RPC_PRIVATEINFO_PARAMETERS {
	int instanceID;
	enum AUDIO_ENUM_PRIVAETINFO type;
	volatile int privateInfo[16];
};

struct AUDIO_RPC_PRIVATEINFO_RETURNVAL {
	int instanceID;
	volatile int privateInfo[16];
};

void add_rcpu(struct proc_rpc_stat *rpc_stat, int type)
{

	switch (type) {
	case AUDIO_ID:
		rpc_stat->acpu = 1;
		break;
	case VIDEO_ID:
		rpc_stat->vcpu = 1;
		break;
	case VE3_ID:
		rpc_stat->ve3 = 1;
		break;
	case HIFI_ID:
		rpc_stat->hifi = 1;
		break;
	default:
		break;
	}

	return;

}

void remove_rcpu(struct proc_rpc_stat *rpc_stat, int type)
{
	switch (type) {
	case AUDIO_ID:
		rpc_stat->acpu = 0;
		break;
	case VIDEO_ID:
		rpc_stat->vcpu = 0;
		break;
	case VE3_ID:
		rpc_stat->ve3 = 0;
		break;
	case HIFI_ID:
		rpc_stat->hifi = 0;
		break;
	default:
		break;
	}

	return;

}

long rtk_urpc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	pid_t g_pid;
	pid_t g_tgid;
	int type;
	struct proc_rpc_stat *rpc_stat = filp->private_data;

	switch (cmd) {
	case URPC_IOCTGETGPID:
		g_tgid = task_tgid_nr(current);
		dev_dbg(urpc_dev, "[%s][URPC_IOCTGETGPID]get current global g_tgid:%d\n", __func__, g_tgid);
		if (copy_to_user((int __user *)arg, &g_tgid, sizeof(g_tgid))) {
			dev_err(urpc_dev, "[RPC_IOCTGETGPID] copy_to_user failed\n");
			ret = -EFAULT;
		}
		break;
	case URPC_IOCTGETGTGID:
		g_pid = task_pid_nr(current);
		dev_dbg(urpc_dev, "[%s][URPC_IOCTGETGTGID]get current global g_pid:%d\n", __func__, g_pid);
		if (copy_to_user((int __user *)arg, &g_pid, sizeof(g_tgid))) {
			dev_err(urpc_dev, "[RPC_IOCTGETGTGID] copy_to_user failed\n");
			ret = -EFAULT;
		}
		break;
	case URPC_IOCSETRCPUSTAT:
		if (copy_from_user(&type, (void __user *)arg, sizeof(type)))
			return -EFAULT;
		add_rcpu(rpc_stat, type);
		break;
	case URPC_IOCRESETRCPUSTAT:
		if (copy_from_user(&type, (void __user *)arg, sizeof(type)))
			return -EFAULT;
		remove_rcpu(rpc_stat, type);
		break;
	default:
		pr_warn("[urpc]: error ioctl command...\n");
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static char *prepare_rpc_data(struct rtk_krpc_ept_info *krpc_ept_info, uint32_t command, uint32_t param1, uint32_t param2, int *len)
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

int urpc_send_rpc(struct rtk_krpc_ept_info *krpc_ept_info, char *buf, int len, uint32_t *retval)
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
		dev_err(urpc_dev, "kernel rpc timeout: %s...\n", krpc_ept_info->name);
		rtk_krpc_dump_ringbuf_info(krpc_ept_info);
		mutex_unlock(&krpc_ept_info->send_mutex);
		return -EINVAL;
	}
	mutex_unlock(&krpc_ept_info->send_mutex);

	return 0;
}

static int send_rpc(struct rtk_krpc_ept_info *ept_info, uint32_t command, uint32_t param1, uint32_t param2, uint32_t *retval)
{
	int ret = 0;
	char *buf;
	int len;

	buf = prepare_rpc_data(ept_info, command, param1, param2, &len);
	if (!IS_ERR(buf)) {
		ret = urpc_send_rpc(ept_info, buf, len, retval);
		kfree(buf);
	}

	return ret;
}

static int acpu_self_destroy(pid_t pid)
{
	struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *cmd = NULL;
	struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *res;
	uint32_t retval;
	int ret = -1;
	phys_addr_t dat;
	unsigned int offset;
	dma_addr_t paddr;
	void *vaddr;

	/* Allocate memory */
	mutex_lock(&urpc_mutex);
	rheap_setup_dma_pools(urpc_dev, NULL, AUDIO_ION_FLAG, __func__);
	vaddr = dma_alloc_coherent(urpc_dev, PAGE_SIZE, &paddr, GFP_KERNEL);
	mutex_unlock(&urpc_mutex);
	if (!vaddr) {
		dev_err(urpc_dev, "%s: cannot get vaddr (pid:%d)", __func__, pid);
		return 0;
	}
	dev_dbg(urpc_dev, "[%s]Allocated coherent memory, vaddr: 0x%0llX, paddr: 0x%0llX\n", __func__, (u64)vaddr, paddr);

	cmd = (struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;
	offset = ALIGN(sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS), 4);
	res = (struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *)((unsigned long)cmd + offset);
	memset_io(cmd, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	cmd->instanceID = htonl(-1);
	cmd->type = htonl(ENUM_PRIVATEINFO_AUDIO_DO_SELF_DESTROY_FLOW);
	cmd->privateInfo[0] = htonl(pid);

	dev_info(urpc_dev, "[%s]send self destroy(pid:%d)", __func__, pid);
	ret = send_rpc(self_destroy_epts.acpu_ept_info, ENUM_KERNEL_RPC_PRIVATEINFO, dat, dat + offset, &retval);
	if (ret) {
		dev_err(urpc_dev, "[%s]send rpc failed!\n", __func__);
		goto exit;
	}

	if (retval != S_OK) {
		dev_err(urpc_dev, "[%s]get rpc retval error!\n", __func__);
		goto exit;
	}

exit:
	dma_free_coherent(urpc_dev, PAGE_SIZE, vaddr, paddr);

	return ret;

}

static int vcpu_self_destroy(pid_t pid)
{
	uint32_t *pid_info;
	uint32_t *res;
	uint32_t retval;
	int ret = -1;
	phys_addr_t dat;
	unsigned int offset;
	dma_addr_t paddr;
	void *vaddr;

	mutex_lock(&urpc_mutex);
	rheap_setup_dma_pools(urpc_dev, NULL, AUDIO_ION_FLAG, __func__);
	vaddr = dma_alloc_coherent(urpc_dev, PAGE_SIZE, &paddr, GFP_KERNEL);
	mutex_unlock(&urpc_mutex);
	if (!vaddr) {
		dev_err(urpc_dev, "%s: cannot get vaddr (pid:%d)", __func__, pid);
		return 0;
	}
	dev_dbg(urpc_dev, "[%s]Allocated coherent memory, vaddr: 0x%0llX, paddr: 0x%0llX\n", __func__, (u64)vaddr, paddr);

	pid_info = (uint32_t *)vaddr;
	dat = paddr;
	offset = sizeof(uint32_t);
	res = (uint32_t *)(pid_info + offset);
	memset_io(pid_info, 0, sizeof(uint32_t));
	*pid_info =  htonl(pid);

	dev_info(urpc_dev, "[%s]send self destroy(pid:%d)", __func__, pid);

	ret = send_rpc(self_destroy_epts.vcpu_ept_info, 170, dat, dat + offset, &retval);
	if (ret) {
		dev_err(urpc_dev, "[%s]send rpc failed!\n", __func__);
		goto exit;
	}

	if (retval != S_OK) {
		dev_err(urpc_dev, "[%s]get rpc retval error!\n", __func__);
		goto exit;
	}

exit:
	dma_free_coherent(urpc_dev, PAGE_SIZE, vaddr, paddr);

	return ret;

}

static int hifi_self_destroy(pid_t pid)
{
	struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *cmd = NULL;
	struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *res;
	uint32_t retval;
	int ret = -1;
	phys_addr_t dat;
	unsigned int offset;
	dma_addr_t paddr;
	void *vaddr;

	/* Allocate memory */
	mutex_lock(&urpc_mutex);
	rheap_setup_dma_pools(urpc_dev, NULL, AUDIO_ION_FLAG, __func__);
	vaddr = dma_alloc_coherent(urpc_dev, PAGE_SIZE, &paddr, GFP_KERNEL);
	mutex_unlock(&urpc_mutex);
	if (!vaddr) {
		dev_err(urpc_dev, "%s: cannot get vaddr (pid:%d)", __func__, pid);
		return 0;
	}
	dev_dbg(urpc_dev, "[%s]Allocated coherent memory, vaddr: 0x%0llX, paddr: 0x%0llX\n", __func__, (u64)vaddr, paddr);

	cmd = (struct AUDIO_RPC_PRIVATEINFO_PARAMETERS *)vaddr;
	dat = paddr;
	offset = ALIGN(sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS), 128);
	res = (struct AUDIO_RPC_PRIVATEINFO_RETURNVAL *)((unsigned long)cmd + offset);
	memset_io(cmd, 0, sizeof(struct AUDIO_RPC_PRIVATEINFO_PARAMETERS));
	cmd->instanceID = htonl(-1);
	cmd->type = htonl(ENUM_PRIVATEINFO_AUDIO_DO_SELF_DESTROY_FLOW);
	cmd->privateInfo[0] = htonl(pid);

	dev_info(urpc_dev, "[%s]send self destroy(pid:%d)", __func__, pid);
	ret = send_rpc(self_destroy_epts.hifi_ept_info, ENUM_KERNEL_RPC_PRIVATEINFO, dat, dat + offset, &retval);
	if (ret) {
		dev_err(urpc_dev, "[%s]send rpc failed!\n", __func__);
		goto exit;
	}

	if (retval != S_OK) {
		dev_err(urpc_dev, "[%s]get rpc retval error!\n", __func__);
		goto exit;
	}

exit:
	dma_free_coherent(urpc_dev, PAGE_SIZE, vaddr, paddr);

	return ret;

}

static int rtk_urpc_release(struct inode *inode, struct file *filp)
{
	struct proc_rpc_stat *rpc_stat = filp->private_data;

	kfree(rpc_stat);

	return 0;

}

static int rtk_urpc_flush(struct file *filp, fl_owner_t id)
{
	struct proc_rpc_stat *rpc_stat = filp->private_data;
	struct task_struct *task;

	if (file_count(filp) > 1)
		return 0;

	task = pid_task(find_pid_ns(rpc_stat->tgid, &init_pid_ns), PIDTYPE_PID);
	if (task != NULL && (task->flags & PF_SIGNALED))
		task = NULL;

	if (task == NULL) {
		if (rpc_stat->acpu == 1)
			acpu_self_destroy(rpc_stat->tgid);

		if (rpc_stat->vcpu == 1)
			vcpu_self_destroy(rpc_stat->tgid);

		if (rpc_stat->hifi == 1)
			hifi_self_destroy(rpc_stat->tgid);
	}

	return 0;

}

int rtk_urpc_open(struct inode *inode, struct file *filp)
{
	struct proc_rpc_stat *rpc_stat;


	if (file_count(filp) > 1)
		return 0;

	rpc_stat = kzalloc(sizeof(*rpc_stat), GFP_KERNEL);
	filp->private_data = rpc_stat;
	rpc_stat->tgid = current->tgid;

	return 0;
}

static const struct file_operations rtk_urpc_fops = {
	.owner = THIS_MODULE,
	.open = rtk_urpc_open,
	.flush = rtk_urpc_flush,
	.release = rtk_urpc_release,
	.unlocked_ioctl = rtk_urpc_ioctl,
	.compat_ioctl = rtk_urpc_ioctl,
};

static int krpc_rcpu_cb(struct rtk_krpc_ept_info *krpc_ept_info, char *buf)
{
	uint32_t *tmp;
	struct rpc_struct *rpc = (struct rpc_struct *)buf;

	if (rpc->programID == REPLYID) {
		tmp = (uint32_t *)(buf + sizeof(struct rpc_struct));
		*(krpc_ept_info->retval) = *(tmp + 1);

		complete(&krpc_ept_info->ack);
	}

	return 0;
}

int self_destroy_ept_init(struct rtk_krpc_ept_info *krpc_ept_info, char *name)
{
	int ret = 0;

	ret = krpc_info_init(krpc_ept_info, name, krpc_rcpu_cb);

	return ret;
}

static int rtk_urpc_probe(struct platform_device *pdev)
{
	struct class *urpc_class = NULL;
	int ret = 0;
	struct cdev *cdev;
	struct device *dev;
	dev_t devno;

	ret = alloc_chrdev_region(&devno, 0, 1, "rtk_urpc");
	if (ret < 0) {
		dev_err(&pdev->dev, "alloc_chrdev_region failed");
		return ret;
	}

	cdev = cdev_alloc();
	if (IS_ERR(cdev)) {
		ret = PTR_ERR(cdev);
		goto unregister_devno;
	}

	cdev_init(cdev, &rtk_urpc_fops);
	cdev->owner = THIS_MODULE;
	ret = cdev_add(cdev, devno, 1);
	if (ret)
		goto free_cdev;

	urpc_class = class_create("rtk_urpc");
	if (IS_ERR(urpc_class)) {
		ret = PTR_ERR(urpc_class);
		goto unregister_cdev;
	}

	dev = device_create(urpc_class, NULL, devno, NULL, "rtk_urpc");
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto delete_class;
	}

	INIT_LIST_HEAD(&proc_lists.list);
	mutex_init(&proc_mutex);


	self_destroy_epts.acpu_ept_info = of_krpc_ept_info_get(pdev->dev.of_node, 0);
	if (IS_ERR(self_destroy_epts.acpu_ept_info)) {
		ret = PTR_ERR(self_destroy_epts.acpu_ept_info);
		if (ret == -EPROBE_DEFER) {
			dev_dbg(&pdev->dev, "acpu krpc ept info not ready, retry\n");
			goto delete_device;
		} else {
			dev_err(&pdev->dev, "failed to get acpu krpc ept info: %d\n", ret);
			self_destroy_epts.acpu_ept_info = NULL;
		}
	}

	self_destroy_epts.vcpu_ept_info = of_krpc_ept_info_get(pdev->dev.of_node, 1);
	if (IS_ERR(self_destroy_epts.vcpu_ept_info)) {
		ret = PTR_ERR(self_destroy_epts.vcpu_ept_info);
		if (ret == -EPROBE_DEFER) {
			if (!self_destroy_epts.acpu_ept_info)
				krpc_ept_info_put(self_destroy_epts.acpu_ept_info);
			dev_dbg(&pdev->dev, "vcpu krpc ept info not ready, retry\n");
			goto delete_device;
		} else {
			dev_err(&pdev->dev, "failed to get vcpu krpc ept info: %d\n", ret);
			self_destroy_epts.vcpu_ept_info = NULL;
		}
	}

	self_destroy_epts.hifi_ept_info = of_krpc_ept_info_get(pdev->dev.of_node, 2);
	if (IS_ERR(self_destroy_epts.hifi_ept_info)) {
		ret = PTR_ERR(self_destroy_epts.hifi_ept_info);
		if (ret == -EPROBE_DEFER) {
			if (!self_destroy_epts.acpu_ept_info)
				krpc_ept_info_put(self_destroy_epts.acpu_ept_info);
			if (!self_destroy_epts.vcpu_ept_info)
				krpc_ept_info_put(self_destroy_epts.vcpu_ept_info);
			dev_dbg(&pdev->dev, "hifi krpc ept info not ready, retry\n");
			goto delete_device;
		} else {
			dev_err(&pdev->dev, "failed to get hifi krpc ept info: %d\n", ret);
			self_destroy_epts.hifi_ept_info = NULL;
		}
	}

	if (self_destroy_epts.acpu_ept_info)
		self_destroy_ept_init(self_destroy_epts.acpu_ept_info, "acpu-self-destroy");

	if (self_destroy_epts.vcpu_ept_info)
		self_destroy_ept_init(self_destroy_epts.vcpu_ept_info, "vcpu-self-destroy");

	if (self_destroy_epts.hifi_ept_info)
		self_destroy_ept_init(self_destroy_epts.hifi_ept_info, "hifi-self-destroy");

	urpc_dev = &pdev->dev;
	set_dma_ops(urpc_dev, &rheap_dma_ops);

	dev_info(&pdev->dev, "probe\n");

	return 0;
delete_device:
	device_destroy(urpc_class, devno);

delete_class:
	class_destroy(urpc_class);

unregister_cdev:
	cdev_del(cdev);

free_cdev:
	kfree(cdev);

unregister_devno:
	unregister_chrdev_region(devno, 1);

	return ret;
}


static const struct of_device_id rtk_urpc_of_match[] = {
	{ .compatible = "realtek, urpc-service"},
	{},
};
MODULE_DEVICE_TABLE(of, rtk_urpc_of_match);

static struct platform_driver rtk_urpc_driver = {
	.probe = rtk_urpc_probe,
	.driver = {
		.name = "rtk-urpc",
		.of_match_table = rtk_urpc_of_match,
	},
};

static int __init rtk_urpc_init(void)
{

	return platform_driver_register(&rtk_urpc_driver);
}
late_initcall(rtk_urpc_init);

static void __exit rtk_urpc_exit(void)
{
	platform_driver_register(&rtk_urpc_driver);
}
module_exit(rtk_urpc_exit);

MODULE_LICENSE("GPL");
