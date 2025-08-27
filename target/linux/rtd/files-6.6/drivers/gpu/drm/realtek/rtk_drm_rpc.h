/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019 Realtek Semiconductor Corp.
 */

#ifndef _RTK_DRM_RPC_H
#define _RTK_DRM_RPC_H

#include <linux/kernel.h>
#include <linux/dma-buf.h>
#include <soc/realtek/rtk_ipc_shm.h>
#include <soc/realtek/rtk-krpc-agent.h>

#define S_OK 0x10000000

#define RPC_CMD_BUFFER_SIZE 4096
#define DC_VO_SET_NOTIFY 	(1U << 0)
#define DC_VO_FEEDBACK_NOTIFY	(__cpu_to_be32(1U << 1))
#define DC_HAS_BIT(addr, bit)           (readl(addr) & bit)
#define DC_SET_BIT(addr,bit)            (writel((readl(addr)|bit), addr))
#define DC_RESET_BIT(addr,bit)          (writel((readl(addr)&~bit), addr))

#define REPLYID 99

#define DMABUF_HEAPS_RTK

#ifdef DMABUF_HEAPS_RTK
#include <soc/realtek/memory.h>
#include <soc/realtek/rtk_media_heap.h>

#define AUDIO_RTK_FLAG \
		 (RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC | RTK_FLAG_ACPUACC)
#define VIDEO_RTK_FLAG (RTK_FLAG_SCPUACC | RTK_FLAG_HWIPACC | RTK_FLAG_ACPUACC | RTK_FLAG_NONCACHED)

#define BUFFER_NONCACHED	(0x1)
#define BUFFER_SCPUACC		(0x1 << 1)
#define BUFFER_ACPUACC		(0x1 << 2)
#define BUFFER_HWIPACC		(0x1 << 3)
#define BUFFER_VE_SPEC		(0x1 << 4)
#define BUFFER_ALGO_LAST_FIT	(0x1 << 5)
#define BUFFER_PROTECTED	(0x1 << 6)
#define BUFFER_SECURE_AUDIO	(0x1 << 7)
#define BUFFER_HEAP_MEDIA	(0x1 << 8)
#define BUFFER_HEAP_AUDIO	(0x1 << 9)
#define BUFFER_HEAP_SYSTEM	(0x1 << 10)
#define BUFFER_HEAP_DMA		(0x1 << 11)
#define BUFFER_HEAP_SECURE	(0x1 << 12)
#define BUFFER_MASK		((0x1 << 13) - 1)

#define RPC_ALIGN_SZ 128

extern unsigned int get_rtk_flags(unsigned int dumb_flags);
extern bool is_media_heap(unsigned int dumb_flags);
extern bool is_audio_heap(unsigned int dumb_flags);

#endif

#define FW_RETURN_SUCCESS  0x10000000

enum {
	ENUM_KERNEL_RPC_CREATE_AGENT,
	ENUM_KERNEL_RPC_INIT_RINGBUF,
	ENUM_KERNEL_RPC_PRIVATEINFO,
	ENUM_KERNEL_RPC_RUN,
	ENUM_KERNEL_RPC_PAUSE,
	ENUM_KERNEL_RPC_SWITCH_FOCUS,
	ENUM_KERNEL_RPC_MALLOC_ADDR,
	ENUM_KERNEL_RPC_VOLUME_CONTROL,
	ENUM_KERNEL_RPC_FLUSH,
	ENUM_KERNEL_RPC_CONNECT,
	ENUM_KERNEL_RPC_SETREFCLOCK,
	ENUM_KERNEL_RPC_DAC_I2S_CONFIG,
	ENUM_KERNEL_RPC_DAC_SPDIF_CONFIG,
	ENUM_KERNEL_RPC_HDMI_OUT_EDID,
	ENUM_KERNEL_RPC_HDMI_OUT_EDID2,
	ENUM_KERNEL_RPC_HDMI_SET,
	ENUM_KERNEL_RPC_HDMI_MUTE,
	ENUM_KERNEL_RPC_ASK_DBG_MEM_ADDR,
	ENUM_KERNEL_RPC_DESTROY,
	ENUM_KERNEL_RPC_STOP,
	ENUM_KERNEL_RPC_CHECK_READY,
	ENUM_KERNEL_RPC_GET_MUTE_N_VOLUME,
	ENUM_KERNEL_RPC_EOS,
	ENUM_KERNEL_RPC_ADC0_CONFIG,
	ENUM_KERNEL_RPC_ADC1_CONFIG,
	ENUM_KERNEL_RPC_ADC2_CONFIG,
	ENUM_KERNEL_RPC_HDMI_OUT_VSDB,
	ENUM_VIDEO_KERNEL_RPC_CONFIG_TV_SYSTEM,
	ENUM_VIDEO_KERNEL_RPC_CONFIG_HDMI_INFO_FRAME,
	ENUM_VIDEO_KERNEL_RPC_QUERY_DISPLAY_WIN,
	ENUM_VIDEO_KERNEL_RPC_PP_INIT_PIN,
	ENUM_KERNEL_RPC_INIT_RINGBUF_AO,
	ENUM_VIDEO_KERNEL_RPC_VOUT_EDID_DATA,
	ENUM_KERNEL_RPC_AUDIO_POWER_SET,
	ENUM_VIDEO_KERNEL_RPC_VOUT_VDAC_SET,
	ENUM_VIDEO_KERNEL_RPC_QUERY_CONFIG_TV_SYSTEM,
	ENUM_KERNEL_RPC_AUDIO_CONFIG,
	ENUM_KERNEL_RPC_AIO_PRIVATEINFO,
	ENUM_KERNEL_RPC_QUERY_FW_DEBUG_INFO,
	ENUM_KERNEL_RPC_HDMI_RX_LATENCY_MEM,
	ENUM_KERNEL_RPC_EQ_CONFIG,
	ENUM_VIDEO_KERNEL_RPC_CREATE,
	ENUM_VIDEO_KERNEL_RPC_DISPLAY,
	ENUM_VIDEO_KERNEL_RPC_CONFIGUREDISPLAYWINDOW,
	ENUM_VIDEO_KERNEL_RPC_SETREFCLOCK,
	ENUM_VIDEO_KERNEL_RPC_RUN,
	ENUM_VIDEO_KERNEL_RPC_INITRINGBUFFER,
	ENUM_VIDEO_KERNEL_RPC_SETRESCALEMODE,
	ENUM_VIDEO_KERNEL_RPC_SET_HDMI_VRR,
	ENUM_VIDEO_KERNEL_RPC_CONFIGURE_GRAPHIC_CANVAS,
	ENUM_VIDEO_KERNEL_RPC_SET_MIXER_ORDER,
	ENUM_KERNEL_RPC_DEC_PRIVATEINFO,
	ENUM_KERNEL_RPC_ALSA_FASTER,
	ENUM_VIDEO_KERNEL_RPC_PAUSE,
	ENUM_VIDEO_KERNEL_RPC_STOP,
	ENUM_VIDEO_KERNEL_RPC_DESTROY,
	ENUM_VIDEO_KERNEL_RPC_FLUSH,
	ENUM_VIDEO_KERNEL_RPC_Q_PARAMETER,
	ENUM_VIDEO_KERNEL_RPC_CONFIGCHANNELLOWDELAY,
	ENUM_VIDEO_KERNEL_RPC_PRIVATEINFO,
	ENUM_VIDEO_KERNEL_RPC_QUERYDISPLAYWINNEW,
	ENUM_VIDEO_KERNEL_RPC_SETSPEED,
	ENUM_VIDEO_KERNEL_RPC_SETBACKGROUND,
	ENUM_VIDEO_KERNEL_RPC_KEEPCURPIC,
	ENUM_VIDEO_KERNEL_RPC_KEEPCURPIC_FW_MALLOC,
	ENUM_VIDEO_KERNEL_RPC_KEEPCURPICSVP,
	ENUM_VIDEO_KERNEL_RPC_SET_DEINTFLAG,
	ENUM_VIDEO_KERNEL_RPC_CREATEGRAPHICWINDOW,
	ENUM_VIDEO_KERNEL_RPC_DRAWGRAPHICWINDOW,
	ENUM_VIDEO_KERNEL_RPC_MODIFYGRAPHICWINDOW,
	ENUM_VIDEO_KERNEL_RPC_DELETEGRAPHICWINDOW,
	ENUM_VIDEO_KERNEL_RPC_CONFIGUREOSDPALETTE,
	ENUM_VIDEO_KERNEL_RPC_PMIXER_CONFIGUREPLANEMIXER,
	ENUM_VIDEO_KERNEL_RPC_SET_DISPLAY_OUTPUT_FORMAT,
	ENUM_VIDEO_KERNEL_RPC_GET_DISPLAY_OUTPUT_FORMAT,
	ENUM_VIDEO_KERNEL_RPC_SET_ENHANCEDSDR,
	ENUM_KERNEL_RPC_HDMI_EDID_RAW_DATA,
	ENUM_VIDEO_KERNEL_RPC_ConfigWriteBackFlow,
	ENUM_VIDEO_KERNEL_RPC_NPP_Init,
	ENUM_VIDEO_KERNEL_RPC_NPP_Destroy,
	ENUM_KERNEL_RPC_AFW_DEBUGLEVEL,
	ENUM_KERNEL_RPC_DV_ControlPath_Info,
	ENUM_KERNEL_RPC_GET_AFW_DEBUGLEVEL,
	ENUM_KERNEL_RPC_ConfigureDisplayWindowDispZoomWinRatio,
	ENUM_VIDEO_KERNEL_RPC_GET_MIXER_ORDER,
	ENUM_VIDEO_KERNEL_RPC_SET_DISPLAY_OUTPUT_INTERFACE,
	ENUM_VIDEO_KERNEL_RPC_GET_DISPLAY_OUTPUT_INTERFACE,
};

enum VO_VIDEO_PLANE {
	VO_VIDEO_PLANE_V1,
	VO_VIDEO_PLANE_V2,
	VO_VIDEO_PLANE_SUB1,
	VO_VIDEO_PLANE_OSD1,
	VO_VIDEO_PLANE_OSD2,
	VO_VIDEO_PLANE_WIN1,
	VO_VIDEO_PLANE_WIN2,
	VO_VIDEO_PLANE_WIN3,
	VO_VIDEO_PLANE_WIN4,
	VO_VIDEO_PLANE_WIN5,
	VO_VIDEO_PLANE_WIN6,
	VO_VIDEO_PLANE_WIN7,
	VO_VIDEO_PLANE_WIN8,
	VO_VIDEO_PLANE_NONE,
};

