/**
  ve1.c

  linux device driver for VPU.

 Copyright (C) 2006 - 2013  REALTEK INC.

  This library is free software; you can redistribute it and/or modify it under
  the terms of the GNU Lesser General Public License as published by the Free
  Software Foundation; either version 2.1 of the License, or (at your option)
  any later version.

  This library is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
  details.

  You should have received a copy of the GNU Lesser General Public License
  along with this library; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St, Fifth Floor, Boston, MA
  02110-1301  USA

*/

#ifndef __VPU_DRV_H__
#define __VPU_DRV_H__

#include <linux/fs.h>
#include <linux/types.h>

#include "ve1config.h"

#define SUPPORT_MULTI_INST_INTR
#define SUPPORT_MULTI_INST_INTR_ERROR_CHECK
#define USE_VMALLOC_FOR_INSTANCE_POOL_MEMORY

#define VDI_IOCTL_MAGIC 'V'
#define VDI_IOCTL_ALLOCATE_PHYSICAL_MEMORY _IO(VDI_IOCTL_MAGIC, 0)
#define VDI_IOCTL_FREE_PHYSICALMEMORY _IO(VDI_IOCTL_MAGIC, 1)
#define VDI_IOCTL_WAIT_INTERRUPT _IO(VDI_IOCTL_MAGIC, 2)
#define VDI_IOCTL_SET_CLOCK_GATE _IO(VDI_IOCTL_MAGIC, 3)
#define VDI_IOCTL_RESET _IO(VDI_IOCTL_MAGIC, 4)
#define VDI_IOCTL_GET_INSTANCE_POOL _IO(VDI_IOCTL_MAGIC, 5)
#define VDI_IOCTL_GET_COMMON_MEMORY _IO(VDI_IOCTL_MAGIC, 6)
#define VDI_IOCTL_GET_RESERVED_VIDEO_MEMORY_INFO _IO(VDI_IOCTL_MAGIC, 8)
#define VDI_IOCTL_OPEN_INSTANCE _IO(VDI_IOCTL_MAGIC, 9)
#define VDI_IOCTL_CLOSE_INSTANCE _IO(VDI_IOCTL_MAGIC, 10)
#define VDI_IOCTL_GET_INSTANCE_NUM _IO(VDI_IOCTL_MAGIC, 11)
#define VDI_IOCTL_GET_REGISTER_INFO _IO(VDI_IOCTL_MAGIC, 12)

/* RTK ioctl */
#define VDI_IOCTL_SET_RTK_CLK_GATING _IO(VDI_IOCTL_MAGIC, 16)
#define VDI_IOCTL_SET_RTK_CLK_PLL _IO(VDI_IOCTL_MAGIC, 17)
#define VDI_IOCTL_GET_RTK_CLK_PLL _IO(VDI_IOCTL_MAGIC, 18)
#define VDI_IOCTL_GET_RTK_SUPPORT_TYPE _IO(VDI_IOCTL_MAGIC, 19)
#define VDI_IOCTL_GET_RTK_ASIC_REVISION _IO(VDI_IOCTL_MAGIC, 20)
#define VDI_IOCTL_SET_RTK_CLK_SELECT _IO(VDI_IOCTL_MAGIC, 21)
#define VDI_IOCTL_GET_RTK_CLK_SELECT _IO(VDI_IOCTL_MAGIC, 22)
#define VDI_IOCTL_SET_RTK_DOVI_FLAG _IO(VDI_IOCTL_MAGIC, 23)
#define VDI_IOCTL_GET_TOTAL_INSTANCE_NUM _IO(VDI_IOCTL_MAGIC, 24)
#define VDI_IOCTL_GET_RTK_DCSYS_INFO _IO(VDI_IOCTL_MAGIC, 25)

#define VE_PLL_SYSH	0x000
#define VE_PLL_VE1	0x001
#define VE_PLL_VE2	0x010

typedef struct vpudrv_buffer_t {
	unsigned int size;
	unsigned long phys_addr;
	unsigned long base; /* kernel logical address in use kernel */
	unsigned long virt_addr; /* virtual user space address */
	unsigned int mem_type; /* RTK, for protect memory */
} vpudrv_buffer_t;

typedef struct vpu_bit_firmware_info_t {
	unsigned int size; /* size of this structure*/
	unsigned int core_idx;
	unsigned long reg_base_offset;
	unsigned short bit_code[512];
} vpu_bit_firmware_info_t;

typedef struct vpu_clock_info_t{
	unsigned int core_idx;
	unsigned int enable;
	unsigned int value;
} vpu_clock_info_t;

typedef struct vpudrv_inst_info_t {
	unsigned int core_idx;
	unsigned int inst_idx;
	int inst_open_count; /* for output only*/
} vpudrv_inst_info_t;

typedef struct vpudrv_intr_info_t {
	unsigned int core_idx;
	unsigned int timeout;
	int intr_reason;
#ifdef SUPPORT_MULTI_INST_INTR
	int intr_inst_index;
#endif
} vpudrv_intr_info_t;

