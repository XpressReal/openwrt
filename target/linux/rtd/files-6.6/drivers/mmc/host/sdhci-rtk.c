// SPDX-License-Identifier: GPL-2.0+
/*
 * Realtek SDIO host driver
 *
 * Copyright (c) 2017-2020 Realtek Semiconductor Corp.
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/reset.h>
#include <linux/sys_soc.h>
#include <linux/utsname.h>

#include "sdhci-pltfm.h"
#include "sdhci-rtk.h"

#define BOUNDARY_OK(addr, len) \
	((addr | (SZ_4K - 1)) == ((addr + len - 1) | (SZ_4K - 1)))

struct sdhci_rtk_soc_data {
	const struct sdhci_pltfm_data *pdata;
	u32 rtkquirks;
};

struct sdhci_rtk {
	const struct sdhci_rtk_soc_data *soc_data;
	struct gpio_desc *wifi_rst;
	struct gpio_desc *sd_pwr_gpio;
	struct clk *clk_en_sdio;
	struct clk *clk_en_sdio_ip;
	struct reset_control *rstc_sdio;
	void __iomem *crt_membase;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_3v3_drv;
	struct pinctrl_state *pins_1v8_drv;
	struct pinctrl_state *pins_vsel_3v3;
	struct pinctrl_state *pins_vsel_1v8;
	unsigned int preset_pll;
	int location;
};

static const struct soc_device_attribute rtk_soc_thor[] = {
	{ .family = "Realtek Thor", },
	{ /* sentinel */ }
};

static const struct soc_device_attribute rtk_soc_thor_a01[] = {
	{ .family = "Realtek Thor", .revision = "A01" },
	{ /* sentinel */ }
};

static const struct soc_device_attribute rtk_soc_hank[] = {
	{ .family = "Realtek Hank", },
	{ /* sentinel */ }
};

static const struct soc_device_attribute rtk_soc_groot[] = {
	{ .family = "Realtek Groot", },
	{ /* sentinel */ }
};

static const struct soc_device_attribute rtk_soc_stark[] = {
	{ .family = "Realtek Stark", },
	{ /* sentinel */ }
};

static const struct soc_device_attribute rtk_soc_parker[] = {
	{ .family = "Realtek Parker", },
	{ /* sentinel */ }
};

static void rtk_sdhci_pad_power_ctrl(struct sdhci_host *host, int voltage)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtk *rtk_host = sdhci_pltfm_priv(pltfm_host);
	void __iomem *crt_membase =  rtk_host->crt_membase;
	u32 reg;

	switch (voltage) {
	case MMC_SIGNAL_VOLTAGE_180:
		reg = readl(crt_membase + SYS_PLL_SD1);
		reg &= ~REG_SEL3318_MASK;
		writel(reg, crt_membase + SYS_PLL_SD1);
		mdelay(1);

		reg = readl(crt_membase + SYS_PLL_SD1);
		reg |= FIELD_PREP(REG_SEL3318_MASK, REG_SEL3318_0V);
		writel(reg, crt_membase + SYS_PLL_SD1);
		mdelay(1);

		reg = readl(crt_membase + SYS_PLL_SD1);
		reg |= FIELD_PREP(REG_SEL3318_MASK, REG_SEL3318_1V8);
		writel(reg, crt_membase + SYS_PLL_SD1);
		mdelay(1);

		pinctrl_select_state(rtk_host->pinctrl, rtk_host->pins_vsel_1v8);

		break;
	case MMC_SIGNAL_VOLTAGE_330:
		reg = readl(crt_membase + SYS_PLL_SD1);
		if (FIELD_GET(REG_SEL3318_MASK, reg) == REG_SEL3318_3V3)
			return;

		reg &= ~FIELD_PREP(REG_SEL3318_MASK, REG_SEL3318_3V3);
		writel(reg, crt_membase + SYS_PLL_SD1);
		mdelay(1);

		pinctrl_select_state(rtk_host->pinctrl, rtk_host->pins_vsel_3v3);
		mdelay(1);

		reg = readl(crt_membase + SYS_PLL_SD1);
		reg &= ~REG_SEL3318_MASK;
		writel(reg, crt_membase + SYS_PLL_SD1);
		mdelay(1);

		reg = readl(crt_membase + SYS_PLL_SD1);
		reg |= FIELD_PREP(REG_SEL3318_MASK, REG_SEL3318_3V3);
		writel(reg, crt_membase + SYS_PLL_SD1);
		break;
	}
}

