// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Realtek Inc.
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

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_blend.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_plane.h>
#include <drm/drm_vblank.h>	// DEBUG: struct drm_pending_vblank_event
#include <linux/mm_types.h>
#include <linux/iosys-map.h>
#include "rtk_drm_drv.h"
#include "rtk_drm_fb.h"
#include "rtk_drm_gem.h"
#include "rtk_drm_crtc.h"
#include "rtk_drm_rpc.h"

#define to_rtk_plane(s) container_of(s, struct rtk_drm_plane, plane)
#define MAX_PLANE 5

#define RTK_AFBC_MOD \
	DRM_FORMAT_MOD_ARM_AFBC( \
		AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_YTR \
	)

#define INVERT_BITVAL_1 (~1)

int rtk_plane_rpc_config_disp_win;
module_param(rtk_plane_rpc_config_disp_win, int, 0644);
MODULE_PARM_DESC(rtk_plane_rpc_config_disp_win, "Debug level (0-1)");

static const unsigned int osd_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
};

static const unsigned int video_formats[] = {
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
};

static const unsigned int other_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
};

static const uint64_t format_modifiers_default[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static const uint64_t format_modifiers_afbc[] = {
	RTK_AFBC_MOD,
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static const struct drm_prop_enum_list vo_plane_list[] = {
	{ VO_VIDEO_PLANE_V1, "V1" },
	{ VO_VIDEO_PLANE_V2, "V2" },
	{ VO_VIDEO_PLANE_SUB1, "SUB1" },
	{ VO_VIDEO_PLANE_OSD1, "OSD1" },
	{ VO_VIDEO_PLANE_OSD2, "OSD2" },
};

struct rtk_drm_plane_state {
	struct drm_plane_state state;
	unsigned int display_idx;
	struct drm_property_blob *rtk_meta_data_blob;
	struct drm_property_enum *vo_plane_name;
	struct video_object rtk_meta_data;
};

static struct vo_rectangle rect_plane_disabled = {0};
static struct vo_rectangle rect_osd1;
static struct vo_rectangle rect_sub1;
static struct vo_rectangle rect_video1;

static DEFINE_MUTEX(enable_display_mutex);

ssize_t rtk_plane_enable_osd_display_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
ssize_t rtk_plane_enable_sub_display_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
ssize_t rtk_plane_enable_video_display_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) ;

static DEVICE_ATTR(enable_osd_display, S_IWUSR, NULL, rtk_plane_enable_osd_display_store);
static DEVICE_ATTR(enable_sub_display, S_IWUSR, NULL, rtk_plane_enable_sub_display_store);
static DEVICE_ATTR(enable_video_display, S_IWUSR, NULL, rtk_plane_enable_video_display_store);

#define ulPhyAddrFilter(x) ((x) & ~0xe0000000)

static uint64_t htonll(long long val)
{
	return (((long long) htonl(val)) << 32) + htonl(val >> 32);
}

static inline struct rtk_drm_plane_state *
to_rtk_plane_state(struct drm_plane_state *s)
{
	return container_of(s, struct rtk_drm_plane_state, state);
}

struct rtkplane_dma_buf_attachment {
        struct sg_table sgt;
};

static int rtkplane_dma_buf_attach(struct dma_buf *dmabuf,
                                  struct dma_buf_attachment *attach)
{
	struct rtk_drm_plane *rtk_plane = dmabuf->priv;
	struct drm_device *drm = rtk_plane->plane.dev;
        struct device *dev = drm->dev;
	dma_addr_t daddr = rtk_plane->refclock_dma_handle;
        void *vaddr = rtk_plane->refclock;
        size_t size = dmabuf->size;

        struct rtkplane_dma_buf_attachment *a;
        int ret;

        a = kzalloc(sizeof(*a), GFP_KERNEL);
        if (!a)
                return -ENOMEM;

        ret = dma_get_sgtable(dev, &a->sgt, vaddr, daddr, size);
        if (ret < 0) {
                dev_err(dev, "failed to get scatterlist from DMA API\n");
                kfree(a);
                return -EINVAL;
        }

        attach->priv = a;

        return 0;
}

static void rtkplane_dma_buf_detatch(struct dma_buf *dmabuf,
                                struct dma_buf_attachment *attach)
{
        struct rtkplane_dma_buf_attachment *a = attach->priv;

        sg_free_table(&a->sgt);
        kfree(a);
}

static struct sg_table *rtkplane_map_dma_buf(struct dma_buf_attachment *attach,
                                        enum dma_data_direction dir)
{
        struct rtkplane_dma_buf_attachment *a = attach->priv;
        struct sg_table *table;
        int ret;

        table = &a->sgt;

        ret = dma_map_sgtable(attach->dev, table, dir, 0);
        if (ret)
                table = ERR_PTR(ret);
        return table;
}

static void rtkplane_unmap_dma_buf(struct dma_buf_attachment *attach,
                                struct sg_table *table,
                                enum dma_data_direction dir)
{
        dma_unmap_sgtable(attach->dev, table, dir, 0);
}

static int rtkplane_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
        struct rtk_drm_plane *rtk_plane = dmabuf->priv;
	struct drm_device *drm = rtk_plane->plane.dev;
        struct device *dev = drm->dev;
	dma_addr_t daddr = rtk_plane->refclock_dma_handle;
        void *vaddr = rtk_plane->refclock;
        size_t size = vma->vm_end - vma->vm_start;

        if (vaddr)
                return dma_mmap_coherent(dev, vma, vaddr, daddr, size);

        return 0;
}

static void rtkplane_release(struct dma_buf *dmabuf)
{
        struct rtk_drm_plane *rtk_plane = dmabuf->priv;
	struct drm_device *drm = rtk_plane->plane.dev;
        struct device *dev = drm->dev;
	dma_addr_t daddr = rtk_plane->refclock_dma_handle;
        void *vaddr = rtk_plane->refclock;
        size_t size = dmabuf->size;

        if (vaddr)
                dma_free_coherent(dev, size, vaddr, daddr);

}

static const struct dma_buf_ops rtkplane_dma_buf_ops = {
        .attach = rtkplane_dma_buf_attach,
        .detach = rtkplane_dma_buf_detatch,
	.map_dma_buf = rtkplane_map_dma_buf,
        .unmap_dma_buf = rtkplane_unmap_dma_buf,
        .mmap = rtkplane_mmap,
        .release = rtkplane_release,
};

static int write_cmd_to_ringbuffer(struct rtk_drm_plane *rtk_plane, void *cmd)
{
	void *base_iomap = rtk_plane->ringbase;
	struct tag_ringbuffer_header *rbHeader = rtk_plane->ringheader;
	unsigned int size = ((struct inband_cmd_pkg_header *)cmd)->size;
	unsigned int read = ipcReadULONG((u8 *)&rbHeader->readPtr[0]);
	unsigned int write = ipcReadULONG((u8 *)&(rbHeader->writePtr));
	unsigned int base = ipcReadULONG((u8 *)&(rbHeader->beginAddr));
	unsigned int b_size = ipcReadULONG((u8 *)&(rbHeader->size));
	unsigned int limit = base + b_size;

	if (read + (read > write ? 0 : limit - base) - write > size) {
		unsigned long offset = write - base;
		void *write_io = (void *)((unsigned long)base_iomap + offset);

		if (write + size <= limit) {
			ipcCopyMemory((void *)write_io, cmd, size);
		} else {
			ipcCopyMemory((void *)write_io, cmd, limit - write);
			ipcCopyMemory((void *)base_iomap, (void *)((unsigned long)cmd + limit - write), size - (limit - write));
		}
		write += size;
		write = write < limit ? write : write - (limit - base);

		rbHeader->writePtr = ipcReadULONG((u8 *)&write);
	} else {
		DRM_ERROR("errQ r:%x w:%x size:%u base:%u limit:%u\n",
			  read, write, size, base, limit);
		goto err;
	}

	return 0;
err:
	return -1;
}

