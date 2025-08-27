// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * Realtek I2C driver
 *
 * Copyright (c) 2017 - 2020 Realtek Semiconductor Corporation
 *
 */

#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

/* I2C Registers */
#define I2C_CON			0x00
#define I2C_TAR			0x04
#define I2C_SAR			0x08
#define I2C_DATA_CMD		0x10
#define I2C_SS_SCL_HCNT         0x14
#define I2C_SS_SCL_LCNT         0x18
#define I2C_FS_SCL_HCNT         0x1c
#define I2C_FS_SCL_LCNT         0x20
#define I2C_HS_SCL_HCNT         0x24
#define I2C_HS_SCL_LCNT         0x28
#define I2C_INTR_STAT		0x2C
#define I2C_INTR_MASK		0x30
#define I2C_RAW_INTR_STAT	0x34
#define I2C_RX_TL		0x38
#define I2C_TX_TL		0x3C
#define I2C_CLR_INT		0x40
#define I2C_CLR_RX_UNDER	0x44
#define I2C_CLR_RX_OVER		0x48
#define I2C_CLR_TX_OVER		0x4c
#define I2C_CLR_RD_REQ		0x50
#define I2C_CLR_TX_ABRT		0x54
#define I2C_CLR_RX_DONE		0x58
#define I2C_CLR_ACTIVITY	0x5c
#define I2C_CLR_STOP_DET	0x60
#define I2C_CLR_START_DET	0x64
#define I2C_CLR_GEN_CALL	0x68
#define I2C_ENABLE		0x6C
#define I2C_IC_STATUS		0x70
#define I2C_TXFLR		0x74
#define I2C_RXFLR		0x78
#define I2C_SDA_HOLD		0x7c
#define I2C_TX_ABRT_SOURCE	0x80
#define I2C_COMP_PARAM_1	0xF4

/* I2C_CONTROL Masks */
#define MASTER_EN		(1UL << 0)
#define TEN_BIT_SLAVE		(1UL << 3)
#define TEN_BIT_MASTER		(1UL << 4)
#define RESTART_EN		(1UL << 5)
#define SLAVE_DISABLE		(1UL << 6)
#define TX_EMPTY_CTL		(1UL << 8)
#define SPEED_MSK		0x06
#define SPEED_SS		0x02
#define SPEED_FS		0x04
#define SPEED_HS		0x06

/* I2C_TAR Masks */
#define TAR_TEN_BITADDR		(1UL << 12)

#define STOP_CMD		(1UL << 9)
#define RESTART_CMD		(1UL << 10)

/* INTR_MASK */
#define RX_UNDER		(1UL << 0)
#define RX_OVER			(1UL << 1)
#define RX_FULL			(1UL << 2)
#define TX_OVER			(1UL << 3)
#define TX_EMPTY		(1UL << 4)
#define RD_REQ			(1UL << 5)
#define TX_ABRT			(1UL << 6)
#define RX_DONE			(1UL << 7)
#define ACTIVITY		(1UL << 8)
#define STOP_DET		(1UL << 9)
#define START_DET		(1UL << 10)
#define GEN_CALL		(1UL << 11)
#define INT_DEFAULT_MASK	(RX_FULL | TX_EMPTY | TX_ABRT | STOP_DET)

/* I2C_IC_STATUS */
#define SLV_ACTIVITY		(1UL << 6)

#define STATUS_IDLE		0x0
#define STATUS_W_IN_PROGRESS	0x1
#define STATUS_R_IN_PROGRESS	0x2
#define STATUS_W_TAR_CHANGE	0x4

/*
 * hardware abort codes from the TX_ABRT_SOURCE register
 *
 * only expected abort codes are listed here
 * refer to the datasheet for the full list
 */
#define ABRT_7B_ADDR_NOACK	0
#define ABRT_10ADDR1_NOACK	1
#define ABRT_10ADDR2_NOACK	2
#define ABRT_TXDATA_NOACK	3
#define ABRT_GCALL_NOACK	4
#define ABRT_GCALL_READ		5
#define ABRT_SBYTE_ACKDET	7
#define ABRT_SBYTE_NORSTRT	9
#define ABRT_10B_RD_NORSTRT	10
#define ABRT_MASTER_DIS		11
#define ABRT_LOST		12

#define TX_ABRT_7B_ADDR_NOACK	(1UL << ABRT_7B_ADDR_NOACK)
#define TX_ABRT_10ADDR1_NOACK	(1UL << ABRT_10ADDR1_NOACK)
#define TX_ABRT_10ADDR2_NOACK	(1UL << ABRT_10ADDR2_NOACK)
#define TX_ABRT_TXDATA_NOACK	(1UL << ABRT_TXDATA_NOACK)
#define TX_ABRT_GCALL_NOACK	(1UL << ABRT_GCALL_NOACK)
#define TX_ABRT_GCALL_READ	(1UL << ABRT_GCALL_READ)
#define TX_ABRT_SBYTE_ACKDET	(1UL << ABRT_SBYTE_ACKDET)
#define TX_ABRT_SBYTE_NORSTRT	(1UL << ABRT_SBYTE_NORSTRT)
#define TX_ABRT_10B_RD_NORSTRT	(1UL << ABRT_10B_RD_NORSTRT)
#define TX_ABRT_MASTER_DIS	(1UL << ABRT_MASTER_DIS)
#define TX_ABRT_LOST		(1UL << ABRT_LOST)

#define TX_ABRT_NOACK		(TX_ABRT_7B_ADDR_NOACK | \
				 TX_ABRT_10ADDR1_NOACK | \
				 TX_ABRT_10ADDR2_NOACK | \
				 TX_ABRT_TXDATA_NOACK | \
				 TX_ABRT_GCALL_NOACK)

