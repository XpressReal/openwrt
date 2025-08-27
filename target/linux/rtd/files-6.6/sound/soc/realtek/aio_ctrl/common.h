#ifndef __RTK_AIO_CTRL_COMMON_H__
#define __RTK_AIO_CTRL_COMMON_H__

#include <linux/types.h>

struct device;
struct rtk_krpc_ept_info;

struct rtk_aio_ctrl_buf {
	struct device *dev;
	dma_addr_t dma;
	void *virt;
	size_t size;
};

int rtk_aio_ctrl_alloc_buf(struct device *dev, struct rtk_aio_ctrl_buf *buf);
void rtk_aio_ctrl_free_buf(struct rtk_aio_ctrl_buf *buf);
int rtk_aio_ctrl_copy_to_buf(struct rtk_aio_ctrl_buf *buf, void *data, size_t size);

struct rtk_aio_ctrl_rpc {
        u32 remote_cpu;
	struct rtk_krpc_ept_info *notify_ept_info;
};

int rtk_aio_ctrl_rpc_setup(struct device *dev, struct rtk_aio_ctrl_rpc *rpc);
int rtk_aio_ctrl_rpc_send_msg(struct rtk_aio_ctrl_rpc *rpc, u32 command, dma_addr_t addr, u32 size);
u32 rtk_aio_ctrl_rpc_to_remote32(struct rtk_aio_ctrl_rpc *rpc, u32 val);

#endif