static void init_video_object(struct video_object *obj)
{
	memset(obj, 0, sizeof(struct video_object));

	obj->lumaOffTblAddr = 0xffffffff;
	obj->chromaOffTblAddr = 0xffffffff;
	obj->lumaOffTblAddrR = 0xffffffff;
	obj->chromaOffTblAddrR = 0xffffffff;
	obj->bufBitDepth = 8;
	obj->matrix_coefficients = 1;
	obj->tch_hdr_metadata.specVersion = -1;

	obj->Y_addr_Right = 0xffffffff;
	obj->U_addr_Right = 0xffffffff;
	obj->pLock_Right = 0xffffffff;
}

static int rtk_plane_inband_config_disp_win(struct drm_plane *plane, struct rpc_config_disp_win *disp_win)
{
	struct rtk_drm_plane *rtk_plane = to_rtk_plane(plane);
	struct inband_config_disp_win *inband_cmd;

	inband_cmd = (struct inband_config_disp_win *)kzalloc(sizeof(struct inband_config_disp_win), GFP_KERNEL);
	if(!inband_cmd) {
		DRM_ERROR("rtk_plane_inband_config_disp_win malloc inband_cmd fail\n");
		return -1;
	}

	memset(inband_cmd, 0, sizeof(struct inband_config_disp_win));

	inband_cmd->header.type = VIDEO_VO_INBAND_CMD_TYPE_CONFIGUREDISPLAYWINDOW;
	inband_cmd->header.size = sizeof(struct inband_config_disp_win);

	inband_cmd->videoPlane        = disp_win->videoPlane;
	inband_cmd->videoWin.x        = disp_win->videoWin.x;
	inband_cmd->videoWin.y        = disp_win->videoWin.y;
	inband_cmd->videoWin.width    = disp_win->videoWin.width;
	inband_cmd->videoWin.height   = disp_win->videoWin.height;
	inband_cmd->borderWin.x       = disp_win->borderWin.x;
	inband_cmd->borderWin.y       = disp_win->borderWin.y;
	inband_cmd->borderWin.width   = disp_win->borderWin.width;
	inband_cmd->borderWin.height  = disp_win->borderWin.height;
	inband_cmd->borderColor.c1    = disp_win->borderColor.c1;
	inband_cmd->borderColor.c2    = disp_win->borderColor.c2;
	inband_cmd->borderColor.c3    = disp_win->borderColor.c3;
	inband_cmd->borderColor.isRGB = disp_win->borderColor.isRGB;
	inband_cmd->enBorder          = disp_win->enBorder;

	write_cmd_to_ringbuffer(rtk_plane, inband_cmd);
	kfree(inband_cmd);

	return 0;
}

static int rtk_plane_update_scaling(struct drm_plane *plane)
{
	struct rtk_drm_plane *rtk_plane = to_rtk_plane(plane);
	struct rtk_rpc_info *rpc_info = rtk_plane->rpc_info;
	struct vo_rectangle *old_disp_win;
	struct vo_color blueBorder = {0, 0, 255, 1};

	DRM_DEBUG_DRIVER("[rtk_plane_update_scaling] videoPlane : %d\n", rtk_plane->info.videoPlane);
	DRM_DEBUG_DRIVER("[type] : %d] [new] crtc_x : %d, crtc_y : %d\n",
		plane->type, plane->state->crtc_x, plane->state->crtc_y);
	DRM_DEBUG_DRIVER("[type] : %d] [new] crtc_w : %d, crtc_h : %d\n",
		plane->type, plane->state->crtc_w, plane->state->crtc_h);

	old_disp_win = &rtk_plane->disp_win.videoWin;

	if (old_disp_win->x != plane->state->crtc_x ||
		old_disp_win->y != plane->state->crtc_y ||
		old_disp_win->width != plane->state->crtc_w ||
		old_disp_win->height != plane->state->crtc_h) {

		DRM_DEBUG_DRIVER("plane type \x1b[31m%d\033[0m coordinate or size has changed\n", plane->type);
		DRM_DEBUG_DRIVER("[type] : %d] [old] disp_win->x     : %d, disp_win->y      : %d\n",
			plane->type, old_disp_win->x, old_disp_win->y);
		DRM_DEBUG_DRIVER("[type] : %d] [old] disp_win->width : %d, disp_win->height : %d\n",
			plane->type, old_disp_win->width, old_disp_win->height);

		DRM_DEBUG_DRIVER("[\x1b[33mvideoplane %d on mixer %d\033[0m]\n",
			rtk_plane->info.videoPlane, rtk_plane->mixer);
		rtk_plane->disp_win.videoPlane = rtk_plane->info.videoPlane | (rtk_plane->mixer << 16);

		rtk_plane->disp_win.videoWin.x       = plane->state->crtc_x;
		rtk_plane->disp_win.videoWin.y       = plane->state->crtc_y;
		rtk_plane->disp_win.videoWin.width   = plane->state->crtc_w;
		rtk_plane->disp_win.videoWin.height  = plane->state->crtc_h;
		rtk_plane->disp_win.borderWin.x      = plane->state->crtc_x;
		rtk_plane->disp_win.borderWin.y      = plane->state->crtc_y;
		rtk_plane->disp_win.borderWin.width  = plane->state->crtc_w;
		rtk_plane->disp_win.borderWin.height = plane->state->crtc_h;
		rtk_plane->disp_win.borderColor      = blueBorder;
		rtk_plane->disp_win.enBorder         = 0;

		if (plane->type == DRM_PLANE_TYPE_CURSOR && rtk_plane_rpc_config_disp_win == 0) {
			DRM_DEBUG_DRIVER("[rtk_plane_inband_config_disp_win]\n");
			if (rtk_plane_inband_config_disp_win(plane, &rtk_plane->disp_win)) {
				DRM_ERROR("rtk_plane_inband_config_disp_win fail\n");
				return -1;
			}
		} else {
			DRM_DEBUG_DRIVER("[rpc_video_config_disp_win]\n");
			if (rpc_video_config_disp_win(rpc_info, &rtk_plane->disp_win)) {
				DRM_ERROR("rpc_video_config_disp_win RPC fail\n");
				return -1;
			}
		}
	}

	return 0;
}