enum VO_GRAPHIC_PLANE {
	VO_GRAPHIC_OSD            = 0,
	VO_GRAPHIC_SUB1            = 1,
	VO_GRAPHIC_SUB2            = 2,
	VO_GRAPHIC_OSD1 = 0,        /* modify naming to substitute for VO_GRAPHIC_OSD  on Saturn and later chip */
	VO_GRAPHIC_OSD2 = 2         /* modify naming to substitute for VO_GRAPHIC_SUB2 on Saturn and later chip */
};

enum VIDEO_VF_TYPE {
	VF_TYPE_VIDEO_MPEG2_DECODER,
	VF_TYPE_VIDEO_MPEG4_DECODER,
	VF_TYPE_VIDEO_DIVX_DECODER,
	VF_TYPE_VIDEO_H263_DECODER,
	VF_TYPE_VIDEO_H264_DECODER,
	VF_TYPE_VIDEO_VC1_DECODER,
	VF_TYPE_VIDEO_REAL_DECODER,
	VF_TYPE_VIDEO_JPEG_DECODER,
	VF_TYPE_VIDEO_MJPEG_DECODER,
	VF_TYPE_SPU_DECODER,
	VF_TYPE_VIDEO_OUT,
	VF_TYPE_TRANSITION,
	VF_TYPE_THUMBNAIL,
	VF_TYPE_VIDEO_VP6_DECODER,
	VF_TYPE_VIDEO_IMAGE_DECODER,
	VF_TYPE_FLASH,
	VF_TYPE_VIDEO_AVS_DECODER,
	VF_TYPE_MIXER,
	VF_TYPE_VIDEO_VP8_DECODER,
	VF_TYPE_VIDEO_WMV7_DECODER,
	VF_TYPE_VIDEO_WMV8_DECODER,
	VF_TYPE_VIDEO_RAW_DECODER,
	VF_TYPE_VIDEO_THEORA_DECODER,
	VF_TYPE_VIDEO_FJPEG_DECODER,
	VF_TYPE_VIDEO_H265_DECODER,
	VF_TYPE_VIDEO_VP9_DECODER,
};

enum INBAND_CMD_TYPE {
	INBAND_CMD_TYPE_PTS = 0,
	INBAND_CMD_TYPE_PTS_SKIP,
	INBAND_CMD_TYPE_NEW_SEG,
	INBAND_CMD_TYPE_SEQ_END,
	INBAND_CMD_TYPE_EOS,
	INBAND_CMD_TYPE_CONTEXT,
	INBAND_CMD_TYPE_DECODE,

	/* Video Decoder In-band Command */
	VIDEO_DEC_INBAND_CMD_TYPE_VOBU,
	VIDEO_DEC_INBAND_CMD_TYPE_DVDVR_DCI_CCI,
	VIDEO_DEC_INBAND_CMD_TYPE_DVDV_VATR,

	/* MSG Type for parse mode */
	VIDEO_DEC_INBAND_CMD_TYPE_SEG_INFO,
	VIDEO_DEC_INBAND_CMD_TYPE_PIC_INFO,

	/* Sub-picture Decoder In-band Command */
	VIDEO_SUBP_INBAND_CMD_TYPE_SET_PALETTE,
	VIDEO_SUBP_INBAND_CMD_TYPE_SET_HIGHLIGHT,

	/* Video Mixer In-band Command */
	VIDEO_MIXER_INBAND_CMD_TYPE_SET_BG_COLOR,
	VIDEO_MIXER_INBAND_CMD_TYPE_SET_MIXER_RPTS,
	VIDEO_MIXER_INBAND_CMD_TYPE_BLEND,

	/* Video Scaler In-band Command */
	VIDEO_SCALER_INBAND_CMD_TYPE_OUTPUT_FMT,

	/*DivX3 resolution In-band Command*/
	VIDEO_DIVX3_INBAND_CMD_TYPE_RESOLUTION,

	/*MPEG4 DivX4 detected In-band command*/
	VIDEO_MPEG4_INBAND_CMD_TYPE_MP4,
	/* Audio In-band Commands Start Here */

	/* DV In-band Commands */
	VIDEO_DV_INBAND_CMD_TYPE_VAUX,
	VIDEO_DV_INBAND_CMD_TYPE_FF,	//fast forward

	/* Transport Demux In-band command */
	VIDEO_TRANSPORT_DEMUX_INBAND_CMD_TYPE_PID,
	VIDEO_TRANSPORT_DEMUX_INBAND_CMD_TYPE_PTS_OFFSET,
	VIDEO_TRANSPORT_DEMUX_INBAND_CMD_TYPE_PACKET_SIZE,

	/* Real Video In-band command */
	VIDEO_RV_INBAND_CMD_TYPE_FRAME_INFO,
	VIDEO_RV_INBAND_CMD_TYPE_FORMAT_INFO,
	VIDEO_RV_INBAND_CMD_TYPE_SEGMENT_INFO,

	/*VC1 video In-band command*/
	VIDEO_VC1_INBAND_CMD_TYPE_SEQ_INFO,

	/* general video properties */
	VIDEO_INBAND_CMD_TYPE_VIDEO_USABILITY_INFO,
	VIDEO_INBAND_CMD_TYPE_VIDEO_MPEG4_USABILITY_INFO,

	/*MJPEG resolution In-band Command*/
	VIDEO_MJPEG_INBAND_CMD_TYPE_RESOLUTION,

	/* picture object for graphic */
	VIDEO_GRAPHIC_INBAND_CMD_TYPE_PICTURE_OBJECT,
	VIDEO_GRAPHIC_INBAND_CMD_TYPE_DISPLAY_INFO,

	/* subtitle offset sequence id for 3D video */
	VIDEO_DEC_INBAND_CMD_TYPE_SUBP_OFFSET_SEQUENCE_ID,

	VIDEO_H264_INBAND_CMD_TYPE_DPBBYPASS,

	/* Clear back frame to black color and send it to VO */
	VIDEO_FJPEG_INBAND_CMD_TYPE_CLEAR_SCREEN,

	/* each picture info of MJPEG */
	VIDEO_FJPEG_INBAND_CMD_TYPE_PIC_INFO,

	/*FJPEG resolution In-band Command*/
	VIDEO_FJPEG_INBAND_CMD_TYPE_RESOLUTION,

	/*VO receive VP_OBJ_PICTURE_TYPE In-band Command*/
	VIDEO_VO_INBAND_CMD_TYPE_OBJ_PIC,
	VIDEO_VO_INBAND_CMD_TYPE_OBJ_DVD_SP,
	VIDEO_VO_INBAND_CMD_TYPE_OBJ_DVB_SP,
	VIDEO_VO_INBAND_CMD_TYPE_OBJ_BD_SP,
	VIDEO_VO_INBAND_CMD_TYPE_OBJ_SP_FLUSH,
	VIDEO_VO_INBAND_CMD_TYPE_OBJ_SP_RESOLUTION,

	/* VO receive writeback buffers In-band Command */
	VIDEO_VO_INBAND_CMD_TYPE_WRITEBACK_BUFFER,

	/* for VO debug, VO can dump picture */
	VIDEO_VO_INBAND_CMD_TYPE_DUMP_PIC,
	VIDEO_CURSOR_INBAND_CMD_TYPE_PICTURE_OBJECT,
	VIDEO_CURSOR_INBAND_CMD_TYPE_COORDINATE_OBJECT,
	VIDEO_TRANSCODE_INBAND_CMD_TYPE_PICTURE_OBJECT,
	VIDEO_WRITEBACK_INBAND_CMD_TYPE_PICTURE_OBJECT,

	VIDEO_VO_INBAND_CMD_TYPE_OBJ_BD_SCALE_RGB_SP,

	/* TV code */
	VIDEO_INBAND_CMD_TYPE_DIVX_CERTIFY,

	/* M_DOMAIN resolution In-band Command */
	VIDEO_INBAND_CMD_TYPE_M_DOMAIN_RESOLUTION,

	/* DTV source In-band Command */
	VIDEO_INBAND_CMD_TYPE_SOURCE_DTV,

	/* Din source copy mode In-band Command */
	VIDEO_DIN_INBAND_CMD_TYPE_COPY_MODE,

	/* Video Decoder AU In-band command */
	VIDEO_DEC_INBAND_CMD_TYPE_AU,

	/* Video Decoder parse frame In-band command */
	VIDEO_DEC_INBAND_CMD_TYPE_PARSE_FRAME_IN,
	VIDEO_DEC_INBAND_CMD_TYPE_PARSE_FRAME_OUT,

	/* Set video decode mode In-band command */
	VIDEO_DEC_INBAND_CMD_TYPE_NEW_DECODE_MODE,

	/* Secure buffer protection */
	VIDEO_INBAND_CMD_TYPE_SECURE_PROTECTION,

	/* Dolby HDR inband command */
	VIDEO_DEC_INBAND_CMD_TYPE_DV_PROFILE,

	/* VP9 HDR10 In-band command */
	VIDEO_VP9_INBAND_CMD_TYPE_HDR10_METADATA,

	/* AV1 HDR10 In-band command */
	VIDEO_AV1_INBAND_CMD_TYPE_HDR10_METADATA,

	/* DvdPlayer tell RVSD video BS ring buffer is full */
	VIDEO_DEC_INBAND_CMD_TYPE_BS_RINGBUF_FULL,

	/* Frame Boundary In-band command */
	VIDEO_INBAND_CMD_TYPE_FRAME_BOUNDARY = 100,

	/* VO receive npp writeback buffers In-band Command */
	VIDEO_NPP_INBAND_CMD_TYPE_WRITEBACK_BUFFER,
	VIDEO_NPP_OUT_INBAND_CMD_TYPE_OBJ_PIC,

	/* hevc encoder raw yuv data In-band Commnad */
	VENC_INBAND_CMD_TYPE_RAWYUV,

	/* hevc encoder ref yuv addr In-band Commnad */
	VENC_INBAND_CMD_TYPE_REFYUV,

	/* add frame info for user allocate */
	VIDEO_FRAME_INBAND_ADD,

	/* delete frame info for user allocate */
	VIDEO_FRAME_INBAND_DELETE,

	/* Add for ConfigureDisplayWindow RPC*/
	VIDEO_VO_INBAND_CMD_TYPE_CONFIGUREDISPLAYWINDOW,
};

