/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright (c) 2020 Realtek Semiconductor Corp.
 */

#ifndef __UAPI_RTK_CEC_H
#define __UAPI_RTK_CEC_H

#define MAX_OSD_NAME 32

struct ipc_shm_cec {
	unsigned int  standby_config;
	unsigned char  standby_logical_addr;
	unsigned short standby_physical_addr;
	unsigned char  standby_cec_version;
	unsigned int  standby_vendor_id;
	unsigned short standby_rx_mask;
	unsigned char  standby_cec_wakeup_off;
	unsigned char  standby_osd_name[MAX_OSD_NAME];
};

#endif /* __UAPI_RTK_CEC_H */
