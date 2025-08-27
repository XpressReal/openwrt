// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Realtek Semiconductor Corp.
 */

#include <linux/arm-smccc.h>
#include <linux/errno.h>
#include <linux/types.h>

#define RTK_DECRYPT_DTE_FW_WITH_IK 0x8400ff3b
#define RTK_ENCRYPT_DTE_FW_WITH_IK 0x8400ff3c

int smcp_encrypt_image_with_ik(u32 data_in, u32 data_out, u32 length)
{
	struct arm_smccc_res res;

	arm_smccc_smc(RTK_ENCRYPT_DTE_FW_WITH_IK, data_in, data_out, length, 0, 0, 0, 0, &res);
	if (res.a0 != 0)
		return -EINVAL;
	return 0;
}

int smcp_decrypt_image_with_ik(u32 data_in, u32 data_out, u32 length)
{
	struct arm_smccc_res res;

	arm_smccc_smc(RTK_DECRYPT_DTE_FW_WITH_IK, data_in, data_out, length, -1, 0, 0, 0, &res);
	if (res.a0 != 0)
		return -EINVAL;
	return 0;
}
