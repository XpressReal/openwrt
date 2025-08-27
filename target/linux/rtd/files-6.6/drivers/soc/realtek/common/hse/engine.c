// SPDX-License-Identifier: GPL-2.0-only
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include "hse.h"

#define CREATE_TRACE_POINTS
#include "trace_hse.h"

#define HSE_REG_ENGINE_OFFSET_QB        0x00
#define HSE_REG_ENGINE_OFFSET_QL        0x04
#define HSE_REG_ENGINE_OFFSET_QR        0x08
#define HSE_REG_ENGINE_OFFSET_QW        0x0C
#define HSE_REG_ENGINE_OFFSET_Q         0x10
#define HSE_REG_ENGINE_OFFSET_INTS      0x14
#define HSE_REG_ENGINE_OFFSET_SWAP      0x18
#define HSE_REG_ENGINE_OFFSET_QCL       0x1C
#define HSE_REG_ENGINE_OFFSET_QCH       0x20
#define HSE_REG_ENGINE_OFFSET_INTC      0x24

#define HSE_REG_ENGINE_OFFSET_RCMD0      0x00
#define HSE_REG_ENGINE_OFFSET_RCMD_CTRL  0x20
#define HSE_REG_ENGINE_OFFSET_RCMD_INTS  0x24
#define HSE_REG_ENGINE_OFFSET_RCMD_INTC  0x28

static inline struct device *eng2dev(struct hse_engine *eng)
{
	return eng->hse_dev->dev;
}

static inline int hse_engine_read(struct hse_engine *eng, int offset)
{
	return hse_read(eng->hse_dev, eng->base_offset + offset);
}

static inline void hse_engine_write(struct hse_engine *eng, int offset, unsigned int val)
{
	hse_write(eng->hse_dev, eng->base_offset + offset, val);
}

int hse_engine_type_cq(struct hse_engine *eng)
{
	return eng->desc->type == HSE_ENGINE_MODE_COMMAND_QUEUE;
}

static void hse_engine_stop(struct hse_engine *eng)
{
	/* stop engine */
	hse_engine_write(eng, eng->reg_intc, 0);
	hse_engine_write(eng, eng->reg_ctrl, 0);

	/* clear ints */
	hse_engine_write(eng, eng->reg_ints, 0x6);
}

static struct hse_command_queue *hse_engine_next_cq(struct hse_engine *eng)
{
	return list_first_entry_or_null(&eng->list, struct hse_command_queue, node);
}

/* run with engine locked */
static void hse_engine_execute_cq(struct hse_engine *eng)
{
	struct hse_command_queue *cq;

	lockdep_assert_held(&eng->lock);

	cq = hse_engine_next_cq(eng);
	if (!cq)
		return;
	list_del_init(&cq->node);

	eng->cq = cq;

	if (hse_engine_type_cq(eng)) {
		hse_cq_hw_prepare(cq);

		hse_engine_write(eng, HSE_REG_ENGINE_OFFSET_QB, cq->phys);
		hse_engine_write(eng, HSE_REG_ENGINE_OFFSET_QL, cq->phys + cq->size);
		hse_engine_write(eng, HSE_REG_ENGINE_OFFSET_QR, cq->phys);
		hse_engine_write(eng, HSE_REG_ENGINE_OFFSET_QW, cq->phys + cq->pos);

		dev_dbg(eng2dev(eng), "eng@%03x: cq=%p,qb=%#x,ql=%#x,qr=%#x,qw=%#x\n",
			eng->base_offset, cq,
			hse_engine_read(eng, HSE_REG_ENGINE_OFFSET_QB),
			hse_engine_read(eng, HSE_REG_ENGINE_OFFSET_QL),
			hse_engine_read(eng, HSE_REG_ENGINE_OFFSET_QR),
			hse_engine_read(eng, HSE_REG_ENGINE_OFFSET_QW));
	} else {
		int i;
		u32 *ptr = cq->virt;
		int len = cq->pos / 4;
		int reg = HSE_REG_ENGINE_OFFSET_RCMD0;

		if (len > 8) {
			dev_err(eng2dev(eng), "eng@%03x: invalid cq len=%d\n", eng->base_offset, len);
			return;
		}

		for (i = 0; i < len; i++, reg+=4)
			hse_engine_write(eng, reg, *ptr++);
		for (; i < 8; i++, reg+=4)
			hse_engine_write(eng, reg, 0);
	}

	trace_hse_start_engine(eng, cq);

	/* start engine */
	hse_engine_write(eng, eng->reg_intc, 0x6);
	hse_engine_write(eng, eng->reg_ctrl, 0x1);
}

