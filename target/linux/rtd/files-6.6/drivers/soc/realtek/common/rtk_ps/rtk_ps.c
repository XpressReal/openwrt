// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Realtek Semiconductor Corp.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include "uapi/rtk_ps_info.h"

#define MODULE_NAME "rtk_process_info"

static int rtk_ps_info_fops_open(struct inode *inode, struct file *file);
static int rtk_ps_info_fops_close(struct inode *inode, struct file *file);
static long rtk_ps_info_fops_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static int rtk_ps_info_getcomm_func(int pid, char *comm)
{
	struct task_struct *pp;
	int ret = -1;

	memset(comm, 0, COMM_LEN);
	rcu_read_lock();
	pp = pid_task(find_pid_ns(pid, &init_pid_ns), PIDTYPE_PID);
	if (pp && (pp->pid == pid)) {
		strncpy(comm, pp->comm, COMM_LEN);
		ret = 0;
	}
	rcu_read_unlock();
	return ret;
}

static int rtk_ps_info_checkcomm_func(char *comm, int *pid)
{
	struct task_struct *pp;
	int ret = -1;

	comm[COMM_LEN - 1] = '\0';
	rcu_read_lock();
	for_each_process(pp) {
		if (pp && !strncmp(pp->comm, comm, strlen(comm))) {
			*pid = pp->pid;
			ret = 0;
			break;
		}
	}
	rcu_read_unlock();
	return ret;
}

static long rtk_ps_info_fops_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = -ENOIOCTLCMD;

	switch (cmd) {
	case RTK_PS_INFO_IOC_GETCOMM:
		{
			struct rtk_ps_info_getcomm_data data;

			ret = -EFAULT;
			if (copy_from_user(&data, (void __user *) arg, sizeof(data)))
				break;

			if (rtk_ps_info_getcomm_func(data.pid, data.comm) != 0)
				break;

			if (copy_to_user((void __user *) arg, &data, sizeof(data)))
				break;

			ret = 0;
		}
		break;
	case RTK_PS_INFO_IOC_CHECKCOMM:
		{
			struct rtk_ps_info_checkcomm_data data;

			ret = -EFAULT;
			if (copy_from_user(&data, (void __user *) arg, sizeof(data)))
				break;

			if (rtk_ps_info_checkcomm_func(data.comm, &data.pid) != 0)
				break;

			if (copy_to_user((void __user *) arg, &data, sizeof(data)))
				break;

			ret = 0;
		}
		break;
	default:
		break;

	}
	return ret;
}

static int rtk_ps_info_fops_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int rtk_ps_info_fops_close(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations rtk_ps_info_fops = {
	.owner                  = THIS_MODULE,
	.open                   = rtk_ps_info_fops_open,
	.release                = rtk_ps_info_fops_close,
	.unlocked_ioctl         = rtk_ps_info_fops_ioctl,
	.compat_ioctl           = rtk_ps_info_fops_ioctl,
};

static struct miscdevice rtk_ps_info_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MODULE_NAME,
	.fops = &rtk_ps_info_fops,
};

static int __init rtk_ps_info_init(void)
{
	int ret = -ENOMEM;

	ret = misc_register(&rtk_ps_info_misc);
	if (unlikely(ret)) {
		pr_err("register Realtek process info dev fail\n");
		return ret;
	}

	return 0;
}

static void __exit rtk_ps_info_exit(void)
{
	misc_deregister(&rtk_ps_info_misc);
}

module_init(rtk_ps_info_init);
module_exit(rtk_ps_info_exit);
MODULE_LICENSE("GPL");
