/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rtk_smcc.h - RTK SMCC driver
 *
 * Copyright (c) 2020 Realtek Semiconductor Corp.
 */
#ifndef __RTK_SMCCC_H
#define __RTK_SMCCC_H

#include <linux/types.h>

#define RTK_OTP_WRITE 0x8400ff28
#define RTK_OTP_READ 0x8400ff27
#define SMC_RETURN_OK 0

typedef struct {
	uint64_t ret_value;
	uint64_t reserve1[7];
	uint64_t ret_value_h;
	uint64_t reserve2[7];
	uint64_t burning_data[32];
} OPT_Data_T;

#endif
