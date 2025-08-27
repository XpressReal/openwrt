// SPDX-License-Identifier: GPL-2.0-only

#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/tee_drv.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/thermal.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include "dpi_ta.h"

#define DPI_WORK_DELAY_MS             (4000)
#define DPI_WORK_DELAY_LONG_MS        (20000)

#define MIS_DUMMY2  0xe4

static const uuid_t dpi_ta_uuid = UUID_INIT(0xe92e6998, 0xbe42, 0x11ed,  0xaf, 0xa1, 0x02, 0x42, 0xac, 0x12, 0x00, 0x02);

struct dpi_tee_device {
	struct regmap *misc;
	struct thermal_zone_device *tz;
	struct delayed_work delayed_work;
	int temp_prev;
	int thermal_state;
	struct tee_context *tee_context;
	struct device *dev;
	unsigned int tee_session;
	struct dpi_params params;
	struct task_struct *reboot_helper;
	bool session_cancelled;
};

static int dpi_tee_match(struct tee_ioctl_version_data *data, const void *vers)
{
	return 1;
}

static int dpi_tee_open_context(struct dpi_tee_device *dpi)
{
	struct device *dev = dpi->dev;
	struct tee_ioctl_version_data vers = {
		.impl_id = TEE_OPTEE_CAP_TZ,
		.impl_caps = TEE_IMPL_ID_OPTEE,
		.gen_caps = TEE_GEN_CAP_GP,
	};

	dpi->tee_context = tee_client_open_context(NULL, dpi_tee_match, NULL, &vers);
	if (IS_ERR(dpi->tee_context)) {
		if (PTR_ERR(dpi->tee_context) == -ENODEV) {
			dev_info(dev, "teedev may not ready, retry\n");
			return -EPROBE_DEFER;
		}
		dev_err(dev, "error %pe: failed in tee_client_open_context\n", dpi->tee_context);
		return PTR_ERR(dpi->tee_context);
	}
	return 0;
}

static int dpi_tee_open_session(struct dpi_tee_device *dpi)
{
	struct device *dev = dpi->dev;
	struct tee_ioctl_open_session_arg sess_arg = {0};
	int ret;

	memcpy(sess_arg.uuid, dpi_ta_uuid.b, TEE_IOCTL_UUID_LEN);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 0;

	ret = tee_client_open_session(dpi->tee_context, &sess_arg, NULL);
	if (ret < 0 || sess_arg.ret != 0) {
		dev_err(dev, "failed to tee_client_open_session: %pe / %#x\n", ERR_PTR(ret), sess_arg.ret);
		tee_client_close_context(dpi->tee_context);
		return ret ?: -EOPNOTSUPP;
	}

	dpi->tee_session = sess_arg.session;
	return 0;
}

static int dpi_invoke_cmd_setup_params(struct dpi_tee_device *dpi)
{
	struct tee_param param[4] = {0};
	struct tee_ioctl_invoke_arg arg = {0};
	struct tee_shm *tee_shm;
	struct dpi_params *p;
	int ret;

	tee_shm = tee_shm_alloc(dpi->tee_context, sizeof(*p), TEE_SHM_MAPPED | TEE_SHM_DMA_BUF);
	if (IS_ERR(tee_shm))
		return PTR_ERR(tee_shm);

	p = tee_shm_get_va(tee_shm, 0);
	memcpy(p, &dpi->params, sizeof(*p));

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param[0].u.memref.size = sizeof(*p);
	param[0].u.memref.shm = tee_shm;
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	arg.func = DPI_TA_CMD_SETUP_PARAMS;
	arg.session = dpi->tee_session;
	arg.num_params = 4;

	ret = tee_client_invoke_func(dpi->tee_context, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		dev_err(dpi->dev, "%s: failed to invoke func: %pe / %#x\n", __func__, ERR_PTR(ret), arg.ret);
		tee_shm_free(tee_shm);
		return ret ?: -EOPNOTSUPP;
	}

	tee_shm_free(tee_shm);
	return 0;
}

static int dpi_invoke_cmd_update_thermal_state(struct dpi_tee_device *dpi, int state)
{
	struct tee_param param[4] = {0};
	struct tee_ioctl_invoke_arg arg = {0};
	int ret;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = state;
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	arg.func = DPI_TA_CMD_UPDATE_THERMAL_STATE;
	arg.session = dpi->tee_session;
	arg.num_params = 4;

	ret = tee_client_invoke_func(dpi->tee_context, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		dev_warn(dpi->dev, "%s: failed to invoke func: %pe / %#x\n", __func__, ERR_PTR(ret),
			 arg.ret);
		return ret ?: -EOPNOTSUPP;
	}
	return 0;
}

