// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Realtek DHC SoCs Secure Digital Host Controller Interface
 *
 * Copyright (c) 2022 Realtek Semiconductor Corp.
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
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "sdhci-pltfm.h"
#include "sdhci-of-rtkstb.h"

#define BOUNDARY_OK(addr, len) \
	((addr | (SZ_4K - 1)) == ((addr + len - 1) | (SZ_4K - 1)))

struct sdhci_rtkstb_soc_data {
	const struct sdhci_pltfm_data *pdata;
	u32 rtkquirks;
};

struct timing_phase_path {
	int start;
	int end;
	int mid;
	int len;
};

struct sdhci_rtkstb {
	const struct sdhci_rtkstb_soc_data *soc_data;
	struct gpio_desc *wifi_rst;
	struct gpio_desc *sd_pwr_gpio;
	struct clk *sd_clk_en;
	struct clk *sd_ip_clk_en;
	struct reset_control *sd_rstn;
	void __iomem *wrap_base;
	void __iomem *dhc_base;
	u32 preset_pll;
	struct regmap *crt_base;
	struct regmap *iso_base;
	struct regmap *pinctrl_base;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_vsel_3v3;
	struct pinctrl_state *pins_vsel_1v8;
	struct pinctrl_state *pins_3v3_drv;
	struct pinctrl_state *pins_1v8_drv;
	struct pinctrl_state *pins_pud_cfg;
};

static const struct soc_device_attribute rtk_soc_parker[] = {
	{ .family = "Realtek Parker", },
	{ /* sentinel */ }
};

static const struct soc_device_attribute rtk_soc[] = {
	{ .family = "Realtek Danvers", },
	{ .family = "Realtek RTD1325", },
	{ /* sentinel */ }
};

static inline void rtkstb_sdhci_set_bit(struct sdhci_rtkstb *rtk_host, u32 offset, u32 bit)
{
	void __iomem *dhc_base = rtk_host->dhc_base;
	u32 reg;

	reg = readl(dhc_base + offset);
	reg |= bit;
	writel(reg, dhc_base + offset);
}

static inline void rtkstb_sdhci_clear_bit(struct sdhci_rtkstb *rtk_host, u32 offset, u32 bit)
{
	void __iomem *dhc_base = rtk_host->dhc_base;
	u32 reg;

	reg = readl(dhc_base + offset);
	reg &= ~bit;
	writel(reg, dhc_base + offset);
}

static int rtkstb_sdhci_pad_power_check(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);
	struct device *dev = mmc_dev(host->mmc);
	struct regmap *pinctrl_base = rtk_host->pinctrl_base;
	int pad_pwr_chk = 0, pad_pwr_3v3 = 0;
	u32 reg;
	u16 ctrl;

	do {
		regmap_read(pinctrl_base, ISO_DBG_STATUS, &reg);
		ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
		if (reg & SDIO0_H3L1_STATUS_HI) {
			ctrl &= ~SDHCI_CTRL_VDD_180;
			sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);
			pad_pwr_3v3 = 1;
		} else {
			dev_dbg(dev, "pad power switch to 1.8v, retry %d times", pad_pwr_chk);
			ctrl |= SDHCI_CTRL_VDD_180;
			sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);
			pad_pwr_3v3 = 0;
		}

		pad_pwr_chk += pad_pwr_chk;
	} while (pad_pwr_3v3 && pad_pwr_chk < PAD_PWR_CHK_CNT);

	return pad_pwr_3v3;
}

/*
 * The sd_reset pin of Realtek sdio wifi connects to the specific gpio of SoC.
 * Toggling this gpio will reset the SDIO interface of Realtek wifi devices.
 */
static void rtkstb_sdhci_wifi_device_reset(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);
	struct device *dev = mmc_dev(host->mmc);

	if (gpiod_direction_output(rtk_host->wifi_rst, 0))
		dev_err(dev, "fail to set wifi reset gpio low\n");

	mdelay(150);

	if (gpiod_direction_input(rtk_host->wifi_rst))
		dev_err(dev, "wifi reset fail\n");
}

static void rtkstb_sdhci_set_cmd_timeout_irq(struct sdhci_host *host, bool enable)
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

static int rtkstb_sdhci_pad_power_ctrl(struct sdhci_host *host, int voltage)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);
	struct device *dev = mmc_dev(host->mmc);
	struct regmap *crt_base = rtk_host->crt_base;
	int err = 0;
	u32 reg;

	switch (voltage) {
	case MMC_SIGNAL_VOLTAGE_180:
		regmap_read(crt_base, SYS_PLL_SD1, &reg);
		reg &= ~REG_SEL3318_MASK;
		regmap_write(crt_base, SYS_PLL_SD1, reg);
		mdelay(1);

		regmap_read(crt_base, SYS_PLL_SD1, &reg);
		reg |= FIELD_PREP(REG_SEL3318_MASK, REG_SEL3318_0V);
		regmap_write(crt_base, SYS_PLL_SD1, reg);
		mdelay(1);

		regmap_read(crt_base, SYS_PLL_SD1, &reg);
		reg |= FIELD_PREP(REG_SEL3318_MASK, REG_SEL3318_1V8);
		regmap_write(crt_base, SYS_PLL_SD1, reg);
		mdelay(1);

		err = rtkstb_sdhci_pad_power_check(host);
		if (err) {
			dev_err(dev, "switch LDO to 1.8v fail\n");
			goto exit;
		}

		pinctrl_select_state(rtk_host->pinctrl, rtk_host->pins_vsel_1v8);

		break;
	case MMC_SIGNAL_VOLTAGE_330:
		regmap_read(crt_base, SYS_PLL_SD1, &reg);
		if (FIELD_GET(REG_SEL3318_MASK, reg) == REG_SEL3318_3V3)
			goto exit;

		reg &= ~FIELD_PREP(REG_SEL3318_MASK, REG_SEL3318_3V3);
		regmap_write(crt_base, SYS_PLL_SD1, reg);
		mdelay(1);

		pinctrl_select_state(rtk_host->pinctrl, rtk_host->pins_vsel_3v3);
		mdelay(1);

		regmap_read(crt_base, SYS_PLL_SD1, &reg);
		reg &= ~REG_SEL3318_MASK;
		regmap_write(crt_base, SYS_PLL_SD1, reg);
		mdelay(1);

		regmap_read(crt_base, SYS_PLL_SD1, &reg);
		reg |= FIELD_PREP(REG_SEL3318_MASK, REG_SEL3318_3V3);
		regmap_write(crt_base, SYS_PLL_SD1, reg);
		break;
	}