/*
 * The sd_reset pin of Realtek sdio wifi connects to the specific gpio of SoC.
 * Toggling this gpio will reset the SDIO interface of Realtek wifi devices.
 */
static void rtk_sdhci_wifi_device_reset(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtk *rtk_host = sdhci_pltfm_priv(pltfm_host);
	struct device *dev = mmc_dev(host->mmc);

	if (gpiod_direction_output(rtk_host->wifi_rst, 0))
		dev_err(dev, "fail to set sd reset gpio low\n");

	mdelay(150);

	if (gpiod_direction_input(rtk_host->wifi_rst))
		dev_err(dev, "wifi reset fail\n");
}

static void rtk_sdhci_set_cmd_timeout_irq(struct sdhci_host *host, bool enable)
{
	if (host->mmc->card != NULL)
		return;

	if (enable)
		host->ier |= SDHCI_INT_TIMEOUT;
	else
		host->ier &= ~SDHCI_INT_TIMEOUT;

	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
}

static void rtk_sdhci_pad_driving_configure(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtk *rtk_host = sdhci_pltfm_priv(pltfm_host);
	void __iomem *crt_membase =  rtk_host->crt_membase;

	if (soc_device_match(rtk_soc_thor)) {
		writel((readl(crt_membase + 0x4E024) & 0xf) | 0xAF75EEB0, crt_membase + 0x4E024);
		writel(0x5EEBDD7B, crt_membase + 0x4E028);
		writel((readl(crt_membase + 0x4E02c) & 0xffffffc0) | 0x37, crt_membase + 0x4E02c);
	} else {
		if (host->timing > MMC_TIMING_SD_HS)
			pinctrl_select_state(rtk_host->pinctrl, rtk_host->pins_1v8_drv);
		else
			pinctrl_select_state(rtk_host->pinctrl, rtk_host->pins_3v3_drv);
	}
}

static void rtk_sdhci_pll_configure(struct sdhci_host *host, u32 pll, int execute_tuning)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtk *rtk_host = sdhci_pltfm_priv(pltfm_host);
	void __iomem *crt_membase =  rtk_host->crt_membase;
	u32 val;

	writel(0x00000006, crt_membase + SYS_PLL_SDIO4);

	val = FIELD_PREP(REG_TUNE11, REG_TUNE11_1V9) |
	      FIELD_PREP(SSCPLL_CS1, SSCPLL_CS1_INIT_VALUE) |
	      FIELD_PREP(SSC_DEPTH, SSC_DEPTH_1_N) |
	      SSC_8X_EN |
	      FIELD_PREP(SSC_DIV_EXT_F, SSC_DIV_EXT_F_200M) |
	      EN_CPNEW;

	if (soc_device_match(rtk_soc_parker) || soc_device_match(rtk_soc_stark)) {
		val |= FIELD_PREP(SSCPLL_ICP, SSCPLL_ICP_10U) |
		       FIELD_PREP(SSCPLL_RS, SSCPLL_RS_13K);
	} else {
		val |= FIELD_PREP(SSCPLL_ICP, SSCPLL_ICP_20U) |
		       FIELD_PREP(SSCPLL_RS, SSCPLL_RS_10K);
	}

	/* The SSC shouldn't enable when execute tuning */
	if (!execute_tuning)
		writel(val, crt_membase + SYS_PLL_SDIO2);

	writel(pll, crt_membase + SYS_PLL_SDIO3);
	mdelay(2);
	writel(0x00000007, crt_membase + SYS_PLL_SDIO4);
	udelay(200);
}

static void sdhci_rtk_sd_slot_init(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtk *rtk_host = sdhci_pltfm_priv(pltfm_host);
	void __iomem *crt_membase =  rtk_host->crt_membase;
	struct device *dev = mmc_dev(host->mmc);

	if (soc_device_match(rtk_soc_stark)) {

		if (gpiod_direction_output(rtk_host->sd_pwr_gpio, 0))
			dev_err(dev, "fail to enable sd power");

		writel((readl(crt_membase + 0x4E080) & 0xfffFBfff) | 0x00004000, crt_membase + 0x4E080);
		mdelay(10);

		if (host->quirks2 & SDHCI_QUIRK2_NO_1_8_V)
			rtk_sdhci_pad_power_ctrl(host, MMC_SIGNAL_VOLTAGE_330);
		else
			rtk_sdhci_pad_power_ctrl(host, MMC_SIGNAL_VOLTAGE_180);
	}
}