static int dpi_invoke_cmd_update_thermal_delta(struct dpi_tee_device *dpi)
{
	struct tee_param param[4] = {0};
	struct tee_ioctl_invoke_arg arg = {0};
	int ret;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	arg.func = DPI_TA_CMD_UPDATE_THERMAL_DELTA;
	arg.session = dpi->tee_session;
	arg.num_params = 4;

	ret = tee_client_invoke_func(dpi->tee_context, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		dev_warn(dpi->dev, "%s: failed to invoke func: %pe / %#x\n", __func__, ERR_PTR(ret),
			 arg.ret);
		return ret ?: -EOPNOTSUPP;
	}

	return 0;
}

static void dpi_tee_restart_tm_work(struct dpi_tee_device *dpi, u32 timeout_ms)
{
	dev_dbg(dpi->dev, "%s: timeout=%d\n", __func__, timeout_ms);
	mod_delayed_work(system_freezable_wq, &dpi->delayed_work,
			 round_jiffies(msecs_to_jiffies(timeout_ms)));
}

static void dpi_tee_cancel_tm_work(struct dpi_tee_device *dpi)
{
	bool pending = cancel_delayed_work_sync(&dpi->delayed_work);

	dev_dbg(dpi->dev, "%s: pending=%d\n", __func__, pending);
}

static void dpi_tee_handle_temp_hc(struct dpi_tee_device *dpi, int temp)
{
	int state;
	int ret;
	ktime_t s;

	if (dpi->params.temp.temp_hot && temp > dpi->params.temp.temp_hot)
		state = DPI_THERMAL_STATE_HOT;
	else if (dpi->params.temp.temp_cold && temp < dpi->params.temp.temp_cold)
		state = DPI_THERMAL_STATE_COLD;
	else
		state = DPI_THERMAL_STATE_NORMAL;

	if (dpi->thermal_state == state)
		return;

	dev_dbg(dpi->dev, "%s: state=%d(%d)\n", __func__, state, temp);

	s = ktime_get();
	ret = dpi_invoke_cmd_update_thermal_state(dpi, state);

	dev_info(dpi->dev, "%s: invoke cmd returns %d, and takes %lld ms\n",
		 __func__, ret, ktime_to_ms(ktime_sub(ktime_get(), s)));
	if (ret)
		return;

	dpi->thermal_state = state;
}

static void dpi_tee_handle_temp_delta(struct dpi_tee_device *dpi, int temp)
{
	int delta = temp - dpi->temp_prev;
	int ret;
	ktime_t s;

	if (!dpi->params.temp.temp_delta)
		return;

	if (dpi->temp_prev && abs(delta) <= dpi->params.temp.temp_delta)
		return;

	dev_dbg(dpi->dev, "%s: delta=%d(%d)\n", __func__, delta, temp);

	s = ktime_get();
	ret = dpi_invoke_cmd_update_thermal_delta(dpi);

	dev_info(dpi->dev, "%s: invoke cmd returns %d, and takes %lld ms\n",
		 __func__, ret, ktime_to_ms(ktime_sub(ktime_get(), s)));
	if (ret)
		return;

	dpi->temp_prev = temp;
}

static void dpi_tee_handle_tm_works(struct dpi_tee_device *dpi)
{
	int temp;
	int ret;

	ret = thermal_zone_get_temp(dpi->tz, &temp);
	if (ret) {
		dev_warn(dpi->dev, "failed to read temp: %d\n", ret);
		return;
	}

	dpi_tee_handle_temp_hc(dpi, temp);
	dpi_tee_handle_temp_delta(dpi, temp);
}

static void dpi_tee_tm_work(struct work_struct *work)
{
	struct dpi_tee_device *dpi = container_of(work, struct dpi_tee_device,
						  delayed_work.work);

	dpi_tee_handle_tm_works(dpi);

	dpi_tee_restart_tm_work(dpi, DPI_WORK_DELAY_MS);
}

static int dpi_tee_reboot_helper(void *arg)
{
	struct dpi_tee_device *dpi = arg;

	allow_signal(SIGTERM);
	allow_signal(SIGKILL);
	set_freezable();

	do {
		if (signal_pending(current)) {
			dev_info(dpi->dev, "%s: signalled to cancel session\n", __func__);
			dpi_tee_cancel_tm_work(dpi);
			dpi_tee_restart_tm_work(dpi, DPI_WORK_DELAY_LONG_MS);
			flush_signals(current);
		}

		freezable_schedule_timeout_interruptible(msecs_to_jiffies(2000));
	}  while (!kthread_should_stop());

	return 0;
}