exit:
	return err;
}

static void rtkstb_sdhci_pad_driving_configure(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);

	if (host->timing > MMC_TIMING_SD_HS)
		pinctrl_select_state(rtk_host->pinctrl, rtk_host->pins_1v8_drv);
	else
		pinctrl_select_state(rtk_host->pinctrl, rtk_host->pins_3v3_drv);
}

static void rtkstb_sdhci_set_ssc(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);
	struct regmap *crt_base = rtk_host->crt_base;
	void __iomem *wrap_base = rtk_host->wrap_base;
	u32 reg;

	regmap_read(crt_base, SYS_PLL_SD1, &reg);
	reg &= ~PHRT0;
	regmap_write(crt_base, SYS_PLL_SD1, reg);

	regmap_read(crt_base, SYS_PLL_SD2, &reg);
	reg &= ~(RTD13XXE_SSC_OC_EN | RTD13XXE_SSC_FLAG_INIT);
	regmap_write(crt_base, SYS_PLL_SD2, reg);

	reg = readl(wrap_base + SDHCI_RTK_SSC2);
	reg &= ~GRAN_SET;
	reg |= FIELD_PREP(GRAN_SET, GRAN_SET_208M_1PERCENT);
	writel(reg, wrap_base + SDHCI_RTK_SSC2);

	reg = readl(wrap_base + SDHCI_RTK_SSC1);
	reg &= ~(NCODE_SSC | FCODE_SSC | DOT_GRAN | EN_SSC);
	reg |= FIELD_PREP(NCODE_SSC, NCODE_SSC_INIT_VALUE) |
	       FIELD_PREP(FCODE_SSC, FCODE_SSC_208M_1PERCENT) |
	       FIELD_PREP(DOT_GRAN, DOT_GRAN_INIT_VALUE) | EN_SSC;
	writel(reg, wrap_base + SDHCI_RTK_SSC1);

	regmap_read(crt_base, SYS_PLL_SD2, &reg);
	reg |= RTD13XXE_SSC_OC_EN | RTD13XXE_SSC_FLAG_INIT;
	regmap_write(crt_base, SYS_PLL_SD2, reg);

	regmap_read(crt_base, SYS_PLL_SD2, &reg);
	reg &= ~RTD13XXE_SSC_OC_EN;
	regmap_write(crt_base, SYS_PLL_SD2, reg);

	regmap_read(crt_base, SYS_PLL_SD1, &reg);
	reg |= PHRT0;
	regmap_write(crt_base, SYS_PLL_SD1, reg);
	udelay(100);
}

static void rtkstb_sdhci_pll_configure(struct sdhci_host *host, u32 ssc, int down_clk_tuning)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_rtkstb_soc_data *soc_data = rtk_host->soc_data;
	struct regmap *crt_base = rtk_host->crt_base;
	u32 rtkquirks = soc_data->rtkquirks;
	u32 reg, sscpll;

	regmap_read(crt_base, SYS_PLL_SD4, &reg);
	reg &= ~SSC_RSTB;
	reg |= SSC_PLL_RSTB | SSC_PLL_POW;
	regmap_write(crt_base, SYS_PLL_SD4, reg);

	sscpll = FIELD_PREP(REG_TUNE11, REG_TUNE11_1V9) |
		 FIELD_PREP(SSCPLL_CS1, SSCPLL_CS1_INIT_VALUE) |
		 FIELD_PREP(SSCPLL_ICP, SSCPLL_ICP_CURRENT) |
		 FIELD_PREP(SSCPLL_RS, SSCPLL_RS_13K) |
		 FIELD_PREP(SSC_DEPTH, SSC_DEPTH_1_N) |
		 SSC_8X_EN |
		 FIELD_PREP(SSC_DIV_EXT_F, SSC_DIV_EXT_F_INIT_VAL) |
		 EN_CPNEW;

	if (down_clk_tuning || (rtkquirks & RTKQUIRK_SSC_CLK_JITTER))
		sscpll &= ~SSC_DEPTH;

	regmap_write(crt_base, SYS_PLL_SD2, sscpll);
	regmap_write(crt_base, SYS_PLL_SD3, ssc);
	mdelay(2);

	reg |= SSC_RSTB;
	regmap_write(crt_base, SYS_PLL_SD4, reg);
	udelay(200);
}