enum INBAND_CMD_GRAPHIC_FORMAT {
	INBAND_CMD_GRAPHIC_FORMAT_2BIT = 0,
	INBAND_CMD_GRAPHIC_FORMAT_4BIT = 1,
	INBAND_CMD_GRAPHIC_FORMAT_8BIT = 2,
	INBAND_CMD_GRAPHIC_FORMAT_RGB332 = 3,
	INBAND_CMD_GRAPHIC_FORMAT_RGB565 = 4,
	INBAND_CMD_GRAPHIC_FORMAT_ARGB1555 = 5,
	INBAND_CMD_GRAPHIC_FORMAT_ARGB4444 = 6,
	INBAND_CMD_GRAPHIC_FORMAT_ARGB8888 = 7,
	INBAND_CMD_GRAPHIC_FORMAT_YCBCRA4444 = 11,
	INBAND_CMD_GRAPHIC_FORMAT_YCBCRA8888 = 12,
	INBAND_CMD_GRAPHIC_FORMAT_RGBA5551 = 13,
	INBAND_CMD_GRAPHIC_FORMAT_RGBA4444 = 14,
	INBAND_CMD_GRAPHIC_FORMAT_RGBA8888 = 15,
	INBAND_CMD_GRAPHIC_FORMAT_420 = 16,
	INBAND_CMD_GRAPHIC_FORMAT_422 = 17,
	INBAND_CMD_GRAPHIC_FORMAT_RGB323 = 18,
	INBAND_CMD_GRAPHIC_FORMAT_RGB233 = 19,
	INBAND_CMD_GRAPHIC_FORMAT_RGB556 = 20,
	INBAND_CMD_GRAPHIC_FORMAT_RGB655 = 21,
	INBAND_CMD_GRAPHIC_FORMAT_RGB888 = 22,
	INBAND_CMD_GRAPHIC_FORMAT_RGB565_LITTLE  = 36,
	INBAND_CMD_GRAPHIC_FORMAT_ARGB1555_LITTLE = 37,
	INBAND_CMD_GRAPHIC_FORMAT_ARGB4444_LITTLE = 38,
	INBAND_CMD_GRAPHIC_FORMAT_ARGB8888_LITTLE = 39,
	INBAND_CMD_GRAPHIC_FORMAT_YCBCRA4444_LITTLE = 43,
	INBAND_CMD_GRAPHIC_FORMAT_YCBCRA8888_LITTLE = 44,
	INBAND_CMD_GRAPHIC_FORMAT_RGBA5551_LITTLE = 45,
	INBAND_CMD_GRAPHIC_FORMAT_RGBA4444_LITTLE = 46,
	INBAND_CMD_GRAPHIC_FORMAT_RGBA8888_LITTLE = 47,
	INBAND_CMD_GRAPHIC_FORMAT_RGB556_LITTLE = 52,
	INBAND_CMD_GRAPHIC_FORMAT_RGB655_LITTLE = 53,
	INBAND_CMD_GRAPHIC_FORMAT_RGB888_LITTLE = 54,
};

enum INBAND_CMD_GRAPHIC_RGB_ORDER {
	INBAND_CMD_GRAPHIC_COLOR_RGB = 0,
	INBAND_CMD_GRAPHIC_COLOR_BGR = 1,
	INBAND_CMD_GRAPHIC_COLOR_GRB = 2,
	INBAND_CMD_GRAPHIC_COLOR_GBR = 3,
	INBAND_CMD_GRAPHIC_COLOR_RBG = 4,
	INBAND_CMD_GRAPHIC_COLOR_BRG = 5,
};

enum INBAND_CMD_GRAPHIC_3D_MODE {
	INBAND_CMD_GRAPHIC_2D_MODE,
	INBAND_CMD_GRAPHIC_SIDE_BY_SIDE,
	INBAND_CMD_GRAPHIC_TOP_AND_BOTTOM,
	INBAND_CMD_GRAPHIC_FRAME_PACKING,
};

enum {
	INTERLEAVED_TOP_FIELD = 0,  /* top	field data stored in even lines of a frame buffer */
	INTERLEAVED_BOT_FIELD,	  /* bottom field data stored in odd  lines of a frame buffer */
	CONSECUTIVE_TOP_FIELD,	  /* top	field data stored consecutlively in all lines of a field buffer */
	CONSECUTIVE_BOT_FIELD,	  /* bottom field data stored consecutlively in all lines of a field buffer */
	CONSECUTIVE_FRAME,		   /* progressive frame data stored consecutlively in all lines of a frame buffer */
	INTERLEAVED_TOP_FIELD_422,  /* top	field data stored in even lines of a frame buffer */
	INTERLEAVED_BOT_FIELD_422,	  /* bottom field data stored in odd  lines of a frame buffer */
	CONSECUTIVE_TOP_FIELD_422,	  /* top	field data stored consecutlively in all lines of a field buffer */
	CONSECUTIVE_BOT_FIELD_422,	  /* bottom field data stored consecutlively in all lines of a field buffer */
	CONSECUTIVE_FRAME_422,		/* progressive frame with 4:2:2 chroma */
	TOP_BOTTOM_FRAME,			/* top field in the 0~height/2-1, bottom field in the height/2~height-1 in the frame */
	INTERLEAVED_TOP_BOT_FIELD,   /* one frame buffer contains one top and one bot field, top field first */
	INTERLEAVED_BOT_TOP_FIELD,   /* one frame buffer contains one bot and one top field, bot field first */

	MPEG2_PIC_MODE_NOT_PROG	  /*yllin: for MPEG2 check pic mode usage */
};

enum AVSYNC_MODE {
	AVSYNC_FORCED_SLAVE,
	AVSYNC_FORCED_MASTER,
	AVSYNC_AUTO_SLAVE,
	AVSYNC_AUTO_MASTER,
	AVSYNC_AUTO_MASTER_NO_SKIP,
	AVSYNC_AUTO_MASTER_CONSTANT_DELAY,
};

enum AUTOMASTER_STATE {
	AUTOMASTER_NOT_MASTER,
	AUTOMASTER_IS_MASTER,
};

enum VO_HDMI_MODE {
	VO_DVI_ON,
	VO_HDMI_ON,
	VO_HDMI_OFF,
};

enum VO_HDMI_AUDIO_SAMPLE_FREQ {
	VO_HDMI_AUDIO_NULL,
	VO_HDMI_AUDIO_32K,
	VO_HDMI_AUDIO_44_1K,
	VO_HDMI_AUDIO_48K,
	VO_HDMI_AUDIO_88_2K,
	VO_HDMI_AUDIO_96K,
	VO_HDMI_AUDIO_176_4K,
	VO_HDMI_AUDIO_192K,
};

enum VO_INTERFACE_TYPE {
	VO_ANALOG_AND_DIGITAL,
	VO_ANALOG_ONLY,
	VO_DIGITAL_ONLY,
	VO_DISPLAY_PORT_ONLY,
	VO_HDMI_AND_DISPLAY_PORT_SAME_SOURCE,
	VO_HDMI_AND_DISPLAY_PORT_DIFF_SOURCE,
	VO_DISPLAY_PORT_AND_CVBS_SAME_SOURCE,
	VO_HDMI_AND_DP_DIFF_SOURCE_WITH_CVBS,
	VO_FORCE_DP_OFF,
};

enum VO_PEDESTAL_TYPE {
	VO_PEDESTAL_TYPE_300_700_ON,
	VO_PEDESTAL_TYPE_300_700_OFF,
	VO_PEDESTAL_TYPE_286_714_ON,
	VO_PEDESTAL_TYPE_286_714_OFF,
};

enum VO_STANDARD {
	VO_STANDARD_NTSC_Mi,
	VO_STANDARD_NTSC_J,
	VO_STANDARD_NTSC_443,
	VO_STANDARD_PAL_B,
	VO_STANDARD_PAL_D,
	VO_STANDARD_PAL_G,
	VO_STANDARD_PAL_H,
	VO_STANDARD_PAL_I,
	VO_STANDARD_PAL_N,
	VO_STANDARD_PAL_NC,
	VO_STANDARD_PAL_M,
	VO_STANDARD_PAL_60,
	VO_STANDARD_SECAM,
	VO_STANDARD_HDTV_720P_60,
	VO_STANDARD_HDTV_720P_50,
	VO_STANDARD_HDTV_720P_30,
	VO_STANDARD_HDTV_720P_25,
	VO_STANDARD_HDTV_720P_24,
	VO_STANDARD_HDTV_1080I_60,
	VO_STANDARD_HDTV_1080I_50,
	VO_STANDARD_HDTV_1080P_30,
	VO_STANDARD_HDTV_1080P_25,
	VO_STANDARD_HDTV_1080P_24,
	VO_STANDARD_VGA,
	VO_STANDARD_SVGA,
	VO_STANDARD_HDTV_1080P_60,
	VO_STANDARD_HDTV_1080P_50,
	VO_STANDARD_HDTV_1080I_59,
	VO_STANDARD_HDTV_720P_59,
	VO_STANDARD_HDTV_1080P_23,
	VO_STANDARD_HDTV_1080P_59,
	VO_STANDARD_HDTV_1080P_60_3D,
	VO_STANDARD_HDTV_1080P_50_3D,
	VO_STANDARD_HDTV_1080P_30_3D,
	VO_STANDARD_HDTV_1080P_24_3D,
	VO_STANDARD_HDTV_720P_60_3D,
	VO_STANDARD_HDTV_720P_50_3D,
	VO_STANDARD_HDTV_720P_30_3D,
	VO_STANDARD_HDTV_720P_24_3D,
	VO_STANDARD_HDTV_720P_59_3D,
	VO_STANDARD_HDTV_1080I_60_3D,
	VO_STANDARD_HDTV_1080I_59_3D,
	VO_STANDARD_HDTV_1080I_50_3D,
	VO_STANDARD_HDTV_1080P_23_3D,
	VO_STANDARD_HDTV_2160P_30,
	VO_STANDARD_HDTV_2160P_29,
	VO_STANDARD_HDTV_2160P_25,
	VO_STANDARD_HDTV_2160P_24,
	VO_STANDARD_HDTV_2160P_23,
	VO_STANDARD_HDTV_4096_2160P_24,
	VO_STANDARD_HDTV_2160P_60,
	VO_STANDARD_HDTV_2160P_50,
	VO_STANDARD_HDTV_4096_2160P_25,
	VO_STANDARD_HDTV_4096_2160P_30,
	VO_STANDARD_HDTV_4096_2160P_50,
	VO_STANDARD_HDTV_4096_2160P_60,
	VO_STANDARD_HDTV_2160P_60_420,
	VO_STANDARD_HDTV_2160P_50_420,
	VO_STANDARD_HDTV_4096_2160P_60_420,
	VO_STANDARD_HDTV_4096_2160P_50_420,
	VO_STANDARD_DP_FORMAT_1920_1080P_60,
	VO_STANDARD_DP_FORMAT_2160P_30,
	VO_STANDARD_HDTV_2160P_24_3D,
	VO_STANDARD_HDTV_2160P_23_3D,
	VO_STANDARD_HDTV_2160P_59,
	VO_STANDARD_HDTV_2160P_59_420,
	VO_STANDARD_HDTV_2160P_25_3D,
	VO_STANDARD_HDTV_2160P_30_3D,
	VO_STANDARD_HDTV_2160P_50_3D,
	VO_STANDARD_HDTV_2160P_60_3D,
	VO_STANDARD_HDTV_4096_2160P_24_3D,
	VO_STANDARD_HDTV_4096_2160P_25_3D,
	VO_STANDARD_HDTV_4096_2160P_30_3D,
	VO_STANDARD_HDTV_4096_2160P_50_3D,
	VO_STANDARD_HDTV_4096_2160P_60_3D,
	VO_STANDARD_DP_FORMAT_1280_720P_60,
	VO_STANDARD_DP_FORMAT_3840_2160P_60,
	VO_STANDARD_DP_FORMAT_1024_768P_60,
	VO_STANDARD_HDTV_2160P_50_422_12bit,
	VO_STANDARD_HDTV_2160P_60_422_12bit,
	VO_STANDARD_DP_FORMAT_1280_800P_60,
	VO_STANDARD_DP_FORMAT_1440_900P_60,
	VO_STANDARD_DP_FORMAT_1440_768P_60,
	VO_STANDARD_DP_FORMAT_960_544P_60,
	VO_STANDARD_HDTV_720P_120_3D,
	VO_STANDARD_DP_FORMAT_800_480P_60,
	VO_STANDARD_HDTV_2160P_59_422_12bit,
	VO_STANDARD_DP_FORMAT_800_1280P_60,
	VO_STANDARD_DP_FORMAT_1280_720P_50,
	VO_STANDARD_HDTV_1080P_144,
	VO_STANDARD_HDTV_1024_768P_70,
	VO_STANDARD_DP_FORMAT_1024_600P_60,
	VO_STANDARD_DP_FORMAT_600_1024P_60,
	VO_STANDARD_DSIMIPI_FORMAT_1200_1920P_60,
	VO_STANDARD_HDTV_1080P_120,
	VO_STANDARD_HDTV_720P_P24,
	VO_STANDARD_HDTV_720P_P25,
	VO_STANDARD_HDTV_720P_P30,
	VO_STANDARD_HDTV_720P_P23,
	VO_STANDARD_HDTV_720P_P29,
	VO_STANDARD_DP_FORMAT_1366_768P_60,
	VO_STANDARD_HDTV_1080P_29,
	VO_STANDARD_HDTV_2560_1440P_60,
	VO_STANDARD_HDTV_2560_1080P_60,
	VO_STANDARD_HDTV_1920_720P_60,
	VO_STANDARD_ERROR,
};