static void sdhci_rtk_hw_init(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtk *rtk_host = sdhci_pltfm_priv(pltfm_host);
	void __iomem *crt_membase =  rtk_host->crt_membase;
	u32 val;

	if (soc_device_match(rtk_soc_thor_a01)) {
		writel(readl(crt_membase + SYS_PLL_SDIO1) | 0x1, crt_membase + SYS_PLL_SDIO1);
		writel(readl(crt_membase + SYS_PLL_SDIO2) | 0x1, crt_membase + SYS_PLL_SDIO2);
		udelay(200);
	} else if (soc_device_match(rtk_soc_groot) || soc_device_match(rtk_soc_hank)) {
		writel(readl(crt_membase + 0x7064) | (0x3 << 4), crt_membase + 0x7064);
		udelay(200);
	} else if (soc_device_match(rtk_soc_parker) || soc_device_match(rtk_soc_stark)) {
		writel(readl(crt_membase + 0x70c4) | (0x3 << 4), crt_membase + 0x70c4);
		udelay(200);
	}

	if (rtk_host->location == SDIO_LOC_0) {
		sdhci_rtk_sd_slot_init(host);
	} else if (soc_device_match(rtk_soc_stark)) {
		if (host->quirks2 & SDHCI_QUIRK2_NO_1_8_V)
			pinctrl_select_state(rtk_host->pinctrl, rtk_host->pins_vsel_3v3);
		else
			pinctrl_select_state(rtk_host->pinctrl, rtk_host->pins_vsel_1v8);
	}

	val = FIELD_PREP(SSC_TBASE, SSC_TBASE_INIT_VALUE) |
	      FIELD_PREP(SSC_STEP_IN, SSC_STEP_IN_INIT_VALUE) |
	      FIELD_PREP(SSC_DIV_N, SSC_DIV_N_200M);

	rtk_sdhci_pll_configure(host, val, 0);

	if (soc_device_match(rtk_soc_groot) || soc_device_match(rtk_soc_hank))
		writel(readl(crt_membase + SDIO_RTK_DUMMY_SYS1) | 0x80000000, crt_membase + SDIO_RTK_DUMMY_SYS1);

	writel(0x00000011, crt_membase + SDIO_RTK_ISREN);
	writel(0x00000001, crt_membase + SDIO_RTK_CTL);
}

static void rtk_sdhci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sdhci_host *host = mmc_priv(mmc);

	rtk_sdhci_set_cmd_timeout_irq(host, true);

	sdhci_request(mmc, mrq);
}

static void rtk_sdhci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtk *rtk_host = sdhci_pltfm_priv(pltfm_host);
	void __iomem *crt_membase =  rtk_host->crt_membase;

	sdhci_set_ios(mmc, ios);

	rtk_sdhci_pad_driving_configure(host);

	/* enlarge wait time for data */
	if ((host->timing == MMC_TIMING_UHS_SDR104) &&
	    soc_device_match(rtk_soc_thor))
		writel(0x1d, crt_membase + SDIO_RTK_DUMMY_SYS1);
}

static void rtk_sdhci_reset(struct sdhci_host *host, u8 mask)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtk *rtk_host = sdhci_pltfm_priv(pltfm_host);
	void __iomem *crt_membase =  rtk_host->crt_membase;

	writel(0x00000003, crt_membase + SDIO_RTK_CTL);

	if (mask & SDHCI_RESET_DATA)
		sdhci_writel(host, SDHCI_INT_DATA_END, SDHCI_INT_STATUS);

	sdhci_reset(host, mask);

	writel(0x00000001, crt_membase + SDIO_RTK_CTL);
}