static int rtk_plane_update_video_obj(struct drm_plane *plane)
{
	struct rtk_drm_plane *rtk_plane = to_rtk_plane(plane);
	struct rtk_drm_fence *rtk_fence = rtk_plane->rtk_fence;
	struct drm_framebuffer *fb = plane->state->fb;
	struct drm_gem_object *gem[4];
	struct rtk_gem_object *rtk_gem[4];
	const struct drm_format_info *info;

	struct rtk_drm_plane_state *s = to_rtk_plane_state(plane->state);
	int i;
	int index;
	struct video_object *obj = (struct video_object *)kzalloc(sizeof(struct video_object), GFP_KERNEL);
	if(!obj) {
		DRM_ERROR("rtk_plane_update_video_obj malloc video_object fail\n");
		return -1;
	}

	info = drm_format_info(fb->format->format);
	for (i = 0; i < info->num_planes; i++) {
		gem[i] = rtk_fb_get_gem_obj(fb, i);
		if (!gem[i])
			gem[i] = gem[0];
		rtk_gem[i] = to_rtk_gem_obj(gem[i]);
	}
	init_video_object(obj);
#ifdef CONFIG_RTK_METADATA_AUTOJUDGE
	if (rtk_gem[0]->dmabuf_type == DMABUF_TYPE_NORMAL) {
#else
	if (s->rtk_meta_data.header.type != METADATA_HEADER) {
#endif
		obj->header.type = VIDEO_VO_INBAND_CMD_TYPE_OBJ_PIC;
		obj->header.size = sizeof(struct video_object);
		obj->version = 0x72746B3F;
		obj->width = fb->width;
		obj->height = fb->height;
		obj->Y_pitch = fb->width;
		obj->mode = CONSECUTIVE_FRAME;
		obj->Y_addr = rtk_gem[0]->paddr + fb->offsets[0];
		obj->U_addr = rtk_gem[1]->paddr + fb->offsets[1];
	} else {
#ifdef CONFIG_RTK_METADATA_AUTOJUDGE
		struct video_object *decObj = (struct video_object *)rtk_gem[0]->vaddr;
#else
		struct video_object *decObj = (struct video_object *)(&s->rtk_meta_data);
#endif
		memcpy(obj, decObj, sizeof(struct video_object));

		obj->header.type = VIDEO_VO_INBAND_CMD_TYPE_OBJ_PIC;
		obj->header.size = sizeof(struct video_object);
		obj->version = 0x72746B3F;

		index = s->display_idx;
		obj->context = index;
		obj->pLock = rtk_fence->pLock_paddr+index;
		obj->pReceived = rtk_fence->pReceived_paddr+index;
		obj->PTSH = decObj->PTSH;
		obj->PTSL = decObj->PTSL;
		obj->RPTSH = decObj->RPTSH;
		obj->RPTSL = decObj->RPTSL;

		if(rtk_plane->rtk_fence->usePlock)
			rtk_fence_set_buf_st(rtk_plane->rtk_fence, index, PLOCK_STATUS_QPEND);
	}

	write_cmd_to_ringbuffer(rtk_plane, obj);
	kfree(obj);
	return 0;
}

static int rtk_plane_update_graphic_obj(struct drm_plane *plane)
{
	struct rtk_drm_plane *rtk_plane = to_rtk_plane(plane);
	struct drm_framebuffer *fb = plane->state->fb;
	struct drm_gem_object *gem[4];
	struct rtk_gem_object *rtk_gem[4];
	const struct drm_format_info *info;
	unsigned int flags = 0;
	int i;

	struct graphic_object *obj = (struct graphic_object *)kzalloc(sizeof(struct graphic_object), GFP_KERNEL);
	if(!obj) {
		DRM_ERROR("rtk_plane_update_graphic_obj malloc graphic_object fail\n");
		return -1;
	}

	info = drm_format_info(fb->format->format);
	for (i = 0; i < info->num_planes; i++) {
		gem[i] = rtk_fb_get_gem_obj(fb, i);
		if (!gem[i])
			gem[i] = gem[0];
		rtk_gem[i] = to_rtk_gem_obj(gem[i]);
	}

	memset(obj, 0, sizeof(struct graphic_object));
	obj->header.type = VIDEO_GRAPHIC_INBAND_CMD_TYPE_PICTURE_OBJECT;
	obj->header.size = sizeof(struct graphic_object);

	obj->colorkey = -1;
	if (fb->format->format == DRM_FORMAT_XRGB8888) {
		flags |= eBuffer_USE_GLOBAL_ALPHA;
		obj->alpha = 0x3ff;
		obj->format = INBAND_CMD_GRAPHIC_FORMAT_ARGB8888_LITTLE;
	} else if (fb->format->format == DRM_FORMAT_ABGR8888) {
		obj->format = INBAND_CMD_GRAPHIC_FORMAT_RGBA8888;
	} else if (fb->format->format == DRM_FORMAT_ARGB8888) {
		obj->format = INBAND_CMD_GRAPHIC_FORMAT_ARGB8888_LITTLE;
	}

	if (fb->modifier & AFBC_FORMAT_MOD_YTR) {
		flags |= eBuffer_AFBC_Enable | eBuffer_AFBC_YUV_Transform;
	}

	if (rtk_plane->buflock_idx >= 1024) {
		rtk_plane->buflock_idx = 0;
	}

	obj->context = rtk_plane->buflock_idx;

	rtk_plane->context = obj->context;
	rtk_plane->buflock_idx++;

	obj->width = fb->width;
	obj->height = fb->height;
	obj->pitch = fb->pitches[0];
	obj->address = rtk_gem[0]->paddr;
	obj->picLayout = INBAND_CMD_GRAPHIC_2D_MODE;
	obj->afbc = (flags & eBuffer_AFBC_Enable)?1:0;
//     obj->afbc_block_split = (flags & eBuffer_AFBC_Split)?1:0;
	obj->afbc_yuv_transform = (flags & eBuffer_AFBC_YUV_Transform)?1:0;

	write_cmd_to_ringbuffer(rtk_plane, obj);
	kfree(obj);
	return 0;
}

bool rtk_plane_check_update_done(struct rtk_drm_plane *rtk_plane)
{
	int refContext = -1;
	if (rtk_plane->refclock) {
		refContext = htonl(rtk_plane->refclock->videoContext);
		if (refContext == rtk_plane->context) {
			// DRM_INFO("context %d update successful\n", rtk_plane->context);
			return true;
		}
	}

	DRM_DEBUG_DRIVER("context %d update fail, continue to show %d\n",
		rtk_plane->context, refContext);

	return false;
}

static int rtk_plane_rpc_init(struct rtk_drm_plane *rtk_plane,
			      enum VO_VIDEO_PLANE layer_nr)
{
	struct drm_device *drm = rtk_plane->plane.dev;
	struct rtk_rpc_info *rpc_info = rtk_plane->rpc_info;
	void *vaddr;
	struct rpc_refclock refclock;
	struct rpc_ringbuffer ringbuffer;
	unsigned int id;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	int err = 0;
	enum VO_VIDEO_PLANE videoplane;
	struct vo_rectangle rect;
	struct vo_color blueBorder = {0, 0, 255, 1};

	videoplane = layer_nr;


	vaddr = dma_alloc_coherent(drm->dev, 65*SZ_1K, &rtk_plane->dma_handle,
				GFP_KERNEL | __GFP_NOWARN);

	if (!vaddr) {
		dev_err(drm->dev, "%s dma_alloc fail \n", __func__);
		return -ENOMEM;
	}
	rtk_plane->ringbase = vaddr;
	rtk_plane->ringheader = (struct tag_ringbuffer_header *)
			((unsigned long)(vaddr)+(64*1024));

	vaddr = dma_alloc_coherent(drm->dev, SZ_2K,
				&rtk_plane->refclock_dma_handle,
				GFP_KERNEL | __GFP_NOWARN);

        if (!vaddr) {
		dev_err(drm->dev, "%s dma_alloc fail \n", __func__);
		dma_free_coherent(drm->dev, 65*SZ_1K, rtk_plane->ringbase,
				 rtk_plane->dma_handle);
		return -ENOMEM;
	}

	rtk_plane->refclock = (struct tag_refclock *)(vaddr);

	exp_info.ops = &rtkplane_dma_buf_ops;
	exp_info.size = SZ_2K;
	exp_info.flags = O_RDWR;
	exp_info.priv = rtk_plane;
	rtk_plane->refclock_dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(rtk_plane->refclock_dmabuf)) {
		dev_err(drm->dev, "%s dmabuf export fail \n", __func__);
		dma_free_coherent(drm->dev, SZ_2K, vaddr,
				 rtk_plane->refclock_dma_handle);
		dma_free_coherent(drm->dev, 65*SZ_1K, rtk_plane->ringbase,
				 rtk_plane->dma_handle);
		return PTR_ERR(rtk_plane->refclock_dmabuf);
	}


	if (rpc_create_video_agent(rpc_info, &id, VF_TYPE_VIDEO_OUT)) {
		DRM_ERROR("rpc_create_video_agent RPC fail\n");
		return -1;
	}

	rtk_plane->info.instance = id;
	rtk_plane->info.videoPlane = videoplane;
	rtk_plane->info.zeroBuffer = 0;
	rtk_plane->info.realTimeSrc = 0;

	if (rpc_video_display(rpc_info, &rtk_plane->info)) {
		DRM_ERROR("rpc_video_display RPC fail\n");
		return -1;
	}

	rect.x = 0;
	rect.y = 0;
	rect.width = 0;
	rect.height = 0;

	rtk_plane->disp_win.videoPlane = videoplane | (rtk_plane->mixer << 16);

	rtk_plane->disp_win.videoWin = rect;
	rtk_plane->disp_win.borderWin = rect;
	rtk_plane->disp_win.borderColor = blueBorder;
	rtk_plane->disp_win.enBorder = 0;

	if (rpc_video_config_disp_win(rpc_info, &rtk_plane->disp_win)) {
		DRM_ERROR("rpc_video_config_disp_win RPC fail\n");
		return -1;
	}

	rtk_plane->refclock->RCD = htonll(-1LL);
	rtk_plane->refclock->RCD_ext = htonl(-1L);
	rtk_plane->refclock->masterGPTS = htonll(-1LL);
	rtk_plane->refclock->GPTSTimeout = htonll(0LL);
	rtk_plane->refclock->videoSystemPTS = htonll(-1LL);
	rtk_plane->refclock->audioSystemPTS = htonll(-1LL);
	rtk_plane->refclock->videoRPTS = htonll(-1LL);
	rtk_plane->refclock->audioRPTS = htonll(-1LL);
	rtk_plane->refclock->videoContext = htonl(-1);
	rtk_plane->refclock->audioContext = htonl(-1);
	rtk_plane->refclock->videoEndOfSegment = htonl(-1);
	rtk_plane->refclock->videoFreeRunThreshold = htonl(0x7FFFFFFF);
	rtk_plane->refclock->audioFreeRunThreshold = htonl(0x7FFFFFFF);
	rtk_plane->refclock->VO_Underflow = htonl(0);
	rtk_plane->refclock->AO_Underflow = htonl(0);
	rtk_plane->refclock->mastership.systemMode = (unsigned char)AVSYNC_FORCED_SLAVE;
	rtk_plane->refclock->mastership.videoMode = (unsigned char)AVSYNC_FORCED_MASTER;
	rtk_plane->refclock->mastership.audioMode = (unsigned char)AVSYNC_FORCED_MASTER;
	rtk_plane->refclock->mastership.masterState = (unsigned char)AUTOMASTER_NOT_MASTER;
	refclock.instance = id;
	refclock.pRefClock = (long)(0xffffffff&(rtk_plane->refclock_dma_handle));
	if (rpc_video_set_refclock(rpc_info, &refclock)) {
		DRM_ERROR("rpc_video_set_refclock RPC fail\n");
		return -1;
	}

	rtk_plane->ringheader->beginAddr = htonl((long)(0xffffffff&(rtk_plane->dma_handle)));
	rtk_plane->ringheader->size = htonl(64*1024);
	rtk_plane->ringheader->writePtr = rtk_plane->ringheader->beginAddr;
	rtk_plane->ringheader->readPtr[0] = rtk_plane->ringheader->beginAddr;
	rtk_plane->ringheader->bufferID = htonl(1);
	memset(&ringbuffer, 0, sizeof(ringbuffer));
	ringbuffer.instance = id;
	ringbuffer.readPtrIndex = 0;
	ringbuffer.pinID = 0;
	ringbuffer.pRINGBUFF_HEADER = (long)(0xffffffff&(rtk_plane->dma_handle))+64*1024;

	if (rpc_video_init_ringbuffer(rpc_info, &ringbuffer)) {
		DRM_ERROR("rpc_video_int_ringbuffer RPC fail\n");
		return -1;
	}

	if (rpc_video_run(rpc_info, id)) {
		DRM_ERROR("rpc_video_run RPC fail\n");
		return -1;
	}

	rtk_plane->flags |= RPC_READY;

	rtk_plane->info.instance = id;
	rtk_plane->info.videoPlane = videoplane;
	rtk_plane->info.zeroBuffer = 1;
	rtk_plane->info.realTimeSrc = 0;

	if (rpc_video_display(rpc_info, &rtk_plane->info)) {
		DRM_ERROR("rpc_video_display RPC fail\n");
		return -1;
	}

	if(videoplane == VO_VIDEO_PLANE_V1)
		err = device_create_file(drm->dev, &dev_attr_enable_video_display);
	else if(videoplane == VO_VIDEO_PLANE_OSD1)
		err = device_create_file(drm->dev, &dev_attr_enable_osd_display);
	else if(videoplane == VO_VIDEO_PLANE_SUB1)
		err = device_create_file(drm->dev, &dev_attr_enable_sub_display);
	else
		DRM_DEBUG("Not create %d plane for device attribute\n", videoplane);

	if (err < 0)
		DRM_ERROR("failed to create %d plane devide attribute\n", videoplane);

	return 0;
}

