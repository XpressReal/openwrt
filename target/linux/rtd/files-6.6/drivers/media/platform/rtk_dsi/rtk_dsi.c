
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/reset.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <soc/realtek/memory.h>
#include <soc/realtek/rtk_media_heap.h>

#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>

#include "rtk_dsi_reg.h"
#include "rtk_dsi_rpc.h"
#include <soc/realtek/rtk-krpc-agent.h>


const char mipidsi_support_vic[] = {
	VIC_1280X720P60,
	VIC_1920X1080P60,
	VIC_1200_1920P_60HZ,
};

enum {
	DPTX_GET_SINK_CAPABILITY,
	DPTX_GET_RAW_EDID,
	DPTX_GET_LINK_STATUS,
	DPTX_GET_VIDEO_CONFIG,
	DPTX_SEND_AVMUTE,
	DPTX_CONFIG_TV_SYSTEM,
	DPTX_CONFIG_AVI_INFO,
	DPTX_SET_FREQUNCY,
	DPTX_SEND_AUDIO_MUTE,
	DPTX_SEND_AUDIO_VSDB_DATA,
	DPTX_SEND_AUDIO_EDID2,
	DPTX_CHECK_TMDS_SRC,
	// new added ioctl from kernel 4.9
	DPTX_GET_EDID_SUPPORT_LIST,
	DPTX_GET_OUTPUT_FORMAT,
	DPTX_SET_OUTPUT_FORMAT,
};

struct dsi_format_support {
	unsigned char vic;
	unsigned char reserved1;
	unsigned char reserved2;
	unsigned char reserved3;
};

struct dsi_support_list {
	struct dsi_format_support tx_support[25];
	unsigned int tx_support_size;
};

struct dsi_format_setting {
	unsigned char mode;
	unsigned char vic;
	unsigned char display_mode; // same source, different source..
	unsigned char reserved1;
};

struct rtk_dsi {
	struct device *dev;
	struct miscdevice miscdev;

	struct reset_control *rstc;

	void __iomem *reg;
	struct dsi_format_setting format;
	struct dsi_support_list list;
	unsigned char out_type;
	struct rtk_krpc_ept_info *acpu_ept_info;
};

static void rtk_dsi_hw_init(struct rtk_dsi *dsi)
{
	struct dsi_format_setting *format = &dsi->format;
	void __iomem *base = dsi->reg;
	unsigned int reg;

	writel(0xf000000, base + PAT_GEN);

	if (format->display_mode == 0)
		return;

	if (dsi->out_type == VIC_1920X1080P60 || dsi->out_type == VIC_1280X720P60)
		writel(0x3f4000, base + CLOCK_GEN);
	else
		writel(0x7f4000, base + CLOCK_GEN);

	writel(0x1632, base + WATCHDOG);
	writel(0x7000000, base + CTRL_REG);
	writel(0x1927c20, base + DF);

	if (dsi->out_type == VIC_1280X720P60) {
		writel(0x4260426, base + SSC2);
		writel(0x280F0F, base + SSC3);

	} else {
		writel(0x4c05ed, base + SSC2);
		writel(0x282219, base + SSC3);
	}
	writel(0x403592b, base + MPLL);
	writel(0x70d0100, base + TX_DATA1);
	writel(0x81d090f, base + TX_DATA2);
	writel(0x5091408, base + TX_DATA3);

	reg = readl(base + CLOCK_GEN);
	reg |= 0x7f0;
	writel(reg, base + CLOCK_GEN);
	writel(0x1927c3c, base + DF);
	writel(0x161a, base + WATCHDOG);

//	writel(0x7, base + INTE);
	if (dsi->out_type == VIC_1280X720P60) {
		writel(0x280f00, base + TC0);
		writel(0x502d0, base + TC2);
		writel(0x500056d, base + TC5);
		writel(0xDC006e, base + TC1);
		writel(0x140005, base + TC3);
	} else if (dsi->out_type == VIC_1920X1080P60) {
		writel(0x2c1680, base + TC0);
		writel(0x50438, base + TC2);
		writel(0x780073c, base + TC5);
		writel(0x940058, base + TC1);
		writel(0x240004, base + TC3);
	} else {
		writel(0x40e10, base + TC0);
		writel(0x10780, base + TC2);
		writel(0x4b0041b, base + TC5);
		writel(0x7a00a2, base + TC1);
		writel(0x190023, base + TC3);
	}
	writel(0x241032b, base + TC4);
//	writel(0x8808080, base + PAT_GEN);
	writel(0x7610031, base + CTRL_REG);
}

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