static u32 rtk_sdhci_irq(struct sdhci_host *host, u32 intmask)
{
	u32 command;
	u16 clk;

	if (host->mmc->card != NULL)
		return intmask;

	if (host->cmd != NULL) {
		rtk_sdhci_set_cmd_timeout_irq(host, true);
		return intmask;
	}

	if (intmask & SDHCI_INT_TIMEOUT) {
		command = SDHCI_GET_CMD(sdhci_readw(host, SDHCI_COMMAND));
		clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	} else {
		return intmask;
	}

	if (!clk && (command == MMC_GO_IDLE_STATE))
		rtk_sdhci_set_cmd_timeout_irq(host, false);

	if (command == MMC_GO_IDLE_STATE || command == SD_SEND_IF_COND ||
	    command == SD_IO_SEND_OP_COND || command == SD_IO_RW_DIRECT ||
	    command == MMC_APP_CMD || command == MMC_SEND_OP_COND) {
		intmask &= ~SDHCI_INT_TIMEOUT;
		sdhci_writel(host, SDHCI_INT_TIMEOUT, SDHCI_INT_STATUS);
		del_timer(&host->timer);
	}

	return intmask;
}

static int rtk_mmc_send_tuning(struct mmc_host *mmc, u32 opcode, int tx)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct mmc_command cmd = {0};
	u32 reg, mask;
	int err;

	if (tx) {
		mask = ~(SDHCI_INT_CRC | SDHCI_INT_END_BIT | SDHCI_INT_INDEX |
			 SDHCI_INT_DATA_TIMEOUT | SDHCI_INT_DATA_CRC |
			 SDHCI_INT_DATA_END_BIT | SDHCI_INT_BUS_POWER |
			 SDHCI_INT_AUTO_CMD_ERR | SDHCI_INT_ADMA_ERROR);

		reg = sdhci_readl(host, SDHCI_INT_ENABLE);
		reg &= mask;
		sdhci_writel(host, reg, SDHCI_INT_ENABLE);

		reg = sdhci_readl(host, SDHCI_SIGNAL_ENABLE);
		reg &= mask;
		sdhci_writel(host, reg, SDHCI_SIGNAL_ENABLE);

		cmd.opcode = opcode;
		if (mmc_card_sdio(mmc->card)) {
			cmd.arg = 0x2000;
			cmd.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_AC;
		} else {
			cmd.arg = mmc->card->rca << 16;
			cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC;
		}

		err = mmc_wait_for_cmd(mmc, &cmd, 0);
		if (err)
			return err;
	} else {
		return mmc_send_tuning(mmc, opcode, NULL);
	}

	return 0;
}

static int rtk_sdhci_change_phase(struct sdhci_host *host, u8 sample_point, int tx)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtk *rtk_host = sdhci_pltfm_priv(pltfm_host);
	void __iomem *crt_membase =  rtk_host->crt_membase;
	unsigned int temp_reg = 0;
	u16 clk = 0;

	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk &= ~SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	writel(readl(crt_membase + SYS_PLL_SDIO1) & 0xfffffffd, crt_membase + SYS_PLL_SDIO1);

	temp_reg = readl(crt_membase + SYS_PLL_SDIO1);

	if (tx)
		temp_reg = (temp_reg & ~0x000000F8) | (sample_point << 3);
	else
		temp_reg = (temp_reg & ~0x00001F00) | (sample_point << 8);

	writel(temp_reg, crt_membase + SYS_PLL_SDIO1);

	writel(readl(crt_membase + SYS_PLL_SDIO1) | 0x2, crt_membase + SYS_PLL_SDIO1);

	udelay(100);
	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	return 0;
}

static inline u32 test_phase_bit(u32 phase_map, unsigned int bit)
{
	bit %= MAX_PHASE;
	return phase_map & (1 << bit);
}

static int sd_get_phase_len(u32 phase_map, unsigned int start_bit)
{
	int i;

	for (i = 0; i < MAX_PHASE; i++) {
		if (test_phase_bit(phase_map, start_bit + i) == 0)
			return i;
	}
	return MAX_PHASE;
}

