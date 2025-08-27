// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt)        KBUILD_MODNAME ": " fmt

#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include "hse.h"

#define HSE_CQ_COMPACT_BUF_SIZE 64

struct hse_command_queue_compact {
	struct hse_command_queue cq;
	u32 buf[HSE_CQ_COMPACT_BUF_SIZE];
};

struct hse_command_queue *hse_cq_alloc_compact(struct hse_device *hse_dev)
{
	struct hse_command_queue_compact *cqc;
	struct hse_command_queue *cq = NULL;

	cqc = kzalloc(sizeof(*cqc), GFP_KERNEL);
	if (!cqc)
		return NULL;
	cq = &cqc->cq;

	cq->hse_dev = hse_dev;
	cq->size = sizeof(cqc->buf);
	cq->virt = cqc->buf;
	cq->is_compact = 1;

	pr_debug("cq %pK: alloc_cq_compact: virt=%pK\n", cq, cq->virt);
	return cq;
}

struct hse_command_queue *hse_cq_alloc(struct hse_device *hse_dev)
{
	struct hse_command_queue *cq = NULL;
	size_t size = PAGE_SIZE;

	cq = kzalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq)
		return NULL;

	cq->hse_dev = hse_dev;
	cq->size = size;
	cq->virt = dma_alloc_coherent(hse_dev->dev, cq->size, &cq->phys, GFP_NOWAIT);
	if (!cq->virt)
		goto free_cq;

	pr_debug("cq %pK: alloc_cq: phys=%pad, virt=%pK\n", cq, &cq->phys, cq->virt);
	return cq;

free_cq:
	kfree(cq);
	return NULL;
}

static int hse_cq_resize(struct hse_command_queue *cq, size_t new_size)
{
	void *new_virt;
	dma_addr_t new_dma;

	if (cq->is_compact)
		return -EPERM;

	new_virt = dma_alloc_coherent(cq->hse_dev->dev, new_size, &new_dma, GFP_NOWAIT);
	if (!new_virt)
		return -ENOMEM;

	memcpy(new_virt, cq->virt, cq->pos);

	dma_free_coherent(cq->hse_dev->dev, cq->size, cq->virt, cq->phys);

	cq->size = new_size;
	cq->virt = new_virt;
	cq->phys = new_dma;
	return 0;
}

void hse_cq_free(struct hse_command_queue *cq)
{
	pr_debug("cq %pK: free_cq: virt=%pK\n", cq, cq->virt);

	if (cq->is_compact) {
		kfree(container_of(cq, struct hse_command_queue_compact, cq));
		return;
	}

	dma_free_coherent(cq->hse_dev->dev, cq->size, cq->virt, cq->phys);
	kfree(cq);
}

static inline void hse_cq_seal(struct hse_command_queue *cq)
{
	u32 *ptr = cq->virt;
	u32 len = cq->pos / 4;
	int i;

	if (cq->is_sealed)
		return;

	for (i = 0; i < len; i+=4) {
		swap(ptr[0], ptr[3]);
		swap(ptr[1], ptr[2]);
		ptr += 4;
	}
	cq->is_sealed = 1;
}

int hse_cq_append_compact(struct hse_command_queue *cq, struct hse_command_queue *compact)
{
	if ((cq->pos + compact->pos + 16) > cq->size)
		return -EINVAL;

	memcpy(cq->virt + cq->pos, compact->virt, compact->pos);
	cq->pos += compact->pos;
	cq->merge_cnt++;
	return 0;
}

static void __hse_cq_add_data(struct hse_command_queue *cq, u32 *data, size_t len)
{
	u32 *ptr = cq->virt + cq->pos;
	int i;

	for (i = 0; i < len; i++)
		*ptr++ = *data++;
	cq->pos += len * sizeof(*data);
}

int hse_cq_add_data(struct hse_command_queue *cq, u32 *data, size_t len)
{
	if (cq->is_sealed)
		return -EPERM;

	if (cq->pos + (len * sizeof(*data) + 16) >= cq->size) {
		int ret = hse_cq_resize(cq, cq->size * 2);

		if (ret) {
			pr_warn("%s: failed to resize cq %pK to %zu: %d\n", __func__, cq,
				cq->size * 2, ret);
			return ret;
		}

		pr_debug("%s: cq %pK resized to %zu\n", __func__, cq, cq->size);
	}

	pr_debug("cq %pK: add data\n", cq);
	print_hex_dump_debug("data: ", DUMP_PREFIX_NONE, 32, 4, data, len * sizeof(*data), false);

	__hse_cq_add_data(cq, data, len);
	return 0;
}

void hse_cq_hw_prepare(struct hse_command_queue *cq)
{
	struct device *dev = cq->hse_dev->dev;
	u32 paddata[5] = {0};
	u32 padsize[4] = {1, 4, 3, 2};

	/* end with 0 & align to 128 bits */
	__hse_cq_add_data(cq, paddata, padsize[(cq->pos >> 2) & 0x3]);

	hse_cq_seal(cq);

	dma_sync_single_for_device(dev, cq->phys, cq->size, DMA_TO_DEVICE);
}

void hse_cq_reset(struct hse_command_queue *cq)
{
	cq->pos = 0;
	cq->merge_cnt = 0;
	cq->is_sealed = 0;
}
