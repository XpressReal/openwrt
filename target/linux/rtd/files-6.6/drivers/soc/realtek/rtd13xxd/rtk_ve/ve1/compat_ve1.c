/*
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

#include "ve1.h"
#include "compat_ve1.h"

/* See drivers/soc/realtek/rtd129x/rtk_ve/ve1/ve1.h for the definition of these structs */
typedef struct compat_vpudrv_buffer_t {
	compat_uint_t size;
	compat_ulong_t phys_addr;
	compat_ulong_t base; /* kernel logical address in use kernel */
	compat_ulong_t virt_addr; /* virtual user space address */
	compat_uint_t mem_type;
} compat_vpudrv_buffer_t;

typedef struct compat_vpu_clock_info_t{
	compat_uint_t core_idx;
	compat_uint_t enable;
	compat_uint_t value;
} compat_vpu_clock_info_t;

typedef struct compat_vpudrv_inst_info_t {
	compat_uint_t core_idx;
	compat_uint_t inst_idx;
	compat_int_t inst_open_count;	/* for output only*/
} compat_vpudrv_inst_info_t;

typedef struct compat_vpudrv_intr_info_t {
	compat_uint_t core_idx;
	compat_uint_t timeout;
	compat_int_t intr_reason;
#ifdef SUPPORT_MULTI_INST_INTR
	compat_int_t intr_inst_index;
#endif
} compat_vpudrv_intr_info_t;

typedef struct compat_vpudrv_dovi_info_t{
	compat_uint_t core_idx;
	compat_uint_t inst_idx;
	compat_uint_t enable;
} compat_vpudrv_dovi_info_t;

#define COMPAT_VDI_IOCTL_ALLOCATE_PHYSICAL_MEMORY _IO(VDI_IOCTL_MAGIC, 0)
#define COMPAT_VDI_IOCTL_FREE_PHYSICALMEMORY _IO(VDI_IOCTL_MAGIC, 1)
#define COMPAT_VDI_IOCTL_WAIT_INTERRUPT _IO(VDI_IOCTL_MAGIC, 2)
#define COMPAT_VDI_IOCTL_GET_INSTANCE_POOL _IO(VDI_IOCTL_MAGIC, 5)
#define COMPAT_VDI_IOCTL_GET_COMMON_MEMORY _IO(VDI_IOCTL_MAGIC, 6)
#define COMPAT_VDI_IOCTL_GET_RESERVED_VIDEO_MEMORY_INFO _IO(VDI_IOCTL_MAGIC, 8)
#define COMPAT_VDI_IOCTL_OPEN_INSTANCE _IO(VDI_IOCTL_MAGIC, 9)
#define COMPAT_VDI_IOCTL_CLOSE_INSTANCE _IO(VDI_IOCTL_MAGIC, 10)
#define COMPAT_VDI_IOCTL_GET_INSTANCE_NUM _IO(VDI_IOCTL_MAGIC, 11)
#define COMPAT_VDI_IOCTL_GET_REGISTER_INFO _IO(VDI_IOCTL_MAGIC, 12)

/* RTK ioctl */
#define COMPAT_VDI_IOCTL_SET_RTK_CLK_GATING _IO(VDI_IOCTL_MAGIC, 16)
#define COMPAT_VDI_IOCTL_SET_RTK_CLK_PLL _IO(VDI_IOCTL_MAGIC, 17)
#define COMPAT_VDI_IOCTL_GET_RTK_CLK_PLL _IO(VDI_IOCTL_MAGIC, 18)
#define COMPAT_VDI_IOCTL_GET_RTK_SUPPORT_TYPE _IO(VDI_IOCTL_MAGIC, 19)
#define COMPAT_VDI_IOCTL_SET_RTK_CLK_SELECT _IO(VDI_IOCTL_MAGIC, 21)
#define COMPAT_VDI_IOCTL_GET_RTK_CLK_SELECT _IO(VDI_IOCTL_MAGIC, 22)
#define COMPAT_VDI_IOCTL_SET_RTK_DOVI_FLAG _IO(VDI_IOCTL_MAGIC, 23)
#define COMPAT_VDI_IOCTL_GET_TOTAL_INSTANCE_NUM _IO(VDI_IOCTL_MAGIC, 24)
#define COMPAT_VDI_IOCTL_GET_RTK_DCSYS_INFO _IO(VDI_IOCTL_MAGIC, 25)