int dsi_ept_init(struct rtk_krpc_ept_info *krpc_ept_info)
{
	int ret = 0;

	ret = krpc_info_init(krpc_ept_info, "rtk-dsi", krpc_acpu_cb);

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

int dsi_send_rpc(struct device *dev, struct rtk_krpc_ept_info *krpc_ept_info, char *buf, int len, uint32_t *retval)
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
		dev_err(dev, "kernel rpc timeout: %s...\n", krpc_ept_info->name);
		rtk_krpc_dump_ringbuf_info(krpc_ept_info);
		mutex_unlock(&krpc_ept_info->send_mutex);
		return -EINVAL;
	}
	mutex_unlock(&krpc_ept_info->send_mutex);

	return 0;
}


static int send_rpc(struct rtk_dsi *dsi, int opt, uint32_t command, uint32_t param1, uint32_t param2, uint32_t *retval)
{
	int ret = 0;
	char *buf;
	int len;

	if (opt == RPC_AUDIO) {
		buf = prepare_rpc_data(dsi->acpu_ept_info, command, param1, param2, &len);
		if (!IS_ERR(buf)) {
			ret = dsi_send_rpc(dsi->dev, dsi->acpu_ept_info, buf, len, retval);
			kfree(buf);
		}
	}

	return ret;
}




static int rpc_dsi_query_tv_system(struct rtk_dsi *dsi,
				 struct VIDEO_RPC_VOUT_CONFIG_TV_SYSTEM *arg)
{
	struct VIDEO_RPC_VOUT_CONFIG_TV_SYSTEM *i_rpc = NULL;
	struct VIDEO_RPC_VOUT_CONFIG_TV_SYSTEM *o_rpc = NULL;
	int ret;
	u32 RPC_ret;
	phys_addr_t dat;
	unsigned int offset;
	struct device *dev = dsi->dev;

	i_rpc = dma_alloc_coherent(dev, SZ_4K, &dat, GFP_KERNEL);
	if (!i_rpc) {
		ret = -ENOMEM;
		goto out;
	}

	offset = get_rpc_alignment_offset(sizeof(struct VIDEO_RPC_VOUT_CONFIG_TV_SYSTEM));
	o_rpc = (struct VIDEO_RPC_VOUT_CONFIG_TV_SYSTEM *)((unsigned long)i_rpc + offset);

	if (send_rpc(dsi, RPC_AUDIO,  ENUM_VIDEO_KERNEL_RPC_QUERY_CONFIG_TV_SYSTEM,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + offset),
		&RPC_ret)) {

		pr_err("[%s %d RPC fail]", __func__, __LINE__);
		ret = -EFAULT;
		goto dma_end;
	}

	arg->interfaceType = htonl(o_rpc->interfaceType);
	arg->videoInfo.standard = htonl(o_rpc->videoInfo.standard);
	arg->videoInfo.enProg = o_rpc->videoInfo.enProg;
	arg->videoInfo.enDIF = o_rpc->videoInfo.enDIF;
	arg->videoInfo.enCompRGB = o_rpc->videoInfo.enCompRGB;
	arg->videoInfo.pedType = htonl(o_rpc->videoInfo.pedType);
	arg->videoInfo.dataInt0 = htonl(o_rpc->videoInfo.dataInt0);
	arg->videoInfo.dataInt1 = htonl(o_rpc->videoInfo.dataInt1);
	arg->hdmiInfo.hdmiMode = htonl(o_rpc->hdmiInfo.hdmiMode);
	arg->hdmiInfo.audioSampleFreq = htonl(o_rpc->hdmiInfo.audioSampleFreq);
	arg->hdmiInfo.audioChannelCount = o_rpc->hdmiInfo.audioChannelCount;
	arg->hdmiInfo.dataByte1 = o_rpc->hdmiInfo.dataByte1;
	arg->hdmiInfo.dataByte2 = o_rpc->hdmiInfo.dataByte2;
	arg->hdmiInfo.dataByte3 = o_rpc->hdmiInfo.dataByte3;
	arg->hdmiInfo.dataByte4 = o_rpc->hdmiInfo.dataByte4;
	arg->hdmiInfo.dataByte5 = o_rpc->hdmiInfo.dataByte5;
	arg->hdmiInfo.dataInt0 = htonl(o_rpc->hdmiInfo.dataInt0);
	arg->hdmiInfo.hdmi2px_feature = htonl(o_rpc->hdmiInfo.hdmi2px_feature);
	arg->hdmiInfo.hdmi_off_mode = htonl(o_rpc->hdmiInfo.hdmi_off_mode);
	arg->hdmiInfo.hdr_ctrl_mode = htonl(o_rpc->hdmiInfo.hdr_ctrl_mode);
	arg->hdmiInfo.reserved4 = htonl(o_rpc->hdmiInfo.reserved4);

	ret = 0;

