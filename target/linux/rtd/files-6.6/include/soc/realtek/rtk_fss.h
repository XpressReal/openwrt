/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SOC_REALTEK_FSS_H
#define __SOC_REALTEK_FSS_H

#include <linux/types.h>

struct device_node;
struct fss_control;

struct fss_calibration_data_cell {
	unsigned int bitmap;
	unsigned int cdl0;
	unsigned int cdl0_size;
	unsigned int cdl1;
	unsigned int cdl1_size;
	unsigned int min_cdl1;
};

struct fss_calibration_data {
	struct fss_calibration_data_cell fss;
	struct fss_calibration_data_cell dsu;
};

#if IS_ENABLED(CONFIG_RTK_FSS)

struct fss_control *of_fss_control_get(struct device_node *np);
void fss_control_put(struct fss_control *ctl);
int fss_control_get_calibration_data(struct fss_control *ctl, struct fss_calibration_data *data);
int fss_control_get_hw_version(struct fss_control *ctl);

#else

static inline struct fss_control *of_fss_control_get(struct device_node *np)
{
	return ERR_PTR(-ENODEV);
}

static inline void fss_control_put(struct fss_control *ctl)
{}

static inline int fss_control_get_calibration_data(struct fss_control *ctl, struct fss_calibration_data *data)
{
	return -ENODEV;
}

static inline int fss_control_get_hw_version(struct fss_control *ctl)
{
	return -ENODEV;
}

#endif

#endif /* __SOC_REALTEK_FSS_H */
