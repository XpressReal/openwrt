// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * Copyright (c) 2020-2024 Realtek Semiconductor Corp.
 */

#include <linux/types.h>
#include <linux/crypto.h>

#define CP_OTP_LOAD		0x19c

/* for SCPU */
/* MCP General Registers */
#define MCP_CTRL		0x100
#define MCP_STATUS		0x104
#define MCP_EN			0x108
#define MCP_CTRL1		0x198

/* MCP Ring-Buffer Registers */
#define MCP_BASE		0x10c
#define MCP_LIMIT		0x110
#define MCP_RDPTR		0x114
#define MCP_WRPTR		0x118
#define MCP_DES_COUNT		0x134
#define MCP_DES_COMPARE		0x138

/* MCP Ini_Key Registers */
#define MCP_DES_INI_KEY		0x11C
#define MCP_AES_INI_KEY		0x124

/* CP Power Management */
#define PWM_CTRL		0x1e0

/* TP registers */
#define TP_KEY_INFO_0		0x58
#define TP_KEY_INFO_1		0x5c
#define TP_KEY_CTRL		0x60

#define MCP_KEY_SEL(x)		(((x) & 0x3) << 12)
#define MCP_KEY_SEL_DDR		0x3
#define MCP_KEY_SEL_CW		0x2
#define MCP_KEY_SEL_OTP		0x1
#define MCP_KEY_SEL_DESC	0x0

/* for register MCP_CTRL use */
#define MCP_WRITE_DATA_1	(0x01)
#define MCP_GO			(0x01 << 1)
#define MCP_IDEL		(0x01 << 2)
#define MCP_SWAP		(0x01 << 3)
#define MCP_CLEAR		(0x01 << 4)

#define MCP_RING_EMPTY		(0x01 << 1)
#define MCP_ERROR		(0x01 << 2)
#define MCP_COMPARE		(0x01 << 3)

#define MCP_KL_DONE		(0x01 << 20)
#define MCP_K_KL_DONE		(0x01 << 13)

/* crypto algorithm */
#define MCP_MODE(x)		((x) & 0x1f)

#define MCP_ALGO_DES		0x00
#define MCP_ALGO_3DES		0x01
#define MCP_ALGO_RC4		0x02
#define MCP_ALGO_MD5		0x03
#define MCP_ALGO_SHA_1		0x04
#define MCP_ALGO_AES		0x05
#define MCP_ALGO_AES_G		0x06
#define MCP_ALGO_AES_H		0x07
#define MCP_ALGO_CMAC		0x08
#define MCP_ALGO_SHA_256	0x0b
#define MCP_ALGO_AES_192	0x1d
#define MCP_ALGO_AES_256	0x15
#define MCP_ALGO_SHA_512	0x16

/* block cipher mode */
#define MCP_BCM(x)		(((x) & 0xf) << 6)
#define MCP_BCM_ECB		0x0
#define MCP_BCM_CBC		0x1
#define MCP_BCM_CTR		0x2
#define MCP_RC4			0x3

/* flag 0:decrypt 1:encrypt */
#define MCP_ENC(x)		(((x) & 0x1) << 5)

#define DES_ECB_ENC		(MCP_MODE(MCP_ALGO_DES) | MCP_BCM(MCP_BCM_ECB) | MCP_ENC(1))
#define DES_ECB_DEC		(MCP_MODE(MCP_ALGO_DES) | MCP_BCM(MCP_BCM_ECB) | MCP_ENC(0))
#define DES_CBC_ENC		(MCP_MODE(MCP_ALGO_DES) | MCP_BCM(MCP_BCM_CBC) | MCP_ENC(1))
#define DES_CBC_DEC		(MCP_MODE(MCP_ALGO_DES) | MCP_BCM(MCP_BCM_CBC) | MCP_ENC(0))

#define TDES_ECB_ENC		(MCP_MODE(MCP_ALGO_3DES) | MCP_BCM(MCP_BCM_ECB) | MCP_ENC(1))
#define TDES_ECB_DEC		(MCP_MODE(MCP_ALGO_3DES) | MCP_BCM(MCP_BCM_ECB) | MCP_ENC(0))
#define TDES_CBC_ENC		(MCP_MODE(MCP_ALGO_3DES) | MCP_BCM(MCP_BCM_CBC) | MCP_ENC(1))
#define TDES_CBC_DEC		(MCP_MODE(MCP_ALGO_3DES) | MCP_BCM(MCP_BCM_CBC) | MCP_ENC(0))

#define AES_ECB_ENC		(MCP_MODE(MCP_ALGO_AES) | MCP_BCM(MCP_BCM_ECB) | MCP_ENC(1))
#define AES_ECB_DEC		(MCP_MODE(MCP_ALGO_AES) | MCP_BCM(MCP_BCM_ECB) | MCP_ENC(0))
#define AES_CBC_ENC		(MCP_MODE(MCP_ALGO_AES) | MCP_BCM(MCP_BCM_CBC) | MCP_ENC(1))
#define AES_CBC_DEC		(MCP_MODE(MCP_ALGO_AES) | MCP_BCM(MCP_BCM_CBC) | MCP_ENC(0))
#define AES_CTR_ENC		(MCP_MODE(MCP_ALGO_AES) | MCP_BCM(MCP_BCM_CTR) | MCP_ENC(1))
#define AES_CTR_DEC		(MCP_MODE(MCP_ALGO_AES) | MCP_BCM(MCP_BCM_CTR) | MCP_ENC(0))

#define AES_192_ECB_ENC		(MCP_MODE(MCP_ALGO_AES_192) | MCP_BCM(MCP_BCM_ECB) | MCP_ENC(1))
#define AES_192_ECB_DEC		(MCP_MODE(MCP_ALGO_AES_192) | MCP_BCM(MCP_BCM_ECB) | MCP_ENC(0))
#define AES_256_ECB_ENC		(MCP_MODE(MCP_ALGO_AES_256) | MCP_BCM(MCP_BCM_ECB) | MCP_ENC(1))
#define AES_256_ECB_DEC		(MCP_MODE(MCP_ALGO_AES_256) | MCP_BCM(MCP_BCM_ECB) | MCP_ENC(0))

struct rtk_mcp_op {
	u32 flags;
	u32 key[6];
	u32 iv[4];
	u32 src;
	u32 dst;
	u32 len;
};

#define CHIP_ID_RTD1295		0x1295
#define CHIP_ID_RTD1395		0x1395
#define CHIP_ID_RTD1619		0x1619
#define CHIP_ID_RTD1319		0x1319
#define CHIP_ID_RTD1619B	0x1619b