static void rtkstb_13xxe_sdhci_pll_configure(struct sdhci_host *host, u32 ssc, int down_clk_tuning)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_rtkstb_soc_data *soc_data = rtk_host->soc_data;
	struct regmap *crt_base = rtk_host->crt_base;
	u32 rtkquirks = soc_data->rtkquirks;
	u32 reg, sscpll;

	regmap_read(crt_base, SYS_PLL_SD1, &reg);
	reg &= ~PHRT0;
	regmap_write(crt_base, SYS_PLL_SD1, reg);

	regmap_write(crt_base, SYS_PLL_SD3, ssc);

	regmap_read(crt_base, SYS_PLL_SD2, &sscpll);
	sscpll &= ~(RTD13XXE_PI_IBSELH | RTD13XXE_SSC_PLL_ICP |
		    RTD13XXE_SSC_PLL_RS | RTD13XXE_SSC_DIV_EXT_F);

	switch (FIELD_GET(RTD13XXE_SSC_DIV_N, ssc)) {
	case RTD13XXE_SSC_DIV_N_208M:
		sscpll |= FIELD_PREP(RTD13XXE_PI_IBSELH, RTD13XXE_PI_IBSELH_150_255M) |
			  FIELD_PREP(RTD13XXE_SSC_PLL_ICP, RTD13XXE_SSC_PLL_ICP_INIT_VALUE) |
			  FIELD_PREP(RTD13XXE_SSC_PLL_RS, RTD13XXE_SSC_PLL_RS_8K) |
			  FIELD_PREP(RTD13XXE_SSC_DIV_EXT_F, RTD13XXE_SSC_DIV_EXT_F_208M);
		break;
	case RTD13XXE_SSC_DIV_N_200M:
		sscpll |= FIELD_PREP(RTD13XXE_PI_IBSELH, RTD13XXE_PI_IBSELH_150_255M) |
			  FIELD_PREP(RTD13XXE_SSC_PLL_ICP, RTD13XXE_SSC_PLL_ICP_INIT_VALUE) |
			  FIELD_PREP(RTD13XXE_SSC_PLL_RS, RTD13XXE_SSC_PLL_RS_8K) |
			  FIELD_PREP(RTD13XXE_SSC_DIV_EXT_F, RTD13XXE_SSC_DIV_EXT_F_200M);
		break;
	case RTD13XXE_SSC_DIV_N_100M:
		sscpll |= FIELD_PREP(RTD13XXE_PI_IBSELH, RTD13XXE_PI_IBSELH_80_150M) |
			  FIELD_PREP(RTD13XXE_SSC_PLL_ICP, RTD13XXE_SSC_PLL_ICP_INIT_VALUE) |
			  FIELD_PREP(RTD13XXE_SSC_PLL_RS, RTD13XXE_SSC_PLL_RS_6K) |
			  FIELD_PREP(RTD13XXE_SSC_DIV_EXT_F, RTD13XXE_SSC_DIV_EXT_F_100M);
		break;
	}

	regmap_write(crt_base, SYS_PLL_SD2, sscpll);
	mdelay(2);

	regmap_read(crt_base, SYS_PLL_SD4, &reg);
	reg &= ~SSC_PLL_POW;
	reg |= SSC_RSTB | SSC_PLL_RSTB;
	regmap_write(crt_base, SYS_PLL_SD4, reg);

	regmap_read(crt_base, SYS_PLL_SD2, &reg);
	reg |= RTD13XXE_SSC_OC_EN;
	regmap_write(crt_base, SYS_PLL_SD2, reg);

	regmap_read(crt_base, SYS_PLL_SD4, &reg);
	reg |= SSC_PLL_POW;
	regmap_write(crt_base, SYS_PLL_SD4, reg);
	udelay(200);

	regmap_read(crt_base, SYS_PLL_SD2, &reg);
	reg &= ~RTD13XXE_SSC_OC_EN;
	regmap_write(crt_base, SYS_PLL_SD2, reg);

	regmap_read(crt_base, SYS_PLL_SD1, &reg);
	reg |= PHRT0;
	regmap_write(crt_base, SYS_PLL_SD1, reg);
	udelay(100);

	if (!down_clk_tuning && !(rtkquirks & RTKQUIRK_SSC_CLK_JITTER)) {
		if (FIELD_GET(RTD13XXE_SSC_DIV_N, ssc) == RTD13XXE_SSC_DIV_N_208M) {
			dev_dbg(mmc_dev(host->mmc), "%s enable ssc function\n", __func__);
			rtkstb_sdhci_set_ssc(host);
		}
	}
}

static void sdhci_rtkstb_hw_init(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);
	struct regmap *crt_base = rtk_host->crt_base;
	struct regmap *iso_base = rtk_host->iso_base;
	struct device *dev = mmc_dev(host->mmc);
	struct regmap *pinctrl_base = rtk_host->pinctrl_base;
	void __iomem *wrap_base = rtk_host->wrap_base;

	u32 reg;

	regmap_update_bits(iso_base, ISO_CTRL_1, SD3_SSCLDO_EN |
			   SD3_BIAS_EN, SD3_SSCLDO_EN | SD3_BIAS_EN);
	udelay(200);

	regmap_read(crt_base, SYS_PLL_SD1, &reg);
	reg |= PHRT0;
	regmap_write(crt_base, SYS_PLL_SD1, reg);

	regmap_read(crt_base, SYS_PLL_SD3, &reg);

	if (soc_device_match(rtk_soc_parker)) {
		reg |= FIELD_PREP(SSC_TBASE, SSC_TBASE_INIT_VALUE) |
		       FIELD_PREP(SSC_STEP_IN, SSC_STEP_IN_INIT_VALUE) |
		       FIELD_PREP(SSC_DIV_N, SSC_DIV_N_200M);
		rtkstb_sdhci_pll_configure(host, reg, 0);
	} else {
		reg |= FIELD_PREP(RTD13XXE_SSC_DIV_N, RTD13XXE_SSC_DIV_N_200M);
		rtkstb_13xxe_sdhci_pll_configure(host, reg, 0);
	}

	if(gpiod_direction_output(rtk_host->sd_pwr_gpio, 0))
		dev_err(dev, "fail to enable sdmmc power");

	mdelay(10);

	pinctrl_select_state(rtk_host->pinctrl, rtk_host->pins_pud_cfg);

	reg = readl(wrap_base + SDHCI_RTK_ISREN);
	reg &= ~(INT1_EN | INT3_EN);
	reg |= ISREN_WRITE_DATA | INT4_EN;
	writel(reg, wrap_base + SDHCI_RTK_ISREN);

	regmap_update_bits(pinctrl_base, ISO_DBG_STATUS, SDIO0_H3L1_DETECT_EN,
			   SDIO0_H3L1_DETECT_EN);
}

static void rtkstb_sdhci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sdhci_host *host = mmc_priv(mmc);

	rtkstb_sdhci_set_cmd_timeout_irq(host, true);

	sdhci_request(mmc, mrq);
}