static u8 rtk_sdhci_search_final_phase(u32 phase_map, int tx)
{
	int start = 0, len = 0;
	int start_final = 0, len_final = 0;
	u8 final_phase = 0xFF;

	if (phase_map == 0) {
		pr_err("phase error: [map:%08x]\n", phase_map);
		return final_phase;
	}

	while (start < MAX_PHASE) {
		len = sd_get_phase_len(phase_map, start);
		if (len_final < len) {
			start_final = start;
			len_final = len;
		}
		start += len ? len : 1;
	}

	if (len_final > MINIMUM_CONTINUE_LENGTH)
		final_phase = (start_final + len_final / 2) % MAX_PHASE;
	else
		final_phase = 0xFF;

	pr_err("%s phase: [map:%08x] [maxlen:%d] [final:%d]\n",
		tx ? "tx" : "rx", phase_map, len_final, final_phase);

	return final_phase;
}

static int rtk_sdhci_tuning(struct sdhci_host *host, u32 opcode, int tx)
{
	struct mmc_host *mmc = host->mmc;
	int err, i, sample_point;
	u32 raw_phase_map[TUNING_CNT] = {0}, phase_map;
	u8 final_phase = 0;

	for (sample_point = 0; sample_point < MAX_PHASE; sample_point++) {
		for (i = 0; i < TUNING_CNT; i++) {
			rtk_sdhci_change_phase(host, (u8) sample_point, tx);
			err = rtk_mmc_send_tuning(mmc, opcode, tx);
			if (err == 0)
				raw_phase_map[i] |= (1 << sample_point);
		}
	}

	phase_map = 0xFFFFFFFF;
	for (i = 0; i < TUNING_CNT; i++)
		phase_map &= raw_phase_map[i];

	pr_err("%s %s phase_map = 0x%08x\n", __func__, tx ? "TX" : "RX", phase_map);

	if (phase_map) {
		final_phase = rtk_sdhci_search_final_phase(phase_map, tx);
		pr_err("%s final phase = 0x%08x\n", __func__, final_phase);
		if (final_phase == 0xFF) {
			pr_err("%s final phase = 0x%08x\n", __func__, final_phase);
			return -EINVAL;
		}
		err = rtk_sdhci_change_phase(host, final_phase, tx);
		if (err < 0)
			return err;
	} else {
		pr_err("%s  fail !phase_map\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static void rtk_sdhci_down_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtk *rtk_host = sdhci_pltfm_priv(pltfm_host);
	void __iomem *crt_membase =  rtk_host->crt_membase;
	u32 reg, ssc_div_n;

	reg = readl(crt_membase + SYS_PLL_SDIO3);
	ssc_div_n = (reg & 0x03FF0000) >> 16;
	/* When PLL set to 96, may interference wifi 2.4Ghz */
	if (ssc_div_n == 158)
		ssc_div_n = ssc_div_n - 7;
	reg = ((reg & (~0x3FF0000)) | ((ssc_div_n - 7) << 16));
	rtk_sdhci_pll_configure(host, reg, 1);
}

static int rtk_sdhci_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtk *rtk_host = sdhci_pltfm_priv(pltfm_host);
	void __iomem *crt_membase =  rtk_host->crt_membase;
	u32 reg, ssc_div_n, tx_opcode;
	int ret = 0;

	pr_err("%s : Execute Clock Phase Tuning\n", __func__);
	/* To disable the SSC during the phase tuning process. */
	reg = readl(crt_membase + SYS_PLL_SDIO2);
	reg &= 0xFFFF1FFF;
	writel(reg, crt_membase + SYS_PLL_SDIO2);

	if (mmc_card_sdio(host->mmc->card))
		tx_opcode = SD_IO_RW_DIRECT;
	else
		tx_opcode = MMC_SEND_STATUS;

	ret = rtk_sdhci_tuning(host, tx_opcode, 1);
	if (ret)
		pr_err("tx tuning fail\n");

	do {
		ret = rtk_sdhci_tuning(host, MMC_SEND_TUNING_BLOCK, 0);
		reg = readl(crt_membase + SYS_PLL_SDIO3);
		ssc_div_n = (reg & 0x03FF0000) >> 16;
		if (ret) {
			if (ssc_div_n <= 100) {
				pr_err("%s: Tuning RX fail\n", __func__);
				return ret;
			}
			rtk_sdhci_down_clock(host);
		}
	} while (ret);

	reg = readl(crt_membase + SYS_PLL_SDIO2);
	reg |= 0x00006000;
	writel(reg, crt_membase + SYS_PLL_SDIO2);

	pr_err("After tuning, current SDIO PLL = %x\n", ssc_div_n);

	return 0;
}

static void rtk_sdhci_set_uhs_signaling(struct sdhci_host *host, unsigned timing)
{
	u16 ctrl_2 = 0;

	sdhci_set_uhs_signaling(host, timing);

	if (timing > MMC_TIMING_SD_HS) {
		ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
		sdhci_writew(host, ctrl_2 | SDHCI_CTRL_VDD_180, SDHCI_HOST_CONTROL2);
	}
}

static void rtk_sdhci_adma_write_desc(struct sdhci_host *host, void **desc, dma_addr_t addr,
				      int len, unsigned int cmd)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtk *rtk_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_rtk_soc_data *soc_data = rtk_host->soc_data;
	u32 rtkquirks = soc_data->rtkquirks;
	int tmplen, offset;

	if (likely(!(rtkquirks & RTKQUIRK_AXI_BURST_BOUNDARY) || !len || BOUNDARY_OK(addr, len))) {
		sdhci_adma_write_desc(host, desc, addr, len, cmd);
		return;
	}

	pr_debug("%s: descriptor splitting, addr %pad, len %d\n", mmc_hostname(host->mmc), &addr, len);

	offset = addr & (SZ_4K - 1);
	tmplen = SZ_4K - offset;
	sdhci_adma_write_desc(host, desc, addr, tmplen, cmd);

	addr += tmplen;
	len -= tmplen;
	sdhci_adma_write_desc(host, desc, addr, len, cmd);
}

