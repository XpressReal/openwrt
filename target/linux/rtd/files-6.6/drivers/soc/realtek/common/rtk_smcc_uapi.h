/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * rtk_smcc.h - RTK SMCC driver
 *
 * Copyright (c) 2020 Realtek Semiconductor Corp.
 */
#ifndef __UAPI_RTK_SMCCC_H
#define __UAPI_RTK_SMCCC_H

#include <linux/ioctl.h>

#define RTK_SMCC_IOC_MAGIC 'k'
#define RTK_SMCC_OTP_READ       _IOWR(RTK_SMCC_IOC_MAGIC, 1, struct otp_info)
#define RTK_SMCC_OTP_WRITE      _IOWR(RTK_SMCC_IOC_MAGIC, 2, struct otp_write_info)

enum OTP_WRITE_CASE {
	DATA_SECTION_CASE = 0x0,
	DRITECT_PARAM_CASE = 0x1,
};

struct otp_info {
	__u32 typeID;
	__u64 ret_value;
	__u64 ret_value_h;
};

struct otp_write_info {
	__u32 typeID;
	__u32 perform_case;
	__u64 burning_value;
	__u64 burning_data[32];
};

#ifndef __KERNEL__
typedef struct otp_info OTP_Info_T;
typedef struct otp_write_info OTP_Write_Info_T;
#endif

#endif