#define RTK_PROCESS_I2C_0	0x8400ff3d
#define RTK_PROCESS_I2C_0_READ	0x8400ff43

#define SDA_DEL_EN

#define IGLOO_CA_TYPE		0x4F

static int ISR_CLR_BIT[] = { 8, 11, 26, 23, 15, 14, 10 };
#ifdef SDA_DEL_EN
static int SDA_DEL_SHIFT[] = { 0x84, 0x80, 0x80, 0x84, 0x88, 0x8C, 0xC0 };
#define I2C_SDA_DEL_MASK	(0x1FF)
#define I2C_SDA_DEL_EN		(0x00000001<<8)
#define I2C_SDA_DEL_SEL(x)	((x & 0x1F)) /* Delay time: (unit 518ns)*/
#define SDA_DEL_518NS		1
#define I2C0_SDA_DEL_520NS    4
#define SDA_DEL_1036NS		2
#define SDA_DEL_1554NS		3
#define SDA_DEL_2072NS		4
#define SDA_DEL_2590NS		5
#endif

static char *abort_sources[] = {
	[ABRT_7B_ADDR_NOACK] =
		"slave address not acknowledged (7bit mode)",
	[ABRT_10ADDR1_NOACK] =
		"first address byte not acknowledged (10bit mode)",
	[ABRT_10ADDR2_NOACK] =
		"second address byte not acknowledged (10bit mode)",
	[ABRT_TXDATA_NOACK] =
		"data not acknowledged",
	[ABRT_GCALL_NOACK] =
		"no acknowledgement for a general call",
	[ABRT_GCALL_READ] =
		"read after general call",
	[ABRT_SBYTE_ACKDET] =
		"start byte acknowledged",
	[ABRT_SBYTE_NORSTRT] =
		"trying to send start byte when restart is disabled",
	[ABRT_10B_RD_NORSTRT] =
		"trying to read when restart is disabled (10bit mode)",
	[ABRT_MASTER_DIS] =
		"trying to use disabled adapter",
	[ABRT_LOST] =
		"lost arbitration",
};

struct rtk_i2c_quirks {
	int high_speed;
};

struct rtk_i2c_dev {
	struct device *dev;
	struct i2c_adapter adap;
	struct i2c_client *slave;
	struct completion msg_done;
	struct clk *clk;
	struct reset_control *rst;
	struct i2c_msg *msgs;
	struct i2c_bus_recovery_info bri;

	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_default;
	struct pinctrl_state *pinctrl_gpio;

	unsigned int tx_buf_len;
	unsigned char *tx_buf;
	unsigned int rx_buf_len;
	unsigned char *rx_buf;
	int msgs_num;
	int msg_w_idx;
	int msg_r_idx;
	int msg_err;
	int rx_outstanding;
	int abort_source;
	int tx_fifo_depth;
	int rx_fifo_depth;

	struct i2c_timings timings;

	void __iomem *base;
	void __iomem *irqbase;
	int irq;
	int status;
	int ca_type;

	const struct rtk_i2c_quirks *quirks;
};

static void rtk_i2c_int_disable(struct rtk_i2c_dev *priv, unsigned int mask)
{
	unsigned int int_en;

	int_en = readl(priv->base + I2C_INTR_MASK);
	writel(int_en & ~mask, priv->base + I2C_INTR_MASK);
}

static void rtk_i2c_int_enable(struct rtk_i2c_dev *priv, unsigned int mask)
{
	unsigned int int_en;

	int_en = readl(priv->base + I2C_INTR_MASK);
	writel(int_en | mask, priv->base + I2C_INTR_MASK);
}

static inline int rtk_i2c_high_speed_supported(struct rtk_i2c_dev *priv)
{
	return priv->quirks && priv->quirks->high_speed;
}