static int of_dpi_parse_rx_odt_sel_params(struct device_node *np, struct dpi_rx_odt_sel_params *params)
{
	int count;

	count = of_property_count_u32_elems(np, "odt-sel-idx");
	if (count < 0)
		return count;

	if (count > DPI_RX_ODT_SEL_NUM_MAX ||
		of_property_count_u32_elems(np, "state-0-value") != count ||
		of_property_count_u32_elems(np, "state-1-value") != count)
		return -EINVAL;

	of_property_read_u32_array(np, "odt-sel-idx", params->odt_idx_q, count);
	of_property_read_u32_array(np, "state-0-value", params->odt_val_q_0, count);
	of_property_read_u32_array(np, "state-1-value", params->odt_val_q_1, count);
	params->num_q = count;
	params->enabled = 1;
	return 0;
}

static int of_dpi_parse_rx_dq_cal_params(struct device_node *np, struct dpi_rx_dq_cal_params *params)
{
	int ret;

	ret = of_property_read_u32(np, "delta-width", &params->delta_width);
	if (ret)
		return ret;
	ret = of_property_read_u32(np, "cal-time", &params->cal_time);
	if (ret)
		return ret;
	params->enabled = 1;
	return 0;
}

static int of_dpi_parse_tx_phase_params(struct device_node *np, struct dpi_tx_phase_params *params)
{
	int count;

	count = of_property_count_u32_elems(np, "pi-sel-idx");
	if (count < 0)
		return count;

	if (count > DPI_TX_PHASE_PI_NUM_MAX ||
		of_property_count_u32_elems(np, "state-0-value") != count ||
		of_property_count_u32_elems(np, "state-1-value") != count)
		return -EINVAL;

	of_property_read_u32_array(np, "pi-sel-idx", params->pi_idx_q, count);
	of_property_read_u32_array(np, "state-0-value", params->pi_val_q_0, count);
	of_property_read_u32_array(np, "state-1-value", params->pi_val_q_1, count);
	params->num_q = count;
	params->enabled = 1;
	return 0;
}

static int of_dpi_parse_tx_ocd_sel_params(struct device_node *np, struct dpi_tx_ocd_sel_params *params)
{
	int count;

	count = of_property_count_u32_elems(np, "ocd-sel-idx");
	if (count < 0)
		return count;

	if (count > DPI_TX_OCD_SEL_NUM_MAX ||
		of_property_count_u32_elems(np, "state-0-value") != count ||
		of_property_count_u32_elems(np, "state-1-value") != count)
		return -EINVAL;

	of_property_read_u32_array(np, "ocd-sel-idx", params->ocd_idx_q, count);
	of_property_read_u32_array(np, "state-0-value", params->ocd_val_q_0, count);
	of_property_read_u32_array(np, "state-1-value", params->ocd_val_q_1, count);
	params->num_q = count;
	params->enabled = 1;
	return 0;
}

static int of_dpi_parse_zq_cal_params(struct device_node *np, struct  dpi_zq_cal_params *params)
{
	int ret;

	ret = of_property_count_u32_elems(np, "zq-vref");
	if (ret < 0)
		return ret;
	params->zq_vref_num = ret;
	ret = of_property_read_u32_array(np, "zq-vref", params->zq_vref, ret);
	if (ret)
		return ret;

	ret = of_property_count_u32_elems(np, "zprog-values");
	if (ret < 0)
		return ret;
	params->zprog_num = ret;
	ret = of_property_read_u32_array(np, "zprog-values", params->zprog, ret);
	if (ret)
		return ret;

	ret = of_property_count_u32_elems(np, "zq-ena-nocd2");
	if (ret < 0)
		return ret;
	params->zq_nocd2_en_num = ret;
	ret = of_property_read_u32_array(np, "zq-ena-nocd2", params->zq_nocd2_en, ret);
	if (ret)
		return ret;

	ret = of_property_count_u32_elems(np, "zprog-nocd2");
	if (ret < 0)
		return ret;
	params->zprog_nocd2_num = ret;
	ret = of_property_read_u32_array(np, "zprog-nocd2", params->zprog_nocd2, ret);
	if (ret)
		return ret;

	params->enabled = 1;
	return 0;
}


static int of_dpi_parse_ddr_params(struct device_node *np, struct dpi_params *params)
{
	struct device_node *child;
	int ret = 0;

	child = of_get_child_by_name(np, "rx-odt-sel");
	if (child)
		ret = of_dpi_parse_rx_odt_sel_params(child, &params->rx_odt_sel);
	if (!child || ret)
		pr_info("no rx-odt-sel params\n");
	of_node_put(child);

	child = of_get_child_by_name(np, "rx-dq-cal");
	if (child)
		ret = of_dpi_parse_rx_dq_cal_params(child, &params->rx_dq_cal);
	if (!child || ret)
		pr_info("no rx-dq-cal params\n");
	of_node_put(child);

	child = of_get_child_by_name(np, "tx-phase");
	if (child)
		ret = of_dpi_parse_tx_phase_params(child, &params->tx_phase);
	if (!child || ret)
		pr_info("no tx-phase params\n");
	of_node_put(child);

	child = of_get_child_by_name(np, "tx-ocd-sel");
	if (child)
		ret = of_dpi_parse_tx_ocd_sel_params(child, &params->tx_ocd_sel);
	if (!child || ret)
		pr_info("no tx-ocd-sel params\n");
	of_node_put(child);


	child = of_get_child_by_name(np, "zq-cal");
	if (child)
		ret = of_dpi_parse_zq_cal_params(child, &params->zq_cal);
	if (!child || ret)
		pr_info("no za-cal params\n");
	of_node_put(child);

	return ret;
}

