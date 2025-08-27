/*
 * rtk_sb2_dbg.h - Realtek SB2 Debug API
 *
 * Copyright (C) 2019 Realtek Semiconductor Corporation
 * Copyright (C) 2019 Cheng-Yu Lee <cylee12@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SOC_REALTEK_SB2_DGB_H
#define __SOC_REALTEK_SB2_DGB_H

enum {
	SB2_DBG_SOURCE_SCPU,
	SB2_DBG_SOURCE_ACPU,
	SB2_DBG_SOURCE_PCPU,
	SB2_DBG_SOURCE_VCPU,
};

enum {
	SB2_DBG_ACCESS_DATA,
	SB2_DBG_ACCESS_INST,
	SB2_DBG_ACCESS_READ,
	SB2_DBG_ACCESS_WRITE,
};

enum {
	SB2_DBG_MONITOR_DATA  = 0x04,
	SB2_DBG_MONITOR_INST  = 0x08,
	SB2_DBG_MONITOR_READ  = 0x20,
	SB2_DBG_MONITOR_WRITE = 0x40,
};

struct sb2_dbg_event_data {
	u32 raw_ints;
	u32 source;
	u32 rw;
	u32 di;
	u32 addr;
};

#endif