static int rtk_i2c_set_speed(struct rtk_i2c_dev *priv)
{
	struct i2c_adapter *adap = &priv->adap;
	struct arm_smccc_res res = { 0 };
	unsigned int KHz = priv->timings.bus_freq_hz / 1000;
	unsigned int scl_time;
	unsigned int div_h = 0;
	unsigned int div_l = 0;
	unsigned int fs_hcnt = 0;
	unsigned int fs_lcnt = 0;
	unsigned int clk_time;
	unsigned int val;
	int max_speed = rtk_i2c_high_speed_supported(priv) ? 3400 : 800;

	if (KHz < 10 || KHz > max_speed) {
		dev_err(priv->dev, "speed %d out of range\n", KHz);
		return -1;
	}

	clk_time = 37;			/*use 27MHZ crystal, one clock 37ns*/
	scl_time = (1000000 / KHz) / 2;	/* the time ns need for SCL */
	if (scl_time % clk_time) {
		if ((scl_time % clk_time) > clk_time / 2)
			scl_time += (clk_time - (scl_time % clk_time));
		else
			scl_time -= (scl_time % clk_time);
	}

	if (rtk_i2c_high_speed_supported(priv)) {
		if (KHz == 100) {
			div_h = 524;
			div_l = 532;
		} else if (KHz == 400) {
			div_h = 106;
			div_l = 146;
		} else if (KHz == 3400) {
			fs_hcnt = 106;
			fs_lcnt = 146;
			div_h = 6;
			div_l = 17;
		}
	} else {
		if (KHz < 400) {
			div_h = (scl_time / clk_time) - 8;
			div_l = (scl_time / clk_time);
		} else {
			div_h = 21;
			div_l = 35;
		}
	}

	writel(0, priv->base + I2C_ENABLE);

	if (priv->ca_type == IGLOO_CA_TYPE) {
		arm_smccc_smc(RTK_PROCESS_I2C_0_READ + adap->nr, 0, 0, 0, 0, 0, 0, 0, &res);
		val = res.a0;
	} else {
		val = readl(priv->base + I2C_CON);
	}

	if (KHz <= 100) {
		if (priv->ca_type == IGLOO_CA_TYPE) {
			val = (val & ~SPEED_MSK) | SPEED_SS;
			arm_smccc_smc(RTK_PROCESS_I2C_0 + adap->nr, val, 0, 0, 0, 0, 0, 0, &res);
		} else
			writel((val & ~SPEED_MSK) | SPEED_SS, priv->base + I2C_CON);
		writel(div_h, priv->base + I2C_SS_SCL_HCNT);
		writel(div_l, priv->base + I2C_SS_SCL_LCNT);
	} else if (KHz == 400) {
		if (priv->ca_type == IGLOO_CA_TYPE) {
			val = (val & ~SPEED_MSK) | SPEED_FS;
			arm_smccc_smc(RTK_PROCESS_I2C_0 + adap->nr, val, 0, 0, 0, 0, 0, 0, &res);
		} else
			writel((val & ~SPEED_MSK) | SPEED_FS, priv->base + I2C_CON);
		writel(div_h, priv->base + I2C_FS_SCL_HCNT);
		writel(div_l, priv->base + I2C_FS_SCL_LCNT);
	} else if (KHz == 3400) {
		if (priv->ca_type == IGLOO_CA_TYPE) {
			val = (val & ~SPEED_MSK) | SPEED_HS;
			arm_smccc_smc(RTK_PROCESS_I2C_0 + adap->nr, val, 0, 0, 0, 0, 0, 0, &res);
		} else
			writel((val & ~SPEED_MSK) | SPEED_HS, priv->base + I2C_CON);
		writel(fs_hcnt, priv->base + I2C_FS_SCL_HCNT);
		writel(fs_lcnt, priv->base + I2C_FS_SCL_LCNT);
		writel(div_h, priv->base + I2C_HS_SCL_HCNT);
		writel(div_l, priv->base + I2C_HS_SCL_LCNT);
		priv->timings.sda_hold_ns = 0x5;
	}
	if (priv->ca_type == IGLOO_CA_TYPE)
		if (res.a0 != 0)
			dev_err(priv->dev, "i2c smcc write fail\n");
	if (adap->nr != 0 && priv->timings.sda_hold_ns == 0 && ((1000000 / KHz) / 4) > 600 )
		priv->timings.sda_hold_ns = (((1000000 / KHz) / 4 - 600) * 27) / 1000;

	writel(priv->timings.sda_hold_ns, priv->base + I2C_SDA_HOLD);

	dev_info(priv->dev, "i2c sda hold time = 0x%x\n", priv->timings.sda_hold_ns);

#ifdef SDA_DEL_EN
	val = readl(priv->irqbase + SDA_DEL_SHIFT[adap->nr]);
	val &= ~I2C_SDA_DEL_MASK;

	if (rtk_i2c_high_speed_supported(priv)) {
		if (KHz == 3400)
			val = 0x0;
		else
			val |= I2C_SDA_DEL_EN | I2C_SDA_DEL_SEL(I2C0_SDA_DEL_520NS);

	} else {
		val |= I2C_SDA_DEL_EN | I2C_SDA_DEL_SEL(SDA_DEL_518NS);
	}
	writel(val, priv->irqbase + SDA_DEL_SHIFT[adap->nr]);
#endif
	return 0;
}

static void rtk_i2c_disable(struct rtk_i2c_dev *priv)
{
	writel(0, priv->base + I2C_ENABLE);
	writel(0, priv->base + I2C_INTR_MASK);
	readl(priv->base + I2C_CLR_INT);
}

static int rtk_i2c_init(struct rtk_i2c_dev *priv)
{
	writel(0, priv->base + I2C_ENABLE);
	writel(0, priv->base + I2C_INTR_MASK);

	priv->tx_fifo_depth =
		(((readl(priv->base + I2C_COMP_PARAM_1) >> 16) & 0xFF) + 1);
	priv->rx_fifo_depth =
		(((readl(priv->base + I2C_COMP_PARAM_1) >> 8) & 0xFF) + 1);

	writel(0, priv->base + I2C_TX_TL);
	writel(0, priv->base + I2C_RX_TL);

	return rtk_i2c_set_speed(priv);
}

#if IS_ENABLED(CONFIG_I2C_SLAVE)
static unsigned int rtk_i2c_clear_intrbits_slave(struct rtk_i2c_dev *priv)
{
	unsigned int status;

	status = readl(priv->base + I2C_INTR_STAT);

	if (status & TX_ABRT)
		readl(priv->base + I2C_CLR_TX_ABRT);
	if (status & RX_UNDER)
		readl(priv->base + I2C_CLR_RX_UNDER);
	if (status & RX_OVER)
		readl(priv->base + I2C_CLR_RX_OVER);
	if (status & TX_OVER)
		readl(priv->base + I2C_CLR_TX_OVER);
	if (status & RX_DONE)
		readl(priv->base + I2C_CLR_RX_DONE);
	if (status & ACTIVITY)
		readl(priv->base + I2C_CLR_ACTIVITY);
	if (status & STOP_DET)
		readl(priv->base + I2C_CLR_STOP_DET);
	if (status & START_DET)
		readl(priv->base + I2C_CLR_START_DET);
	if (status & GEN_CALL)
		readl(priv->base + I2C_CLR_GEN_CALL);

	return status;
}
#endif