static void rtk_sdhci_request_done(struct sdhci_host *host, struct mmc_request *mrq)
{
	rtk_sdhci_set_cmd_timeout_irq(host, true);

	mmc_request_done(host->mmc, mrq);
}

/* Update card information to determine SD/SDIO tx tuning function */
static void rtk_init_card(struct mmc_host *mmc, struct mmc_card *card)
{
	mmc->card = card;
}

static void rtk_replace_mmc_host_ops(struct sdhci_host *host)
{
	host->mmc_host_ops.request	= rtk_sdhci_request;
	host->mmc_host_ops.set_ios	= rtk_sdhci_set_ios;
	host->mmc_host_ops.init_card	= rtk_init_card;
}

static const struct sdhci_ops rtk_sdhci_ops = {
	.set_clock = sdhci_set_clock,
	.irq = rtk_sdhci_irq,
	.set_bus_width = sdhci_set_bus_width,
	.reset = rtk_sdhci_reset,
	.platform_execute_tuning = rtk_sdhci_execute_tuning,
	.set_uhs_signaling = rtk_sdhci_set_uhs_signaling,
	.adma_write_desc = rtk_sdhci_adma_write_desc,
	.request_done = rtk_sdhci_request_done,
};

static const struct sdhci_pltfm_data sdhci_rtk_sdio_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
	    SDHCI_QUIRK_SINGLE_POWER_WRITE |
	    SDHCI_QUIRK_NO_HISPD_BIT |
	    SDHCI_QUIRK_BROKEN_CARD_DETECTION |
	    SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
	    SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC,
	.quirks2 = SDHCI_QUIRK2_BROKEN_DDR50 | SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.ops = &rtk_sdhci_ops,
};

static struct sdhci_rtk_soc_data rtd1319_soc_data = {
	.pdata = &sdhci_rtk_sdio_pdata,
};

static struct sdhci_rtk_soc_data rtd1619b_soc_data = {
	.pdata = &sdhci_rtk_sdio_pdata,
};

static struct sdhci_rtk_soc_data rtd1319d_soc_data = {
	.pdata = &sdhci_rtk_sdio_pdata,
	.rtkquirks = RTKQUIRK_AXI_BURST_BOUNDARY,
};

static const struct of_device_id sdhci_rtk_dt_match[] = {
	{.compatible = "realtek,rtd1319-sdio", .data = &rtd1319_soc_data},
	{.compatible = "realtek,rtd1619b-sdio", .data = &rtd1619b_soc_data},
	{.compatible = "realtek,rtd1319d-sdio", .data = &rtd1319d_soc_data},
	{}
};
MODULE_DEVICE_TABLE(of, sdhci_rtk_dt_match);