enum VIDEO_ENUM_PRIVATEINFO
{
	ENUM_PRIVATEINFO_VIDEO_CHECK_SECURITY_ID = 0,
	ENUM_PRIVATEINFO_VIDEO_DISPLAY_RATIO     = 1,
	ENUM_PRIVATEINFO_VIDEO_DISPLAY_X_Y_W_H = 2,
	ENUM_PRIVATEINFO_VIDEO_CVBS_POWER_OFF = 3,
	ENUM_PRIVATEINFO_VIDEO_VO_CAPTURE = 4,
	ENUM_PRIVATEINFO_VIDEO_VDAC_POWER_OFF = 5,
	ENUM_PRIVATEINFO_HDMI_RANGE_SETTING = 6,
	ENUM_PRIVATEINFO_VO_DISPLAY_RATIO_TYPE = 7,
	ENUM_PRIVATEINFO_MIXDD_DISPLAY_RATIO = 8,
	ENUM_PRIVATEINFO_MIX1_DISPLAY_RATIO = 9,
	ENUM_PRIVATEINFO_MIX2_DISPLAY_RATIO =10,
	ENUM_PRIVATEINFO_MIX2_DISPLAY_XY =11,
	ENUM_PRIVATEINFO_MIXDD_DISPLAY_XY =12,
	ENUM_PRIVATEINFO_V2_DISPLAY_V1_FRAME =13,
	ENUM_PRIVATEINFO_FORCE_EMBEDDED_SUB_DISPLAY_RATIO_FIXED =14,
	ENUM_PRIVATEINFO_FORCE_VO_SUPER_RESOLUTION_DISABLE=15,
	ENUM_PRIVATEINFO_VO_GRAPHIC_FIR_SCALE=16,
	ENUM_PRIVATEINFO_VO_SHARPNESS=17,
	ENUM_PRIVATEINFO_VO_MNR=18,
	ENUM_PRIVATEINFO_VO_3DNR=19,
	ENUM_PRIVATEINFO_FORCE_DISABLE_HDR10PLUS=20,
	ENUM_PRIVATEINFO_FORCE_DISABLE_MAXCLL_MAXFALL=21,
	ENUM_PRIVATEINFO_HDMI_SERVICE_INIT=22,
	ENUM_PRIVATEINFO_DV_VIDEO_PRIORITY_MODE=23,
	ENUM_PRIVATEINFO_HDMI_SPDINFOFRAME_ENABLE = 24,
	ENUM_PRIVATEINFO_VO_OSD_ONLY_USE_GLOBAL_ALPHA_ENABLE = 25,
	ENUM_PRIVATEINFO_VO_CVBS_PLUG_DETECTION_ENABLE = 26,
	ENUM_PRIVATEINFO_VO_DIP_FUNCTION_ENABLE = 27,
	ENUM_PRIVATEINFO_VO_DECOUNTOUR_ENABLE = 28,
	ENUM_PRIVATEINFO_VIDEO_DISPLAY_X_Y_W_H_RATIO = 29,
	ENUM_PRIVATEINFO_VIDEO_SET_TCH_HDR_DAT_MODE = 30,
	ENUM_PRIVATEINFO_VIDEO_SET_CVBS_COLORBAR_ON = 31,
	ENUM_PRIVATEINFO_VIDEO_SET_UI_ALPHA_DIVIDER_ON = 32,
	ENUM_PRIVATEINFO_VIDEO_SET_DLSR_ENABLED = 33,
	ENUM_PRIVATEINFO_VIDEO_SET_DLSR_SCENE_DETECT_MODE = 34,
	ENUM_PRIVATEINFO_VIDEO_SET_HENR_ENABLED = 35,
	ENUM_PRIVATEINFO_VIDEO_SET_CVBS_FORMAT = 36,
	ENUM_PRIVATEINFO_VIDEO_GET_CVBS_FORMAT = 37,
	ENUM_PRIVATEINFO_VIDEO_GET_CVBS_STATUS = 38
};

enum VO_HDMI_OFF_MODE {
	VO_HDMI_OFF_CLOCK_OFF,
	VO_HDMI_OFF_CLOCK_ON,
};

enum VIDEO_LOW_DELAY {
	LOW_DELAY_OFF = 0,
	LOW_DELAY_DECODER = 1,
	LOW_DELAY_DISPLAY = 2,
	LOW_DELAY_DECODER_DISPLAY = 3,
	LOW_DELAY_AVSYNC = 4,
	LOW_DELAY_DECODER_AVSYNC = 5,
	LOW_DELAY_DISPLAY_ORDER = 6,
	VO_LOW_DELAY_ERROR = 7,
};

enum VO_DEINTFLAG
{
	AUTODEINT = 0,
	FORCEDEINT = 1,
	FORCEPROGRESSIVE = 2,
	VO_DEINTFLAG_ERROR
};

enum VO_3D_MODE_TYPE {
	VO_2D_MODE = 0,
	VO_3D_SIDE_BY_SIDE_HALF,
	VO_3D_TOP_AND_BOTTOM,
	VO_3D_FRAME_PACKING
};

enum VO_SDRFLAG
{
	AUTO_SDR_HDR = 0, /*Auto detect*/
	FORCE_HDR2SDR_ON = 1, /* In SDR TV, force enable HDR2SDR.*/
	FORCE_HDR2SDR_OFF = 2, /* In SDR TV, force disable HDR2SDR.*/
	FORCE_HDR_METADATA_OFF =3, /*In HDR TV, force disable HDR metadata.*/
	FORCE_SDR2HDR_OFF = 4, /*In HDR TV, force disable SDR2HDR, disable HDR */
	SDRFLAG_IGNORE = 255
};

/* For ENUM_PRIVATEINFO_HDMI_SPDINFOFRAME_ENABLE */
enum rtk_spd_ctrl {
	RTK_HDMI_SPD_DISABLE,
	RTK_HDMI_SPD_ENABLE
};

enum rtk_hdr_mode {
	/* Abandoned mode */
	HDR_CTRL_NA0 = 0,
	/* Dolby Vision RGB & DV SDR/HDR */
	HDR_CTRL_DV_ON = 1,
	/* HDR OFF */
	HDR_CTRL_SDR = 2,
	HDR_CTRL_HDR_GAMMA = 3,
	/* HDR10 */
	HDR_CTRL_HDR10 = 4,
	/* HLG */
	HDR_CTRL_HLG = 5,
	/* SDR/HDR by video */
	HDR_CTRL_INPUT = 6,
	/* Dolby Vision, low latency */
	HDR_CTRL_DV_LOW_LATENCY_12b_YUV422 = 7,
	HDR_CTRL_DV_LOW_LATENCY_10b_YUV444 = 8,
	HDR_CTRL_DV_LOW_LATENCY_10b_RGB444 = 9,
	HDR_CTRL_DV_LOW_LATENCY_12b_YUV444 = 10,
	HDR_CTRL_DV_LOW_LATENCY_12b_RGB444 = 11,
	/* Dolby Vision, RGB 8bit */
	HDR_CTRL_DV_ON_INPUT = 12,
	HDR_CTRL_DV_ON_LOW_LATENCY_12b422_INPUT = 13,
	/* INPUT_SDR2020 mode */
	HDR_CTRL_INPUT_BT2020 = 14,
	/* Output SDR/HDR10 via DV HW */
	HDR_CTRL_DV_ON_HDR10_VS10 = 15,
	HDR_CTRL_DV_ON_SDR_VS10 = 16,
};

enum dc_buffer_id {
	eFrameBuffer = 0x1U << 0,
	eIONBuffer = 0x1U << 1,
	eUserBuffer = 0x1U << 2,
	eFrameBufferTarget = 0x1U << 3,
	eFrameBufferPartial = 0x1U << 4,
	eFrameBufferSkip = 0x1U << 5,
};

enum dc_overlay_engine {
	eEngine_VO = 0x1U << 0,
	eEngine_SE = 0x1U << 1,
	eEngine_DMA = 0x1U << 2,
	eEngine_MAX = 0x1U << 3,
};

enum dc_buffer_flags {
	eBuffer_AFBC_Enable = 0x1U << 16,
	eBuffer_AFBC_Split = 0x1U << 17,
	eBuffer_AFBC_YUV_Transform = 0x1U << 18,
	eBuffer_USE_GLOBAL_ALPHA = 0x1U << 19,
};