static unsigned int rtk_i2c_clear_intrbits(struct rtk_i2c_dev *priv)
{
	unsigned int status;

	status = readl(priv->base + I2C_INTR_STAT);

	if (status & RX_UNDER)
		readl(priv->base + I2C_CLR_RX_UNDER);
	if (status & RX_OVER)
		readl(priv->base + I2C_CLR_RX_OVER);
	if (status & TX_OVER)
		readl(priv->base + I2C_CLR_TX_OVER);
	if (status & RD_REQ)
		readl(priv->base + I2C_CLR_RD_REQ);
	if (status & TX_ABRT) {
		priv->abort_source = readl(priv->base + I2C_TX_ABRT_SOURCE);
		readl(priv->base + I2C_CLR_TX_ABRT);
	}
	if (status & RX_DONE)
		readl(priv->base + I2C_CLR_RX_DONE);
	if (status & ACTIVITY)
		readl(priv->base + I2C_CLR_ACTIVITY);
	if (status & STOP_DET)
		readl(priv->base + I2C_CLR_STOP_DET);
	if (status & START_DET)
		readl(priv->base + I2C_CLR_START_DET);
	if (status & GEN_CALL)
		readl(priv->base + I2C_CLR_GEN_CALL);

	return status;
}

static void rtk_i2c_handle_tx_abort(struct rtk_i2c_dev *priv)
{
	unsigned long abort_source = priv->abort_source;
	int i;

	if (abort_source & TX_ABRT_NOACK) {
		for_each_set_bit(i, &abort_source, ARRAY_SIZE(abort_sources))
			dev_err(priv->dev,
				"%s: %s\n", __func__, abort_sources[i]);
		return;
	}

	for_each_set_bit(i, &abort_source, ARRAY_SIZE(abort_sources))
		dev_err(priv->dev, "%s: %s\n", __func__, abort_sources[i]);
}

static void rtk_i2c_xfer_read(struct rtk_i2c_dev *priv)
{
	struct i2c_msg *msgs = priv->msgs;
	int rx_valid;

	for (; priv->msg_r_idx < priv->msgs_num; priv->msg_r_idx++) {
		unsigned int len;
		unsigned char *buf;

		if (!(msgs[priv->msg_r_idx].flags & I2C_M_RD))
			continue;

		if (!(priv->status & STATUS_R_IN_PROGRESS)) {
			len = msgs[priv->msg_r_idx].len;
			buf = msgs[priv->msg_r_idx].buf;
		} else {
			len = priv->rx_buf_len;
			buf = priv->rx_buf;
		}

		rx_valid = readl(priv->base + I2C_RXFLR);

		for (; len > 0 && rx_valid > 0; len--, rx_valid--) {
			*buf++ = readl(priv->base + I2C_DATA_CMD);
			priv->rx_outstanding--;
		}

		if (len > 0) {
			priv->status |= STATUS_R_IN_PROGRESS;
			priv->rx_buf_len = len;
			priv->rx_buf = buf;
			return;
		}
		priv->status &= ~STATUS_R_IN_PROGRESS;
	}
}

static void rtk_i2c_xfer_msg(struct rtk_i2c_dev *priv)
{
	struct i2c_msg *msgs = priv->msgs;
	unsigned int addr = msgs[priv->msg_w_idx].addr;
	unsigned int buf_len = priv->tx_buf_len;
	unsigned char *buf = priv->tx_buf;
	int tx_limit, rx_limit;
	bool restart = false;
	unsigned int intr_mask = 0;

	for (; priv->msg_w_idx < priv->msgs_num; priv->msg_w_idx++) {
		if (msgs[priv->msg_w_idx].addr != addr) {
			priv->status |= STATUS_W_TAR_CHANGE;
			break;
		}
		if (msgs[priv->msg_w_idx].len == 0) {
			dev_err(priv->dev,
				"%s: invalid message length\n", __func__);
			priv->msg_err = -EINVAL;
			break;
		}
		if (priv->status & STATUS_W_TAR_CHANGE) {
			writel(0, priv->base + I2C_ENABLE);
			if (msgs[priv->msg_w_idx].flags & I2C_M_TEN)
				writel((msgs[priv->msg_w_idx].addr & 0x3FF) |
					TAR_TEN_BITADDR, priv->base + I2C_TAR);
			else
				writel(msgs[priv->msg_w_idx].addr & 0x7F,
					priv->base + I2C_TAR);
			writel(1, priv->base + I2C_ENABLE);
			restart = true;
			priv->status &= ~STATUS_W_TAR_CHANGE;
		}
		if (!(priv->status & STATUS_W_IN_PROGRESS)) {
			buf = msgs[priv->msg_w_idx].buf;
			buf_len = msgs[priv->msg_w_idx].len;
			if (priv->msg_w_idx > 0)
				restart = true;
		}
		tx_limit = priv->tx_fifo_depth - readl(priv->base + I2C_TXFLR);
		rx_limit = priv->rx_fifo_depth - readl(priv->base + I2C_RXFLR);

		while (buf_len > 0 && tx_limit > 0 && rx_limit > 0) {
			unsigned int cmd = 0;

			if ((priv->msg_w_idx == priv->msgs_num - 1) &&
			   buf_len == 1)
				cmd |= STOP_CMD;
			if (restart) {
				cmd |= RESTART_CMD;
				restart = false;
			}

			if (msgs[priv->msg_w_idx].flags & I2C_M_RD) {
				if (priv->rx_outstanding >= priv->rx_fifo_depth)
					break;
				writel(cmd | 0x100, priv->base + I2C_DATA_CMD);
				rx_limit--;
				priv->rx_outstanding++;
			} else
				writel(cmd | *buf++, priv->base + I2C_DATA_CMD);
			tx_limit--; buf_len--;
		}
		priv->tx_buf = buf;
		priv->tx_buf_len = buf_len;

		if (buf_len > 0) {
			/* more bytes to be written */
			priv->status |= STATUS_W_IN_PROGRESS;
			break;
		}
		priv->status &= ~STATUS_W_IN_PROGRESS;
	}

	if (priv->msg_w_idx == priv->msgs_num)
		intr_mask = TX_EMPTY;

	if (priv->msg_err)
		intr_mask = ~0;

	if (intr_mask)
		rtk_i2c_int_disable(priv, intr_mask);
}

