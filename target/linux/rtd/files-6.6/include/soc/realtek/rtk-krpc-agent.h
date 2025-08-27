/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _RTK_KRPC_AGAENT_H
#define _RTK_KRPC_AGAENT_H

#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <linux/mutex.h>
#include <linux/rpmsg.h>
#include <soc/realtek/rtk-rpmsg.h>

struct device_node;
struct rtk_krpc_agent;
struct rtk_krpc_ept;


#define RPC_AUDIO	0x0
#define RPC_VIDEO	0x1
#define RPC_VIDEO2	0x2
#define RPC_HIFI	0x3


struct rtk_krpc_ept_info {
	struct rtk_krpc_agent *agent;
	struct rtk_krpc_ept *krpc_ept;
	struct rpmsg_device *rpdev;
	u32 id;
	char name[10];
	wait_queue_head_t waitq;
	uint32_t *retval;
	struct completion ack;
	struct mutex send_mutex;
	void *priv;
};

typedef int (*krpc_cb)(struct rtk_krpc_ept_info *, char *);

static inline uint32_t get_rpc_alignment_offset(uint32_t offset)
{
	if ((offset % 4) == 0)
		return offset;
	else
		return (offset + (4 - (offset % 4)));
}


#if IS_ENABLED(CONFIG_RPMSG_RTK_RPC)
int rtk_send_rpc(struct rtk_krpc_ept_info *krpc_info, char *buf, int len);

struct rtk_krpc_ept_info *of_krpc_ept_info_get(struct device_node *np, int index);

void krpc_ept_info_put(struct rtk_krpc_ept_info *krpc_ept_info);

int krpc_info_init(struct rtk_krpc_ept_info *krpc_ept_info, char *name, krpc_cb cb);

void krpc_info_deinit(struct rtk_krpc_ept_info *krpc_ept_info);

void rtk_krpc_dump_ringbuf_info(struct rtk_krpc_ept_info *krpc_ept_info);
#else
static int __attribute__ ((unused)) rtk_send_rpc(struct rtk_krpc_ept_info *krpc_info, char *buf, int len)
{
	return 0;
}

static struct rtk_krpc_ept_info __attribute__ ((unused)) *of_krpc_ept_info_get(struct device_node *np, int index)
{
	return 0;
}

static int __attribute__ ((unused)) krpc_info_init(struct rtk_krpc_ept_info *krpc_ept_info, char *name, krpc_cb cb)
{
	return 0;
}

static void __attribute__ ((unused)) rtk_krpc_dump_ringbuf_info(struct rtk_krpc_ept_info *krpc_ept_info)
{

}

#endif

#endif