enum ENUM_VIDEO_KEEP_CUR_SVP_TYPE
{
	ENUM_VIDEO_KEEP_CUR_SVP_TYPE_GET_CUR = 0,
	ENUM_VIDEO_KEEP_CUR_SVP_TYPE_SET_CUR = 1,
	ENUM_VIDEO_KEEP_CUR_SVP_TYPE_GET_FW_MALLOC_SVP_BUFFER =2
};

struct inband_cmd_pkg_header {
	enum INBAND_CMD_TYPE type;
	unsigned int size;
};

struct graphic_object {
	struct inband_cmd_pkg_header header;
	enum INBAND_CMD_GRAPHIC_FORMAT format;
	unsigned int PTSH;
	unsigned int PTSL;
	unsigned int context;
	int colorkey;
	int alpha;
	unsigned int x;
	unsigned int y;
	unsigned int width;
	unsigned int height;
	unsigned int address;
	unsigned int pitch;
	unsigned int address_right;
	unsigned int pitch_right;
	enum INBAND_CMD_GRAPHIC_3D_MODE picLayout;
	unsigned int afbc;
	unsigned int afbc_block_split;
	unsigned int afbc_yuv_transform;
};

struct rpc_create_video_agent {
	unsigned int instance;
	unsigned int result;
	unsigned int data;
	unsigned int ret;
};

struct rpc_vo_filter_display {
	unsigned int instance;
	enum VO_VIDEO_PLANE videoPlane;
	unsigned char zeroBuffer;
	unsigned char realTimeSrc;
};

struct rpc_vo_filter_display_t {
	unsigned int instance;
	enum VO_VIDEO_PLANE videoPlane;
	unsigned char zeroBuffer;
	unsigned char realTimeSrc;
	unsigned int result;
	unsigned int data;
	unsigned int ret;
};

struct VO_SIZE {
	unsigned short w;
	unsigned short h;
};

struct vo_rectangle {
	short x;
	short y;
	unsigned short width;
	unsigned short height;
};

struct vo_color {
	unsigned short c1;
	unsigned short c2;
	unsigned short c3;
	unsigned short isRGB;
};

enum VO_OSD_COLOR_FORMAT {
	VO_OSD_COLOR_FORMAT_2BIT = 0,
	VO_OSD_COLOR_FORMAT_4BIT = 1,
	VO_OSD_COLOR_FORMAT_8BIT = 2,
	VO_OSD_COLOR_FORMAT_RGB332 = 3,
	VO_OSD_COLOR_FORMAT_RGB565 = 4,
	VO_OSD_COLOR_FORMAT_ARGB1555 = 5,
	VO_OSD_COLOR_FORMAT_ARGB4444 = 6,
	VO_OSD_COLOR_FORMAT_ARGB8888 = 7,
	VO_OSD_COLOR_FORMAT_Reserved0 = 8,
	VO_OSD_COLOR_FORMAT_Reserved1 = 9,
	VO_OSD_COLOR_FORMAT_Reserved2 = 10,
	VO_OSD_COLOR_FORMAT_YCBCRA4444 = 11,
	VO_OSD_COLOR_FORMAT_YCBCRA8888 = 12,
	VO_OSD_COLOR_FORMAT_RGBA5551 = 13,
	VO_OSD_COLOR_FORMAT_RGBA4444 = 14,
	VO_OSD_COLOR_FORMAT_RGBA8888 = 15,
	VO_OSD_COLOR_FORMAT_420 = 16,
	VO_OSD_COLOR_FORMAT_422 = 17,
	VO_OSD_COLOR_FORMAT_RGB323 = 18,
	VO_OSD_COLOR_FORMAT_RGB233 = 19,
	VO_OSD_COLOR_FORMAT_RGB556 = 20,
	VO_OSD_COLOR_FORMAT_RGB655 = 21,
	VO_OSD_COLOR_FORMAT_RGB888 = 22,
	VO_OSD_COLOR_FORMAT_RGB565_LITTLE = 36,
	VO_OSD_COLOR_FORMAT_ARGB1555_LITTLE = 37,
	VO_OSD_COLOR_FORMAT_ARGB4444_LITTLE = 38,
	VO_OSD_COLOR_FORMAT_ARGB8888_LITTLE = 39,
	VO_OSD_COLOR_FORMAT_YCBCRA4444_LITTLE = 43,
	VO_OSD_COLOR_FORMAT_YCBCRA8888_LITTLE = 44,
	VO_OSD_COLOR_FORMAT_RGBA5551_LITTLE = 45,
	VO_OSD_COLOR_FORMAT_RGBA4444_LITTLE = 46,
	VO_OSD_COLOR_FORMAT_RGBA8888_LITTLE = 47,
	VO_OSD_COLOR_FORMAT_RGB556_LITTLE = 52,
	VO_OSD_COLOR_FORMAT_RGB655_LITTLE = 53,
	VO_OSD_COLOR_FORMAT_RGB888_LITTLE = 54,
};

enum VO_GRAPHIC_STORAGE_MODE {
	VO_GRAPHIC_BLOCK = 0,
	VO_GRAPHIC_SEQUENTIAL = 1,
};

enum VO_OSD_RGB_ORDER
{
	VO_OSD_COLOR_RGB,
	VO_OSD_COLOR_BGR,
	VO_OSD_COLOR_GRB,
	VO_OSD_COLOR_GBR,
	VO_OSD_COLOR_RBG,
	VO_OSD_COLOR_BRG
};

struct inband_vo_rectangle {
	int x;
	int y;
	unsigned int width;
	unsigned int height;
};

struct inband_vo_color {
	unsigned char c1;
	unsigned char c2;
	unsigned char c3;
	unsigned char isRGB;
};

struct inband_config_disp_win {
	struct inband_cmd_pkg_header header;
	unsigned int videoPlane;
	struct inband_vo_rectangle videoWin;
	struct inband_vo_rectangle borderWin;
	struct inband_vo_color borderColor;
	unsigned char enBorder;
};

struct rpc_config_disp_win {
	enum VO_VIDEO_PLANE videoPlane;
	struct vo_rectangle videoWin;
	struct vo_rectangle borderWin;
	struct vo_color borderColor;
	unsigned char enBorder;
};

struct rpc_config_disp_win_t {
	enum VO_VIDEO_PLANE videoPlane;
	struct vo_rectangle videoWin;
	struct vo_rectangle borderWin;
	struct vo_color borderColor;
	unsigned char enBorder;
	unsigned int result;
	unsigned int data;
	unsigned int ret;
};

struct rpc_query_disp_win_in {
	enum VO_VIDEO_PLANE plane;
};

struct rpc_query_disp_win_out {
	short result;
	enum VO_VIDEO_PLANE plane;
	unsigned short numWin;
	unsigned short zOrder;
	struct vo_rectangle configWin;
	struct vo_rectangle contentWin;
	short deintMode;
	unsigned short pitch;
	enum INBAND_CMD_GRAPHIC_FORMAT colorType;
	enum INBAND_CMD_GRAPHIC_RGB_ORDER RGBOrder;
	enum INBAND_CMD_GRAPHIC_3D_MODE format3D;
	struct VO_SIZE mix1_size;
	enum VO_STANDARD standard;
	unsigned char enProg;
	unsigned char reserved1;
	unsigned short reserved2;
	struct VO_SIZE mix2_size;
};

/* Setting sub plane size */
struct rpc_config_graphic_canvas {
	enum VO_GRAPHIC_PLANE plane;
	struct vo_rectangle srcWin;
	struct vo_rectangle dispWin;
	unsigned char go;
};

struct rpc_config_graphic_canvas_t{
	enum VO_GRAPHIC_PLANE plane;
	struct vo_rectangle srcWin;
	struct vo_rectangle dispWin;
	unsigned char go;
	unsigned int result;
	unsigned int data;
	unsigned int ret;
};

struct rpc_refclock {
	int instance;
	int pRefClock;
};

struct rpc_refclock_t {
	int instance;
	int pRefClock;
	unsigned int result;
	unsigned int data;
	unsigned int ret;
};

struct rpc_ringbuffer {
	unsigned int instance;
	unsigned int pinID;
	unsigned int readPtrIndex;
	unsigned int pRINGBUFF_HEADER;
};

struct rpc_ringbuffer_t {
	unsigned int result;
	unsigned int data;
	unsigned int ret;
	unsigned int instance;
	unsigned int pinID;
	unsigned int readPtrIndex;
	unsigned int pRINGBUFF_HEADER;
};

struct rpc_video_run_t {
	unsigned int instance;
	unsigned int result;
	unsigned int data;
	unsigned int ret;
};

struct rpc_config_video_standard {
	enum VO_STANDARD standard;
	unsigned char enProg;
	unsigned char enDIF;
	unsigned char enCompRGB;
	enum VO_PEDESTAL_TYPE pedType;
	unsigned int dataInt0;
	unsigned int dataInt1;
};

#define RPC_BPC_MASK  0x3E
#define RPC_BPC_8     0x10
#define RPC_BPC_10    0x16
#define RPC_BPC_12    0x1A

/* hdmi2px_feature */
#define HDMI2PX_2P0_MASK      0x1
#define HDMI2PX_2P0           0x1
#define HDMI2PX_SCRAMBLE_MASK 0x2
#define HDMI2PX_SCRAMBLE      0x2
#define HDMI2PX_FRLRATE_MASK  0x3C
#define HDMI2PX_FRL_3G3L      0x4
#define HDMI2PX_FRL_6G3L      0x8
#define HDMI2PX_FRL_6G4L      0xC

enum rtk_display_interface {
	DISPLAY_INTERFACE_DP,
	DISPLAY_INTERFACE_eDP,
	DISPLAY_INTERFACE_MIPI,
	DISPLAY_INTERFACE_LVDS1,
	DISPLAY_INTERFACE_LVDS2,
	DISPLAY_INTERFACE_CVBS
};

enum rtk_display_interface_mixer {
	DISPLAY_INTERFACE_MIXER1        = 0,
	DISPLAY_INTERFACE_MIXER2        = 1,
	DISPLAY_INTERFACE_MIXER3        = 2,
	DISPLAY_INTERFACE_MIXER_NONE    = 3,
	DISPLAY_INTERFACE_MIXER_INVALID = 4,
};

struct rpc_set_display_out_interface
{
	unsigned int cmd_version;
	enum rtk_display_interface display_interface;
	unsigned int width;
	unsigned int height;
	unsigned int frame_rate;
	enum rtk_hdr_mode hdr_mode;
	enum rtk_display_interface_mixer display_interface_mixer;
	unsigned int reserved[20];
};

