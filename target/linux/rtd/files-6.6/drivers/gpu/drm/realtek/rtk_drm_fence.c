/*
 * Copyright (C) 2019 Realtek Inc.
 * Author: Simon Hsu <simon_hsu@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kthread.h>
#include <drm/drm_file.h>
#include <drm/drm_plane.h>
#include <drm/drm_crtc.h>

#include <linux/dma-buf.h>
#include "rtk_drm_drv.h"
#include "rtk_drm_crtc.h"

static int thread_plock(void *data)
{
	struct rtk_drm_fence *rtk_fence = (struct rtk_drm_fence *)data;
	int checkIdx = 0;

	while(1) {
		int ret, i, idx;
		int tmpIdx = -1;
		volatile unsigned char pReceived;
		volatile unsigned char pLock;
		unsigned char plockSt;

		ret = wait_event_interruptible_timeout(rtk_fence->plock_waitq, kthread_should_stop(), msecs_to_jiffies(rtk_fence->thread_plock_interval));

		if (kthread_should_stop() || (ret == -ERESTART)) {
			return 1;
		}

		for(i=0;i<PLOCK_BUFFER_SET_SIZE;i++) {
			idx = checkIdx + i;
			if(idx >= PLOCK_BUFFER_SET_SIZE)
				idx = idx - PLOCK_BUFFER_SET_SIZE;

			mutex_lock(&rtk_fence->idx_lock);
			pReceived = *(volatile unsigned char*)(rtk_fence->pReceived_viraddr + idx);
			pLock = *(volatile unsigned char*)(rtk_fence->pLock_viraddr + idx);

			if (pLock > 0) {
				plockSt = PLOCK_STATUS_LOCK;
			} else if (pLock == 0) {
				switch (pReceived) {
				case PLOCK_INIT:
					plockSt = PLOCK_STATUS_INIT;
					break;
				case PLOCK_QPEND:
					plockSt = PLOCK_STATUS_QPEND;
					break;
				case PLOCK_RECEIVED:
					plockSt = PLOCK_STATUS_UNLOCK;
					break;
				default:
					pr_err("err, %d\n", idx);
					plockSt = PLOCK_STATUS_ERR;
					break;
				}
			}
			if (plockSt == PLOCK_STATUS_UNLOCK) {
				*(volatile unsigned char*)(rtk_fence->pReceived_viraddr + idx) = PLOCK_INIT;
				dsb(sy);
				rtk_fence->index[idx] = BUF_ST_UNLOCK;
				tmpIdx = idx;
			}
			mutex_unlock(&rtk_fence->idx_lock);

		}

		if(tmpIdx != -1) {
			checkIdx = tmpIdx;
			checkIdx++;
		}
	}

	return  0;
}

int rtk_fence_uninit(struct rtk_drm_plane *rtk_plane)
{
	struct rtk_drm_fence *rtk_fence = rtk_plane->rtk_fence;
	struct drm_device *drm = rtk_plane->plane.dev;

	kthread_stop(rtk_fence->thread_plock);

	mutex_destroy(&rtk_fence->idx_lock);

	dma_free_coherent(drm->dev, SZ_1K, rtk_fence->pLock_viraddr,
			 rtk_fence->pLock_paddr);
	kfree(rtk_fence);

	return  0;
}

int rtk_fence_init(struct rtk_drm_plane *rtk_plane)
{
	struct rtk_drm_fence *rtk_fence;
	struct drm_device *drm = rtk_plane->plane.dev;
	void *vaddr;
	dma_addr_t paddr;
	int i=0;

	rtk_fence = kzalloc(sizeof(*rtk_fence), GFP_KERNEL);

	rtk_plane->rtk_fence = rtk_fence;

	rheap_setup_dma_pools(drm->dev, NULL, AUDIO_RTK_FLAG, __func__);
	vaddr = dma_alloc_coherent(drm->dev, SZ_1K, &paddr, GFP_KERNEL);
	if (!vaddr) {
		dev_err(drm->dev, "%s dma_alloc fail \n", __func__);
		return -ENOMEM;
	}


	rtk_fence->pLock_viraddr = (unsigned char *)(vaddr);
	rtk_fence->pLock_paddr = paddr;
	rtk_fence->pReceived_viraddr = rtk_fence->pLock_viraddr + PLOCK_MAX_BUFFER_INDEX;
	rtk_fence->pReceived_paddr = rtk_fence->pLock_paddr + PLOCK_MAX_BUFFER_INDEX;

	memset(rtk_fence->pLock_viraddr, 0, PLOCK_MAX_BUFFER_INDEX);
	memset(rtk_fence->pReceived_viraddr, PLOCK_INIT, PLOCK_MAX_BUFFER_INDEX);

	rtk_fence->thread_plock_interval = 1;
	init_waitqueue_head(&rtk_fence->plock_waitq);
	mutex_init(&rtk_fence->idx_lock);
	for(i=0;i<PLOCK_MAX_BUFFER_INDEX;i++)
		rtk_fence->index[i] = BUF_ST_UNLOCK;
	rtk_fence->thread_plock = kthread_run(thread_plock, rtk_fence, "plockthread");
	rtk_fence->usePlock = 0;
	rtk_fence->next_disp_idx = 0;

	return 0;
}

int rtk_fence_set_buf_st(struct rtk_drm_fence *rtk_fence, uint32_t idx, unsigned char st)
{
	int ret = 0;

	mutex_lock(&rtk_fence->idx_lock);
	switch(st) {
	 	case PLOCK_STATUS_INIT:
		 	*(volatile unsigned char*)(rtk_fence->pReceived_viraddr + idx) = PLOCK_INIT;
		 	dsb(sy);
		 	break;
		case PLOCK_STATUS_QPEND:
		 	*(volatile unsigned char*)(rtk_fence->pReceived_viraddr + idx) = PLOCK_QPEND;
		 	dsb(sy);
			break;
		default:
			pr_err("Incorrect buffer state setting value\n");
			ret = -EINVAL;
			break;
	}
	mutex_unlock(&rtk_fence->idx_lock);
	return ret;
}

int rtk_fence_get_unlock_buf_ioctl(struct drm_device *dev,
			    void *data, struct drm_file *file)
{
	struct drm_rtk_buf_st *buf_st = (struct drm_rtk_buf_st *)data;
	struct drm_plane *plane = drm_plane_find(dev, file, buf_st->plane_id);
	struct rtk_drm_plane *rtk_plane =  container_of(plane, struct rtk_drm_plane, plane);
	struct rtk_drm_fence *rtk_fence = rtk_plane->rtk_fence;
	unsigned int next_idx;
	int i;
	int idx = -1;

	if(plane->type != DRM_PLANE_TYPE_OVERLAY)
		return -EINVAL;

	mutex_lock(&rtk_fence->idx_lock);
	next_idx = rtk_fence->next_disp_idx;
	for(i=0;i<PLOCK_BUFFER_SET_SIZE;i++) {
		if(i+next_idx < PLOCK_BUFFER_SET_SIZE) {
			if(rtk_fence->index[i + next_idx] == BUF_ST_UNLOCK) {
				idx = i + next_idx;
				break;
			}
		}else{
			if(rtk_fence->index[i + next_idx - PLOCK_BUFFER_SET_SIZE] == BUF_ST_UNLOCK) {
				idx = i + next_idx - PLOCK_BUFFER_SET_SIZE;
				break;
			}
		}
	}

	if(idx == -1) {
		mutex_unlock(&rtk_fence->idx_lock);
		pr_err("Can't find unlock buffer!!\n");
		return -EINVAL;
	}
	buf_st->idx = idx;
	buf_st->st = BUF_ST_LOCK;
	rtk_fence->index[idx] = BUF_ST_LOCK;
	rtk_fence->usePlock = 1;
	rtk_fence->next_disp_idx = ((idx+1)<PLOCK_BUFFER_SET_SIZE)?(idx+1):0;
	mutex_unlock(&rtk_fence->idx_lock);

	return 0;
}


int rtk_fence_get_buf_st_ioctl(struct drm_device *dev,
			    void *data, struct drm_file *file)
{
	struct drm_rtk_buf_st *buf_st = (struct drm_rtk_buf_st *)data;
	struct drm_plane *plane = drm_plane_find(dev, file, buf_st->plane_id);
	struct rtk_drm_plane *rtk_plane =  container_of(plane, struct rtk_drm_plane, plane);
	struct rtk_drm_fence *rtk_fence = rtk_plane->rtk_fence;

	if(plane->type != DRM_PLANE_TYPE_OVERLAY)
		return -EINVAL;

	if(buf_st->idx > PLOCK_BUFFER_SET_SIZE )
		return -EINVAL;

	mutex_lock(&rtk_fence->idx_lock);
	buf_st->st = rtk_fence->index[buf_st->idx];
	mutex_unlock(&rtk_fence->idx_lock);
	return 0;
}