static int of_dpi_parse_generic_params(struct device_node *np, struct dpi_params *params)
{
	int ret;

	of_property_read_u32(np, "temp-delta", &params->temp.temp_delta);
	of_property_read_u32(np, "temp-hot", &params->temp.temp_hot);
	of_property_read_u32(np, "temp-cold", &params->temp.temp_cold);

	ret = of_property_read_u32(np, "ddr-speed", &params->init.ddr_speed);
	if (!ret)
		params->init.ddr_speed_enabled = 1;

	return 0;
}

static const char *dpi_get_dram_type_str(struct dpi_tee_device *dpi)
{
        uint32_t val;

        regmap_read(dpi->misc, MIS_DUMMY2, &val);

        switch (val & 0xf) {
        case 0x4:
                return "ddr4";
        case 0x3:
                return "ddr3";
        case 0xc:
                return "lpddr4";
	default:
		return "unknown";
        }
}

static int dpi_parse_params(struct dpi_tee_device *dpi, struct dpi_params *params)
{
	struct device *dev = dpi->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child;
	const char *ddr_type_str = dpi_get_dram_type_str(dpi);
	int ret;

	child = of_get_child_by_name(np, ddr_type_str);
	if (!child) {
		dev_err(dev, "failed to parse params for %s\n", ddr_type_str);
		return -EINVAL;
	}

	ret = of_dpi_parse_ddr_params(child, params);
	of_node_put(child);

	of_dpi_parse_generic_params(np, params);

	return 0;
}

static int dpi_tee_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dpi_tee_device *dpi;
	int ret;

	dpi = devm_kzalloc(dev, sizeof(*dpi), GFP_KERNEL);
	if (!dpi)
		return -ENOMEM;

	dpi->dev = dev;

	dpi->misc = syscon_regmap_lookup_by_phandle(dev->of_node, "realtek,misc");
	if (IS_ERR(dpi->misc)) {
		ret = PTR_ERR(dpi->misc);
		dev_err(dev, "failed to get syscon: %d\n", ret);
		return ret;
	}

	dpi->tz = thermal_zone_get_zone_by_name("cpu-thermal");
	if (IS_ERR(dpi->tz)) {
		ret = PTR_ERR(dpi->tz);
		dev_err(dev, "failed to get thermal zone: %d\n", ret);
		return ret;
	}

	INIT_DELAYED_WORK(&dpi->delayed_work, dpi_tee_tm_work);
	ret = dpi_parse_params(dpi, &dpi->params);
	if (ret)
		return ret;

	ret = dpi_tee_open_context(dpi);
	if (ret)
		return ret;

	ret = dpi_tee_open_session(dpi);
	if (ret)
		goto error_close_context;

	dpi->reboot_helper = kthread_run(dpi_tee_reboot_helper, dpi, dev_name(dev));
	if (IS_ERR(dpi->reboot_helper))
		goto error_close_session;

	ret = dpi_invoke_cmd_setup_params(dpi);
	if (ret) {
		dev_err(dev, "failed to setup params: %d\n", ret);
		goto error_kthread_stop;
	}

	platform_set_drvdata(pdev, dpi);
	dpi_tee_restart_tm_work(dpi, DPI_WORK_DELAY_MS);

	return 0;

error_kthread_stop:
	kthread_stop(dpi->reboot_helper);
error_close_session:
	tee_client_close_session(dpi->tee_context, dpi->tee_session);
error_close_context:
	tee_client_close_context(dpi->tee_context);
	return ret;
}

static int dpi_tee_remove(struct platform_device *pdev)
{
	struct dpi_tee_device *dpi = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	dpi_tee_cancel_tm_work(dpi);
	kthread_stop(dpi->reboot_helper);
	tee_client_close_session(dpi->tee_context, dpi->tee_session);
	tee_client_close_context(dpi->tee_context);

	return 0;
}

static const struct of_device_id of_dpi_tee_match_table[] = {
	{ .compatible = "realtek,tee-dpi", },
	{}
};

static struct platform_driver dpi_tee_driver = {
	.probe  = dpi_tee_probe,
	.remove = dpi_tee_remove,
	.driver = {
		.name           = "rtk-tee-dpi",
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(of_dpi_tee_match_table),
	},
};

module_platform_driver(dpi_tee_driver);
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