void hse_engine_add_cq(struct hse_engine *eng, struct hse_command_queue *cq)
{
	unsigned long flags;

	if (cq->is_compact) {
		if (hse_engine_type_cq(eng)) {
			dev_err(eng2dev(eng), "eng@%03x: add a compact cq\n", eng->base_offset);
			return;
		} else if (cq->pos > 32) {
			dev_err(eng2dev(eng), "eng@%03x: invalid cq size\n", eng->base_offset);
			return;
		}
	}

	spin_lock_irqsave(&eng->lock, flags);
	list_add_tail(&cq->node, &eng->list);
	spin_unlock_irqrestore(&eng->lock, flags);
}

void hse_engine_remove_cq(struct hse_engine *eng, struct hse_command_queue *cq)
{
	unsigned long flags;

	spin_lock_irqsave(&eng->lock, flags);
	if (eng->cq == cq) {
		hse_engine_stop(eng);
		eng->cq = NULL;
		hse_engine_execute_cq(eng);
	} else
		list_del_init(&cq->node);
	spin_unlock_irqrestore(&eng->lock, flags);
}

void hse_engine_issue_cq(struct hse_engine *eng)
{
	unsigned long flags;

	spin_lock_irqsave(&eng->lock, flags);
	if (!eng->cq)
		hse_engine_execute_cq(eng);
	spin_unlock_irqrestore(&eng->lock, flags);
}

void hse_engine_handle_interrupt(struct hse_engine *eng)
{
	struct hse_command_queue *cq;
	u32 raw_ints;

	spin_lock(&eng->lock);
	raw_ints = hse_engine_read(eng, eng->reg_ints);
	if (raw_ints == 0) {
		spin_unlock(&eng->lock);
		return;
	}
	hse_engine_stop(eng);
	cq = eng->cq;

	if (hse_engine_type_cq(eng)) {
		dev_dbg(eng2dev(eng), "eng@%03x: cq=%pK,qb=%#x,ql=%#x,qr=%#x,qw=%#x,ints=%#x\n",
			eng->base_offset, cq,
			hse_engine_read(eng, HSE_REG_ENGINE_OFFSET_QB),
			hse_engine_read(eng, HSE_REG_ENGINE_OFFSET_QL),
			hse_engine_read(eng, HSE_REG_ENGINE_OFFSET_QR),
			hse_engine_read(eng, HSE_REG_ENGINE_OFFSET_QW),
			raw_ints);
	} else {
		dev_dbg(eng2dev(eng), "eng@%03x: cq=%pK,ints=%#x\n",
			eng->base_offset, cq, raw_ints);

	}
	spin_unlock(&eng->lock);

	if (!cq) {
		dev_dbg(eng2dev(eng), "eng@%03x: interrupt raised with no cq\n", eng->base_offset);
		return;
	}

	if (raw_ints & 0x4)
		cq->status |= HSE_STATUS_IRQ_CMD_ERR;
	if (raw_ints & 0x2)
		cq->status |= HSE_STATUS_IRQ_OK;

	if (cq->cb)
		cq->cb(cq->cb_data);

	spin_lock(&eng->lock);
	eng->cq = NULL;
	hse_engine_execute_cq(eng);
	spin_unlock(&eng->lock);
}

int hse_engine_init(struct hse_device *hse_dev, struct hse_engine *eng, const struct hse_engine_desc *ed)
{
	eng->base_offset = ed->offset;
	eng->hse_dev = hse_dev;
	eng->desc = ed;
	spin_lock_init(&eng->lock);
	INIT_LIST_HEAD(&eng->list);

        if (hse_engine_type_cq(eng)) {
                eng->reg_ctrl = HSE_REG_ENGINE_OFFSET_Q;
                eng->reg_intc = HSE_REG_ENGINE_OFFSET_INTC;
                eng->reg_ints = HSE_REG_ENGINE_OFFSET_INTS;
        } else {
                eng->reg_ctrl = HSE_REG_ENGINE_OFFSET_RCMD_CTRL;
                eng->reg_intc = HSE_REG_ENGINE_OFFSET_RCMD_INTC;
                eng->reg_ints = HSE_REG_ENGINE_OFFSET_RCMD_INTS;
        }

	hse_engine_write(eng, eng->reg_ctrl, 0);
	if (hse_engine_type_cq(eng)) {
		hse_engine_write(eng, HSE_REG_ENGINE_OFFSET_QCL, 0);
		hse_engine_write(eng, HSE_REG_ENGINE_OFFSET_QCH, 0);
	}

	return 0;
}