dma_end:
	dma_free_coherent(dev, SZ_4K, i_rpc, dat);
out:
	return ret;
}

static int rpc_dsi_config_tv_system(struct rtk_dsi *dsi,
				struct VIDEO_RPC_VOUT_CONFIG_TV_SYSTEM *arg)
{
	struct VIDEO_RPC_VOUT_CONFIG_TV_SYSTEM *rpc = NULL;
	int ret;
	phys_addr_t dat;
	u32 RPC_ret;
	struct device *dev = dsi->dev;

	rpc = dma_alloc_coherent(dev, SZ_4K, &dat, GFP_KERNEL);
	if (!rpc) {
		ret = -ENOMEM;
		goto out;
	}

	memset(rpc, 0, sizeof(*rpc));

	rpc->interfaceType = htonl(arg->interfaceType);

	rpc->videoInfo.standard = htonl(arg->videoInfo.standard);
	rpc->videoInfo.enProg = arg->videoInfo.enProg;
	rpc->videoInfo.enDIF = arg->videoInfo.enDIF;
	rpc->videoInfo.enCompRGB = arg->videoInfo.enCompRGB;
	rpc->videoInfo.pedType  = htonl(arg->videoInfo.pedType);
	rpc->videoInfo.dataInt0 = htonl(arg->videoInfo.dataInt0);
	rpc->videoInfo.dataInt1 = htonl(arg->videoInfo.dataInt1);

	rpc->hdmiInfo.hdmiMode  = htonl(arg->hdmiInfo.hdmiMode);
	rpc->hdmiInfo.audioSampleFreq = htonl(arg->hdmiInfo.audioSampleFreq);
	rpc->hdmiInfo.audioChannelCount = arg->hdmiInfo.audioChannelCount;
	rpc->hdmiInfo.dataByte1 = arg->hdmiInfo.dataByte1;
	rpc->hdmiInfo.dataByte2 = arg->hdmiInfo.dataByte2;
	rpc->hdmiInfo.dataByte3 = arg->hdmiInfo.dataByte3;
	rpc->hdmiInfo.dataByte4 = arg->hdmiInfo.dataByte4;
	rpc->hdmiInfo.dataByte5 = arg->hdmiInfo.dataByte5;
	rpc->hdmiInfo.dataInt0  = htonl(arg->hdmiInfo.dataInt0);
	/* Assign hdmi2px_feature after send SCDC */
	rpc->hdmiInfo.hdmi_off_mode = htonl(arg->hdmiInfo.hdmi_off_mode);
	rpc->hdmiInfo.hdr_ctrl_mode = htonl(arg->hdmiInfo.hdr_ctrl_mode);
	rpc->hdmiInfo.reserved4 = htonl(arg->hdmiInfo.reserved4);
	rpc->hdmiInfo.hdmi2px_feature = htonl(arg->hdmiInfo.hdmi2px_feature);

	if (send_rpc(dsi, RPC_AUDIO, ENUM_VIDEO_KERNEL_RPC_CONFIG_TV_SYSTEM,
		CONVERT_FOR_AVCPU(dat),
		CONVERT_FOR_AVCPU(dat + get_rpc_alignment_offset(sizeof(struct VIDEO_RPC_VOUT_CONFIG_TV_SYSTEM))),
		&RPC_ret)) {

		pr_err("[%s %d RPC fail]", __func__, __LINE__);
		ret = -EFAULT;
		goto dma_end;
	}

	ret = 0;
dma_end:
	dma_free_coherent(dev, SZ_4K, rpc, dat);
out:
	return ret;
}