static int sdhci_rtk_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct sdhci_rtk_soc_data *soc_data;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *sd_node;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_rtk *rtk_host;
	int rc = 0;

	pr_info("%s: build at : %s\n", __func__, utsname()->version);

	match = of_match_device(sdhci_rtk_dt_match, &pdev->dev);
	if (!match)
		return -EINVAL;
	soc_data = match->data;

	host = sdhci_pltfm_init(pdev, soc_data->pdata, sizeof(*rtk_host));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	rtk_host = sdhci_pltfm_priv(pltfm_host);
	rtk_host->soc_data = soc_data;
	rtk_host->preset_pll = 0;
	rtk_host->location = SDIO_LOC_1;

	rtk_host->crt_membase = of_iomap(node, 1);
	if (!rtk_host->crt_membase)
		return -ENOMEM;

	sd_node = of_get_compatible_child(node, "sd-slot");
	if (sd_node) {
		rtk_host->location = SDIO_LOC_0;

		rtk_host->sd_pwr_gpio = devm_gpiod_get(&pdev->dev, "sd-power", GPIOD_OUT_LOW);
		if (IS_ERR(rtk_host->sd_pwr_gpio))
			dev_err(&pdev->dev, "%s can't request power gpio\n", __func__);

		of_node_put(sd_node);
	}

	rtk_host->wifi_rst = devm_gpiod_get(&pdev->dev, "wifi-rst", GPIOD_OUT_LOW);
	if (IS_ERR(rtk_host->wifi_rst))
		dev_err(&pdev->dev, "%s rtk wifi reset gpio invalid\n", __func__);
	else
		rtk_sdhci_wifi_device_reset(host);

	rtk_host->rstc_sdio = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(rtk_host->rstc_sdio)) {
		pr_warn("Failed to get sdio reset control(%ld)\n", PTR_ERR(rtk_host->rstc_sdio));
		rtk_host->rstc_sdio = NULL;
	}

	rtk_host->clk_en_sdio = devm_clk_get(&pdev->dev, "sdio");
	if (IS_ERR(rtk_host->clk_en_sdio)) {
		pr_warn("Failed to get sdio clk(%ld)\n", PTR_ERR(rtk_host->clk_en_sdio));
		rtk_host->clk_en_sdio = NULL;
	}

	rtk_host->clk_en_sdio_ip = devm_clk_get(&pdev->dev, "sdio_ip");
	if (IS_ERR(rtk_host->clk_en_sdio_ip)) {
		pr_warn("Failed to get sdio ip clk(%ld)\n", PTR_ERR(rtk_host->clk_en_sdio_ip));
		rtk_host->clk_en_sdio_ip = NULL;
	}

	rtk_host->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(rtk_host->pinctrl)) {
		rc = PTR_ERR(rtk_host->pinctrl);
		pr_err("fail to get pinctrl\n");
		goto err_sdio_clk;
	}

	rtk_host->pins_default = pinctrl_lookup_state(rtk_host->pinctrl, PINCTRL_STATE_DEFAULT);
	if (IS_ERR(rtk_host->pins_default)) {
		rc = PTR_ERR(rtk_host->pins_default);
		pr_warn("fail to get default state\n");
		goto err_sdio_clk;
	}

	rtk_host->pins_3v3_drv = pinctrl_lookup_state(rtk_host->pinctrl, "sdio-3v3-drv");
	if (IS_ERR(rtk_host->pins_3v3_drv)) {
		rc = PTR_ERR(rtk_host->pins_3v3_drv);
		pr_warn("fail to get pad driving for 3.3V state\n");
		goto err_sdio_clk;
	}

	rtk_host->pins_1v8_drv = pinctrl_lookup_state(rtk_host->pinctrl, "sdio-1v8-drv");
	if (IS_ERR(rtk_host->pins_1v8_drv)) {
		rc = PTR_ERR(rtk_host->pins_1v8_drv);
		pr_warn("fail to get pad driving for 1.8V state\n");
		goto err_sdio_clk;
	}

	if (soc_device_match(rtk_soc_stark)) {
		rtk_host->pins_vsel_3v3 = pinctrl_lookup_state(rtk_host->pinctrl, "sdio-vsel-3v3");
		if (IS_ERR(rtk_host->pins_vsel_3v3)) {
			rc = PTR_ERR(rtk_host->pins_vsel_3v3);
			pr_warn("fail to get pad power state for 3.3V\n");
			goto err_sdio_clk;
		}

		rtk_host->pins_vsel_1v8 = pinctrl_lookup_state(rtk_host->pinctrl, "sdio-vsel-1v8");
		if (IS_ERR(rtk_host->pins_vsel_1v8)) {
			rc = PTR_ERR(rtk_host->pins_vsel_1v8);
			pr_warn("fail to get pad power state for 1.8V\n");
			goto err_sdio_clk;
		}

		if (device_property_read_bool(&pdev->dev, "no-1-8-v"))
			host->quirks2 |= SDHCI_QUIRK2_NO_1_8_V;
	}

	reset_control_deassert(rtk_host->rstc_sdio);
	clk_prepare_enable(rtk_host->clk_en_sdio);
	clk_prepare_enable(rtk_host->clk_en_sdio_ip);

	rtk_replace_mmc_host_ops(host);

	sdhci_rtk_hw_init(host);

	rc = mmc_of_parse(host->mmc);
	if (rc)
		goto err_sdio_clk;

	rc = sdhci_add_host(host);
	if (rc)
		goto err_sdio_clk;

	return 0;