void rtk_plane_destroy(struct drm_plane *plane)
{
	struct rtk_drm_plane *rtk_plane = to_rtk_plane(plane);
	struct drm_device *drm = rtk_plane->plane.dev;
	struct rtk_rpc_info *rpc_info = rtk_plane->rpc_info;
	enum drm_plane_type type = plane->type;
	enum VO_VIDEO_PLANE videoplane;

	if (rtk_plane->rtk_fence)
		rtk_fence_uninit(rtk_plane);

	rpc_destroy_video_agent(rpc_info, rtk_plane->info.instance);

	dma_free_coherent(drm->dev, 65*SZ_1K, rtk_plane->ringbase,
				 rtk_plane->dma_handle);

	dma_buf_put(rtk_plane->refclock_dmabuf);

	if (type == DRM_PLANE_TYPE_PRIMARY)
		videoplane = VO_VIDEO_PLANE_OSD1;
	else if (type == DRM_PLANE_TYPE_CURSOR)
		videoplane = VO_VIDEO_PLANE_SUB1;
	else
		videoplane = VO_VIDEO_PLANE_V1;

	if (videoplane == VO_VIDEO_PLANE_V1)
		device_remove_file(drm->dev, &dev_attr_enable_video_display);
	else if (videoplane == VO_VIDEO_PLANE_OSD1)
		device_remove_file(drm->dev, &dev_attr_enable_osd_display);
	else if (videoplane == VO_VIDEO_PLANE_SUB1)
		device_remove_file(drm->dev, &dev_attr_enable_sub_display);

	drm_plane_cleanup(plane);
}