static unsigned char vo_standard_to_vic(unsigned int vo)
{
	switch(vo) {
	case VO_STANDARD_DSIMIPI_FORMAT_1200_1920P_60:
		return VIC_1200_1920P_60HZ;
	case VO_STANDARD_DP_FORMAT_1280_720P_60:
		return VIC_1280X720P60;
	case VO_STANDARD_DP_FORMAT_1920_1080P_60:
		return VIC_1920X1080P60;
	default:
		return VIC_1200_1920P_60HZ;
	}

}

static unsigned int vic_to_vo_standard(struct rtk_dsi *dsi,
			unsigned char vic)
{
	unsigned int standard;

	switch(vic) {
	case VIC_1200_1920P_60HZ:
		dsi->out_type = vic;
		standard = VO_STANDARD_DSIMIPI_FORMAT_1200_1920P_60;
		break;
	case VIC_1280X720P60:
		dsi->out_type = vic;
		standard = VO_STANDARD_DP_FORMAT_1280_720P_60;
		break;
	case VIC_1920X1080P60:
		dsi->out_type = vic;
		standard = VO_STANDARD_DP_FORMAT_1920_1080P_60;
		break;
	default:
		dsi->out_type = VIC_1200_1920P_60HZ;
		standard = VO_STANDARD_DSIMIPI_FORMAT_1200_1920P_60;
		break;
	}
	return standard;
}


static long rtk_dsi_ioctl(struct file* filp,
			unsigned int cmd, unsigned long arg)
{
	struct rtk_dsi *dsi = filp->private_data;
	struct device *dev = dsi->dev;
	struct VIDEO_RPC_VOUT_CONFIG_TV_SYSTEM rpc;
	struct dsi_format_setting *format = &dsi->format;
	struct dsi_support_list *list = &dsi->list;
	int i;

	switch (cmd) {
	case DPTX_GET_SINK_CAPABILITY:
		break;
	case DPTX_GET_EDID_SUPPORT_LIST:
		list->tx_support_size = sizeof(mipidsi_support_vic);
		for (i = 0; i < list->tx_support_size; i++)
			list->tx_support[i].vic = mipidsi_support_vic[i];
		if (copy_to_user((void __user *)arg, list, sizeof(struct dsi_support_list))) {
			dev_err(dev, "[%s] failed to copy to user !\n", __func__);
			return -EFAULT;
		}
		break;
	case DPTX_CONFIG_TV_SYSTEM:
		break;
	case DPTX_GET_OUTPUT_FORMAT:
		rpc_dsi_query_tv_system(dsi, &rpc);
		format->vic = vo_standard_to_vic(rpc.videoInfo.pedType);
		format->display_mode = rpc.interfaceType;
		dev_info(dev, "dsi format vic = %d, display_mode = %d\n", format->vic, format->display_mode);
		if (copy_to_user((void __user *)arg, format, sizeof(struct dsi_format_setting))) {
			dev_err(dev, "[%s] failed to copy to user !\n", __func__);
			return -EFAULT;
		}
		break;
	case DPTX_SET_OUTPUT_FORMAT:
		if (copy_from_user(format, (void __user *)arg, sizeof(format))) {
			dev_err(dev, "[%s] copy from user fail\n", __func__);
			break;
		}
		rpc_dsi_query_tv_system(dsi, &rpc);
		rpc.videoInfo.pedType = vic_to_vo_standard(dsi, format->vic);
		rpc.interfaceType = format->display_mode;
		dev_info(dev, "dsi set format vo = %d, display_mode = %d\n", rpc.videoInfo.pedType, rpc.interfaceType);

		rtk_dsi_hw_init(dsi);
		rpc_dsi_config_tv_system(dsi, &rpc);
		break;
	}
	return 0;
}

#ifdef CONFIG_64BIT
static long rtk_dsi_compat_ioctl(struct file* filp,
			unsigned int cmd, unsigned long arg)
{
	long (*ioctl)(struct file*, unsigned int, unsigned long);

	if (!filp->f_op->unlocked_ioctl)
		return -ENOTTY;

	ioctl = filp->f_op->unlocked_ioctl;

	switch(cmd) {
	case DPTX_GET_SINK_CAPABILITY:
	case DPTX_CONFIG_TV_SYSTEM:
	case DPTX_GET_EDID_SUPPORT_LIST:
	case DPTX_GET_OUTPUT_FORMAT:
	case DPTX_SET_OUTPUT_FORMAT:
		return ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
	default:
		break;
	}
	return 0;
}
#endif /* end of CONFIG_64BIT */

