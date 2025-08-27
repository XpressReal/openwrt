/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019 RealTek Inc.
 */

#ifndef _RTK_DSI_H
#define _RTK_DSI_H

enum dsi_fmt {
	DSI_FMT_720P_60 = 0,
	DSI_FMT_1080P_60,
	DSI_FMT_1200_1920P_60,
	DSI_FMT_800_1280P_60,
	DSI_FMT_600_1024P_60,
	DSI_FMT_1920_720P_60,
	DSI_FMT_1920_720P_30,
	DSI_FMT_600_1024P_30,
	DSI_FMT_800_480P_60,
};

enum dsi_pat_gen {
	DSI_PAT_GEN_COLORBAR = 0,
	DSI_PAT_GEN_BLACK = 1,
	DSI_PAT_GEN_WHITE = 2,
	DSI_PAT_GEN_RED = 3,
	DSI_PAT_GEN_BLUE = 4,
	DSI_PAT_GEN_YELLOW = 5,
	DSI_PAT_GEN_MAGENTA = 6,
	DSI_PAT_GEN_USER_DEFINE = 7,
	DSI_PAT_GEN_MAX,
};

#endif /* _RTK_DSI_H */
