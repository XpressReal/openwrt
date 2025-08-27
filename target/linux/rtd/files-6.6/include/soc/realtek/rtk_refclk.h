/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SOC_REALTEK_REFCLK_H
#define __SOC_REALTEK_REFCLK_H

#include <linux/types.h>
#include <linux/math64.h>

#if IS_ENABLED(CONFIG_RTK_REFCLK)
s64 refclk_get_val_raw(void);
#else
static inline s64 refclk_get_val_raw(void)
{
	return -ENOTSUPP;
}
#endif

static inline s64 refclk_get_val_ms(void)
{
	s64 val = refclk_get_val_raw();

	if (val < 0)
		return val;
	do_div(val, 90);
	return val;
}

static inline s64 refclk_get_val_us(void)
{
	s64 val = refclk_get_val_raw();

	if (val < 0)
		return val;
	return val * 11;
}

#endif