static int rtk_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
	int num)
{
	struct rtk_i2c_dev *priv = i2c_get_adapdata(adap);
	struct arm_smccc_res res = { 0 };
	unsigned int val;
	unsigned int addr = 0;

	if (priv->slave != NULL) {
		priv->msg_err = -EPROTOTYPE;
		goto fail;
	}

	reinit_completion(&priv->msg_done);
	priv->msgs = msgs;
	priv->msgs_num = num;
	priv->msg_err = 0;
	priv->msg_w_idx = 0;
	priv->msg_r_idx = 0;
	priv->status = STATUS_IDLE;
	priv->rx_outstanding = 0;
	priv->abort_source = 0;

	writel(0, priv->base + I2C_ENABLE);

	if (priv->ca_type == IGLOO_CA_TYPE) {
		arm_smccc_smc(RTK_PROCESS_I2C_0_READ + adap->nr, 0, 0, 0, 0, 0, 0, 0, &res);
		val = res.a0;
	} else
		val = readl(priv->base + I2C_CON);

	if (msgs[priv->msg_w_idx].flags & I2C_M_TEN) {
		addr = msgs[priv->msg_w_idx].addr & 0x3FF;
		writel(addr | TAR_TEN_BITADDR, priv->base + I2C_TAR);

		val |= TEN_BIT_MASTER | SLAVE_DISABLE | MASTER_EN | TX_EMPTY_CTL;
		if (priv->ca_type == IGLOO_CA_TYPE)
			arm_smccc_smc(RTK_PROCESS_I2C_0 + adap->nr, val, 0, 0, 0, 0, 0, 0, &res);
		else
			writel(val, priv->base + I2C_CON);
	} else {
		addr = msgs[priv->msg_w_idx].addr & 0x7F;
		writel(addr, priv->base + I2C_TAR);

		val = val & ~TEN_BIT_MASTER;
		val |= SLAVE_DISABLE | MASTER_EN | TX_EMPTY_CTL;
		if (priv->ca_type == IGLOO_CA_TYPE)
			arm_smccc_smc(RTK_PROCESS_I2C_0 + adap->nr, val, 0, 0, 0, 0, 0, 0, &res);
		else
			writel(val, priv->base + I2C_CON);
	}

	rtk_i2c_int_disable(priv, ~0);

	writel(1, priv->base + I2C_ENABLE);

	readl(priv->base + I2C_CLR_INT);
	rtk_i2c_int_enable(priv, INT_DEFAULT_MASK);

	if (!wait_for_completion_timeout(&priv->msg_done, adap->timeout)) {
		priv->msg_err = -ETIMEDOUT;
		goto fail;
	}

	if (priv->msg_w_idx != priv->msgs_num) {
		priv->msg_err = -EPROTO;
		rtk_i2c_init(priv);
		goto fail;
	}

	writel(0, priv->base + I2C_ENABLE);

	if (priv->msg_err)
		goto fail;

	return num;
fail:
	if (priv->msg_err == -EPROTO)
		rtk_i2c_handle_tx_abort(priv);

	if (priv->msg_err == -ETIMEDOUT) {
		rtk_i2c_disable(priv);
		if (priv->adap.bus_recovery_info) {
			i2c_recover_bus(&priv->adap);
		} else {
			reset_control_reset(priv->rst);
		}
		rtk_i2c_init(priv);
		dev_err(priv->dev, "reset module & reinit\n");
	}

	dev_err(priv->dev, "transmit error %d 0x%x 0x%x 0x%x\n",
		priv->msg_err, addr, priv->msg_w_idx, num);

	return priv->msg_err;
}

static unsigned int rtk_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_10BIT_ADDR |
		I2C_FUNC_PROTOCOL_MANGLING | I2C_FUNC_SLAVE;
}

#if IS_ENABLED(CONFIG_I2C_SLAVE)
static void rtk_i2c_slave_irq(struct rtk_i2c_dev *priv)
{
	unsigned int status;
	unsigned int raw_status;
	unsigned char val, activity;

	status = readl(priv->base + I2C_INTR_STAT);
	raw_status = readl(priv->base + I2C_RAW_INTR_STAT);
	activity = (readl(priv->base + I2C_IC_STATUS) & SLV_ACTIVITY) >> 6;

	if ((status & RX_FULL) && (status & STOP_DET))
		i2c_slave_event(priv->slave, I2C_SLAVE_WRITE_REQUESTED, &val);

	if ((status & RD_REQ) && activity) {
		if (status & RX_FULL) {
			val = readl(priv->base + I2C_DATA_CMD);
			if (!i2c_slave_event(priv->slave,
					I2C_SLAVE_WRITE_RECEIVED, &val))
				dev_dbg(priv->dev, "Byte %x acked!\n", val);
			readl(priv->base + I2C_CLR_RD_REQ);
			status = rtk_i2c_clear_intrbits_slave(priv);
		} else {
			readl(priv->base + I2C_CLR_RD_REQ);
			readl(priv->base + I2C_CLR_RX_UNDER);
			status = rtk_i2c_clear_intrbits_slave(priv);
		}
		if (!i2c_slave_event(priv->slave,
				I2C_SLAVE_READ_REQUESTED, &val))
			writel(val, priv->base + I2C_DATA_CMD);
	}

	if (status & RX_DONE) {
		if (!i2c_slave_event(priv->slave,
				I2C_SLAVE_READ_PROCESSED, &val))
			readl(priv->base + I2C_CLR_RD_REQ);

		i2c_slave_event(priv->slave, I2C_SLAVE_STOP, &val);
		status = rtk_i2c_clear_intrbits_slave(priv);
		return;
	}

	if (status & RX_FULL) {
		val = readl(priv->base + I2C_DATA_CMD);
		if (!i2c_slave_event(priv->slave,
				I2C_SLAVE_WRITE_RECEIVED, &val))
			dev_dbg(priv->dev, "Byte 0x%x acked!\n", val);
	} else {
		i2c_slave_event(priv->slave, I2C_SLAVE_STOP, &val);
		status = rtk_i2c_clear_intrbits_slave(priv);
	}
}

