/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019 RealTek Inc.
 */

#ifndef _RTK_DSI_REG_H
#define _RTK_DSI_REG_H

//-------------------------------------
//	DSI Control Register
//-------------------------------------
#define CTRL_REG	0x000
#define INTE		0x010
#define INTS		0x014
#define TC0		0x100
#define TC1		0x104
#define	TC2		0x108
#define	TC3		0x10C
#define	TC4		0x110
#define	TC5		0x114
#define IDMA0		0x200
#define IDMA1		0x204
#define IDMA2		0x208
#define IDMA3		0x20C
#define TO0		0x300
#define TO1		0x304
#define TO2		0x308
#define TO3		0x30C
#define CMD_GO		0x400
#define CMD0		0x404
#define PAT_GEN		0x610
#define CLK_CONTINUE	0x708

//-------------------------------------
//	MIPI_DPHY_REG
//-------------------------------------
#define CLOCK_GEN	0x800
#define TX_DATA0	0x808
#define TX_DATA1	0x80C
#define TX_DATA2	0x810
#define TX_DATA3	0x814
#define SSC0		0x840
#define SSC1		0x844
#define SSC2		0x848
#define SSC3		0x84C
#define WATCHDOG	0x850
#define TX_SWAP		0x868
#define RX_SWAP		0x86C
#define MPLL		0xC00
#define DF		0xC0C

#endif /* _RTK_DSI_REG_H */