struct rpc_config_info_frame {
	enum VO_HDMI_MODE hdmiMode;
	enum VO_HDMI_AUDIO_SAMPLE_FREQ audioSampleFreq;
	unsigned char audioChannelCount;
	unsigned char dataByte1;
	unsigned char dataByte2;
	unsigned char dataByte3;
	unsigned char dataByte4;
	unsigned char dataByte5;
	/* dataInt0 [Bit5:2] 4-24bits 5-30bit 6-36bits [Bit1] deep color */
	unsigned int dataInt0;
	/* hdmi2px_feature [Bit5:2] FRL Rate [Bit1]Scramble [Bit0]HDMI 2.x */
	unsigned int hdmi2px_feature;
	enum VO_HDMI_OFF_MODE hdmi_off_mode;
	enum rtk_hdr_mode hdr_ctrl_mode;
	unsigned int reserved4;
};

struct rpc_config_tv_system {
	enum VO_INTERFACE_TYPE interfaceType;
	struct rpc_config_video_standard videoInfo;
	struct rpc_config_info_frame info_frame;
};

enum rtk_display_mode {
	DISPLAY_MODE_OFF,
	DISPLAY_MODE_DVI,
	DISPLAY_MODE_HDMI,
	DISPLAY_MODE_FRL,
};

struct rpc_set_q_param {
	unsigned int depth;
	unsigned int init_frame;
	unsigned int jitter;
};

struct rpc_config_channel_lowdelay {
	enum VIDEO_LOW_DELAY mode;
	union {
		unsigned int plane_id;
		unsigned int instanceId;
	};
	unsigned int reserved[6];
};

struct rpc_privateinfo_param {
	union {
		unsigned int plane_id;
		unsigned int instanceId;
	};
	enum VIDEO_ENUM_PRIVATEINFO type;
	unsigned int privateInfo[16];
};

struct rpc_privateinfo_returnval {
	unsigned int  instanceID;
	unsigned int  privateInfo[16];
};

struct rpc_query_disp_win_out_new {
	short result;
	union {
		unsigned int plane_id;
		enum VO_VIDEO_PLANE plane;
	};
	unsigned short numWin;
	unsigned short zOrder;
	struct vo_rectangle configWin;
	struct vo_rectangle contentWin;
	short deintMode;
	unsigned short pitch;
	enum INBAND_CMD_GRAPHIC_FORMAT colorType;
	enum INBAND_CMD_GRAPHIC_RGB_ORDER RGBOrder;
	enum INBAND_CMD_GRAPHIC_3D_MODE format3D;
	struct VO_SIZE mix1_size;
	enum VO_STANDARD standard;
	unsigned char enProg;
	unsigned char cvbs_off;
	unsigned short reserved2;
	struct vo_rectangle srcZoomWin;
	struct VO_SIZE mix2_size;
	struct VO_SIZE mixdd_size;
	unsigned int wb_usedFormat;
	unsigned int channel_total_drop_rpc;
	unsigned int channel_total_drop_rpc_anycase;
	long reserved3[5];
};

struct rpc_set_speed {
	union {
		unsigned int plane_id;
		unsigned int instanceId;
	};
	unsigned int speed;
};

struct rpc_set_background
{
	struct vo_color bgColor;
	unsigned char bgEnable;
};

struct rpc_keep_curpic
{
	union {
		unsigned int plane_id;
		enum VO_VIDEO_PLANE plane;
	};
	unsigned int ptr;
	unsigned int lock;
	unsigned int offsetTable_yaddr;
	unsigned int offsetTable_caddr;
	unsigned int offsetTable_ysize;
	unsigned int offsetTable_csize;
	unsigned int is_destroy;
	unsigned int reserved[7];
};

struct rpc_keep_curpic_svp
{
	unsigned int result_ok;
	union {
		unsigned int plane_id;
		enum VO_VIDEO_PLANE plane;
	};
	enum ENUM_VIDEO_KEEP_CUR_SVP_TYPE type;
	unsigned int lock;
	unsigned int Yaddr;
	unsigned int Caddr;
	unsigned int offsetTable_yaddr;
	unsigned int offsetTable_caddr;
	unsigned int Ysize;
	unsigned int Csize;
	unsigned int is_destroy;
	unsigned int offsetTable_ysize;
	unsigned int offsetTable_csize;
	unsigned int reserved[3];
};

struct rpc_set_deintflag {
	enum VO_DEINTFLAG flag;
};

struct rpc_create_graphic_win
{
	enum VO_GRAPHIC_PLANE plane;
	struct vo_rectangle winPos;
	enum VO_OSD_COLOR_FORMAT colorFmt;
	enum VO_OSD_RGB_ORDER rgbOrder;
	int colorKey;
	unsigned char alpha;
	unsigned char reserved;
};

struct rpc_draw_graphic_win
{
	enum VO_GRAPHIC_PLANE plane;
	unsigned short winID;
	enum VO_GRAPHIC_STORAGE_MODE storageMode;
	unsigned char paletteIndex;
	unsigned char compressed;
	unsigned char interlace_Frame;
	unsigned char bottomField;
	unsigned short startX[4];
	unsigned short startY[4];
	unsigned short imgPitch[4];
	long pImage[4];
	unsigned char reserved;
	unsigned char go;
};

struct rpc_modify_graphic_win
{
	enum VO_GRAPHIC_PLANE plane;
	unsigned char winID;
	unsigned char reqMask;
	struct vo_rectangle winPos;
	enum VO_OSD_COLOR_FORMAT colorFmt;
	enum VO_OSD_RGB_ORDER rgbOrder;
	int colorKey;
	unsigned char alpha;
	enum VO_GRAPHIC_STORAGE_MODE storageMode;
	unsigned char paletteIndex;
	unsigned char compressed;
	unsigned char interlace_Frame;
	unsigned char bottomField;
	unsigned short startX[4];
	unsigned short startY[4];
	unsigned short imgPitch[4];
	long pImage[4];
	unsigned char reserved;
	unsigned char go;
};

struct rpc_delete_graphic_win
{
	enum VO_GRAPHIC_PLANE plane;
	unsigned short winID;
	unsigned char go;
};

struct rpc_config_osd_palette
{
	unsigned char paletteIndex;
	long pPalette;
};

struct PLANE_MIXER_WIN
{
	enum VO_VIDEO_PLANE winID;
	short opacity;
	short alpha;
};

struct rpc_config_plane_mixer
{
	union {
		unsigned int plane_id;
		unsigned int instanceId;
	};
	enum VO_VIDEO_PLANE targetPlane;
	enum VO_VIDEO_PLANE mixOrder[8];
	struct PLANE_MIXER_WIN win[8];
	unsigned int dataIn0;
	unsigned int dataIn1;
	unsigned int dataIn2;
};

enum rtk_hdmi_colorspace {
	COLORSPACE_RGB,
	COLORSPACE_YUV422,
	COLORSPACE_YUV444,
	COLORSPACE_YUV420
};

struct rtk_infoframe_packet {
	u8 type;
	u8 version;
	u8 length;
	u8 checksum;
	u8 date_byte[28];
};

enum rtk_3d_mode {
	HDMI_2D,
	HDMI_3D_SIDE_BY_SIDE_HALF,
	HDMI_3D_TOP_AND_BOTTOM,
	HDMI_3D_FRAME_PACKING,
};

struct rpc_display_output_format {
	unsigned int cmd_version;
	enum rtk_display_mode display_mode;
	unsigned int vic;
	unsigned int clock; /* in kHz */
	unsigned int is_fractional_fps;
	enum rtk_hdmi_colorspace colorspace;
	unsigned int color_depth; /* 8 or 10 or 12 */
	unsigned int tmds_config;
	enum rtk_hdr_mode hdr_mode;
	struct rtk_infoframe_packet avi_infoframe;
	enum rtk_3d_mode src_3d_fmt;
	enum rtk_3d_mode dst_3d_fmt;
	unsigned int en_dithering; /* Color dithering */
	unsigned int reserved[44];
};

struct rpc_set_sdrflag
{
	enum VO_SDRFLAG flag;
	int VideoSdrToHdrNits;
	int VideoSaturation;
	int VideoHDRtoSDRgma;
};

// struct VIDEO_RPC_VOUT_SET_MIXER_ORDER
struct rpc_disp_mixer_order {
	unsigned char pic ;
	unsigned char dd ;
	unsigned char v1 ;
	unsigned char sub1 ;
	unsigned char v2 ;
	unsigned char osd1 ;
	unsigned char osd2 ;
	unsigned char csr ;
	unsigned char sub2 ;
	unsigned char v3;
	unsigned char v4;
	unsigned char osd3;
	unsigned char osd4;
	unsigned char reserved[3] ;
} ;

/**
 * struct rpc_vout_edid_raw_data -
 *   Parameter of RPC ENUM_KERNEL_RPC_HDMI_EDID_RAW_DATA
 *
 * @address: physical address of edid data
 * @size: data size in bytes
 */
struct rpc_vout_edid_raw_data {
	u_int paddr;
	u_int size;
	u_int reserved[6];
};

enum ENUM_HDMI_VRR_FUNC_TYPE {
	HDMI_VRR_ON_OFF = 0,
	HDMI_VRR_TARGET_RATE = 1,
	HDMI_ALLM_ON_OFF = 2,
};

enum ENUM_HDMI_VRR_ACT {
	/* When vrr_function = HDMI_VRR_ON_OFF */
	HDMI_VRR_DISABLE = 0,
	HDMI_VRR_ENABLE = 1,
	HDMI_QMS_ENABLE = 2,
	/* When vrr_function = HDMI_VRR_TARGET_RATE */
	HDMI_VRR_60HZ = 0,
	HDMI_VRR_50HZ = 1,
	HDMI_VRR_48HZ = 2,
	HDMI_VRR_24HZ = 3,
	HDMI_VRR_59HZ = 4,
	HDMI_VRR_47HZ = 5,
	HDMI_VRR_30HZ = 6,
	HDMI_VRR_29HZ = 7,
	HDMI_VRR_25HZ = 8,
	HDMI_VRR_23HZ = 9,
	/* When vrr_function = HDMI_ALLM_ON_OFF */
	HDMI_ALLM_DISABLE = 0,
	HDMI_ALLM_ENABLE = 1,
};

/**
 * struct rpc_vout_hdmi_vrr -
 *   Parameter of RPC ENUM_VIDEO_KERNEL_RPC_SET_HDMI_VRR
 *
 * @vrr_function: enum ENUM_HDMI_VRR_FUNC_TYPE
 * @vrr_act: enum ENUM_HDMI_VRR_ACT
 */