static void rtkstb_sdhci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);
	struct regmap *crt_base = rtk_host->crt_base;

	u32 ssc;

	sdhci_set_ios(mmc, ios);

	rtkstb_sdhci_pad_driving_configure(host);

	regmap_read(crt_base, SYS_PLL_SD3, &ssc);

	if (soc_device_match(rtk_soc_parker)) {
		ssc &= ~SSC_DIV_N;
	} else {
		ssc &= ~RTD13XXE_SSC_DIV_N;
	}

	switch (host->timing) {
	case MMC_TIMING_SD_HS:
		dev_err(mmc_dev(host->mmc), "high speed mode\n");
		if (soc_device_match(rtk_soc_parker)) {
			ssc |= FIELD_PREP(SSC_DIV_N, SSC_DIV_N_200M);
			rtkstb_sdhci_pll_configure(host, ssc, 0);
		} else {
			ssc |= FIELD_PREP(RTD13XXE_SSC_DIV_N, RTD13XXE_SSC_DIV_N_200M);
			rtkstb_13xxe_sdhci_pll_configure(host, ssc, 0);
		}
		break;
	case MMC_TIMING_UHS_SDR12:
	case MMC_TIMING_UHS_SDR25:
		dev_err(mmc_dev(host->mmc), "SDR25 mode\n");
		if (soc_device_match(rtk_soc_parker)) {
			ssc |= FIELD_PREP(SSC_DIV_N, SSC_DIV_N_200M);
			rtkstb_sdhci_pll_configure(host, ssc, 0);
		} else {
			ssc |= FIELD_PREP(RTD13XXE_SSC_DIV_N, RTD13XXE_SSC_DIV_N_200M);
			rtkstb_13xxe_sdhci_pll_configure(host, ssc, 0);
		}
		break;
	case MMC_TIMING_UHS_SDR50:
		dev_err(mmc_dev(host->mmc), "SDR50 mode\n");
		if (soc_device_match(rtk_soc_parker)) {
			ssc |= FIELD_PREP(SSC_DIV_N, SSC_DIV_N_200M);
			rtkstb_sdhci_pll_configure(host, ssc, 0);
		} else {
			ssc |= FIELD_PREP(RTD13XXE_SSC_DIV_N, RTD13XXE_SSC_DIV_N_200M);
			rtkstb_13xxe_sdhci_pll_configure(host, ssc, 0);
		}
		break;
	case MMC_TIMING_UHS_SDR104:
		dev_err(mmc_dev(host->mmc), "SDR104 mode\n");
		if (soc_device_match(rtk_soc_parker)) {
			ssc |= FIELD_PREP(SSC_DIV_N, SSC_DIV_N_208M);
			rtkstb_sdhci_pll_configure(host, ssc, 0);
		} else {
			ssc |= FIELD_PREP(RTD13XXE_SSC_DIV_N, RTD13XXE_SSC_DIV_N_208M);
			rtkstb_13xxe_sdhci_pll_configure(host, ssc, 0);
		}
		break;
	}

	if (host->timing > MMC_TIMING_LEGACY)
		rtkstb_sdhci_set_bit(rtk_host, SDHCI_RTK_DUTY_CTRL, SDIO_STOP_CLK_EN);
}

static u32 rtkstb_sdhci_irq(struct sdhci_host *host, u32 intmask)
{
	u32 command;
	u16 clk;

	if (host->mmc->card != NULL)
		return intmask;

	if (host->cmd != NULL) {
		rtkstb_sdhci_set_cmd_timeout_irq(host, true);
		return intmask;
	}

	if (intmask & SDHCI_INT_TIMEOUT) {
		command = SDHCI_GET_CMD(sdhci_readw(host, SDHCI_COMMAND));
		clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	} else {
		return intmask;
	}

	if (!clk && (command == MMC_GO_IDLE_STATE))
		rtkstb_sdhci_set_cmd_timeout_irq(host, false);

	if (command == MMC_GO_IDLE_STATE || command == SD_SEND_IF_COND ||
	    command == SD_IO_SEND_OP_COND || command == SD_IO_RW_DIRECT ||
	    command == MMC_APP_CMD || command == MMC_SEND_OP_COND) {
		intmask &= ~SDHCI_INT_TIMEOUT;
		sdhci_writel(host, SDHCI_INT_TIMEOUT, SDHCI_INT_STATUS);
		del_timer(&host->timer);
	}

	return intmask;
}

static int rtkstb_mmc_send_tuning(struct mmc_host *mmc, u32 opcode, int tx)
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

static int rtkstb_sdhci_change_phase(struct sdhci_host *host, u8 sample_point, int tx)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);
	struct regmap *crt_base = rtk_host->crt_base;

	u32 reg = 0;
	u16 clk = 0;

	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk &= ~SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
	/* To disable reset signal */
	regmap_read(crt_base, SYS_PLL_SD1, &reg);
	reg &= ~PHRT0;
	regmap_write(crt_base, SYS_PLL_SD1, reg);

	regmap_read(crt_base, SYS_PLL_SD1, &reg);

	if (tx) {
		reg &= ~PHSEL0_MASK;
		reg |= (sample_point << PHSEL0_SHIFT);
	} else {
		reg &= ~PHSEL1_MASK;
		reg |= (sample_point << PHSEL1_SHIFT);
	}

	regmap_write(crt_base, SYS_PLL_SD1, reg);
	/* re-enable reset signal */
	regmap_read(crt_base, SYS_PLL_SD1, &reg);
	reg |= PHRT0;
	regmap_write(crt_base, SYS_PLL_SD1, reg);
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

