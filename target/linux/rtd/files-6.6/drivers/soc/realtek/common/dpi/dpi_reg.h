#define DPI_DLL_CRT_PLL_PI0				0x10
#define DPI_DLL_CRT_PLL_PI1				0x14
#define DPI_DLL_CRT_PLL_PI2				0x98
#define DPI_DLL_CRT_PLL_PI3				0xA0
#define DPI_DLL_CRT_SSC0				0x1C
#define DPI_DLL_CRT_SSC2				0x24
#define DPI_DLL_CRT_SSC3				0x28

#define DPI_DLL_CRT_DQ_ODT_SEL			0x1d4
#define DPI_DLL_CRT_DQS_P_ODT_SEL		0x1dc
#define DPI_DLL_CRT_DQS_N_ODT_SEL		0x1e0
#define DPI_DLL_CRT_CLK_ODT_SEL			0x418
#define DPI_DLL_CRT_ADR_ODT_SEL			0x474
#define DPI_DLL_CRT_CKE_ODT_SEL			0x478
#define DPI_DLL_CRT_CS_ODT_SEL			0x47c

#define DPI_DLL_CRT_CKE_OCD_SEL			0x1c0
#define DPI_DLL_CRT_DQ_OCD_SEL			0x1d8
#define DPI_DLL_CRT_DQS_OCD_SEL_0		0x1e4
#define DPI_DLL_CRT_DQS_OCD_SEL_1		0x1e8
#define DPI_DLL_CRT_CS_OCD_SEL			0x1ec
#define DPI_DLL_CRT_CK_OCD_SEL			0x1f0
#define DPI_DLL_CRT_OCD_SEL_0			0x3c4
#define DPI_DLL_CRT_OCD_SEL_1			0x3c8
#define DPI_DLL_CRT_OCD_SEL_2			0x3cc
#define DPI_DLL_CRT_OCD_SEL_3			0x3d0
#define DPI_DLL_CRT_OCD_SEL_4			0x3d4
#define DPI_DLL_CRT_OCD_SEL_5			0x3d8
#define DPI_DLL_CRT_OCD_SEL_6			0x3dc
#define DPI_DLL_CRT_OCD_SEL_7			0x3e0
#define DPI_DLL_CRT_OCD_SEL_8			0x3e4

#define DPI_DLL_CRT_PAD_CTRL_PROG		0x130
#define DPI_DLL_CRT_ZQ_PAD_CTRL			0x134
#define DPI_DLL_CRT_PAD_CTRL_ZPROG		0x138
#define DPI_DLL_CRT_PAD_NOCD2_ZPROG		0x13c
#define DPI_DLL_CRT_ZQ_NOCD2			0x140
#define DPI_DLL_CRT_PAD_ZCTRL_STATUS	0x144
#define DPI_DLL_CRT_DPI_CTRL_0			0x208
#define DPI_DLL_CRT_DPI_CTRL_1			0x20c
#define DPI_DLL_CRT_INT_STATUS_2		0x22c
#define DPI_DLL_PAD_RZCTRL_STATUS		0x150
#define DPI_DLL_ODT_TTCP0_SET0			0x190
#define DPI_DLL_ODT_TTCP1_SET0			0x194
#define DPI_DLL_ODT_TTCN0_SET0			0x198
#define DPI_DLL_ODT_TTCN1_SET0			0x19c
#define DPI_DLL_OCDP0_SET0				0x1a0
#define DPI_DLL_OCDP1_SET0				0x1a4
#define DPI_DLL_OCDN0_SET0				0x1a8
#define DPI_DLL_OCDN1_SET0				0x1ac

#define DPI_DLL_TEST_CTRL0				0x168
#define DPI_DLL_TEST_CTRL1				0x16c
#define DPI_DLL_READ_CTRL_1				0x174
#define DPI_DLL_DBG_READ_1				0x184
#define DPI_DLL_DPI_CTRL_0				0x208
#define DPI_DLL_DPI_CTRL_1				0x20c
#define DPI_DLL_DPI_CTRL_2				0x210
#define DPI_DLL_CAL_VREF_CTR			0x368
#define DPI_DLL_CAL_MODE_CTRL			0x36c
#define DPI_RW_DQS_IN_DLY_0(x)			0x520 + (x & 1) * 4 + (x >> 1) * 0x300
#define DPI_RW_DQS_IN_DLY_1(x)			0x528 + (x & 1) * 4 + (x >> 1) * 0x300
#define DPI_RW_DQS_IN_DLY_2(x)			0x538 + (x & 1) * 4 + (x >> 1) * 0x300
#define DPI_RW_DQS_IN_DLY_3(x)			0x540 + (x & 1) * 4 + (x >> 1) * 0x300
#define DPI_RW_DQS_IN_DLY_1_DBI(x)		0x530 + (x & 1) * 4 + (x >> 1) * 0x300
#define DPI_RW_DQS_IN_DLY_3_DBI(x)		0x548 + (x & 1) * 4 + (x >> 1) * 0x300
#define DPI_RW_CAL_OUT_0(x)				0x570 + (x & 1) * 4 + (x >> 1) * 0x300
#define DPI_RW_CAL_OUT_1(x)				0x578 + (x & 1) * 4 + (x >> 1) * 0x300
#define DPI_RW_READ_DBG_CTRL(x)			0x5e0 + (x & 1) * 4 + (x >> 1) * 0x300
#define DPI_RW_DQ_DEL_PSEL(x)			0x620 + (x & 1) * 4 + (x >> 1) * 0x300
#define DPI_RW_DQ_DEL_NSEL(x)			0x628 + (x & 1) * 4 + (x >> 1) * 0x300
#define DPI_RW_DM_DEL_PNSEL(x)			0x640 + (x & 1) * 4 + (x >> 1) * 0x300
#define DPI_RW_READ_CTRL_4(x)			0x6c8 + (x & 1) * 4 + (x >> 1) * 0x300
#define DPI_RW_VALID_WIN_DET_PFIFO(x)	0x6d0 + (x & 1) * 4 + (x >> 1) * 0x300
#define DPI_RW_VALID_WIN_DET_NFIFO(x)	0x6d8 + (x & 1) * 4 + (x >> 1) * 0x300