struct rpc_vout_hdmi_vrr {
	enum ENUM_HDMI_VRR_FUNC_TYPE vrr_function;
	enum ENUM_HDMI_VRR_ACT vrr_act;
	int reserved[15];
};

/**
 * struct audio_sad -Short Audio Descriptors
 * @byte0: Bit[6:3] Audio Format Code, Bit[2:0] Max Number of channels -1
 * @byte1: Sampling rates
 * @byte2: depends on format
 */
struct audio_sad {
	u8 byte0;
	u8 byte1;
	u8 byte2;
} __packed;

#define MAX_SAD_SIZE  10
struct audio_edid_data {
	u8 data_size;
	struct audio_sad sad[MAX_SAD_SIZE];
} __packed;

#define AUDIO_CTRL_VERSION 3
struct rpc_audio_ctrl_data {
	uint32_t version;
	uint32_t hdmi_en_state;
	uint32_t edid_data_addr; /* abandoned */
};

/**
 * struct rpc_audio_hdmi_freq - hdmi frequency for AO
 * @hdmi_frequency: [Bit23:Bit16] FRL Rate [Bit15:Bit0] TMDS freq(MHz)
 */
#define AUDIO_FRL_RATE1 0x00010000
#define AUDIO_FRL_RATE2 0x00020000
#define AUDIO_FRL_RATE3 0x00030000
struct rpc_audio_hdmi_freq {
	uint32_t tmds_freq;
};

/**
 * struct rpc_audio_mute_info - ENUM_KERNEL_RPC_HDMI_MUTE arg
 * @instanceID: 3
 * @hdmi_mute: 1 -mute, 0 - unmute
 */
struct rpc_audio_mute_info {
	uint32_t instanceID;
	char hdmi_mute;
};

#ifdef CONFIG_RTK_CACHEABLE_HEADER

/* Ring Buffer header structure (cacheable) */
struct tag_ringbuffer_header {
	/* align 128 bytes */
	unsigned int writePtr;
	unsigned char w_reserved[124];

	/* align 128 bytes */
	unsigned int readPtr[4];
	unsigned char  r_reserved[112];

	unsigned int magic;
	unsigned int beginAddr;
	unsigned int size;
	unsigned int bufferID;
	unsigned int numOfReadPtr;
	unsigned int reserve2;
	unsigned int reserve3;

	int          fileOffset;
	int          requestedFileOffset;
	int          fileSize;
	int          bSeekable;

	unsigned char  readonly[84];
};
#else
/* Ring Buffer header is the shared memory structure */
struct tag_ringbuffer_header {
	unsigned int magic;	//Magic number
	unsigned int beginAddr;
	unsigned int size;
	unsigned int bufferID;	// RINGBUFFER_TYPE, choose a type from RINGBUFFER_TYPE

	unsigned int writePtr;
	unsigned int numOfReadPtr;
	unsigned int reserve2;	//Reserve for Red Zone
	unsigned int reserve3;	//Reserve for Red Zone

	unsigned int readPtr[4];

	int fileOffset;
	int requestedFileOffset;
	int fileSize;

	int bSeekable;		//Can't be sought if data is streamed by HTTP
};
#endif

struct tag_master_ship {
	unsigned char systemMode;	/* enum AVSYNC_MODE */
	unsigned char videoMode;	/* enum AVSYNC_MODE */
	unsigned char audioMode;	/* enum AVSYNC_MODE */
	unsigned char masterState;	/* enum AUTOMASTER_STATE */
};

#ifdef CONFIG_RTK_CACHEABLE_HEADER

struct MASTERSHIP {
    unsigned char systemMode;  /* enum AVSYNC_MODE */
    unsigned char videoMode;   /* enum AVSYNC_MODE */
    unsigned char audioMode;   /* enum AVSYNC_MODE */
    unsigned char masterState; /* enum AUTOMASTER_STATE */
};

struct tag_refclock {
    union
    {
        struct
        {
            volatile int64_t   GPTSTimeout;
            volatile int64_t   videoSystemPTS;
            volatile int64_t   videoRPTS;
            volatile uint32_t  videoContext;
            volatile uint32_t  videoFreeRunThreshold;
            volatile uint32_t  audioFreeRunThreshold;
            volatile int32_t   audioPauseFlag;
            volatile int32_t   VO_Underflow;
            volatile int32_t   videoEndOfSegment;
            volatile int64_t   vsyncPTS;
            volatile uint8_t   reserved[8];
        };
        char read_only[128];
    };
    union
    {
        struct
        {
            volatile int64_t   RCD;
            volatile uint32_t  RCD_ext;
            volatile int64_t   masterGPTS;
        };
        char sync_with_VO[128];
    };
    union
    {
        struct
        {
            volatile int64_t   audioSystemPTS;
            volatile int64_t   audioRPTS;
            volatile uint32_t  audioContext;
            volatile int32_t   audioFullness;
            volatile int32_t   AO_Underflow;
            volatile int32_t   audioEndOfSegment;
        };
        char sync_with_ARM[128];
    };
    union
    {
        struct MASTERSHIP         mastership;
        char mastership_sync_with_ARM[128];
    };
};
#else
struct tag_refclock {
	long long RCD;
	unsigned int RCD_ext;
	long long GPTSTimeout;
	long long videoSystemPTS;
	long long audioSystemPTS;
	long long videoRPTS;
	long long audioRPTS;
	unsigned int videoContext;
	unsigned int audioContext;

	struct tag_master_ship mastership;
	unsigned int videoFreeRunThreshold;
	unsigned int audioFreeRunThreshold;
	long long masterGPTS;	// this is the value of GPTS (hardware PTS) when master set the reference clock
	int audioFullness;	// This flag will be turned on when AE's output buffer is almost full.
				// VE which is monitoring this flag will issue auto-pause then.
				// (0: still have enough output space for encoding.   1: AE request pause)
	int audioPauseFlag;	// This flag will be turned on when VE decides to auto-pause.
				// AE which is monitoring this flag will auto-pause itself then.
				// (0: ignore.  1: AE have to pauseEncode immediately)
	int VO_Underflow;	// (0: VO is working properly; otherwise, VO underflow)
	int AO_Underflow;	// (0: AO is working properly; otherwise, AO underflow)
	int videoEndOfSegment;	// set to the flow EOS.eventID by VO after presenting the EOS sample
	int audioEndOfSegment;	// set to the flow EOS.eventID by AO after presenting the EOS sample
#ifdef DC2VO_SUPPORT_MEMORY_TRASH
	unsigned int memorytrashAddr;
	unsigned int memorytrashContext;
	unsigned char reserved[8];
#else /* DC2VO_SUPPORT_MEMORY_TRASH */
	unsigned char  reserved[16];
#endif /* End of DC2VO_SUPPORT_MEMORY_TRASH */
};
#endif

struct tch_metadata_variables {
	int tmInputSignalBlackLevelOffset;
	int tmInputSignalWhiteLevelOffset;
	int shadowGain;
	int highlightGain;
	int midToneWidthAdjFactor;
	int tmOutputFineTuningNumVal;
	int tmOutputFineTuningX[15];
	int tmOutputFineTuningY[15];
	int saturationGainNumVal;
	int saturationGainX[15];
	int saturationGainY[15];
};

struct tch_metadata_tables {
	int luminanceMappingNumVal;
	int luminanceMappingX[33];
	int luminanceMappingY[33];
	int colourCorrectionNumVal;
	int colourCorrectionX[33];
	int colourCorrectionY[33];
	int chromaToLumaInjectionMuA;
	int chromaToLumaInjectionMuB;
};

struct tch_metadata {
	int specVersion;
	int payloadMode;
	int hdrPicColourSpace;
	int hdrMasterDisplayColourSpace;
	int hdrMasterDisplayMaxLuminance;
	int hdrMasterDisplayMinLuminance;
	int sdrPicColourSpace;
	int sdrMasterDisplayColourSpace;
	union {
		struct tch_metadata_variables variables;
		struct tch_metadata_tables tables;
	} u;
};

struct video_object {
	struct inband_cmd_pkg_header header;
	unsigned int version;
	unsigned int mode;
	unsigned int Y_addr;
	unsigned int U_addr;
	unsigned int pLock;
	unsigned int width;
	unsigned int height;
	unsigned int Y_pitch;
	unsigned int C_pitch;
	unsigned int RPTSH;
	unsigned int RPTSL;
	unsigned int PTSH;
	unsigned int PTSL;

	/* for send two interlaced fields in the same packet, */
	/* valid only when mode is INTERLEAVED_TOP_BOT_FIELD or INTERLEAVED_BOT_TOP_FIELD */
	unsigned int RPTSH2;
	unsigned int RPTSL2;
	unsigned int PTSH2;
	unsigned int PTSL2;

	unsigned int context;
	unsigned int pRefClock;		/* not used now */

	unsigned int pixelAR_hor;	/* pixel aspect ratio hor, not used now */
	unsigned int pixelAR_ver;	/* pixel aspect ratio ver, not used now */

	unsigned int Y_addr_Right;	/* for mvc */
	unsigned int U_addr_Right;	/* for mvc */
	unsigned int pLock_Right;	/* for mvc */
	unsigned int mvc;		/* 1: mvc */
	unsigned int subPicOffset;	/* 3D Blu-ray dependent-view sub-picture plane offset metadata as defined in BD spec sec. 9.3.3.6. */
					/* Valid only when Y_BufId_Right and C_BufId_Right are both valid */
	unsigned int pReceived;		// fix bug 44329 by version 0x72746B30 'rtk0'
	unsigned int pReceived_Right;	// fix bug 44329 by version 0x72746B30 'rtk0'

	unsigned int fps;		// 'rtk1'

	unsigned int IsForceDIBobMode;	// force vo use bob mode to do deinterlace, 'rtk2'.
	unsigned int lumaOffTblAddr;	// 'rtk3'
	unsigned int chromaOffTblAddr;	// 'rtk3'
	unsigned int lumaOffTblAddrR;	// for mvc, 'rtk3'
	unsigned int chromaOffTblAddrR;	// for mvc, 'rtk3'

	unsigned int bufBitDepth;	// 'rtk3'
	unsigned int bufFormat;		// 'rtk3', according to VO spec: 10bits Pixel Packing mode selection,
					// "0": use 2 bytes to store 1 components. MSB justified.
					// "1": use 4 bytes to store 3 components. LSB justified.

	// VUI (Video Usability Information)
	unsigned int transferCharacteristics;	// 0:SDR, 1:HDR, 2:ST2084, 'rtk3'
	// Mastering display colour volume SEI, 'rtk3'
	unsigned int display_primaries_x0;
	unsigned int display_primaries_y0;
	unsigned int display_primaries_x1;
	unsigned int display_primaries_y1;
	unsigned int display_primaries_x2;
	unsigned int display_primaries_y2;
	unsigned int white_point_x;
	unsigned int white_point_y;
	unsigned int max_display_mastering_luminance;
	unsigned int min_display_mastering_luminance;

