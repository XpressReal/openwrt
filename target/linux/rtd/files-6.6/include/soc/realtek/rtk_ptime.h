/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SOC_REALTEK_PTIME_H
#define __SOC_REALTEK_PTIME_H

#include <linux/device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define RTK_PTIME_TIMESTAMP_BEGIN_2020 1577836800LL
#define RTK_PTIME_TIMESTAMP_DEFAULT    RTK_PTIME_TIMESTAMP_BEGIN_2020

#define RTK_PTIME_ERROR 0xffffffff
#define RTK_PTIME_SHIFT 6
#define RTK_PTIME_DATA_MASK  (0x3fffffff >> RTK_PTIME_SHIFT)

static inline unsigned int _calc_vd(unsigned int v)
{
#if RTK_PTIME_SHIFT == 2
	return (v ^ (v >> 4) ^ (v >> 8) ^ (v >> 12) ^ (v >> 16) ^
		(v >> 20) ^ (v >> 24) ^ (v >> 28)) & 0xf;
#elif RTK_PTIME_SHIFT == 6
	return (v ^ (v >> 8) ^ (v >> 16) ^ (v >> 24)) & 0xff;
#else
#warn "unimplemented"
	return 0;
#endif
}

static inline unsigned int rtk_ptime_encode(unsigned int v)
{
	unsigned int vs = (v >> RTK_PTIME_SHIFT);

	if ((vs & ~RTK_PTIME_DATA_MASK) != 0)
		return RTK_PTIME_ERROR;

	return vs | (_calc_vd(vs) << (30 - RTK_PTIME_SHIFT));
}

static inline unsigned int rtk_ptime_decode(unsigned int v)
{
	if (_calc_vd(v) != 0)
		return RTK_PTIME_ERROR;

	v &= RTK_PTIME_DATA_MASK;
	return ((v) << RTK_PTIME_SHIFT) + (1 << RTK_PTIME_SHIFT) - 1;
}

static inline int rtk_ptime_store_time(struct device *dev, uint64_t time)
{
	struct regmap *regmap;
	unsigned int offset;
	unsigned int val;

	if (!dev->of_node)
		return -ENODEV;

	if (time < RTK_PTIME_TIMESTAMP_DEFAULT)
		return -EINVAL;

	regmap = syscon_regmap_lookup_by_phandle_args(dev->of_node, "realtek,ptime-reg", 1, &offset);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	val = rtk_ptime_encode(time - RTK_PTIME_TIMESTAMP_DEFAULT);
	if (val == RTK_PTIME_ERROR)
		return -EINVAL;

	regmap_write(regmap, offset, val);
	return 0;
}

static inline int rtk_ptime_load_time(struct device *dev, uint64_t *time)
{
	struct regmap *regmap;
	unsigned int offset;
	unsigned int val;

	if (!dev->of_node)
		return -ENODEV;

	regmap = syscon_regmap_lookup_by_phandle_args(dev->of_node, "realtek,ptime-reg", 1, &offset);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	regmap_read(regmap, offset, &val);

	val = rtk_ptime_decode(val);
	if (val == RTK_PTIME_ERROR)
		return -EINVAL;

	*time = (uint64_t)val + RTK_PTIME_TIMESTAMP_DEFAULT;
	return 0;
}

#endif