struct drm_plane_state *
rtk_plane_atomic_plane_duplicate_state(struct drm_plane *plane)
{
	struct rtk_drm_plane_state *state;
	struct rtk_drm_plane_state *copy;

	if (WARN_ON(!plane->state))
		return NULL;

	state = to_rtk_plane_state(plane->state);
	copy = kmemdup(state, sizeof(*state), GFP_KERNEL);
	if (copy == NULL)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &copy->state);

	return &copy->state;
}

void rtk_plane_atomic_plane_destroy_state(struct drm_plane *plane,
					   struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(state);
	kfree(to_rtk_plane_state(state));
}

static void rtk_plane_atomic_plane_reset(struct drm_plane *plane)
{
	struct rtk_drm_plane_state *state;

	if (plane->state) {
		rtk_plane_atomic_plane_destroy_state(plane, plane->state);
		plane->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return;

	__drm_atomic_helper_plane_reset(plane, &state->state);
}

static int rtk_plane_atomic_set_property(struct drm_plane *plane,
				   struct drm_plane_state *state,
				   struct drm_property *property,
				   uint64_t val)
{
	struct rtk_drm_plane *rtk_plane = to_rtk_plane(plane);
	struct rtk_drm_plane_state *s =	to_rtk_plane_state(state);

	if (property == rtk_plane->display_idx_prop) {
		s->display_idx = (unsigned int)val;
		return 0;
	}

	if (property == rtk_plane->rtk_meta_data_prop) {
		struct drm_property_blob *meta_data =
			drm_property_lookup_blob(rtk_plane->plane.dev, val);

		if (meta_data->length > sizeof(struct video_object)) {
			DRM_ERROR("Meta data structure size not match\n");
			meta_data->length = sizeof(struct video_object);
		}
		memcpy(&s->rtk_meta_data, meta_data->data, meta_data->length);

		drm_property_blob_put(meta_data);

		return 0;
	}

	if (property == rtk_plane->vo_plane_name_prop)
		return 0;

	DRM_ERROR("failed to set rtk plane atomic property\n");
	return -EINVAL;
}


static int rtk_plane_atomic_get_property(struct drm_plane *plane,
				   const struct drm_plane_state *state,
				   struct drm_property *property,
				   uint64_t *val)
{
	struct rtk_drm_plane *rtk_plane = to_rtk_plane(plane);
	const struct rtk_drm_plane_state *s =
		container_of(state, const struct rtk_drm_plane_state, state);

	if (property == rtk_plane->display_idx_prop) {
		*val = s->display_idx;
		return 0;
	}

	if (property == rtk_plane->rtk_meta_data_prop) {
		*val = (s->rtk_meta_data_blob) ? s->rtk_meta_data_blob->base.id : 0;
		return 0;
	}

	if (property == rtk_plane->vo_plane_name_prop) {
		*val = rtk_plane->layer_nr;
		return 0;
	}

	DRM_ERROR("failed to get rtk plane atomic property\n");
	return -EINVAL;
}

static bool rtk_plane_mod_supported(struct drm_plane *plane,
				   u32 format, u64 modifier)
{
	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return true;

	if (modifier == RTK_AFBC_MOD)
		return true;
	else
		return false;
}

static const struct drm_plane_funcs rtk_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.destroy = rtk_plane_destroy,
	.disable_plane = drm_atomic_helper_disable_plane,
	.reset = rtk_plane_atomic_plane_reset,
	.atomic_duplicate_state = rtk_plane_atomic_plane_duplicate_state,
	.atomic_destroy_state = rtk_plane_atomic_plane_destroy_state,
	.atomic_set_property = rtk_plane_atomic_set_property,
	.atomic_get_property = rtk_plane_atomic_get_property,
	.format_mod_supported = rtk_plane_mod_supported,
};

static int rtk_plane_atomic_check(struct drm_plane *plane,
				  struct drm_atomic_state *state)
{
	DRM_DEBUG_KMS("%d\n", __LINE__);

	return 0;
}

static void rtk_plane_atomic_update(struct drm_plane *plane,
				    struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc = plane->state->crtc;
	struct drm_framebuffer *fb = plane->state->fb;

	DRM_DEBUG_KMS("%s, width=%d, height=%d\n", __func__,
			fb->width, fb->height);

	if (!crtc || WARN_ON(!fb))
		return;

	if (plane->type == DRM_PLANE_TYPE_OVERLAY)
		rtk_plane_update_video_obj(plane);
	else
		rtk_plane_update_graphic_obj(plane);

	if (rtk_plane_update_scaling(plane)) {
		DRM_ERROR("rtk_plane_update_scaling fail\n");
		return;
	}
}

static void rtk_plane_atomic_disable(struct drm_plane *plane,
				struct drm_atomic_state *old_state)
{
	struct rtk_drm_plane *rtk_plane = to_rtk_plane(plane);
	struct rtk_rpc_info *rpc_info = rtk_plane->rpc_info;
	struct vo_rectangle rect;

	DRM_DEBUG_KMS("rtk_plane_atomic_disable plane type %d\n", plane->type);

	rect.x = 0;
	rect.y = 0;
	rect.width = 0;
	rect.height = 0;

	rtk_plane->disp_win.videoWin = rect;
	rtk_plane->disp_win.borderWin = rect;

	if (rpc_video_config_disp_win(rpc_info, &rtk_plane->disp_win))
		DRM_ERROR("rpc_video_config_disp_win RPC fail\n");
}

static const struct drm_plane_helper_funcs rtk_plane_helper_funcs = {
	.atomic_check = rtk_plane_atomic_check,
	.atomic_update = rtk_plane_atomic_update,
	.atomic_disable = rtk_plane_atomic_disable,
};

int rtk_plane_init(struct drm_device *drm, struct rtk_drm_plane *rtk_plane,
		   unsigned long possible_crtcs, enum drm_plane_type type,
		   enum VO_VIDEO_PLANE layer_nr)
{
	struct drm_plane *plane = &rtk_plane->plane;
	struct rtk_drm_private *priv = drm->dev_private;
	int err;
	const uint32_t *plane_formats;
	unsigned int format_count;
	const uint64_t *format_modifiers;

	if (type == DRM_PLANE_TYPE_OVERLAY) {
		plane_formats = video_formats;
		format_count = ARRAY_SIZE(video_formats);
		format_modifiers = format_modifiers_default;
	} else if (type == DRM_PLANE_TYPE_PRIMARY) {
		plane_formats = osd_formats;
		format_count = ARRAY_SIZE(osd_formats);
		format_modifiers = format_modifiers_afbc;
	} else {
		plane_formats = other_formats;
		format_count = ARRAY_SIZE(other_formats);
		format_modifiers = format_modifiers_default;
	}