	// for transcode interlaced feild use.	//'rtk4'
	unsigned int Y_addr_prev;		//'rtk4'
	unsigned int U_addr_prev;		//'rtk4'
	unsigned int Y_addr_next;		//'rtk4'
	unsigned int U_addr_next;		//'rtk4'
	unsigned int video_full_range_flag;	//'rtk4' default= 1
	unsigned int matrix_coefficients;	//'rtk4' default= 1

	// for transcode interlaced feild use.	//'rtk5'
	unsigned int pLock_prev;
	unsigned int pReceived_prev;
	unsigned int pLock_next;
	unsigned int pReceived_next;

	unsigned int is_tch_video;		//'rtk6'
	struct tch_metadata tch_hdr_metadata;	//'rtk6'

	unsigned int pFrameBufferDbg;		//'rtk7'
	unsigned int pFrameBufferDbg_Right;
	unsigned int Y_addr_EL;			//'rtk8' for dolby vision
	unsigned int U_addr_EL;
	unsigned int width_EL;
	unsigned int height_EL;
	unsigned int Y_pitch_EL;
	unsigned int C_pitch_EL;
	unsigned int lumaOffTblAddr_EL;
	unsigned int chromaOffTblAddr_EL;

	unsigned int dm_reg1_addr;
	unsigned int dm_reg1_size;
	unsigned int dm_reg2_addr;
	unsigned int dm_reg2_size;
	unsigned int dm_reg3_addr;
	unsigned int dm_reg3_size;
	unsigned int dv_lut1_addr;
	unsigned int dv_lut1_size;
	unsigned int dv_lut2_addr;
	unsigned int dv_lut2_size;

	unsigned int slice_height;		//'rtk8'

	unsigned int hdr_metadata_addr;		//'rtk9'
	unsigned int hdr_metadata_size;		//'rtk9'
	unsigned int tch_metadata_addr;		//'rtk9'
	unsigned int tch_metadata_size;		//'rtk9'
	unsigned int is_dolby_video;		//'rtk10'

	unsigned int lumaOffTblSize;		//'rtk11'
	unsigned int chromaOffTblSize;		//'rtk11'
	// 'rtk12'
	unsigned int Combine_Y_Addr;
	unsigned int Combine_U_Addr;
	unsigned int Combine_Width;
	unsigned int Combine_Height;
	unsigned int Combine_Y_Pitch;
	unsigned int secure_flag;

	// 'rtk13'
	unsigned int tvve_picture_width;
	unsigned int tvve_lossy_en;
	unsigned int tvve_bypass_en;
	unsigned int tvve_qlevel_sel_y;
	unsigned int tvve_qlevel_sel_c;
	unsigned int is_ve_tile_mode;
	unsigned int film_grain_metadat_addr;
	unsigned int film_grain_metadat_size;

	// 'rtk14'
	unsigned int partialSrcWin_x;           /* rtk14 0x72746B3E */
	unsigned int partialSrcWin_y;
	unsigned int partialSrcWin_w;
	unsigned int partialSrcWin_h;

	// 'rtk15'
	unsigned int dolby_out_hdr_metadata_addr;   /* 'rtk15' 0x72746B3F */
	unsigned int dolby_out_hdr_metadata_size;
};
#ifdef CONFIG_KERN_RPC_HANDLE_COMMAND
typedef struct RPC_STRUCT {
	uint32_t programID;
	uint32_t versionID;
	uint32_t procedureID;
	uint32_t taskID;
	uint32_t sysTID;
	uint32_t sysPID;
	uint32_t parameterSize;
	uint32_t mycontext;
} RPC_STRUCT;
#endif
struct rtk_rpc_info {
	struct device *dev;
	volatile void *vo_sync_flag;
	struct rtk_krpc_ept_info *acpu_ept_info;
	struct rtk_krpc_ept_info *hifi_ept_info;
	uint32_t *hdmi_new_mac;
	uint32_t *ao_in_hifi; /* Is audio output in HiFi */
#ifdef DMABUF_HEAPS_RTK
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
#endif
	void *vaddr;
	dma_addr_t paddr;
#ifdef CONFIG_KERN_RPC_HANDLE_COMMAND
	struct task_struct *rpc_thread;
	int pid;
#endif
	struct mutex lock;
};

unsigned int ipcReadULONG(u8 *src);
void ipcCopyMemory(void *p_des, void *p_src, unsigned long len);

int rtk_rpc_init(struct device *dev, struct rtk_rpc_info *rpc_info);
int rpc_create_video_agent(struct rtk_rpc_info *rpc_info, unsigned int *videoId,
			   unsigned int pinId);
int rpc_destroy_video_agent(struct rtk_rpc_info *rpc_info, u32 pinId);

int rpc_video_display(struct rtk_rpc_info *rpc_info,
		      struct rpc_vo_filter_display *argp);
int rpc_video_config_disp_win(struct rtk_rpc_info *rpc_info,
			      struct rpc_config_disp_win *argp);
int rpc_video_query_dis_win(struct rtk_rpc_info *rpc_info,
	struct rpc_query_disp_win_in *argp_in,
	struct rpc_query_disp_win_out* argp_out);
int rpc_video_config_graphic(struct rtk_rpc_info *rpc_info,
	struct rpc_config_graphic_canvas* argp);
int rpc_video_set_refclock(struct rtk_rpc_info *rpc_info,
			   struct rpc_refclock *argp);
int rpc_video_init_ringbuffer(struct rtk_rpc_info *rpc_info,
			      struct rpc_ringbuffer *argp);
int rpc_video_run(struct rtk_rpc_info *rpc_info, unsigned int instance);
int rpc_video_pause(struct rtk_rpc_info *rpc_info, unsigned int instance);
int rpc_video_stop(struct rtk_rpc_info *rpc_info, unsigned int instance);
int rpc_video_flush(struct rtk_rpc_info *rpc_info, unsigned int instance);
int rpc_video_destroy(struct rtk_rpc_info *rpc_info, unsigned int instance);
int rpc_set_spd_infoframe(struct rtk_rpc_info *rpc_info,
			unsigned int enable, char *vendor_str, char *product_str,
			unsigned int sdi);
int rpc_send_edid_raw_data(struct rtk_rpc_info *rpc_info,
			u8 *edid_data, u32 edid_size);
int rpc_set_hdmi_audio_onoff(struct rtk_rpc_info *rpc_info,
			 struct rpc_audio_ctrl_data *arg);
int rpc_send_hdmi_freq(struct rtk_rpc_info *rpc_info,
			 struct rpc_audio_hdmi_freq *arg);
int rpc_set_vrr(struct rtk_rpc_info *rpc_info,
			struct rpc_vout_hdmi_vrr *arg);
int rpc_query_tv_system(struct rtk_rpc_info *rpc_info,
			struct rpc_config_tv_system *arg);
int rpc_set_tv_system(struct rtk_rpc_info *rpc_info,
			 struct rpc_config_tv_system *arg);
int rpc_get_display_format(struct rtk_rpc_info *rpc_info,
			struct rpc_display_output_format *output_fmt);
int rpc_set_display_format(struct rtk_rpc_info *rpc_info,
			struct rpc_display_output_format *output_fmt);
int rpc_set_hdmi_audio_mute(struct rtk_rpc_info *rpc_info,
			struct rpc_audio_mute_info *mute_info);
int rpc_set_cvbs_auto_detection(struct rtk_rpc_info *rpc_info,
		unsigned int enable);
int rpc_get_cvbs_format(struct rtk_rpc_info *rpc_info,
		unsigned int *p_cvbs_fmt);
int rpc_set_cvbs_format(struct rtk_rpc_info *rpc_info,
		unsigned int cvbs_fmt);
int rpc_set_mixer_order(struct rtk_rpc_info *rpc_info,
			struct rpc_disp_mixer_order *arg);
int rpc_get_mixer_order(struct rtk_rpc_info *rpc_info,
			struct rpc_disp_mixer_order *arg);
int rpc_get_cvbs_connection_status(struct rtk_rpc_info *rpc_info,
		unsigned int *status);
int rpc_video_set_q_param(struct rtk_rpc_info *rpc_info,
			struct rpc_set_q_param *arg);
int rpc_video_config_channel_lowdelay(struct rtk_rpc_info *rpc_info,
				struct rpc_config_channel_lowdelay *arg);
int rpc_video_privateinfo_param(struct rtk_rpc_info *rpc_info, struct rpc_privateinfo_param *arg);
int rpc_video_query_disp_win_new(struct rtk_rpc_info *rpc_info,
				struct rpc_query_disp_win_in *argp_in,
				struct rpc_query_disp_win_out_new *argp_out);
int rpc_video_set_speed(struct rtk_rpc_info *rpc_info, struct rpc_set_speed *arg);
int rpc_video_set_background(struct rtk_rpc_info *rpc_info,
			struct rpc_set_background *arg);
int rpc_video_keep_curpic(struct rtk_rpc_info *rpc_info, struct rpc_keep_curpic *arg);
int rpc_video_keep_curpic_fw(struct rtk_rpc_info *rpc_info, struct rpc_keep_curpic *arg);
int rpc_video_keep_curpic_svp(struct rtk_rpc_info *rpc_info, struct rpc_keep_curpic_svp *arg);
int rpc_video_set_deintflag(struct rtk_rpc_info *rpc_info, struct rpc_set_deintflag *arg);
int rpc_video_create_graphic_win(struct rtk_rpc_info *rpc_info, struct rpc_create_graphic_win *arg);
int rpc_video_draw_graphic_win(struct rtk_rpc_info *rpc_info, struct rpc_draw_graphic_win *arg);
int rpc_video_modify_graphic_win(struct rtk_rpc_info *rpc_info, struct rpc_modify_graphic_win *arg);
int rpc_video_delete_graphic_win(struct rtk_rpc_info *rpc_info, struct rpc_delete_graphic_win *arg);
int rpc_video_config_osd_palette(struct rtk_rpc_info *rpc_info, struct rpc_config_osd_palette *arg);
int rpc_video_config_plane_mixer(struct rtk_rpc_info *rpc_info, struct rpc_config_plane_mixer *arg);
int rpc_video_set_sdrflag(struct rtk_rpc_info *rpc_info, struct rpc_set_sdrflag *arg);
int rpc_query_out_interface(struct rtk_rpc_info *rpc_info,
			    struct rpc_set_display_out_interface *arg);
int rpc_set_out_interface(struct rtk_rpc_info *rpc_info,
			    struct rpc_set_display_out_interface *arg);

#endif
