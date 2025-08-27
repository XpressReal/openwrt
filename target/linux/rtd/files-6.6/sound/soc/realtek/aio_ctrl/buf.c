#include <linux/dma-mapping.h>
#include <soc/realtek/rtk_media_heap.h>
#include <soc/realtek/memory.h>
#include "common.h"

int rtk_aio_ctrl_alloc_buf(struct device *dev, struct rtk_aio_ctrl_buf *buf)
{
	dma_addr_t dma_addr;
	void *vaddr;
	size_t size = PAGE_SIZE;

	rheap_setup_dma_pools(dev, "rtk_audio_heap",
			      RTK_FLAG_NONCACHED | RTK_FLAG_SCPUACC | RTK_FLAG_ACPUACC, __func__);

	vaddr = dma_alloc_coherent(dev, size, &dma_addr, GFP_KERNEL);
	if (!vaddr)
		return -ENOMEM;

	buf->dev = dev;
	buf->dma = dma_addr;
	buf->virt = vaddr;
	buf->size = size;
	return 0;
}

void rtk_aio_ctrl_free_buf(struct rtk_aio_ctrl_buf *buf)
{
	dma_free_coherent(buf->dev, buf->size, buf->virt, buf->dma);
}

int rtk_aio_ctrl_copy_to_buf(struct rtk_aio_ctrl_buf *buf, void *data, size_t size)
{
	if (size > buf->size)
		return -EINVAL;
	memcpy(buf->virt, data, size);
	return 0;
}
