/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SOC_REALTEK_HSE_H__
#define __SOC_REALTEK_HSE_H__

#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>

#define HSE_REG_BYPASS                  0x41c

enum {
	HSE_STATUS_IRQ_OK = 0x1,
	HSE_STATUS_IRQ_CMD_ERR = 0x2,
};

struct hse_device;
struct hse_command_queue;

enum {
	HSE_ENGINE_MODE_COMMAND_QUEUE = 0,
	HSE_ENGINE_MODE_RCMD
};

struct hse_engine_desc {
	int offset;
	int type;
};

struct hse_engine {
	struct hse_device *hse_dev;
	int base_offset;
	spinlock_t lock;
	struct list_head list;
	struct hse_command_queue *cq;
	const struct hse_engine_desc *desc;
	int reg_ctrl;
	int reg_ints;
	int reg_intc;
};

int hse_engine_init(struct hse_device *hse_dev, struct hse_engine *eng, const struct hse_engine_desc *ed);
void hse_engine_handle_interrupt(struct hse_engine *eng);
void hse_engine_add_cq(struct hse_engine *eng, struct hse_command_queue *cq);
void hse_engine_remove_cq(struct hse_engine *eng, struct hse_command_queue *cq);
void hse_engine_issue_cq(struct hse_engine *eng);
int hse_engine_type_cq(struct hse_engine *eng);

struct hse_command_queue {
	struct hse_device *hse_dev;
	dma_addr_t phys;
	void *virt;
	size_t size;
	u32 pos;
	u32 merge_cnt;

	u32 status;
	void (*cb)(void *data);
	void *cb_data;
	struct list_head node;

	u32 is_compact : 1;
	u32 is_sealed : 1;
};

struct hse_command_queue *hse_cq_alloc(struct hse_device *hse_dev);
struct hse_command_queue *hse_cq_alloc_compact(struct hse_device *hse_dev);

void hse_cq_free(struct hse_command_queue *cq);
int hse_cq_add_data(struct hse_command_queue *cq, u32 *data, size_t size);
void hse_cq_reset(struct hse_command_queue *cq);
int hse_cq_append_compact(struct hse_command_queue *cq, struct hse_command_queue *compact_cq);
void hse_cq_hw_prepare(struct hse_command_queue *cq);

static inline void hse_cq_set_complete_callback(struct hse_command_queue *cq,
	void (*cb)(void *data), void *cb_data)
{
	cq->cb = cb;
	cq->cb_data = cb_data;
}

struct hse_quirks;
struct hse_dma_chan;

struct hse_device {
	struct miscdevice mdev;
	struct device *dev;
	struct clk *clk;
	struct reset_control *rstc;
	struct reset_control *rstc_bist;
	void *base;
	int irq;
	const struct hse_quirks *quirks;
	struct hse_engine *eng;
	int num_eng;

	struct dma_device dma_dev;
	int chans_num;
	struct hse_dma_chan *chans;

	u32 miscdevice_ready : 1;
	u32 dmaengine_ready : 1;
};

static inline void hse_write(struct hse_device *hse_dev, u32 offset, u32 val)
{
	dev_vdbg(hse_dev->dev, "w: offset=%03x, val=%08x\n", offset, val);
	writel(val, hse_dev->base + offset);
}

static inline u32 hse_read(struct hse_device *hse_dev, u32 offset)
{
	u32 v;

	v = readl(hse_dev->base + offset);
	dev_vdbg(hse_dev->dev, "r: offset=%03x, val=%08x\n", offset, v);
	return v;
}

static inline struct hse_engine *hse_device_get_engine(struct hse_device *hse_dev, int id)
{
	if (WARN_ON(id >= hse_dev->num_eng))
		id = 0;
	return &hse_dev->eng[id];
}

struct hse_quirks {
	unsigned int bypass_en_disable : 1;
	unsigned int xor_copy_v2 : 1;

	const struct hse_engine_desc *eng_desc;
	int num_eng;
};

