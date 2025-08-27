/*
 * drivers/soc/realtek/rtd129x/rtk_ve/jdi/compat_jpu.h
 *
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "jpu.h"
#include "compat_jpu.h"

#define COMPAT_JDI_IOCTL_ALLOCATE_PHYSICAL_MEMORY _IO(JDI_IOCTL_MAGIC, 0)
#define COMPAT_JDI_IOCTL_FREE_PHYSICALMEMORY _IO(JDI_IOCTL_MAGIC, 1)
#define COMPAT_JDI_IOCTL_WAIT_INTERRUPT _IO(JDI_IOCTL_MAGIC, 2)
#define COMPAT_JDI_IOCTL_GET_INSTANCE_POOL _IO(JDI_IOCTL_MAGIC, 5)
#define COMPAT_JDI_IOCTL_GET_RESERVED_VIDEO_MEMORY_INFO _IO(JDI_IOCTL_MAGIC, 6)
#define COMPAT_JDI_IOCTL_GET_REGISTER_INFO _IO(JDI_IOCTL_MAGIC, 11)
#define COMPAT_JDI_IOCTL_GET_BONDING_INFO _IO(JDI_IOCTL_MAGIC, 13)
#define COMPAT_JDI_IOCTL_SET_RTK_DOVI_FLAG _IO(JDI_IOCTL_MAGIC, 14)

long compat_jpu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret = 0;

	if (!filp->f_op->unlocked_ioctl)
		return -ENOTTY;

	switch (cmd) {
	case COMPAT_JDI_IOCTL_ALLOCATE_PHYSICAL_MEMORY:
	{
		compat_jpudrv_buffer_t data32;
		jpudrv_buffer_pool_t *jbp;

		jpu_sem_down();

		jbp = kzalloc(sizeof(*jbp), GFP_KERNEL);
		if (!jbp) {
			jpu_sem_up();
			return -ENOMEM;
		}

		ret = copy_from_user(&data32, compat_ptr(arg), sizeof(data32));
		if (ret)
			return -EFAULT;

		jbp->jb = (jpudrv_buffer_t){
			.size = data32.size,
			.phys_addr = data32.phys_addr,
			.base = data32.base,
			.virt_addr = data32.virt_addr,
		};

		ret = jpu_alloc_dma_buffer(&(jbp->jb));
		if (ret == -1) {
			kfree(jbp);
			jpu_sem_up();
			return -ENOMEM;
		}

		data32 = (compat_jpudrv_buffer_t){
			.size      = jbp->jb.size,
			.phys_addr = jbp->jb.phys_addr,
			.base      = jbp->jb.base,
			.virt_addr = jbp->jb.virt_addr,
		};

		ret = copy_to_user(compat_ptr(arg), &data32, sizeof(data32));
		if (ret) {
			kfree(jbp);
			jpu_sem_up();
			return -EFAULT;
		}

		jpu_add_jbp_list(jbp, filp);

		jpu_sem_up();
	}
	break;
	case COMPAT_JDI_IOCTL_FREE_PHYSICALMEMORY:
	{
		compat_jpudrv_buffer_t data32;
		jpudrv_buffer_t jb;

		jpu_sem_down();

		ret = copy_from_user(&data32, compat_ptr(arg), sizeof(data32));
		if (ret) {
			jpu_sem_up();
			return -EACCES;
		}

		jb = (jpudrv_buffer_t){
			.size = data32.size,
			.phys_addr = data32.phys_addr,
			.base = data32.base,
			.virt_addr = data32.virt_addr,
		};

		if (jb.base)
			jpu_free_dma_buffer(&jb);

		jpu_free_mem(&jb);

		jpu_sem_up();
	}
	break;
	case COMPAT_JDI_IOCTL_GET_RESERVED_VIDEO_MEMORY_INFO:
	{
#ifdef JPU_SUPPORT_RESERVED_VIDEO_MEMORY
		compat_jpudrv_buffer_t data32;
		jpudrv_buffer_t *image_memory;

		image_memory = jpu_get_image_memory();

		if (image_memory->base != 0) {

			data32 = (compat_jpudrv_buffer_t){
				.size = image_memory->size,
				.phys_addr = image_memory->phys_addr,
				.base = image_memory->base,
				.virt_addr = image_memory->virt_addr,
			};

			ret = copy_to_user(compat_ptr(arg), data32, sizeof(data32));
			if (ret)
				return -EFAULT;

		} else {
			return -EFAULT;
		}
#endif
	}
	break;
	case COMPAT_JDI_IOCTL_WAIT_INTERRUPT:
	{
		jpu_drv_context_t *dev = (jpu_drv_context_t *)filp->private_data;
		compat_jpudrv_intr_info_t data32;
		jpudrv_intr_info_t info;

		ret = copy_from_user(&data32, compat_ptr(arg), sizeof(data32));
		if (ret)
			return -EFAULT;

		info = (jpudrv_intr_info_t){
			.timeout = data32.timeout,
			.intr_reason = data32.intr_reason,
		};

		jpu_wait_init(dev, &info);

		data32 = (compat_jpudrv_intr_info_t){
			.timeout = info.timeout,
			.intr_reason = info.intr_reason,
		};

		ret = copy_to_user(compat_ptr(arg), &data32, sizeof(data32));
		if (ret)
			return -EFAULT;
	}
	break;
	case JDI_IOCTL_ENABLE_INTERRUPT:
	case JDI_IOCTL_SET_CLOCK_GATE:
	case JDI_IOCTL_OPEN_INSTANCE:
	case JDI_IOCTL_CLOSE_INSTANCE:
	case JDI_IOCTL_GET_INSTANCE_NUM:
	case JDI_IOCTL_RESET:
	case JDI_IOCTL_SET_CLOCK_ENABLE:
	{
		return filp->f_op->unlocked_ioctl(filp, cmd,
										(unsigned long)compat_ptr(arg));
	}
	break;
	case COMPAT_JDI_IOCTL_GET_INSTANCE_POOL:
	{
		compat_jpudrv_buffer_t data32;
		jpudrv_buffer_t data;
		jpudrv_buffer_t *instance_pool;

		ret = copy_from_user(&data32, compat_ptr(arg), sizeof(data32));
		if (ret)
			return -EFAULT;

		data = (jpudrv_buffer_t){
			.size      = data32.size,
			.phys_addr = data32.phys_addr,
			.base      = data32.base,
			.virt_addr = data32.virt_addr,
		};

		jpu_sem_down();

		instance_pool = jpu_get_instance_pool();

		if (instance_pool->base != 0) {

			data32 = (compat_jpudrv_buffer_t) {
				.size      = instance_pool->size,
				.phys_addr = instance_pool->phys_addr,
				.base      = instance_pool->base,
				.virt_addr = instance_pool->virt_addr,
			};

			ret = copy_to_user(compat_ptr(arg), &data32, sizeof(data32));
			if (ret) {
				jpu_sem_up();
				return -EFAULT;
			}

		} else {
			memcpy(instance_pool, &data, sizeof(data));
#ifdef J_USE_VMALLOC_FOR_INSTANCE_POOL_MEMORY
			ret = jpu_alloc_from_vm();
			if (ret) {
				jpu_sem_up();
				return -EFAULT;
			}
#else
			ret = jpu_alloc_from_dmabuffer2();
			if (ret) {
				jpu_sem_up();
				return -EFAULT;
			}
#endif
			memcpy(&data, instance_pool, sizeof(data));

			data32 = (compat_jpudrv_buffer_t){
				.size      = data.size,
				.phys_addr = data.phys_addr,
				.base      = data.base,
				.virt_addr = data.virt_addr,
			};

			ret = copy_to_user(compat_ptr(arg), &data32, sizeof(data32));
			if (ret) {
				jpu_sem_up();
				return -EFAULT;
			}

			jpu_sem_up();
		}
	}
	break;
	case COMPAT_JDI_IOCTL_GET_REGISTER_INFO:
	case COMPAT_JDI_IOCTL_GET_BONDING_INFO:
	{
		compat_jpudrv_buffer_t data32;
		jpudrv_buffer_t *jpu_register;
		jpudrv_buffer_t *bonding_register;

		bonding_register = jpu_get_bonding_register();
		jpu_register = jpu_get_jpu_register();

		if (cmd == COMPAT_JDI_IOCTL_GET_REGISTER_INFO) {
			data32 = (compat_jpudrv_buffer_t) {
				.size      = jpu_register->size,
				.phys_addr = jpu_register->phys_addr,
				.base      = jpu_register->base,
				.virt_addr = jpu_register->virt_addr,
			};

			ret = copy_to_user(compat_ptr(arg), &data32, sizeof(data32));
			if (ret != 0)
				return -EFAULT;
		} else {
			data32 = (compat_jpudrv_buffer_t) {
				.size      = bonding_register->size,
				.phys_addr = bonding_register->phys_addr,
				.base      = bonding_register->base,
				.virt_addr = bonding_register->virt_addr,
			};

			ret = copy_to_user(compat_ptr(arg), &data32, sizeof(data32));
			if (ret)
				return -EFAULT;
		}
	}
	break;
	default:
	{
		pr_err("[COMPAT JPU]No such IOCTL, cmd is %d\n", cmd);
		return -ENOIOCTLCMD;
	}
	break;
	}

	return ret;
}
EXPORT_SYMBOL(compat_jpu_ioctl);

MODULE_LICENSE("GPL");