static int rtk_i2c_slave_init(struct rtk_i2c_dev *priv)
{
	struct i2c_client *slave = priv->slave;
	unsigned int val = 0;

	writel(0, priv->base + I2C_ENABLE);
	rtk_i2c_int_disable(priv, ~0);

	if (slave->flags & I2C_CLIENT_TEN) {
		val = slave->addr & 0x3FF;
		writel(val, priv->base + I2C_SAR);
		val = readl(priv->base + I2C_CON) |
			(TEN_BIT_SLAVE & ~(SLAVE_DISABLE | MASTER_EN));
		writel(val, priv->base + I2C_CON);
	} else {
		val = slave->addr & 0x7F;
		writel(val, priv->base + I2C_SAR);
		val = readl(priv->base + I2C_CON) &
			~(SLAVE_DISABLE | MASTER_EN |
			TEN_BIT_SLAVE | TEN_BIT_MASTER);
		writel(val, priv->base + I2C_CON);
	}
	val = STOP_DET | RD_REQ | RX_FULL | RX_DONE;

	writel(0, priv->base + I2C_TX_TL);
	writel(0, priv->base + I2C_RX_TL);

	rtk_i2c_int_enable(priv, val);

	writel(1, priv->base + I2C_ENABLE);
	return 0;
}

static int rtk_i2c_reg_slave(struct i2c_client *slave)
{
	struct rtk_i2c_dev *priv = i2c_get_adapdata(slave->adapter);

	if (priv->slave)
		return -EBUSY;

	priv->slave = slave;
	rtk_i2c_slave_init(priv);

	return 0;
}

static int rtk_i2c_unreg_slave(struct i2c_client *slave)
{
	struct rtk_i2c_dev *priv = i2c_get_adapdata(slave->adapter);

	rtk_i2c_init(priv);
	priv->slave = NULL;

	return 0;
}

#endif

static const struct i2c_algorithm rtk_i2c_algo = {
	.master_xfer = rtk_i2c_xfer,
	.functionality = rtk_i2c_func,
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	.reg_slave      = rtk_i2c_reg_slave,
	.unreg_slave    = rtk_i2c_unreg_slave,
#endif
};

static irqreturn_t rtk_i2c_irq(int this_irq, void *dev_id)
{
	struct rtk_i2c_dev *priv = dev_id;
	struct i2c_adapter *adap = &priv->adap;
	unsigned int status, enabled;

	enabled = readl(priv->base + I2C_ENABLE);
	status = readl(priv->base + I2C_INTR_STAT);

	if (!enabled || !(status & ~ACTIVITY)) {
		dev_err(priv->dev, "IRQ_NONE: 0x%x, 0x%x\n", enabled, status);
		return IRQ_NONE;
	}

#if IS_ENABLED(CONFIG_I2C_SLAVE)
	if (!(readl(priv->base + I2C_CON) & SLAVE_DISABLE)) {
		rtk_i2c_slave_irq(priv);
		goto clear;
	}
#endif
	status = rtk_i2c_clear_intrbits(priv);

	if (status & TX_ABRT) {
		priv->msg_err = -EPROTO;
		priv->status = STATUS_IDLE;
		rtk_i2c_int_disable(priv, ~0);
		goto out;
	}
	if (status & RX_FULL)
		rtk_i2c_xfer_read(priv);

	if (status & TX_EMPTY)
		rtk_i2c_xfer_msg(priv);

out:
	if ((status & (TX_ABRT | STOP_DET)) || priv->msg_err)
		complete(&priv->msg_done);
#if IS_ENABLED(CONFIG_I2C_SLAVE)
clear:
#endif
	writel((1 << ISR_CLR_BIT[adap->nr]), priv->irqbase);

	return IRQ_HANDLED;
}

static void rtk_i2c_prepare_recovery(struct i2c_adapter *adap)
{
	struct rtk_i2c_dev *priv = container_of(adap, struct rtk_i2c_dev, adap);

	dev_info(priv->dev, "%s\n", __func__);

	reset_control_assert(priv->rst);
	clk_disable_unprepare(priv->clk);
	pinctrl_select_state(priv->pinctrl, priv->pinctrl_gpio);
}

static void rtk_i2c_unprepare_recovery(struct i2c_adapter *adap)
{
	struct rtk_i2c_dev *priv = container_of(adap, struct rtk_i2c_dev, adap);

	dev_info(priv->dev, "%s\n", __func__);
	pinctrl_select_state(priv->pinctrl, priv->pinctrl_default);
	clk_prepare_enable(priv->clk);
	reset_control_deassert(priv->rst);
}