long rtd13xxd_compat_vpu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret = 0;

	if (!filp->f_op->unlocked_ioctl)
		return -ENOTTY;

	switch (cmd) {
	case COMPAT_VDI_IOCTL_ALLOCATE_PHYSICAL_MEMORY:
	{
		compat_vpudrv_buffer_t data32;
		vpudrv_buffer_pool_t *vbp;

		ret = rtd13xxd_vpu_down_interruptible();
		if (ret != 0) {
			rtd13xxd_vpu_sem_up();
			return -EFAULT;
		}

		vbp = kzalloc(sizeof(*vbp), GFP_KERNEL);
		if (!vbp) {
			rtd13xxd_vpu_sem_up();
			return -ENOMEM;
		}

		ret = copy_from_user(&data32, compat_ptr(arg), sizeof(data32));
		if (ret) {
			rtd13xxd_vpu_sem_up();
			return -EFAULT;
		}

		vbp->vb = (vpudrv_buffer_t) {
			.size      = data32.size,
			.phys_addr = data32.phys_addr,
			.base      = data32.base,
			.virt_addr = data32.virt_addr,
			.mem_type  = data32.mem_type,
		};

		ret = rtd13xxd_vpu_alloc_dma_buffer(&(vbp->vb));
		if (ret == -ENOMEM) {
			kfree(vbp);
			rtd13xxd_vpu_sem_up();
			return -ENOMEM;
		}

		data32 = (compat_vpudrv_buffer_t) {
			.size      = vbp->vb.size,
			.phys_addr = vbp->vb.phys_addr,
			.base      = vbp->vb.base,
			.virt_addr = vbp->vb.virt_addr,
			.mem_type  = vbp->vb.mem_type,
		};

		ret = copy_to_user(compat_ptr(arg), &data32,
				   sizeof(data32));
		if (ret) {
			rtd13xxd_vpu_sem_up();
			return -EFAULT;
		}

		rtd13xxd_vpu_add_vbp_list(vbp, filp);
	}
	break;
	case COMPAT_VDI_IOCTL_FREE_PHYSICALMEMORY:
	{
		compat_vpudrv_buffer_t data32;
		vpudrv_buffer_t vb;

		ret = rtd13xxd_vpu_down_interruptible();
		if (ret != 0) {
			rtd13xxd_vpu_sem_up();
			return -EFAULT;
		}

		ret = copy_from_user(&data32, compat_ptr(arg), sizeof(data32));
		if (ret) {
			rtd13xxd_vpu_sem_up();
			return -ENOMEM;
		}

		vb = (vpudrv_buffer_t) {
			.size      = data32.size,
			.phys_addr = data32.phys_addr,
			.base      = data32.base,
			.virt_addr = data32.virt_addr,
			.mem_type  = data32.mem_type,
		};

		if (vb.base)
			rtd13xxd_vpu_free_dma_buffer(&vb);

		rtd13xxd_vpu_free_mem(&vb);
	}
	break;
	case COMPAT_VDI_IOCTL_GET_RESERVED_VIDEO_MEMORY_INFO:
	{
#ifdef VPU_SUPPORT_RESERVED_VIDEO_MEMORY
		compat_vpudrv_buffer_t data32;
		static vpudrv_buffer_t *video_memory;

		video_memory = vpu_get_video_memory();

		if (video_memory->base == 0)
			return -EFAULT;

		data32 = (compat_vpudrv_buffer_t) {
			.size      = video_memory->size,
			.phys_addr = video_memory->phys_addr,
			.base      = video_memory->base,
			.virt_addr = video_memory->virt_addr,
			.mem_type  = video_memory->mem_type,
		};

		ret = copy_to_user(compat_ptr(arg), &data32,
				   sizeof(data32));
		if (ret)
			return -EFAULT;
#endif /* VPU_SUPPORT_RESERVED_VIDEO_MEMORY */
	}
	break;
	case COMPAT_VDI_IOCTL_WAIT_INTERRUPT:
	{
		vpu_drv_context_t *dev = (vpu_drv_context_t *)filp->private_data;
		compat_vpudrv_intr_info_t data32;
		vpudrv_intr_info_t info;

		ret = copy_from_user(&data32, compat_ptr(arg), sizeof(data32));
		if (ret)
			return -EFAULT;

		info = (vpudrv_intr_info_t) {
			.core_idx        = data32.core_idx,
			.timeout         = data32.timeout,
			.intr_reason     = data32.intr_reason,
			.intr_inst_index = data32.intr_inst_index,
		};

		ret = rtd13xxd_vpu_wait_init(dev, &info);
		if (ret != 0)
			return -EFAULT;

		data32 = (compat_vpudrv_intr_info_t) {
			.core_idx        = info.core_idx,
			.timeout         = info.timeout,
			.intr_reason     = info.intr_reason,
			.intr_inst_index = info.intr_inst_index,
		};

		ret = copy_to_user(compat_ptr(arg), &data32, sizeof(data32));
		if (ret)
			return -EFAULT;
	}
	break;
	case VDI_IOCTL_SET_CLOCK_GATE:
	case VDI_IOCTL_RESET:
	case VDI_IOCTL_GET_RTK_ASIC_REVISION:
	{
		return filp->f_op->unlocked_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
	}
	break;
	case COMPAT_VDI_IOCTL_GET_INSTANCE_POOL:
	{
		compat_vpudrv_buffer_t data32;
		vpudrv_buffer_t data;
		vpudrv_buffer_t *instance_pool;


		ret = copy_from_user(&data32, compat_ptr(arg), sizeof(data32));
		if (ret)
			return -EFAULT;

		data = (vpudrv_buffer_t) {
			.size      = data32.size,
			.phys_addr = data32.phys_addr,
			.base      = data32.base,
			.virt_addr = data32.virt_addr,
			.mem_type  = data32.mem_type,
		};

		ret = rtd13xxd_vpu_down_interruptible();
		if (ret != 0) {
			rtd13xxd_vpu_sem_up();
			return -EFAULT;
		}

		instance_pool = rtd13xxd_vpu_get_instance_pool();

		if (instance_pool->base != 0) {

			data32 = (compat_vpudrv_buffer_t) {
				.size      = instance_pool->size,
				.phys_addr = instance_pool->phys_addr,
				.base      = instance_pool->base,
				.virt_addr = instance_pool->virt_addr,
				.mem_type  = instance_pool->mem_type,
			};

			ret = copy_to_user(compat_ptr(arg), &data32,
					   sizeof(data32));
			if (ret) {
				rtd13xxd_vpu_sem_up();
				return -EFAULT;
			}
		} else {
			memcpy(instance_pool, &data, sizeof(vpudrv_buffer_t));

#ifdef USE_VMALLOC_FOR_INSTANCE_POOL_MEMORY
			ret = rtd13xxd_vpu_alloc_from_vm();
			if (ret) {
				rtd13xxd_vpu_sem_up();
				return -EFAULT;
			}
#else
			ret = rtd13xxd_vpu_alloc_from_dmabuffer2();
			if (ret) {
				rtd13xxd_vpu_sem_up();
				return -EFAULT;
			}
#endif /* USE_VMALLOC_FOR_INSTANCE_POOL_MEMORY */

			memcpy(&data, instance_pool, sizeof(data));

			data32 = (compat_vpudrv_buffer_t) {
				.size      = data.size,
				.phys_addr = data.phys_addr,
				.base      = data.base,
				.virt_addr = data.virt_addr,
				.mem_type  = data.mem_type,
			};

			ret = copy_to_user(compat_ptr(arg), &data32,
					   sizeof(data32));
			if (ret) {
				rtd13xxd_vpu_sem_up();
				return -EFAULT;
			}
		}

		rtd13xxd_vpu_sem_up();
	}
	break;
	case COMPAT_VDI_IOCTL_GET_COMMON_MEMORY:
	{
		compat_vpudrv_buffer_t data32;
		vpudrv_buffer_t data;
		vpudrv_buffer_t *common_memory;

		ret = copy_from_user(&data32, compat_ptr(arg), sizeof(data32));
		if (ret)
			return -EFAULT;

		data = (vpudrv_buffer_t) {
			.size      = data32.size,
			.phys_addr = data32.phys_addr,
			.base      = data32.base,
			.virt_addr = data32.virt_addr,
			.mem_type  = data32.mem_type,
		};

		common_memory = rtd13xxd_vpu_get_common_memory();

		if (common_memory->base != 0) {

			data32 = (compat_vpudrv_buffer_t) {
				.size      = common_memory->size,
				.phys_addr = common_memory->phys_addr,
				.base      = common_memory->base,
				.virt_addr = common_memory->virt_addr,
				.mem_type  = common_memory->mem_type,
			};

			ret = copy_to_user(compat_ptr(arg), &data32,
					   sizeof(data32));
			if (ret)
				return -EFAULT;

		} else {
			memcpy(common_memory, &data, sizeof(vpudrv_buffer_t));

			if (rtd13xxd_vpu_alloc_dma_buffer(common_memory) != 0)
				return -ENOMEM;

			memcpy(&data, common_memory, sizeof(vpudrv_buffer_t));

			data32 = (compat_vpudrv_buffer_t) {
				.size      = data.size,
				.phys_addr = data.phys_addr,
				.base      = data.base,
				.virt_addr = data.virt_addr,
				.mem_type  = data.mem_type,
			};

			ret = copy_to_user(compat_ptr(arg), &data32,
					   sizeof(data32));
			if (ret)
				return -ENOMEM;
		}
	}
	break;
	case COMPAT_VDI_IOCTL_OPEN_INSTANCE:
	{
		compat_vpudrv_inst_info_t data32;
		vpudrv_inst_info_t inst_info;

		ret = copy_from_user(&data32, compat_ptr(arg), sizeof(data32));
		if (ret)
			return -EFAULT;

		inst_info = (vpudrv_inst_info_t) {
			.core_idx        = data32.core_idx,
			.inst_idx        = data32.inst_idx,
			.inst_open_count = data32.inst_open_count,
		};

		ret = rtd13xxd_vpu_open_inst(&inst_info, filp);
		if (ret)
			return -ENOMEM;

		rtd13xxd_vpu_open_ref_count_inc();

		data32 = (compat_vpudrv_inst_info_t) {
			.core_idx        = inst_info.core_idx,
			.inst_idx        = inst_info.inst_idx,
			.inst_open_count = inst_info.inst_open_count,
		};

		ret = copy_to_user(compat_ptr(arg), &data32, sizeof(data32));
		if (ret)
			return -EFAULT;
	}
	break;
	case COMPAT_VDI_IOCTL_CLOSE_INSTANCE:
	{
		compat_vpudrv_inst_info_t data32;
		vpudrv_inst_info_t inst_info;

		ret = copy_from_user(&data32, compat_ptr(arg), sizeof(data32));
		if (ret)
			return -EFAULT;

		inst_info = (vpudrv_inst_info_t) {
			.core_idx        = data32.core_idx,
			.inst_idx        = data32.inst_idx,
			.inst_open_count = data32.inst_open_count,
		};

		rtd13xxd_vpu_close_inst(&inst_info);

		data32 = (compat_vpudrv_inst_info_t) {
			.core_idx        = inst_info.core_idx,
			.inst_idx        = inst_info.inst_idx,
			.inst_open_count = inst_info.inst_open_count,
		};

		ret = copy_to_user(compat_ptr(arg), &data32, sizeof(data32));
		if (ret)
			return -EFAULT;
	}
	break;
	case COMPAT_VDI_IOCTL_GET_INSTANCE_NUM:
	{
		compat_vpudrv_inst_info_t data32;
		vpudrv_inst_info_t inst_info;

		ret = copy_from_user(&data32, compat_ptr(arg), sizeof(data32));
		if (ret)
			return -EFAULT;

		inst_info = (vpudrv_inst_info_t) {
			.core_idx        = data32.core_idx,
			.inst_idx        = data32.inst_idx,
			.inst_open_count = data32.inst_open_count,
		};

		rtd13xxd_vpu_get_inst_num(&inst_info);

		data32 = (compat_vpudrv_inst_info_t) {
			.core_idx        = inst_info.core_idx,
			.inst_idx        = inst_info.inst_idx,
			.inst_open_count = inst_info.inst_open_count,
		};

		ret = copy_to_user(compat_ptr(arg), &data32, sizeof(data32));
		if (ret)
			return -EFAULT;
	} break;
	case COMPAT_VDI_IOCTL_GET_REGISTER_INFO: {
		compat_vpudrv_buffer_t data32;
		vpudrv_buffer_t *vpu_register;

		vpu_register = rtd13xxd_vpu_get_vpu_register();

		data32 = (compat_vpudrv_buffer_t) {
			.size      = vpu_register->size,
			.phys_addr = vpu_register->phys_addr,
			.base      = vpu_register->base,
			.virt_addr = vpu_register->virt_addr,
			.mem_type  = vpu_register->mem_type,
		};

		ret = copy_to_user(compat_ptr(arg), &data32,
				   sizeof(data32));
		if (ret)
			return -EFAULT;
	}
	break;
	case COMPAT_VDI_IOCTL_SET_RTK_CLK_GATING:
	{
		compat_vpu_clock_info_t data32;
		vpu_clock_info_t clockInfo;

		ret = copy_from_user(&data32, compat_ptr(arg), sizeof(data32));
		if (ret)
			return -EFAULT;

		clockInfo = (vpu_clock_info_t) {
			.core_idx = data32.core_idx,
			.enable   = data32.enable,
			.value    = data32.value,
		};

		rtd13xxd_vpu_clock_getting(&clockInfo);
	}
	break;
	case COMPAT_VDI_IOCTL_SET_RTK_CLK_PLL:
	{
		return -ENOIOCTLCMD;
	}
	break;
	case COMPAT_VDI_IOCTL_GET_RTK_CLK_PLL:
	{
		return  -ENOIOCTLCMD;
	}
	break;
	case COMPAT_VDI_IOCTL_SET_RTK_CLK_SELECT:
	{
		return  -ENOIOCTLCMD;
	}
	break;
	case COMPAT_VDI_IOCTL_GET_RTK_CLK_SELECT:
	{
		return  -ENOIOCTLCMD;
	}
	break;
	case COMPAT_VDI_IOCTL_GET_RTK_SUPPORT_TYPE:
	{
		return -ENOIOCTLCMD;
	}
	break;
	case COMPAT_VDI_IOCTL_GET_RTK_DCSYS_INFO:
	{
		return -ENOIOCTLCMD;
	}
	break;
	case COMPAT_VDI_IOCTL_GET_TOTAL_INSTANCE_NUM:
	{
		compat_vpudrv_inst_info_t data32;
		vpudrv_inst_info_t inst_info;

		ret = copy_from_user(&data32, compat_ptr(arg), sizeof(data32));
		if (ret)
			return -EFAULT;

		inst_info = (vpudrv_inst_info_t) {
			.core_idx        = data32.core_idx,
			.inst_idx        = data32.inst_idx,
			.inst_open_count = data32.inst_open_count,
		};

		rtd13xxd_vpu_get_total_inst_num(&inst_info);

		data32 = (compat_vpudrv_inst_info_t) {
			.core_idx        = inst_info.core_idx,
			.inst_idx        = inst_info.inst_idx,
			.inst_open_count = inst_info.inst_open_count,
		};

		ret = copy_to_user(compat_ptr(arg), &data32, sizeof(data32));
		if (ret)
			return -EFAULT;

	}
	break;
	default:
	{
		pr_err("[COMPAT_VPUDRV] No such IOCTL, cmd is %d\n", cmd);
		return -ENOIOCTLCMD;
	}
	}

	return ret;
}
EXPORT_SYMBOL(rtd13xxd_compat_vpu_ioctl);

MODULE_LICENSE("GPL");