static u8 rtkstb_sdhci_search_final_phase(struct sdhci_host *host, u32 phase_map, int tx)
{
	int start = 0, len = 0;
	int start_final = 0, len_final = 0;
	u8 final_phase = 0xFF;

	if (phase_map == 0) {
		dev_err(mmc_dev(host->mmc), "phase error: [map:%08x]\n", phase_map);
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

	dev_err(mmc_dev(host->mmc), "%s phase: [map:%08x] [maxlen:%d] [final:%d]\n",
		tx ? "tx" : "rx", phase_map, len_final, final_phase);

	return final_phase;
}

static int rtkstb_sdhci_tuning(struct sdhci_host *host, u32 opcode, int tx)
{
	struct mmc_host *mmc = host->mmc;
	int err, i, sample_point;
	u32 raw_phase_map[TUNING_CNT] = {0}, phase_map;
	u8 final_phase = 0;

	for (sample_point = 0; sample_point < MAX_PHASE; sample_point++) {
		for (i = 0; i < TUNING_CNT; i++) {
			rtkstb_sdhci_change_phase(host, (u8) sample_point, tx);
			err = rtkstb_mmc_send_tuning(mmc, opcode, tx);
			if (err == 0)
				raw_phase_map[i] |= (1 << sample_point);
		}
	}

	phase_map = 0xFFFFFFFF;
	for (i = 0; i < TUNING_CNT; i++) {
		dev_dbg(mmc_dev(host->mmc), "%s raw_phase_map[%d] = 0x%08x\n",
			tx ? "tx" : "rx", i, raw_phase_map[i]);
		phase_map &= raw_phase_map[i];
	}

	if (phase_map) {
		final_phase = rtkstb_sdhci_search_final_phase(host, phase_map, tx);
		if (final_phase == 0xFF) {
			dev_err(mmc_dev(host->mmc), "final phase invalid\n");
			return -EINVAL;
		}

		err = rtkstb_sdhci_change_phase(host, final_phase, tx);
		if (err < 0)
			return err;
	} else {
		dev_err(mmc_dev(host->mmc), "tuning fail, phase map unavailable\n");
		return -EINVAL;
	}

	return 0;
}

static void rtkstb_sdhci_down_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);
	struct regmap *crt_base = rtk_host->crt_base;

	u32 ssc;

	regmap_read(crt_base, SYS_PLL_SD3, &ssc);

	if (soc_device_match(rtk_soc_parker)) {
		switch (FIELD_GET(SSC_DIV_N, ssc)) {
		case SSC_DIV_N_208M:
			ssc &= ~SSC_DIV_N;
			ssc |= FIELD_PREP(SSC_DIV_N, SSC_DIV_N_200M);
			rtkstb_sdhci_pll_configure(host, ssc, 1);
			break;
		case SSC_DIV_N_200M:
			ssc &= ~SSC_DIV_N;
			ssc |= FIELD_PREP(SSC_DIV_N, SSC_DIV_N_100M);
			rtkstb_sdhci_pll_configure(host, ssc, 1);
			break;
		case SSC_DIV_N_100M:
			ssc &= ~SSC_DIV_N;
			ssc |= FIELD_PREP(SSC_DIV_N, SSC_DIV_N_50M);
			rtkstb_sdhci_pll_configure(host, ssc, 1);
			break;
		}
	} else {
		switch (FIELD_GET(RTD13XXE_SSC_DIV_N, ssc)) {
		case RTD13XXE_SSC_DIV_N_208M:
			ssc &= ~RTD13XXE_SSC_DIV_N;
			ssc |= FIELD_PREP(RTD13XXE_SSC_DIV_N, RTD13XXE_SSC_DIV_N_200M);
			rtkstb_13xxe_sdhci_pll_configure(host, ssc, 1);
			dev_err(mmc_dev(host->mmc), "%s down clock to 200M\n", __func__);
			break;
		case RTD13XXE_SSC_DIV_N_200M:
			ssc &= ~RTD13XXE_SSC_DIV_N;
			ssc |= FIELD_PREP(RTD13XXE_SSC_DIV_N, RTD13XXE_SSC_DIV_N_100M);
			rtkstb_13xxe_sdhci_pll_configure(host, ssc, 1);
			dev_err(mmc_dev(host->mmc), "%s down clock to 100M\n", __func__);
			break;
		case RTD13XXE_SSC_DIV_N_100M:
			ssc &= ~RTD13XXE_SSC_DIV_N;
			ssc |= FIELD_PREP(RTD13XXE_SSC_DIV_N, RTD13XXE_SSC_DIV_N_50M);
			rtkstb_13xxe_sdhci_pll_configure(host, ssc, 1);
			break;
		}
	}
}

static int rtkstb_sdhci_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_rtkstb_soc_data *soc_data = rtk_host->soc_data;
	struct regmap *crt_base = rtk_host->crt_base;
	void __iomem *wrap_base =  rtk_host->wrap_base;
	u32 rtkquirks = soc_data->rtkquirks;
	u32 reg, tx_opcode;
	int ret = 0;

	dev_info(mmc_dev(host->mmc), "execute clock phase tuning\n");
	/* To disable the SSC during the phase tuning process. */
	if (soc_device_match(rtk_soc_parker)) {
		regmap_read(crt_base, SYS_PLL_SD2, &reg);
		reg &= ~SSC_DEPTH;
		regmap_write(crt_base, SYS_PLL_SD2, reg);
	} else {
		reg = readl(wrap_base + SDHCI_RTK_SSC1);
		reg &= ~EN_SSC;
		writel(reg, wrap_base + SDHCI_RTK_SSC1);
	}

	if (mmc_card_sdio(host->mmc->card))
		tx_opcode = SD_IO_RW_DIRECT;
	else
		tx_opcode = MMC_SEND_STATUS;

	ret = rtkstb_sdhci_tuning(host, tx_opcode, 1);
	if (ret)
		dev_err(mmc_dev(host->mmc), "tx tuning fail\n");

	do {
		ret = rtkstb_sdhci_tuning(host, MMC_SEND_TUNING_BLOCK, 0);
		if (ret) {
			regmap_read(crt_base, SYS_PLL_SD3, &reg);
			if ((FIELD_GET(SSC_DIV_N, reg) == SSC_DIV_N_50M) ||
			    (FIELD_GET(RTD13XXE_SSC_DIV_N, reg) == RTD13XXE_SSC_DIV_N_50M)) {
				dev_err(mmc_dev(host->mmc), "rx tuning fail\n");
				return ret;
			}

			dev_err(mmc_dev(host->mmc), "down clock, and rx retuning\n");
			rtkstb_sdhci_down_clock(host);
		}
	} while (ret);

	if (!(rtkquirks & RTKQUIRK_SSC_CLK_JITTER)) {
		if (soc_device_match(rtk_soc_parker)) {
			regmap_read(crt_base, SYS_PLL_SD2, &reg);
			reg |= FIELD_PREP(SSC_DEPTH, SSC_DEPTH_1_N);
			regmap_write(crt_base, SYS_PLL_SD2, reg);
		} else {
			regmap_read(crt_base, SYS_PLL_SD3, &reg);
			if (FIELD_GET(RTD13XXE_SSC_DIV_N, reg) == RTD13XXE_SSC_DIV_N_208M) {
				reg = readl(wrap_base + SDHCI_RTK_SSC1);
				reg |= EN_SSC;
				writel(reg, wrap_base + SDHCI_RTK_SSC1);
			}
		}
	}

	regmap_read(crt_base, SYS_PLL_SD3, &reg);

	if (soc_device_match(rtk_soc_parker))
		dev_info(mmc_dev(host->mmc), "after tuning, current pll = %lx\n",
			 FIELD_GET(SSC_DIV_N, reg));
	else
		dev_info(mmc_dev(host->mmc), "after tuning, current pll = %lx\n",
			 FIELD_GET(RTD13XXE_SSC_DIV_N, reg));

	return 0;
}