#ifdef CONFIG_CFI_CLANG

#define RECOVERY_NDELAY		5000
#define RECOVERY_CLK_CNT	9

static int get_scl_gpio_value(struct i2c_adapter *adap)
{
	return gpiod_get_value_cansleep(adap->bus_recovery_info->scl_gpiod);
}

static void set_scl_gpio_value(struct i2c_adapter *adap, int val)
{
	gpiod_set_value_cansleep(adap->bus_recovery_info->scl_gpiod, val);
}

static int get_sda_gpio_value(struct i2c_adapter *adap)
{
	return gpiod_get_value_cansleep(adap->bus_recovery_info->sda_gpiod);
}

static void set_sda_gpio_value(struct i2c_adapter *adap, int val)
{
	gpiod_set_value_cansleep(adap->bus_recovery_info->sda_gpiod, val);
}

static int rtk_i2c_generic_bus_free(struct i2c_adapter *adap)
{
	int ret = -EOPNOTSUPP;

	ret = get_sda_gpio_value(adap);

	if (ret < 0)
		return ret;

	return ret ? 0 : -EBUSY;
}

int rtk_i2c_generic_scl_recovery(struct i2c_adapter *adap)
{
	struct i2c_bus_recovery_info *bri = adap->bus_recovery_info;
	int i = 0, scl = 1, ret = 0;

	if (bri->prepare_recovery)
		bri->prepare_recovery(adap);
	if (bri->pinctrl)
		pinctrl_select_state(bri->pinctrl, bri->pins_gpio);

	/*
	 * If we can set SDA, we will always create a STOP to ensure additional
	 * pulses will do no harm. This is achieved by letting SDA follow SCL
	 * half a cycle later. Check the 'incomplete_write_byte' fault injector
	 * for details. Note that we must honour tsu:sto, 4us, but lets use 5us
	 * here for simplicity.
	 */
	set_scl_gpio_value(adap, scl);
	ndelay(RECOVERY_NDELAY);

	set_sda_gpio_value(adap, scl);
	ndelay(RECOVERY_NDELAY / 2);

	/*
	 * By this time SCL is high, as we need to give 9 falling-rising edges
	 */
	while (i++ < RECOVERY_CLK_CNT * 2) {
		if (scl) {
			/* SCL shouldn't be low here */
			if (!get_scl_gpio_value(adap)) {
				dev_err(&adap->dev,
					"SCL is stuck low, exit recovery\n");
				ret = -EBUSY;
				break;
			}
		}

		scl = !scl;
		set_scl_gpio_value(adap, scl);
		/* Creating STOP again, see above */
		if (scl)  {
			/* Honour minimum tsu:sto */
			ndelay(RECOVERY_NDELAY);
		} else {
			/* Honour minimum tf and thd:dat */
			ndelay(RECOVERY_NDELAY / 2);
		}
		set_sda_gpio_value(adap, scl);
		ndelay(RECOVERY_NDELAY / 2);

		if (scl) {
			ret = rtk_i2c_generic_bus_free(adap);
			if (ret == 0)
				break;
		}
	}

	/* If we can't check bus status, assume recovery worked */
	if (ret == -EOPNOTSUPP)
		ret = 0;

	if (bri->unprepare_recovery)
		bri->unprepare_recovery(adap);
	if (bri->pinctrl)
		pinctrl_select_state(bri->pinctrl, bri->pins_default);

	return ret;
}
#endif

static int rtk_i2c_init_recovery_info(struct rtk_i2c_dev *priv)
{
	struct device *dev = priv->dev;
	struct i2c_bus_recovery_info *bri = &priv->bri;

	priv->pinctrl = devm_pinctrl_get(dev);
	if (!priv->pinctrl || IS_ERR(priv->pinctrl)) {
		dev_err(dev, "can't get pinctrl, bus recovery not support\n");
		return PTR_ERR(priv->pinctrl);
	}

	priv->pinctrl_default = pinctrl_lookup_state(priv->pinctrl, "default");
	priv->pinctrl_gpio = pinctrl_lookup_state(priv->pinctrl, "gpio_mode");
	if (IS_ERR(priv->pinctrl_default) || IS_ERR(priv->pinctrl_gpio)) {
		dev_err(dev, "can't get pin state, bus recovery not support\n");
		return -1;
	}

	bri->scl_gpiod = devm_gpiod_get_optional(dev, "scl", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(bri->scl_gpiod))
		return PTR_ERR_OR_ZERO(bri->scl_gpiod);

	bri->sda_gpiod = devm_gpiod_get_optional(dev, "sda", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(bri->scl_gpiod))
		return PTR_ERR_OR_ZERO(bri->sda_gpiod);

	pinctrl_select_state(priv->pinctrl, priv->pinctrl_gpio);

	bri->prepare_recovery = rtk_i2c_prepare_recovery;
	bri->unprepare_recovery = rtk_i2c_unprepare_recovery;
#ifdef CONFIG_CFI_CLANG
	bri->recover_bus = rtk_i2c_generic_scl_recovery;
#else
	bri->recover_bus = i2c_generic_scl_recovery;
#endif
	priv->adap.bus_recovery_info = bri;

	pinctrl_select_state(priv->pinctrl, priv->pinctrl_default);

	return 0;
}

