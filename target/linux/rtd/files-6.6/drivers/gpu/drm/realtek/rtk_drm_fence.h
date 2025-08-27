#ifndef __RTK_FENCE_H__
#define __RTK_FENCE_H__

#include <linux/dma-fence.h>
#include "rtk_drm_rpc.h"

#define PLOCK_BUFFER_SET_SIZE   (32) //bytes
#define PLOCK_BUFFER_SET        (2)  // 2 set of PLock buffer for sequence change
#define PLOCK_MAX_BUFFER_INDEX  (PLOCK_BUFFER_SET_SIZE*PLOCK_BUFFER_SET) //bytes  // seperate to 2 set of PLock buffer for sequence change
#define PLOCK_BUFFER_SIZE       (PLOCK_MAX_BUFFER_INDEX*2) //bytes

#define PLOCK_INIT              0xFF
#define PLOCK_QPEND             0
#define PLOCK_RECEIVED          1


enum
{
	PLOCK_STATUS_INIT	= 0,       // 0  / PLOCK_RESET
	PLOCK_STATUS_QPEND	= 1,       // 0  / PLOCK_QPEND
	PLOCK_STATUS_LOCK	= 2,       // >0 / PLOCK_RESET || PLOCK_QPEND || PLOCK_RECEIVED
	PLOCK_STATUS_UNLOCK	= 3,       // 0  / PLOCK_RECEIVED
	PLOCK_STATUS_ERR	= 4        // else
};


enum
{
	BUF_ST_UNLOCK		= 0,
	BUF_ST_LOCK		= 1,
};

struct rtk_drm_fence {
	struct rtk_rpc_info *rpc_info;
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;

	unsigned char *pLock_viraddr;
	unsigned char *pReceived_viraddr;
	dma_addr_t pLock_paddr;
	dma_addr_t pReceived_paddr;

	struct task_struct *thread_plock;
	int thread_plock_interval;
	wait_queue_head_t plock_waitq;

//	struct fence fence[PLOCK_MAX_BUFFER_INDEX];

	unsigned char index[PLOCK_MAX_BUFFER_INDEX];
	struct mutex idx_lock;
	unsigned int next_disp_idx;
	bool usePlock;
//	struct list_head head;

//	bool sysmem;

	unsigned long timeout;
};

int rtk_fence_set_buf_st(struct rtk_drm_fence *rtk_plane,
	uint32_t idx, unsigned char st);
int rtk_fence_get_unlock_buf_ioctl(struct drm_device *dev,
			    void *data, struct drm_file *file);
int rtk_fence_get_buf_st_ioctl(struct drm_device *dev,
	void *data, struct drm_file *file);
int rtk_fence_set_buf_st_ioctl(struct drm_device *dev,
	void *data, struct drm_file *file);
#endif
