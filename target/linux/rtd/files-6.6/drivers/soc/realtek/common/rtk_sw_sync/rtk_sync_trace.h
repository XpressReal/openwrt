/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_INCLUDE_PATH ../../drivers/soc/realtek/common/rtk_sw_sync
#define TRACE_SYSTEM rtk_sync_trace

#if !defined(_TRACE_RTK_SYNC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RTK_SYNC_H

#include "rtk_sw_sync.h"
#include <linux/tracepoint.h>

#define __assign_str_n(dst, src, size)						\
	strlcpy(__get_str(dst), (src) ? (const char *)(src) : "(null)", size);

TRACE_EVENT(rtk_sync_timeline,
	TP_PROTO(struct rtk_sync_timeline *timeline),

	TP_ARGS(timeline),

	TP_STRUCT__entry(
			__string(name, timeline->name)
			__field(u32, value)
	),

	TP_fast_assign(
			__assign_str_n(name, timeline->name, sizeof(timeline->name));
			__entry->value = timeline->value;
	),

	TP_printk("name=%s value=%d", __get_str(name), __entry->value)
);

#endif /* if !defined(_TRACE_RTK_SYNC_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */
#include <trace/define_trace.h>
