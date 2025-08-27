/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Realtek, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hse

#if !defined(_TRACE_HSE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HSE_H

#undef TRACE_SYSTEM
#define TRACE_SYSTEM hse

#include <linux/tracepoint.h>

TRACE_EVENT(hse_start_engine,

	TP_PROTO(struct hse_engine *eng, struct hse_command_queue *cq),

	TP_ARGS(eng, cq),

	TP_STRUCT__entry(
			 __field(struct hse_command_queue *, cq)
			 __field(int, type)
			 __field(u32, merge_cnt)
	),

	TP_fast_assign(
		       __entry->cq = cq;
		       __entry->type = eng->desc->type;
		       __entry->merge_cnt = cq->merge_cnt;
	),

	TP_printk("cq=0x%p type_cq=%d merge_cnt=%u",
			__entry->cq, (__entry->type == HSE_ENGINE_MODE_COMMAND_QUEUE),
			__entry->merge_cnt)
);

TRACE_EVENT(hse_complete_done,

	TP_PROTO(struct hse_command_queue *cq),

	TP_ARGS(cq),

	TP_STRUCT__entry(
			 __field(struct hse_command_queue *, cq)
			 __field(int, status)
			 __field(u32, merge_cnt)
	),

	TP_fast_assign(
			   __entry->cq = cq;
			   __entry->status = cq->status;
	),

	TP_printk("cq=0x%p status=0x%x", __entry->cq, __entry->status)
);

#endif /* _TRACE_HSE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_hse

#include <trace/define_trace.h>