	err = drm_universal_plane_init(drm, plane, possible_crtcs,
				       &rtk_plane_funcs, plane_formats,
				       format_count, format_modifiers,
				       type, NULL);
	if (err) {
		DRM_ERROR("failed to initialize plane\n");
		return err;
	}

	drm_plane_helper_add(plane, &rtk_plane_helper_funcs);

	if(type == DRM_PLANE_TYPE_OVERLAY) {
		drm_plane_create_zpos_immutable_property(plane, 0);

		rtk_plane->display_idx_prop = drm_property_create_range(plane->dev, DRM_MODE_PROP_ATOMIC,
					"display_idx", 0, PLOCK_BUFFER_SET_SIZE-1);
		if (!rtk_plane->display_idx_prop) {
			pr_err("III %s create display_idx property fail\n", __func__);
			return -ENOMEM;
		}

		drm_object_attach_property(&plane->base, rtk_plane->display_idx_prop, 0);
	}
	else if (type == DRM_PLANE_TYPE_PRIMARY) {
		drm_plane_create_zpos_immutable_property(plane, 1);
	}
	else {
		drm_plane_create_zpos_immutable_property(plane, 2);
	}

	rtk_plane->vo_plane_name_prop = drm_property_create_enum(plane->dev, 0, "plane name",
				vo_plane_list, ARRAY_SIZE(vo_plane_list));

	drm_object_attach_property(&plane->base, rtk_plane->vo_plane_name_prop, 0);
	rtk_plane->layer_nr = layer_nr;

	rtk_plane->rtk_meta_data_prop = drm_property_create(plane->dev,
							DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
							"RTK_META_DATA", 0);
	if (!rtk_plane->rtk_meta_data_prop) {
		DRM_ERROR("%s create rtk_meta_data_prop property fail\n", __func__);
		return -ENOMEM;
	}
	drm_object_attach_property(&plane->base, rtk_plane->rtk_meta_data_prop, 0);

	rtk_plane->rpc_info = &priv->rpc_info;
	rtk_plane->gAlpha = 0;
	rtk_plane->flags &= ~BG_SWAP;
	rtk_plane->flags |= VSYNC_FORCE_LOCK;

	rtk_plane_rpc_init(rtk_plane, layer_nr);
	if (type == DRM_PLANE_TYPE_OVERLAY)
		rtk_fence_init(rtk_plane);

	return 0;
}

int rtk_plane_export_refclock_fd_ioctl(struct drm_device *dev,
			    void *data, struct drm_file *file)
{
	struct drm_rtk_refclk *refclk = (struct drm_rtk_refclk *)data;
	struct drm_plane *plane = drm_plane_find(dev, file, refclk->plane_id);
	struct rtk_drm_plane *rtk_plane =  container_of(plane, struct rtk_drm_plane, plane);
	int ret = 0;

	get_dma_buf(rtk_plane->refclock_dmabuf);
	refclk->fd = dma_buf_fd(rtk_plane->refclock_dmabuf, O_CLOEXEC);
	if (refclk->fd < 0) {
		dma_buf_put(rtk_plane->refclock_dmabuf);
		return refclk->fd;
	}

	return ret;
}

int rtk_plane_get_plane_id(struct drm_device *dev,
			    void *data, struct drm_file *file)
{
	struct drm_rtk_vo_plane *rtk_vo_plane = (struct drm_rtk_vo_plane *)data;
	struct drm_plane *plane;
	struct rtk_drm_plane *rtk_plane;
	int ret = -1;

	drm_for_each_plane(plane, dev) {
		rtk_plane = container_of(plane, struct rtk_drm_plane, plane);
		if(rtk_plane->info.videoPlane == rtk_vo_plane->vo_plane) {
			rtk_vo_plane->plane_id = rtk_plane->plane.base.id;
			ret = 0;
			break;
		}
	}

	return ret;
}

int rtk_plane_set_q_param(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rtk_drm_private *priv = dev->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;

	if(!rpc_info)
		return -1;

	return rpc_video_set_q_param(rpc_info, (struct rpc_set_q_param *)data);
}

int rtk_plane_config_channel_lowdelay(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rpc_config_channel_lowdelay *param = (struct rpc_config_channel_lowdelay *)data;
	struct drm_plane *plane = drm_plane_find(dev, file, param->plane_id);
	struct rtk_drm_plane *rtk_plane;
	struct rtk_rpc_info *rpc_info;
	struct rpc_config_channel_lowdelay arg;

	if (IS_ERR_OR_NULL(plane))
		return -1;

	rtk_plane = container_of(plane, struct rtk_drm_plane, plane);
	rpc_info = rtk_plane->rpc_info;

	if(!rpc_info)
		return -1;

	arg.mode = param->mode;
	arg.instanceId = rtk_plane->info.instance;

	return rpc_video_config_channel_lowdelay(rpc_info, &arg);
}

int rtk_plane_query_dispwin_new(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rpc_query_disp_win_out_new *param = (struct rpc_query_disp_win_out_new *)data;
	struct drm_plane *plane = drm_plane_find(dev, file, param->plane_id);
	struct rtk_drm_plane *rtk_plane = container_of(plane, struct rtk_drm_plane, plane);
	struct rtk_rpc_info *rpc_info = rtk_plane->rpc_info;
	struct rpc_query_disp_win_in argp_in;

	argp_in.plane = rtk_plane->disp_win.videoPlane;

	return rpc_video_query_disp_win_new(rpc_info, &argp_in, param);
}

int rtk_plane_get_privateinfo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rpc_privateinfo_param *param = (struct rpc_privateinfo_param *)data;
	struct drm_plane *plane = drm_plane_find(dev, file, param->plane_id);
	struct rtk_drm_plane *rtk_plane;
	struct rtk_rpc_info *rpc_info;

	if (IS_ERR_OR_NULL(plane))
		return -1;

	rtk_plane = container_of(plane, struct rtk_drm_plane, plane);
	rpc_info = rtk_plane->rpc_info;

	if(!rpc_info)
		return -1;

	param->instanceId = rtk_plane->info.instance;

	return rpc_video_privateinfo_param(rpc_info, param);
}

int rtk_plane_set_speed(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rpc_set_speed *param = (struct rpc_set_speed *)data;
	struct drm_plane *plane = drm_plane_find(dev, file, param->plane_id);
	struct rtk_drm_plane *rtk_plane;
	struct rtk_rpc_info *rpc_info;

	if (IS_ERR_OR_NULL(plane))
		return -1;

	rtk_plane = container_of(plane, struct rtk_drm_plane, plane);
	rpc_info = rtk_plane->rpc_info;

	if(!rpc_info)
		return -1;

	param->instanceId = rtk_plane->info.instance;

	return rpc_video_set_speed(rpc_info, param);
}

int rtk_plane_set_background(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rtk_drm_private *priv = dev->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;

	if(!rpc_info)
		return -1;

	return rpc_video_set_background(rpc_info, (struct rpc_set_background *)data);
}

int rtk_plane_keep_curpic(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rpc_keep_curpic *param = (struct rpc_keep_curpic *)data;
	struct drm_plane *plane = drm_plane_find(dev, file, param->plane_id);
	struct rtk_drm_plane *rtk_plane;
	struct rtk_rpc_info *rpc_info;

	if (IS_ERR_OR_NULL(plane))
		return -1;

	rtk_plane = container_of(plane, struct rtk_drm_plane, plane);
	rpc_info = rtk_plane->rpc_info;

	if(!rpc_info)
		return -1;

	param->plane = rtk_plane->disp_win.videoPlane;

	return rpc_video_keep_curpic(rpc_info, param);
}