static void rtkstb_sdhci_set_uhs_signaling(struct sdhci_host *host, unsigned timing)
{
	u16 ctrl_2;

	sdhci_set_uhs_signaling(host, timing);

	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);

	if (timing > MMC_TIMING_SD_HS) {
		ctrl_2 |= SDHCI_CTRL_VDD_180;
		sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
	}
}

static void rtkstb_sdhci_card_event(struct sdhci_host *host)
{
	int err = 0;

	dev_err(mmc_dev(host->mmc), "card event detected\n");

	err = rtkstb_sdhci_pad_power_ctrl(host, MMC_SIGNAL_VOLTAGE_330);
	if (err)
		dev_err(mmc_dev(host->mmc), "reset voltage to 3.3v fail\n");

	if (mmc_gpio_get_cd(host->mmc))
		dev_err(mmc_dev(host->mmc), "card insert\n");
	else
		dev_err(mmc_dev(host->mmc), "card remove\n");
}

static void rtkstb_sdhci_voltage_switch(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);
	int err = 0;

	rtkstb_sdhci_clear_bit(rtk_host, SDHCI_RTK_DUTY_CTRL, SDIO_STOP_CLK_EN);

	err = rtkstb_sdhci_pad_power_ctrl(host, MMC_SIGNAL_VOLTAGE_180);
	if (!err)
		dev_err(mmc_dev(host->mmc), "voltage switch to 1.8v\n");
}

static void rtkstb_sdhci_adma_write_desc(struct sdhci_host *host, void **desc, dma_addr_t addr,
					 int len, unsigned int cmd)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_rtkstb_soc_data *soc_data = rtk_host->soc_data;
	u32 rtkquirks = soc_data->rtkquirks;
	int tmplen, offset;

	if (likely(!(rtkquirks & RTKQUIRK_AXI_BURST_BOUNDARY) || !(mmc_card_sdio(host->mmc->card)) ||
	    !len || BOUNDARY_OK(addr, len))) {
		sdhci_adma_write_desc(host, desc, addr, len, cmd);
		return;
	}

	dev_dbg(mmc_dev(host->mmc), "descriptor splitting, addr %pad, len %d\n", &addr, len);

	offset = addr & (SZ_4K - 1);
	tmplen = SZ_4K - offset;
	sdhci_adma_write_desc(host, desc, addr, tmplen, cmd);

	addr += tmplen;
	len -= tmplen;
	sdhci_adma_write_desc(host, desc, addr, len, cmd);
}

static void rtkstb_sdhci_request_done(struct sdhci_host *host, struct mmc_request *mrq)
{
	rtkstb_sdhci_set_cmd_timeout_irq(host, true);

	mmc_request_done(host->mmc, mrq);
}

/* Update card information to determine SD/SDIO tx tuning function */
static void rtkstb_init_card(struct mmc_host *mmc, struct mmc_card *card)
{
	mmc->card = card;
}

static void rtkstb_replace_mmc_host_ops(struct sdhci_host *host)
{
	host->mmc_host_ops.request	= rtkstb_sdhci_request;
	host->mmc_host_ops.set_ios	= rtkstb_sdhci_set_ios;
	host->mmc_host_ops.init_card	= rtkstb_init_card;
}

static const struct sdhci_ops rtkstb_sdhci_ops = {
	.set_clock	= sdhci_set_clock,
	.irq 		= rtkstb_sdhci_irq,
	.set_bus_width	= sdhci_set_bus_width,
	.reset		= sdhci_reset,
	.platform_execute_tuning = rtkstb_sdhci_execute_tuning,
	.set_uhs_signaling = rtkstb_sdhci_set_uhs_signaling,
	.card_event	= rtkstb_sdhci_card_event,
	.voltage_switch = rtkstb_sdhci_voltage_switch,
	.adma_write_desc = rtkstb_sdhci_adma_write_desc,
	.request_done	= rtkstb_sdhci_request_done,
};

static const struct sdhci_pltfm_data sdhci_rtkstb_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK_SINGLE_POWER_WRITE |
		  SDHCI_QUIRK_NO_HISPD_BIT |
		  SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12 |
		  SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
		  SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC,
	.quirks2 = SDHCI_QUIRK2_BROKEN_DDR50 |
		   SDHCI_QUIRK2_ACMD23_BROKEN |
		   SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.ops = &rtkstb_sdhci_ops,
};

static struct sdhci_rtkstb_soc_data rtd1319d_soc_data = {
	.pdata = &sdhci_rtkstb_pdata,
	.rtkquirks = RTKQUIRK_SSC_CLK_JITTER |
		     RTKQUIRK_AXI_BURST_BOUNDARY,
};

static struct sdhci_rtkstb_soc_data rtd13xxe_soc_data = {
	.pdata = &sdhci_rtkstb_pdata,
	.rtkquirks = RTKQUIRK_AXI_BURST_BOUNDARY,
};