err_sdio_clk:
	clk_disable_unprepare(rtk_host->clk_en_sdio_ip);
	clk_disable_unprepare(rtk_host->clk_en_sdio);
	sdhci_pltfm_free(pdev);

	return rc;
}

static int sdhci_rtk_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	int dead = (readl(host->ioaddr + SDHCI_INT_STATUS) == 0xFFFFFFFF);

	sdhci_remove_host(host, dead);
	sdhci_pltfm_free(pdev);

	return 0;
}

static void sdhci_rtk_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sdhci_host *host = dev_get_drvdata(dev);

	pr_err("[SDIO] %s\n", __func__);

	sdhci_suspend_host(host);
}

static int __maybe_unused sdhci_rtk_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtk *rtk_host = sdhci_pltfm_priv(pltfm_host);
	void __iomem *crt_membase =  rtk_host->crt_membase;

	pr_err("[SDIO] %s start\n", __func__);

	rtk_host->preset_pll = readl(crt_membase + SYS_PLL_SDIO1);

	sdhci_suspend_host(host);
	reset_control_assert(rtk_host->rstc_sdio);
	clk_disable_unprepare(rtk_host->clk_en_sdio);
	clk_disable_unprepare(rtk_host->clk_en_sdio_ip);

	pr_err("[SDIO] %s OK\n", __func__);
	return 0;
}

static int __maybe_unused sdhci_rtk_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtk *rtk_host = sdhci_pltfm_priv(pltfm_host);
	void __iomem *crt_membase =  rtk_host->crt_membase;
	u8 reg;

	host->clock = 0;

	pr_err("[SDIO] %s start\n", __func__);

	reset_control_deassert(rtk_host->rstc_sdio);
	clk_prepare_enable(rtk_host->clk_en_sdio);
	clk_prepare_enable(rtk_host->clk_en_sdio_ip);

	sdhci_rtk_hw_init(host);

	if ((host->timing == MMC_TIMING_UHS_SDR50) || (host->timing == MMC_TIMING_UHS_SDR104))
		writel(rtk_host->preset_pll, crt_membase + SYS_PLL_SDIO1);

	reg = sdhci_readb(host, SDHCI_POWER_CONTROL);
	reg |= SDHCI_POWER_ON | SDHCI_POWER_330;
	sdhci_writeb(host, reg, SDHCI_POWER_CONTROL);

	sdhci_resume_host(host);

	pr_err("[SDIO] %s OK\n", __func__);

	return 0;
}

static SIMPLE_DEV_PM_OPS(sdhci_rtk_pmops, sdhci_rtk_suspend, sdhci_rtk_resume);

static struct platform_driver sdhci_rtk_driver = {
	.driver = {
		.name = "sdhci-rtk",
		.of_match_table = sdhci_rtk_dt_match,
		.pm = &sdhci_rtk_pmops,
		},
	.probe = sdhci_rtk_probe,
	.remove = sdhci_rtk_remove,
	.shutdown = sdhci_rtk_shutdown,
};
module_platform_driver(sdhci_rtk_driver);

MODULE_DESCRIPTION("SDIO driver for Realtek DHC SoCs");
MODULE_LICENSE("GPL");