static int rtk_i2c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct rtk_i2c_dev *priv = NULL;
	struct resource *res;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	priv->quirks = of_device_get_match_data(dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	if (of_find_property(np, "nvmem-cell-names", NULL)) {
		struct nvmem_cell *cell;

		cell = nvmem_cell_get(priv->dev, "ca-type");
		if (!IS_ERR(cell)) {
			unsigned char *buf;
			size_t buf_size;

			buf = nvmem_cell_read(cell, &buf_size);
			priv->ca_type = buf[0];
			kfree(buf);
			nvmem_cell_put(cell);
		} else {
			dev_err(dev, "missing nvmem resource\n");
			return -EPROBE_DEFER;
		}
	} else {
		dev_info(dev, "can't find nvmem cell node\n");
		priv->ca_type = 0;
	}

	priv->irqbase = of_iomap(np, 1);
	if (!priv->irqbase)
		return PTR_ERR(priv->irqbase);

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0) {
		dev_err(dev, "missing interrupt resource\n");
		return priv->irq;
	}

	priv->clk = of_clk_get(np, 0);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "missing clock\n");
		return PTR_ERR(priv->clk);
	}
	clk_prepare_enable(priv->clk);

	priv->rst = devm_reset_control_get_optional_exclusive(&pdev->dev, NULL);
	if (IS_ERR(priv->rst))
		return PTR_ERR(priv->rst);

	reset_control_reset(priv->rst);

	i2c_parse_fw_timings(dev, &priv->timings, 1);

	priv->adap.timeout = msecs_to_jiffies(250);
	priv->adap.retries = 5;
	priv->adap.dev.parent = dev;
	priv->adap.algo = &rtk_i2c_algo;
	priv->adap.owner = THIS_MODULE;
	priv->adap.dev.of_node = np;
	priv->adap.nr = of_alias_get_id(np, "i2c");
	strlcpy(priv->adap.name, "realtek i2c adapter", sizeof(priv->adap.name));

	rtk_i2c_init_recovery_info(priv);

	platform_set_drvdata(pdev, priv);
	i2c_set_adapdata(&priv->adap, priv);

	init_completion(&priv->msg_done);

	/* rtk i2c initial */
	ret = rtk_i2c_init(priv);
	if (ret)
		goto probe_fail;

	ret = devm_request_irq(dev, priv->irq, rtk_i2c_irq, IRQF_SHARED,
				dev_name(dev), priv);
	if (ret)
		goto probe_fail;

	ret = i2c_add_numbered_adapter(&priv->adap);
	if (ret)
		goto probe_fail;

	return 0;

probe_fail:
	clk_disable_unprepare(priv->clk);
	return ret;
}

static int rtk_i2c_remove(struct platform_device *pdev)
{
	struct rtk_i2c_dev *priv = platform_get_drvdata(pdev);

	i2c_del_adapter(&priv->adap);
	rtk_i2c_disable(priv);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rtk_i2c_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtk_i2c_dev *priv = platform_get_drvdata(pdev);

	dev_info(dev, "Enter %s\n", __func__);

	i2c_lock_bus(&priv->adap, I2C_LOCK_ROOT_ADAPTER);
	rtk_i2c_disable(priv);
	reset_control_assert(priv->rst);
	clk_disable_unprepare(priv->clk);
	i2c_unlock_bus(&priv->adap, I2C_LOCK_ROOT_ADAPTER);

	dev_info(dev, "Exit %s\n", __func__);
	return 0;
}

static void rtk_i2c_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	rtk_i2c_suspend(dev);
}

static int rtk_i2c_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtk_i2c_dev *priv = platform_get_drvdata(pdev);

	dev_info(dev, "Enter %s\n", __func__);

	i2c_lock_bus(&priv->adap, I2C_LOCK_ROOT_ADAPTER);
	clk_prepare_enable(priv->clk);
	reset_control_deassert(priv->rst);
	rtk_i2c_init(priv);
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	if (priv->slave)
		rtk_i2c_slave_init(priv);
#endif
	i2c_unlock_bus(&priv->adap, I2C_LOCK_ROOT_ADAPTER);

	dev_info(dev, "Exit %s\n", __func__);
	return 0;
}

static const struct dev_pm_ops rtk_i2c_pm_ops = {
	.suspend_noirq = rtk_i2c_suspend,
	.resume_noirq = rtk_i2c_resume,
};

#define RTK_I2C_PM_OPS (&rtk_i2c_pm_ops)
#else
#define RTK_I2C_PM_OPS NULL
#define rtk_i2c_shutdown NULL
#endif /* CONFIG_PM_SLEEP */

static const struct rtk_i2c_quirks rtk_i2c_hs_quirks = {
	.high_speed = 1,
};

/* Match table for of_platform binding */
static const struct of_device_id rtk_i2c_of_match[] = {
	{ .compatible = "realtek,i2c", },
	{ .compatible = "realtek,highspeed-i2c", .data = &rtk_i2c_hs_quirks, },
	{},
};
MODULE_DEVICE_TABLE(of, rtk_i2c_of_match);

static struct platform_driver rtk_i2c_driver = {
	.probe = rtk_i2c_probe,
	.remove = rtk_i2c_remove,
	.shutdown = rtk_i2c_shutdown,
	.driver = {
		.name = "realtek-i2c",
		.owner = THIS_MODULE,
		.pm = RTK_I2C_PM_OPS,
		.of_match_table = rtk_i2c_of_match,
	},
};

/* I2C may be needed to bring up other drivers */
static int __init rtk_i2c_init_driver(void)
{
	return platform_driver_register(&rtk_i2c_driver);
}
rootfs_initcall(rtk_i2c_init_driver);

static void __exit rtk_i2c_exit_driver(void)
{
	platform_driver_unregister(&rtk_i2c_driver);
}
module_exit(rtk_i2c_exit_driver);

MODULE_AUTHOR("Simon Hsu <simon_hsu@realtek.com>");
MODULE_DESCRIPTION("Realtek I2C bus driver");
MODULE_LICENSE("GPL");