int rtk_plane_keep_curpic_fw(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rpc_keep_curpic *param = (struct rpc_keep_curpic *)data;
	struct drm_plane *plane = drm_plane_find(dev, file, param->plane_id);
	struct rtk_drm_plane *rtk_plane;
	struct rtk_rpc_info *rpc_info;

	if (IS_ERR_OR_NULL(plane))
		return -1;

	rtk_plane = container_of(plane, struct rtk_drm_plane, plane);
	rpc_info = rtk_plane->rpc_info;

	if(!rpc_info)
		return -1;

	param->plane = rtk_plane->disp_win.videoPlane;

	return rpc_video_keep_curpic_fw(rpc_info, param);
}

int rtk_plane_keep_curpic_svp(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rpc_keep_curpic_svp *param = (struct rpc_keep_curpic_svp *)data;
	struct drm_plane *plane = drm_plane_find(dev, file, param->plane_id);
	struct rtk_drm_plane *rtk_plane;
	struct rtk_rpc_info *rpc_info;

	if (IS_ERR_OR_NULL(plane))
		return -1;

	rtk_plane = container_of(plane, struct rtk_drm_plane, plane);
	rpc_info = rtk_plane->rpc_info;

	if(!rpc_info)
		return -1;

	param->plane = rtk_plane->disp_win.videoPlane;

	return rpc_video_keep_curpic_svp(rpc_info, param);
}

int rtk_plane_set_deintflag(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rtk_drm_private *priv = dev->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;

	if(!rpc_info)
		return -1;

	return rpc_video_set_deintflag(rpc_info, (struct rpc_set_deintflag *)data);
}

int rtk_plane_create_graphic_win(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rtk_drm_private *priv = dev->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;

	if(!rpc_info)
		return -1;

	return rpc_video_create_graphic_win(rpc_info, (struct rpc_create_graphic_win *)data);
}

int rtk_plane_draw_graphic_win(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rtk_drm_private *priv = dev->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;

	if(!rpc_info)
		return -1;

	return rpc_video_draw_graphic_win(rpc_info, (struct rpc_draw_graphic_win *)data);
}

int rtk_plane_modify_graphic_win(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rtk_drm_private *priv = dev->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;

	if(!rpc_info)
		return -1;

	return rpc_video_modify_graphic_win(rpc_info, (struct rpc_modify_graphic_win *)data);
}

int rtk_plane_delete_graphic_win(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rtk_drm_private *priv = dev->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;

	if(!rpc_info)
		return -1;

	return rpc_video_delete_graphic_win(rpc_info, (struct rpc_delete_graphic_win *)data);
}

int rtk_plane_conf_osd_palette(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rtk_drm_private *priv = dev->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;

	if(!rpc_info)
		return -1;

	return rpc_video_config_osd_palette(rpc_info, (struct rpc_config_osd_palette *)data);
}

int rtk_plane_conf_plane_mixer(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rpc_config_plane_mixer *param = (struct rpc_config_plane_mixer *)data;
	struct drm_plane *plane = drm_plane_find(dev, file, param->plane_id);
	struct rtk_drm_plane *rtk_plane;
	struct rtk_rpc_info *rpc_info;

	if (IS_ERR_OR_NULL(plane))
		return -1;

	rtk_plane = container_of(plane, struct rtk_drm_plane, plane);
	rpc_info = rtk_plane->rpc_info;

	if(!rpc_info)
		return -1;

	param->instanceId = rtk_plane->info.instance;

	return rpc_video_config_plane_mixer(rpc_info, param);
}

int rtk_plane_set_tv_system(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rtk_drm_private *priv = dev->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;

	if(!rpc_info)
		return -1;

	return rpc_set_tv_system(rpc_info, (struct rpc_config_tv_system *)data);
}

int rtk_plane_get_tv_system(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rtk_drm_private *priv = dev->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;

	if(!rpc_info)
		return -1;

	return rpc_query_tv_system(rpc_info, (struct rpc_config_tv_system *)data);
}

int rtk_plane_set_dispout_format(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rtk_drm_private *priv = dev->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;

	if(!rpc_info)
		return -1;

	return rpc_set_display_format(rpc_info, (struct rpc_display_output_format *)data);
}

int rtk_plane_get_dispout_format(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rtk_drm_private *priv = dev->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;

	if(!rpc_info)
		return -1;

	return rpc_get_display_format(rpc_info, (struct rpc_display_output_format *)data);
}

int rtk_plane_set_hdmi_audio_mute(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rtk_drm_private *priv = dev->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;

	if(!rpc_info)
		return -1;

	return rpc_set_hdmi_audio_mute(rpc_info, (struct rpc_audio_mute_info *)data);
}

int rtk_plane_set_sdrflag(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct rtk_drm_private *priv = dev->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;

	if(!rpc_info)
		return -1;

	return rpc_video_set_sdrflag(rpc_info, (struct rpc_set_sdrflag *)data);
}

int rtk_plane_set_pause_ioctl(struct drm_device *dev,
			    void *data, struct drm_file *file)
{
	struct drm_rtk_pause *pause = (struct drm_rtk_pause *)data;
	struct drm_plane *plane = drm_plane_find(dev, file, pause->plane_id);
	struct rtk_drm_plane *rtk_plane;
	struct rtk_rpc_info *rpc_info;

	if (IS_ERR_OR_NULL(plane))
		return -1;

	rtk_plane = container_of(plane, struct rtk_drm_plane, plane);
	rpc_info = rtk_plane->rpc_info;

	if(!rpc_info)
		return -1;

	if(pause->enable) {
		if (rpc_video_pause(rpc_info, rtk_plane->info.instance)) {
			DRM_ERROR("rpc_video_pause RPC fail\n");
			return -1;
		}
	} else {
		if (rpc_video_run(rpc_info, rtk_plane->info.instance)) {
			DRM_ERROR("rpc_video_run RPC fail\n");
			return -1;
		}
	}

	return 0;
}

int rtk_plane_set_flush_ioctl(struct drm_device *dev,
			    void *data, struct drm_file *file)
{
	uint32_t *plane_id = (uint32_t *)data;
	struct drm_plane *plane = drm_plane_find(dev, file, *plane_id);
	struct rtk_drm_plane *rtk_plane;
	struct rtk_rpc_info *rpc_info;

	if (IS_ERR_OR_NULL(plane))
		return -1;

	rtk_plane = container_of(plane, struct rtk_drm_plane, plane);
	rpc_info = rtk_plane->rpc_info;

	if(!rpc_info)
		return -1;

	if (rpc_video_flush(rpc_info, rtk_plane->info.instance)) {
		DRM_ERROR("rpc_video_flush RPC fail\n");
		return -1;
	}

	return 0;
}

static int plane_display_get(struct rtk_rpc_info *rpc_info,
	struct vo_rectangle* rect, enum VO_VIDEO_PLANE plane_type)
{
	struct rpc_query_disp_win_in structQueryDispWin_in;
	struct rpc_query_disp_win_out structQueryDispWin_out;

	mutex_lock(&enable_display_mutex);

	memset(&structQueryDispWin_in, 0, sizeof(structQueryDispWin_in));
	memset(&structQueryDispWin_out, 0, sizeof(structQueryDispWin_out));