int rtk_dsi_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *misc = filp->private_data;
	struct rtk_dsi *dsi = container_of(misc, struct rtk_dsi, miscdev);

	if (nonseekable_open(inode, filp))
		return -ENODEV;

	filp->private_data = dsi;

	return 0;
}

static ssize_t rtk_dsi_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *pos)
{
	struct rtk_dsi *dsi = filp->private_data;
	struct VIDEO_RPC_VOUT_CONFIG_TV_SYSTEM rpc;

	rpc_dsi_query_tv_system(dsi, &rpc);
	rtk_dsi_hw_init(dsi);
	rpc_dsi_config_tv_system(dsi, &rpc);

	return count;
}

static struct file_operations dsi_fops = {
	.owner = THIS_MODULE,
	.open = rtk_dsi_open,
	.write = rtk_dsi_write,
	.unlocked_ioctl = rtk_dsi_ioctl,
#ifdef CONFIG_64BIT
	.compat_ioctl = rtk_dsi_compat_ioctl,
#endif /* end of CONFIG_64BIT */
};

static int rtk_dsi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtk_dsi *dsi;
	struct clk *clk;
	struct resource *iores;
	int ret = 0;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);
	dsi->dev = dev;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dsi->reg = devm_ioremap_resource(dev, iores);
	if (IS_ERR(dsi->reg))
		return PTR_ERR(dsi->reg);

	clk = devm_clk_get(dev, "clk_en_dsi");
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get clock\n");
		return PTR_ERR(clk);
	}
	clk_prepare_enable(clk);

	dsi->rstc = devm_reset_control_get(dev, "dsi");
	if (IS_ERR(dsi->rstc)) {
		dev_err(dev, "failed to get reset controller\n");
		return PTR_ERR(dsi->rstc);
	}
	reset_control_deassert(dsi->rstc);

	dsi->miscdev.minor = MISC_DYNAMIC_MINOR;
	dsi->miscdev.name = "dsi";
	dsi->miscdev.mode = 0666;
	dsi->miscdev.fops = &dsi_fops;
	if(misc_register(&dsi->miscdev)) {
		dev_err(dev, "[%s] misc register fail\n", __func__);
		return -ENODEV;
	}
	if (IS_ENABLED(CONFIG_RPMSG_RTK_RPC)) {
		dsi->acpu_ept_info = of_krpc_ept_info_get(dev->of_node, 0);
		if (IS_ERR(dsi->acpu_ept_info)) {
			ret = PTR_ERR(dsi->acpu_ept_info);
			if (ret == -EPROBE_DEFER) {
				dev_dbg(dev, "krpc ept info not ready, retry\n");
				return ret;
			} else {
				dev_err(dev, "failed to get krpc ept info: %d\n", ret);
				return ret;
			}
		}
		dsi_ept_init(dsi->acpu_ept_info);
	}

	set_dma_ops(dev, &rheap_dma_ops);
	rheap_setup_dma_pools(dev, "rtk_audio_heap", RTK_FLAG_NONCACHED |
				 RTK_FLAG_SCPUACC | RTK_FLAG_ACPUACC, __func__);

	dev->coherent_dma_mask = DMA_BIT_MASK(32);
	dev->dma_mask = (u64 *)&dev->coherent_dma_mask;
	return 0;
}

static const struct of_device_id rtk_dsi_dt_ids[] = {
	{ .compatible = "realtek,rtk-dsi", },
	{},
};
MODULE_DEVICE_TABLE(of, rtk_dsi_dt_ids);

static struct platform_driver rtk_dsi_driver = {
	.probe = rtk_dsi_probe,
	.driver = {
		.name = "rtk_dsi",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(rtk_dsi_dt_ids),
	},
};

static int __init rtk_dsi_init(void)
{
	if (platform_driver_register(&rtk_dsi_driver)) {
		return -EFAULT;
	}
	return 0;
}

static void __exit rtk_dsi_exit(void)
{

}

module_init(rtk_dsi_init);
module_exit(rtk_dsi_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek DSI kernel module");