static inline int hse_should_disable_bypass_en(struct hse_device *hse_dev)
{
	if (!hse_dev->quirks)
		return 0;
	return hse_dev->quirks->bypass_en_disable;
}

static inline int hse_should_workaround_copy(struct hse_device *hse_dev)
{
	if (!hse_dev->quirks)
		return 1;

	return !hse_dev->quirks->xor_copy_v2;
}


/* cq prep function */
int hse_cq_prep_copy(struct hse_device *hse_dev, struct hse_command_queue *cq,
		     dma_addr_t dst, dma_addr_t src, u32 size, u64 flags);
int hse_cq_prep_copy_sg(struct hse_device *hse_dev, struct hse_command_queue *cq,
			struct scatterlist *dst_sg, u32 dst_nents, u32 dst_offset,
			struct scatterlist *src_sg, u32 src_nents, u32 src_offset,
			u32 size, u64 flags);
int hse_cq_prep_picture_copy(struct hse_device *hse_dev, struct hse_command_queue *cq,
			     dma_addr_t dst, u16 dst_pitch, dma_addr_t src, u16 src_pitch,
			     u16 width, u16 height, u64 flags);
int hse_cq_prep_picture_copy_sg(struct hse_device *hse_dev, struct hse_command_queue *cq,
				struct scatterlist *dst_sg, u32 dst_nents, u32 dst_offset, u16 dst_pitch,
				struct scatterlist *src_sg, u32 src_nents, u32 src_offset, u16 src_pitch,
				u16 width, u16 height, u64 flags);
int hse_cq_prep_constant_fill(struct hse_device *hse_dev, struct hse_command_queue *cq,
			      dma_addr_t dst, u32 val, u32 size, u64 flags);
int hse_cq_prep_constant_fill_sg(struct hse_device *hse_dev, struct hse_command_queue *cq,
				 struct scatterlist *dst_sg, u32 dst_nents, u32 dst_offset,
				 u32 val, u32 size, u64 flags);
int hse_cq_prep_xor(struct hse_device *hse_dev, struct hse_command_queue *cq,
		    dma_addr_t dst, dma_addr_t *src, u32 src_cnt, u32 size, u64 flags);
int hse_cq_prep_xor_sg(struct hse_device *hse_dev, struct hse_command_queue *cq,
		       struct scatterlist *dst_sg, u32 dst_nents, u32 dst_offset,
		       struct scatterlist **src_sg, u32 *src_nents, u32 *src_offset,
		       u32 src_cnt, u32 size, u64 flags);
int hse_cq_prep_yuy2_to_nv16_sg(struct hse_device *hse_dev, struct hse_command_queue *cq,
				struct scatterlist *luma_sg, u32 luma_nents, u32 luma_offset,
				struct scatterlist *chroma_sg, u32 chroma_nents, u32 chroma_offset,
				u16 dst_pitch, struct scatterlist *src_sg, u32 src_nents, u32 src_offset,
				u16 src_pitch, u16 width, u16 height, u64 flags);
int hse_cq_prep_rotate(struct hse_device *hse_dev, struct hse_command_queue *cq,
		       dma_addr_t dst, u32 dst_pitch, dma_addr_t src, u32 src_pitch,
		       u32 width, u32 height, u32 mode, u32 color_format);

int hse_setup_miscdevice(struct hse_device *hse_dev);
void hse_teardown_miscdevice(struct hse_device *hse_dev);

#ifdef CONFIG_RTK_HSE_DMA
int hse_setup_dmaengine(struct hse_device *hse_dev);
void hse_teardown_dmaengine(struct hse_device *hse_dev);
#else
static inline int hse_setup_dmaengine(struct hse_device *hse_dev)
{
	return 0;
}

static inline void hse_teardown_dmaengine(struct hse_device *hse_dev)
{
}
#endif /* CONFIG_RTK_HSE_DMA */

#endif /* __SOC_REALTEK_HSE_H__ */