	structQueryDispWin_in.plane = plane_type;

	if (rpc_video_query_dis_win(rpc_info, &structQueryDispWin_in, &structQueryDispWin_out))
	{
		DRM_ERROR("[%s %d]\n", __FUNCTION__, __LINE__);
		return -1;
	}

	rect->x = structQueryDispWin_out.configWin.x;
	rect->y = structQueryDispWin_out.configWin.y;
	rect->width = structQueryDispWin_out.configWin.width;
	rect->height = structQueryDispWin_out.configWin.height;
	mutex_unlock(&enable_display_mutex);

	return 0;
}

static int plane_display_set(struct rtk_rpc_info *rpc_info,
	struct vo_rectangle* rect, enum VO_VIDEO_PLANE plane_type)
{
	mutex_lock(&enable_display_mutex);

	if (plane_type == VO_VIDEO_PLANE_V1 || plane_type == VO_VIDEO_PLANE_OSD1)
	{
		struct rpc_config_disp_win structConfigDispWin;
		struct vo_color blueBorder = {0,0,255,1};

		memset(&structConfigDispWin, 0, sizeof(structConfigDispWin));
		structConfigDispWin.videoPlane = plane_type;
		structConfigDispWin.videoWin = *rect;
		structConfigDispWin.borderWin = *rect;
		structConfigDispWin.borderColor = blueBorder;
		structConfigDispWin.enBorder = 0;
		if (rpc_video_config_disp_win(rpc_info, &structConfigDispWin))
		{
			DRM_ERROR("[%s %d]\n", __FUNCTION__, __LINE__);
			return -1;
		}
	}else if (plane_type == VO_VIDEO_PLANE_SUB1)
	{
		struct rpc_config_graphic_canvas  structConfigGraphicCanvas;

		memset(&structConfigGraphicCanvas, 0, sizeof(structConfigGraphicCanvas));
		structConfigGraphicCanvas.plane = VO_GRAPHIC_SUB1;
		structConfigGraphicCanvas.srcWin.width = 1280;
		structConfigGraphicCanvas.srcWin.height = 720;
		structConfigGraphicCanvas.srcWin.x = 0;
		structConfigGraphicCanvas.srcWin.y = 0;
		structConfigGraphicCanvas.dispWin = *rect;
		structConfigGraphicCanvas.go = 1;
		if (rpc_video_config_graphic(rpc_info, &structConfigGraphicCanvas))
		{
			DRM_ERROR("[%s %d]\n", __FUNCTION__, __LINE__);
			return -1;
		}
	}else
	{
		DRM_ERROR("[%s %d]\n", __FUNCTION__, __LINE__);
		return -1;
	}

	mutex_unlock(&enable_display_mutex);
	return 0;
}

ssize_t rtk_plane_enable_osd_display_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct rtk_drm_private *priv = drm->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;
	unsigned long state;
	int ret;
	struct vo_rectangle rect = {0};

	ret = kstrtol(buf, 0, &state);
	/* valid input value: 0 or 1 */
	if (ret != 0 || state & INVERT_BITVAL_1)
	    return -EINVAL;
	if (state == 0){
	    if(plane_display_get(rpc_info, &rect, VO_VIDEO_PLANE_OSD1) != 0)
	        return -EINVAL;
	    if (rect.x == 0 && rect.y == 0 && rect.width == 0 && rect.height == 0){
	        DRM_ERROR("osd1 plane is already disabled \n");
	        return count;
	    }
	    rect_osd1 = rect;
	    if(plane_display_set(rpc_info, &rect_plane_disabled, VO_VIDEO_PLANE_OSD1) != 0)
	        return -EINVAL;
	}else if(state == 1){
	    if(plane_display_get(rpc_info, &rect, VO_VIDEO_PLANE_OSD1) != 0)
	        return -EINVAL;
	    if (rect.x != 0 || rect.y != 0 || rect.width != 0 || rect.height != 0){
	        DRM_ERROR("osd1 plane is already enabled \n");
	        return count;
	    }
	    if(plane_display_set(rpc_info, &rect_osd1, VO_VIDEO_PLANE_OSD1) != 0)
	        return -EINVAL;
	}else{
	    DRM_ERROR("enable_osd_display_store fail \n");
	}
	return count;
}

ssize_t rtk_plane_enable_sub_display_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct rtk_drm_private *priv = drm->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;
	unsigned long state;
	int ret;
	struct vo_rectangle rect = {0};

	ret = kstrtol(buf, 0, &state);
	/* valid input value: 0 or 1 */
	if (ret != 0 || state & INVERT_BITVAL_1)
	    return -EINVAL;
	if (state == 0){
	    if(plane_display_get(rpc_info, &rect, VO_VIDEO_PLANE_SUB1) != 0)
	        return -EINVAL;
	    if (rect.x == 0 && rect.y == 0 && rect.width == 0 && rect.height == 0){
	        DRM_ERROR("sub1 plane is already disabled \n");
	        return count;
	    }
	    rect_sub1 = rect;
	    if(plane_display_set(rpc_info, &rect_plane_disabled, VO_VIDEO_PLANE_SUB1) != 0)
	        return -EINVAL;
	}else if(state == 1){
	    if(plane_display_get(rpc_info, &rect, VO_VIDEO_PLANE_SUB1) != 0)
	        return -EINVAL;
	    if (rect.x != 0 || rect.y != 0 || rect.width != 0 || rect.height != 0){
	        DRM_ERROR("sub1 plane is already enabled \n");
	        return count;
	    }
	    if(plane_display_set(rpc_info, &rect_sub1, VO_VIDEO_PLANE_SUB1) != 0)
	        return -EINVAL;
	}else{
	    DRM_ERROR("enable_sub_display_store fail \n");
	}
	return count;
}

ssize_t rtk_plane_enable_video_display_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct rtk_drm_private *priv = drm->dev_private;
	struct rtk_rpc_info *rpc_info = &priv->rpc_info;
	unsigned long state;
	int ret;
	struct vo_rectangle rect = {0};
	ret = kstrtol(buf, 0, &state);
	/* valid input value: 0 or 1 */
	if (ret != 0 || state & INVERT_BITVAL_1)
	    return -EINVAL;
	if (state == 0){
	    if(plane_display_get(rpc_info, &rect, VO_VIDEO_PLANE_V1) != 0)
	        return -EINVAL;
	    if (rect.x == 0 && rect.y == 0 && rect.width == 0 && rect.height == 0){
	        DRM_ERROR("video1 plane is already disabled \n");
	        return count;
	    }
	    rect_video1 = rect;
	    if(plane_display_set(rpc_info, &rect_plane_disabled, VO_VIDEO_PLANE_V1) != 0)
	        return -EINVAL;
	}else if(state == 1){
	    if(plane_display_get(rpc_info, &rect, VO_VIDEO_PLANE_V1) != 0)
	        return -EINVAL;
	    if (rect.x != 0 || rect.y != 0 || rect.width != 0 || rect.height != 0){
	        DRM_ERROR("video1 plane is already enabled \n");
	        return count;
	    }
	    if(plane_display_set(rpc_info, &rect_video1, VO_VIDEO_PLANE_V1) != 0)
	        return -EINVAL;
	}else{
	    DRM_ERROR("enable_video_display_store fail \n");
	}
	return count;
}