static const struct of_device_id sdhci_rtkstb_dt_match[] = {
	{.compatible = "realtek,rtd1319d-sdmmc", .data = &rtd1319d_soc_data},
	{.compatible = "realtek,rtd13xxe-sdhci", .data = &rtd13xxe_soc_data},
	{}
};
MODULE_DEVICE_TABLE(of, sdhci_rtkstb_dt_match);

static int sdhci_rtkstb_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct sdhci_rtkstb_soc_data *soc_data;
	struct device_node *node = pdev->dev.of_node;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_rtkstb *rtk_host;
	int ret;

	dev_info(&pdev->dev, " build at : %s\n", utsname()->version);

	match = of_match_device(sdhci_rtkstb_dt_match, &pdev->dev);
	if (!match)
		return -EINVAL;

	soc_data = match->data;

	host = sdhci_pltfm_init(pdev, soc_data->pdata, sizeof(*rtk_host));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	rtk_host = sdhci_pltfm_priv(pltfm_host);
	rtk_host->soc_data = soc_data;

	rtkstb_replace_mmc_host_ops(host);

	rtk_host->wrap_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(rtk_host->wrap_base)) {
		dev_err(&pdev->dev, "%s failed to map wrapper control space\n", __func__);
		ret = PTR_ERR(rtk_host->wrap_base);
		goto err_free_pltfm;
	}

	rtk_host->dhc_base = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(rtk_host->dhc_base)) {
		dev_err(&pdev->dev, "%s failed to map dhc space\n", __func__);
		ret = PTR_ERR(rtk_host->dhc_base);
		goto err_free_pltfm;
	}

	rtk_host->crt_base = syscon_regmap_lookup_by_phandle(node, "realtek,crt");
	if (IS_ERR(rtk_host->crt_base)) {
		dev_err(&pdev->dev, "%s failed to map syscon CRT space\n", __func__);
		ret = -ENOMEM;
		goto err_free_pltfm;
	}

	rtk_host->iso_base = syscon_regmap_lookup_by_phandle(node, "realtek,iso");
	if (IS_ERR(rtk_host->iso_base)) {
		dev_err(&pdev->dev, "%s failed to map syscon ISO space\n", __func__);
		ret = -ENOMEM;
		goto err_free_pltfm;
	}

	rtk_host->pinctrl_base = syscon_regmap_lookup_by_phandle(node, "realtek,pinctrl");
	if (IS_ERR(rtk_host->pinctrl_base)) {
		dev_err(&pdev->dev, "%s failed to map syscon pinctrl space\n", __func__);
		ret = -ENOMEM;
		goto err_free_pltfm;
	}

	if (device_property_read_bool(&pdev->dev, "rtkwifi-sd-rst")) {
		rtk_host->wifi_rst = devm_gpiod_get(&pdev->dev, "wifi-rst", GPIOD_OUT_LOW);
		if (IS_ERR(rtk_host->wifi_rst))
			dev_err(&pdev->dev, "%s rtk wifi sd reset gpio invalid\n", __func__);
		else
			rtkstb_sdhci_wifi_device_reset(host);
	}

	rtk_host->sd_pwr_gpio = devm_gpiod_get(&pdev->dev, "sd-power", GPIOD_OUT_LOW);
	if (IS_ERR(rtk_host->sd_pwr_gpio)) {
		dev_err(&pdev->dev, "%s can't request power gpio\n", __func__);
		ret = PTR_ERR(rtk_host->sd_pwr_gpio);
		goto err_free_pltfm;
	}

	rtk_host->sd_rstn = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(rtk_host->sd_rstn)) {
		dev_err(&pdev->dev, "%s can't request reset control\n", __func__);
		ret = PTR_ERR(rtk_host->sd_rstn);
		goto err_free_pltfm;
	}

	rtk_host->sd_clk_en = devm_clk_get(&pdev->dev, "sd");
	if (IS_ERR(rtk_host->sd_clk_en)) {
		dev_err(&pdev->dev, "%s can't request clock\n", __func__);
		ret = PTR_ERR(rtk_host->sd_clk_en);
		goto err_free_pltfm;
	}

	rtk_host->sd_ip_clk_en = devm_clk_get(&pdev->dev, "sd_ip");
	if (IS_ERR(rtk_host->sd_ip_clk_en)) {
		dev_err(&pdev->dev, "%s can't request IP clock\n", __func__);
		ret = PTR_ERR(rtk_host->sd_clk_en);
		goto err_free_pltfm;
	}

	rtk_host->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(rtk_host->pinctrl)) {
		dev_err(&pdev->dev, "no pinctrl\n");
		ret = PTR_ERR(rtk_host->pinctrl);
		goto err_free_pltfm;
	}

	rtk_host->pins_default = pinctrl_lookup_state(rtk_host->pinctrl, PINCTRL_STATE_DEFAULT);
	if (IS_ERR(rtk_host->pins_default))
		dev_warn(&pdev->dev, "could not get default state\n");

	rtk_host->pins_vsel_3v3 = pinctrl_lookup_state(rtk_host->pinctrl, "sd-vsel-3v3");
	if (IS_ERR(rtk_host->pins_vsel_3v3))
		dev_warn(&pdev->dev, "could not get state sd-vsel-3v3\n");

	rtk_host->pins_vsel_1v8 = pinctrl_lookup_state(rtk_host->pinctrl, "sd-vsel-1v8");
	if (IS_ERR(rtk_host->pins_vsel_1v8))
		dev_warn(&pdev->dev, "could not get state sd-vsel-1v8\n");

	rtk_host->pins_3v3_drv = pinctrl_lookup_state(rtk_host->pinctrl, "sd-3v3-drv");
	if (IS_ERR(rtk_host->pins_3v3_drv))
		dev_warn(&pdev->dev, "could not get state sd-3v3-drv\n");

	rtk_host->pins_1v8_drv = pinctrl_lookup_state(rtk_host->pinctrl, "sd-1v8-drv");
	if (IS_ERR(rtk_host->pins_1v8_drv))
		dev_warn(&pdev->dev, "could not get state sd-1v8-drv\n");

	rtk_host->pins_pud_cfg = pinctrl_lookup_state(rtk_host->pinctrl, "sd-pud-cfg");
	if (IS_ERR(rtk_host->pins_pud_cfg))
		dev_warn(&pdev->dev, "could not get state sd-pud-cfg\n");

	ret = reset_control_deassert(rtk_host->sd_rstn);
	if (ret) {
		dev_err(&pdev->dev, " %s can't deassert reset\n", __func__);
		goto err_free_pltfm;
	}

	ret = clk_prepare_enable(rtk_host->sd_clk_en);
	if (ret) {
		dev_err(&pdev->dev, " %s can't enable clock\n", __func__);
		goto err_deassert_rst;
	}

	ret = clk_prepare_enable(rtk_host->sd_ip_clk_en);
	if (ret) {
		dev_err(&pdev->dev, " %s can't enable IP clock\n", __func__);
		goto err_enable_clk;
	}

	sdhci_rtkstb_hw_init(host);

	ret = mmc_of_parse(host->mmc);
	if (ret) {
		dev_err(&pdev->dev, "%s parsing dt failed\n", __func__);
		goto err_enable_ip_clk;
	}

	ret = sdhci_add_host(host);
	if (ret)
		goto err_enable_ip_clk;

	return 0;

