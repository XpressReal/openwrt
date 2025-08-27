// SPDX-License-Identifier: GPL-2.0-only
/*
 * rtk_smcc.c - RTK SMCC driver
 *
 * Copyright (c) 2020 Realtek Semiconductor Corp.
 */

#include <linux/arm-smccc.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "rtk_smcc_uapi.h"
#include "rtk_smcc.h"

#define RTK_SMCC_DEVICE_NAME "rtk_smcc_core"

struct rtk_smcc_device {
	struct device *dev;
	struct miscdevice mdev;
	struct mutex lock;
};

struct rtk_smcc_file_data {
	struct rtk_smcc_device *smcc_dev;
	void *va;
	dma_addr_t pa;
};

static int rtk_smcc_dev_open(struct inode *inode, struct file *file)
{
	struct rtk_smcc_device *smcc_dev = container_of(file->private_data,
							struct rtk_smcc_device, mdev);
	struct rtk_smcc_file_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->va = dma_alloc_coherent(smcc_dev->dev, PAGE_SIZE, &data->pa, GFP_KERNEL | GFP_DMA);
	if (!data->va) {
		kfree(data);
		return -ENOMEM;
	}

	data->smcc_dev = smcc_dev;
	file->private_data = data;
	return 0;
}

static int rtk_smcc_dev_release(struct inode *inode, struct file *file)
{
	struct rtk_smcc_file_data *data = file->private_data;

	dma_free_coherent(data->smcc_dev->dev, PAGE_SIZE, data->va, data->pa);
	kfree(data);
	return 0;
}

static int rtk_smcc_dev_ioctl_otp_read(struct rtk_smcc_file_data *data, struct otp_info *info)
{
	OPT_Data_T *opt_data_va = data->va;
	struct arm_smccc_res res;

	arm_smccc_smc(RTK_OTP_READ, info->typeID, data->pa,
		      data->pa + offsetof(OPT_Data_T, ret_value_h), 0, 0, 0, 0, &res);

	if (res.a0 != SMC_RETURN_OK)
		return -EPERM;

	info->ret_value = opt_data_va->ret_value;
	info->ret_value_h = opt_data_va->ret_value_h;
	return 0;
}

static int rtk_smcc_dev_ioctl_otp_write(struct rtk_smcc_file_data *data, struct otp_write_info *info)
{
	OPT_Data_T *opt_data_va = data->va;
	struct arm_smccc_res res;

	if (info->perform_case == DATA_SECTION_CASE) {
		memcpy(opt_data_va->burning_data, info->burning_data,
		       sizeof_field(struct otp_write_info, burning_data));

		arm_smccc_smc(RTK_OTP_WRITE, info->typeID,
			      data->pa + offsetof(OPT_Data_T, burning_data), 0, 0, 0, 0, 0,
			      &res);
	} else {
		arm_smccc_smc(RTK_OTP_WRITE, info->typeID, info->burning_value,
			      0, 0, 0, 0, 0, &res);
	}

	return res.a0 != SMC_RETURN_OK ? -EPERM : 0;
}

static long rtk_smcc_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rtk_smcc_file_data *data = file->private_data;
	struct rtk_smcc_device *smcc_dev = data->smcc_dev;
	struct device *dev = data->smcc_dev->dev;
	union {
		struct otp_info otp;
		struct otp_write_info otp_write;
	} args;
	int ret;

	switch (cmd) {
	case RTK_SMCC_OTP_READ:
		if (copy_from_user(&args.otp, (void __user *)arg, sizeof(args.otp)))
			return -EFAULT;

		dev_dbg(dev, "RTK_SMCC_OTP_READ: typeID:[%#x]\n", args.otp.typeID);

		mutex_lock(&smcc_dev->lock);
		ret = rtk_smcc_dev_ioctl_otp_read(data, &args.otp);
		mutex_unlock(&smcc_dev->lock);

		memset(data->va, 0, PAGE_SIZE);
		if (ret)
			return ret;

		dev_dbg(dev, "RTK_SMCC_OTP_READ: ret_value:[%#llx, %#llx]\n",
			args.otp.ret_value, args.otp.ret_value_h);

		if (copy_to_user((void __user *)arg, &args.otp, sizeof(args.otp)))
			return -EFAULT;

		break;

	case RTK_SMCC_OTP_WRITE:
		if (copy_from_user(&args.otp_write, (void __user *)arg, sizeof(args.otp_write)))
			return -EFAULT;

		dev_dbg(dev, "RTK_SMCC_OTP_WRITE: typeID:[%#x] burning_value:[%#llx] perform_case:[%#x], burning_data:[%#llx]\n",
			args.otp_write.typeID, args.otp_write.burning_value,
			args.otp_write.perform_case, args.otp_write.burning_data[0]);

		mutex_lock(&smcc_dev->lock);
		ret = rtk_smcc_dev_ioctl_otp_write(data, &args.otp_write);
		mutex_unlock(&smcc_dev->lock);

		memset(data->va, 0, PAGE_SIZE);
		return ret;

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static const struct file_operations rtk_smcc_ops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = rtk_smcc_dev_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.open = rtk_smcc_dev_open,
	.release = rtk_smcc_dev_release,
};

static int rtk_smcc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtk_smcc_device *smcc_dev = NULL;
	int ret;

	smcc_dev = devm_kzalloc(dev, sizeof(*smcc_dev), GFP_KERNEL);
	if (!smcc_dev)
		return -ENOMEM;

	smcc_dev->dev = dev;
	mutex_init(&smcc_dev->lock);

	smcc_dev->mdev.minor  = MISC_DYNAMIC_MINOR;
	smcc_dev->mdev.name   = RTK_SMCC_DEVICE_NAME;
	smcc_dev->mdev.fops   = &rtk_smcc_ops;
	smcc_dev->mdev.parent = NULL;
	ret = misc_register(&smcc_dev->mdev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, smcc_dev);

	return 0;
}

static int rtk_smcc_remove(struct platform_device *pdev)
{
	struct rtk_smcc_device *smcc_dev = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	misc_deregister(&smcc_dev->mdev);

	return 0;
}

static const struct of_device_id rtk_smcc_ids[] = {
	{ .compatible = "realtek,rtk-smcc" },
	{ /* Sentinel */ },
};

static struct platform_driver rtk_smcc_driver = {
	.probe = rtk_smcc_probe,
	.remove = rtk_smcc_remove,
	.driver = {
		.name = "rtk-smcc",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(rtk_smcc_ids),
	},
};
module_platform_driver(rtk_smcc_driver);

MODULE_LICENSE("GPL");
