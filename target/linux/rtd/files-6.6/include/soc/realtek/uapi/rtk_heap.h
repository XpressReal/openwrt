// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Realtek Semiconductor Corp.
 */

#ifndef __LINUX_RTK_HEAP_H_
#define __LINUX_RTK_HEAP_H_

struct rheap_ioc_get_memory_info_s {
	int handle;
	unsigned int heapMask; /* request: select the heap to be queried */
	unsigned int flags; /* request: set the conditions to query, 0 is to query all the conditions */
	unsigned int usedSize; /* response */
	unsigned int freeSize; /* response */
};

struct rheap_ioc_sync_range {
	int handle;
	u32 phyAddr;
	unsigned int len;
};

struct rheap_ioc_phy_info {
	int handle;
	unsigned long long addr;
	unsigned long long len;
};

#define RHEAP_TILER_ALLOC (0x0)
#define RHEAP_GET_LAST_ALLOC_ADDR (0x1)
#define RHEAP_INVALIDATE (0x10)
#define RHEAP_FLUSH (0x11)
#define RHEAP_GET_MEMORY_INFO \
	_IOWR('D', 0x12, struct rheap_ioc_get_memory_info_s)
#define RHEAP_INVALIDATE_RANGE (0x13)
#define RHEAP_FLUSH_RANGE (0x14)
#define RHEAP_GET_PHYINFO (0x15)

extern unsigned int retry_count_value;
extern unsigned int retry_delay_value;
#endif /* __LINUX_RTK_HEAP_H_ */