err_enable_ip_clk:
	clk_disable_unprepare(rtk_host->sd_ip_clk_en);
err_enable_clk:
	clk_disable_unprepare(rtk_host->sd_clk_en);
err_deassert_rst:
	reset_control_assert(rtk_host->sd_rstn);
err_free_pltfm:
	sdhci_pltfm_free(pdev);

	return ret;
}

static void sdhci_rtkstb_disable_pll_clk(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);
	struct regmap *crt_base = rtk_host->crt_base;
	u32 reg;

	regmap_read(crt_base, SYS_PLL_SD1, &reg);
	rtk_host->preset_pll = reg;
	reg &= ~PHRT0;
	regmap_write(crt_base, SYS_PLL_SD1, reg);

	regmap_read(crt_base, SYS_PLL_SD4, &reg);
	reg &= ~(SSC_RSTB | SSC_PLL_RSTB | SSC_PLL_POW);
	regmap_write(crt_base, SYS_PLL_SD4, reg);

	if (!IS_ERR(rtk_host->sd_clk_en))
		clk_disable_unprepare(rtk_host->sd_clk_en);

	if (!IS_ERR(rtk_host->sd_ip_clk_en))
		clk_disable_unprepare(rtk_host->sd_ip_clk_en);

	reset_control_assert(rtk_host->sd_rstn);
}

static int sdhci_rtkstb_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	int dead = (readl(host->ioaddr + SDHCI_INT_STATUS) == 0xffffffff);

	dev_warn(&pdev->dev, "%s\n", __func__);

	sdhci_remove_host(host, dead);
	sdhci_pltfm_free(pdev);

	return 0;
}

static void sdhci_rtkstb_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sdhci_host *host = dev_get_drvdata(dev);

	dev_warn(dev, "%s\n", __func__);

	sdhci_suspend_host(host);

	sdhci_rtkstb_disable_pll_clk(host);
}

static int __maybe_unused sdhci_rtkstb_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);

	dev_warn(dev, "%s start\n", __func__);

	sdhci_suspend_host(host);

	sdhci_rtkstb_disable_pll_clk(host);

	dev_warn(dev, "%s finish\n", __func__);

	return 0;
}

static int __maybe_unused sdhci_rtkstb_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_rtkstb *rtk_host = sdhci_pltfm_priv(pltfm_host);
	struct regmap *crt_base = rtk_host->crt_base;
	u32 val;
	u8 reg;
	int ret;

	host->clock = 0;

	dev_warn(dev, "%s start\n", __func__);

	reset_control_deassert(rtk_host->sd_rstn);

	ret = clk_prepare_enable(rtk_host->sd_clk_en);
	if (ret) {
		dev_err(dev, " %s can't enable clock\n", __func__);
		goto err_enable_clk;
	}

	ret = clk_prepare_enable(rtk_host->sd_ip_clk_en);
	if (ret) {
		dev_err(dev, " %s can't enable IP clock\n", __func__);
		goto err_enable_ip_clk;
	}

	regmap_read(crt_base, SYS_PLL_SD4, &val);
	val |= SSC_RSTB | SSC_PLL_RSTB | SSC_PLL_POW;
	regmap_write(crt_base, SYS_PLL_SD4, val);
	udelay(10);
	regmap_write(crt_base, SYS_PLL_SD1, rtk_host->preset_pll);

	sdhci_rtkstb_hw_init(host);

	reg = sdhci_readb(host, SDHCI_POWER_CONTROL);
	reg |= SDHCI_POWER_ON | SDHCI_POWER_330;
	sdhci_writeb(host, reg, SDHCI_POWER_CONTROL);

	sdhci_resume_host(host);

	dev_warn(dev, "%s finish\n", __func__);

	return 0;

err_enable_ip_clk:
	clk_disable_unprepare(rtk_host->sd_ip_clk_en);
err_enable_clk:
	clk_disable_unprepare(rtk_host->sd_clk_en);

	return ret;
}

static SIMPLE_DEV_PM_OPS(sdhci_rtkstb_dev_pm_ops, sdhci_rtkstb_suspend,
			 sdhci_rtkstb_resume);

static struct platform_driver sdhci_rtkstb_driver = {
	.driver		= {
		.name	= "sdhci-rtkstb",
		.of_match_table = sdhci_rtkstb_dt_match,
		.pm	= &sdhci_rtkstb_dev_pm_ops,
	},
	.probe		= sdhci_rtkstb_probe,
	.remove		= sdhci_rtkstb_remove,
	.shutdown	= sdhci_rtkstb_shutdown,
};

module_platform_driver(sdhci_rtkstb_driver);

MODULE_DESCRIPTION("SDHCI platform driver for Realtek DHC STB SoC");
MODULE_LICENSE("GPL");