typedef struct vpudrv_dovi_info_t{
	unsigned int core_idx;
	unsigned int inst_idx;
	unsigned int enable;
} vpudrv_dovi_info_t;

typedef struct vpu_drv_context_t {
	struct fasync_struct *async_queue;
	unsigned long interrupt_reason_ve1;
	/* !<< device reference count. Not instance count */
	u32 open_count;
} vpu_drv_context_t;

/* To track the allocated memory buffer */
typedef struct vpudrv_buffer_pool_t {
	struct list_head list;
	struct vpudrv_buffer_t vb;
	struct file *filp;
} vpudrv_buffer_pool_t;

/* To track the instance index and buffer in instance pool */
typedef struct vpudrv_instanace_list_t {
	struct list_head list;
	unsigned long inst_idx;
	unsigned long core_idx;
	struct file *filp;
} vpudrv_instanace_list_t;

typedef struct vpudrv_instance_pool_t {
	unsigned char codecInstPool[MAX_NUM_INSTANCE][MAX_INST_HANDLE_SIZE];
	long long pendingInstIdxPlus1;
	void *pendingInst;
} vpudrv_instance_pool_t;

extern vpudrv_buffer_t *rtd16xxb_vpu_get_instance_pool(void);
extern vpudrv_buffer_t *rtd16xxb_vpu_get_common_memory(void);
extern vpudrv_buffer_t *rtd16xxb_vpu_get_vpu_register(void);
extern vpudrv_buffer_t *rtd16xxb_vpu_get_bond_register(void);
extern vpudrv_buffer_t *rtd16xxb_vpu_get_dc_register(void);
extern vpudrv_buffer_t *rtd16xxb_vpu_get_dmc_register(void);

#ifdef VPU_SUPPORT_RESERVED_VIDEO_MEMORY
extern vpudrv_buffer_t vpu_get_video_memory(void);
#endif /* VPU_SUPPORT_RESERVED_VIDEO_MEMORY */

extern void rtd16xxb_vpu_sem_up(void);
extern void rtd16xxb_vpu_open_ref_count_inc(void);
extern void rtd16xxb_vpu_open_ref_count_dec(void);
extern int rtd16xxb_vpu_down_interruptible(void);
extern void rtd16xxb_vpu_clock_getting(vpu_clock_info_t *clockInfo);
extern void rtd16xxb_vpu_add_vbp_list(vpudrv_buffer_pool_t *vbp, struct file *filp);
extern int rtd16xxb_vpu_open_inst(vpudrv_inst_info_t *inst_info, struct file *filp);
extern void rtd16xxb_vpu_close_inst(vpudrv_inst_info_t *inst_info);
extern void rtd16xxb_vpu_get_inst_num(vpudrv_inst_info_t *inst_info);
extern void rtd16xxb_vpu_get_total_inst_num(vpudrv_inst_info_t *inst_info);
extern void rtd16xxb_vpu_free_mem(vpudrv_buffer_t *vb);
extern int rtd16xxb_vpu_wait_init(vpu_drv_context_t *dev, vpudrv_intr_info_t *info);
extern int rtd16xxb_vpu_alloc_from_vm(void);
extern int rtd16xxb_vpu_alloc_from_dmabuffer2(void);
extern int rtd16xxb_vpu_alloc_dma_buffer(vpudrv_buffer_t *vb);
extern void rtd16xxb_vpu_free_dma_buffer(vpudrv_buffer_t *vb);

extern int rtd16xxb_vdi_ioctl_get_instance_pool(vpudrv_buffer_t *vdb);
extern int rtd16xxb_vdi_ioctl_get_register_info(vpudrv_buffer_t *vdb);
extern int rtd16xxb_vdi_ioctl_set_rtk_clk_gating(vpu_clock_info_t* clockInfo);
extern int rtd16xxb_vdi_ioctl_get_common_memory(vpudrv_buffer_t *vdb);
extern ssize_t rtd16xxb_vdi_write_bit_firmware(vpu_bit_firmware_info_t *buf, size_t len);
extern int rtd16xxb_vdi_ioctl_allocate_physical_memory(void *filp, vpudrv_buffer_t *vdb);
extern int rtd16xxb_vdi_ioctl_free_physical_memory(vpudrv_buffer_t *vdb);
extern int rtd16xxb_vdi_ioctl_allocate_physical_memory_no_mmap(void *filp, vpudrv_buffer_t *vdb);
extern int rtd16xxb_vdi_ioctl_free_physical_memory_no_mmap(vpudrv_buffer_t *vdb);
extern int rtd16xxb_vdi_ioctl_open_instance(void *filp, vpudrv_inst_info_t *inst_info);
extern int rtd16xxb_vdi_ioctl_close_instance(vpudrv_inst_info_t *inst_info);
extern int rtd16xxb_vdi_ioctl_wait_interrupt(vpudrv_intr_info_t *intr_info);
#endif
