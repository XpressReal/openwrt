// SPDX-License-Identifier: GPL-2.0+
/*
 * Realtek SDIO host driver
 *
 * Copyright (c) 2017-2020 Realtek Semiconductor Corp.
 */

#ifndef _DRIVERS_MMC_SDHCI_OF_RTK_H
#define _DRIVERS_MMC_SDHCI_OF_RTK_H

#define MAX_PHASE			32
#define TUNING_CNT			3
#define MINIMUM_CONTINUE_LENGTH		16

/* Controller needs to take care AXI burst boundary */
#define RTKQUIRK_AXI_BURST_BOUNDARY	BIT(0)

#define SDIO_LOC_0			0
#define SDIO_LOC_1			1

/* CRT register */
#define SYS_PLL_SDIO1			0x1A0
#define  PHRT0				BIT(1)
#define  PHSEL0_MASK			GENMASK(7, 3)
#define  PHSEL0_SHIFT			3
#define  PHSEL1_MASK			GENMASK(12, 8)
#define  PHSEL1_SHIFT			8
#define SYS_PLL_SDIO2			0x1A4
#define  REG_TUNE11			GENMASK(2, 1)
#define  REG_TUNE11_1V9			0x2
#define  SSCPLL_CS1			GENMASK(4, 3)
#define  SSCPLL_CS1_INIT_VALUE		0x1
#define  SSCPLL_ICP			GENMASK(9, 5)
#define  SSCPLL_ICP_10U			0x01
#define  SSCPLL_ICP_20U			0x03
#define  SSCPLL_RS			GENMASK(12, 10)
#define  SSCPLL_RS_10K			0x4
#define  SSCPLL_RS_13K			0x5
#define  SSC_DEPTH			GENMASK(15, 13)
#define  SSC_DEPTH_1_N			0x3
#define  SSC_8X_EN			BIT(16)
#define  SSC_DIV_EXT_F			GENMASK(25, 18)
#define  SSC_DIV_EXT_F_50M		0x71
#define  SSC_DIV_EXT_F_100M		0xE3
#define  SSC_DIV_EXT_F_200M		0x0
#define  SSC_DIV_EXT_F_208M		0xE3
#define  EN_CPNEW			BIT(26)
#define SYS_PLL_SDIO3			0x1A8
#define  SSC_TBASE			GENMASK(7, 0)
#define  SSC_TBASE_INIT_VALUE		0x88
#define  SSC_STEP_IN			GENMASK(14, 8)
#define  SSC_STEP_IN_INIT_VALUE		0x43
#define  SSC_DIV_N			GENMASK(25, 16)
#define  SSC_DIV_N_50M			0x28
#define  SSC_DIV_N_100M			0x56
#define  SSC_DIV_N_200M			0xAE
#define  SSC_DIV_N_208M			0xB6
#define SYS_PLL_SDIO4			0x1AC
#define  SSC_RSTB			BIT(0)
#define  SSC_PLL_RSTB			BIT(1)
#define  SSC_PLL_POW			BIT(2)
#define SYS_PLL_SD1			0x1E0
#define  REG_SEL3318_MASK		GENMASK(14, 13)
#define  REG_SEL3318_3V3		0x1
#define  REG_SEL3318_0V			0x2
#define  REG_SEL3318_1V8		0x3
#define SDIO_RTK_CTL			0x10A10
#define SDIO_RTK_ISREN			0x10A34
#define SDIO_RTK_DUMMY_SYS1		0x10A58

#endif /* _DRIVERS_MMC_SDHCI_OF_RTK_H */
