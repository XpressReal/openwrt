

#ifndef __JPU_DRV_H__
#define __JPU_DRV_H__

#include <linux/fs.h>
#include <linux/types.h>

#define J_USE_VMALLOC_FOR_INSTANCE_POOL_MEMORY

#define JDI_IOCTL_MAGIC  'J'
#define JDI_IOCTL_ALLOCATE_PHYSICAL_MEMORY			_IO(JDI_IOCTL_MAGIC, 0)
#define JDI_IOCTL_FREE_PHYSICALMEMORY			_IO(JDI_IOCTL_MAGIC, 1)
#define JDI_IOCTL_WAIT_INTERRUPT			_IO(JDI_IOCTL_MAGIC, 2)
#define JDI_IOCTL_SET_CLOCK_GATE			_IO(JDI_IOCTL_MAGIC, 3)
#define JDI_IOCTL_RESET						_IO(JDI_IOCTL_MAGIC, 4)
#define JDI_IOCTL_GET_INSTANCE_POOL			_IO(JDI_IOCTL_MAGIC, 5)
#define JDI_IOCTL_GET_RESERVED_VIDEO_MEMORY_INFO			_IO(JDI_IOCTL_MAGIC, 6)
#define JDI_IOCTL_OPEN_INSTANCE				_IO(JDI_IOCTL_MAGIC, 7)
#define JDI_IOCTL_CLOSE_INSTANCE			_IO(JDI_IOCTL_MAGIC, 8)
#define JDI_IOCTL_GET_INSTANCE_NUM			_IO(JDI_IOCTL_MAGIC, 9)
#define JDI_IOCTL_ENABLE_INTERRUPT			_IO(JDI_IOCTL_MAGIC, 10)
#define JDI_IOCTL_GET_REGISTER_INFO			_IO(JDI_IOCTL_MAGIC, 11)

/* RTK ioctl */
#define JDI_IOCTL_SET_CLOCK_ENABLE			_IO(JDI_IOCTL_MAGIC, 12)
#define JDI_IOCTL_GET_BONDING_INFO			_IO(JDI_IOCTL_MAGIC, 13)
#define JDI_IOCTL_SET_RTK_DOVI_FLAG			_IO(JDI_IOCTL_MAGIC, 14)

#define MAX_NUM_INSTANCE 8
#define MAX_INST_HANDLE_SIZE (12*1024)

typedef struct jpudrv_buffer_t {
    unsigned int size;
    unsigned long phys_addr;
    unsigned long base;							/* kernel logical address in use kernel */
    unsigned long virt_addr;				/* virtual user space address */
} jpudrv_buffer_t;

typedef struct jpudrv_intr_info_t {
    unsigned int timeout;
    int intr_reason;
} jpudrv_intr_info_t;

typedef struct jpudrv_dovi_info_t{
	unsigned int inst_idx;
	unsigned int enable;
} jpudrv_dovi_info_t;

typedef struct compat_jpudrv_buffer_t {
	compat_uint_t size;
	compat_ulong_t phys_addr;
	compat_ulong_t base;							/* kernel logical address in use kernel */
	compat_ulong_t virt_addr;				/* virtual user space address */
} compat_jpudrv_buffer_t;

typedef struct compat_jpudrv_intr_info_t {
	compat_uint_t timeout;
	compat_int_t intr_reason;
} compat_jpudrv_intr_info_t;

typedef struct compat_jpudrv_dovi_info_t{
	compat_uint_t inst_idx;
	compat_uint_t enable;
} compat_jpudrv_dovi_info_t;

typedef struct jpu_drv_context_t {
	struct fasync_struct *async_queue;
	unsigned long interrupt_reason;
} jpu_drv_context_t;

/* To track the allocated memory buffer */
typedef struct jpudrv_buffer_pool_t {
	struct list_head list;
	struct jpudrv_buffer_t jb;
	struct file *filp;
} jpudrv_buffer_pool_t;

/* To track the instance index and buffer in instance pool */
typedef struct jpudrv_instanace_list_t {
	struct list_head list;
	unsigned long inst_idx;
	struct file *filp;
} jpudrv_instanace_list_t;

typedef struct jpudrv_instance_pool_t {
	unsigned char jpgInstPool[MAX_NUM_INSTANCE][MAX_INST_HANDLE_SIZE];
} jpudrv_instance_pool_t;

extern jpudrv_buffer_t *jpu_get_instance_pool(void);
extern jpudrv_buffer_t *jpu_get_jpu_register(void);
extern jpudrv_buffer_t *jpu_get_bonding_register(void);

#ifdef JPU_SUPPORT_RESERVED_VIDEO_MEMORY
extern jpudrv_buffer_t *jpu_get_image_memory(void);
#endif

extern void jpu_sem_up(void);
extern void jpu_sem_down(void);
extern void jpu_add_jbp_list(jpudrv_buffer_pool_t *jbp, struct file *filp);
extern void jpu_del_jbp_list(jpudrv_buffer_pool_t *jbp, struct file *filp);
extern void jpu_free_mem(jpudrv_buffer_t *jb);
extern int jpu_wait_init(jpu_drv_context_t *dev, jpudrv_intr_info_t *info);
extern int jpu_alloc_from_vm(void);
extern int jpu_alloc_from_dmabuffer2(void);
extern int jpu_alloc_dma_buffer(jpudrv_buffer_t *jb);
extern int jpu_alloc_dma_buffer2(jpudrv_buffer_t *jb);
extern void jpu_free_dma_buffer(jpudrv_buffer_t *jb);
extern int pu_set_dovi_flag(int nCoreIdx, int nInstIdx, unsigned int bEnable);
#endif
