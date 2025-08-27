/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/**
 * Copyright (C) 2022 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#ifndef __SOC_REALTEK_SB2_INV_H
#define __SOC_REALTEK_SB2_INV_H

enum {
	SB2_INV_CPU_ID_UNKNOWN = 0,
	SB2_INV_CPU_ID_SCPU,
	SB2_INV_CPU_ID_ACPU,
	SB2_INV_CPU_ID_SCPU_SWC,
	SB2_INV_CPU_ID_PCPU,
	SB2_INV_CPU_ID_VCPU,
	SB2_INV_CPU_ID_PCPU_2,
	SB2_INV_CPU_ID_AUCPU0,
	SB2_INV_CPU_ID_AUCPU1,
	SB2_INV_CPU_ID_HIF,
	SB2_INV_CPU_ID_MAX
};

struct sb2_inv_event_data {
	u32 raw_ints;
	u32 addr;
	u32 inv_cpu;
	u32 timeout_th;
};

#endif
