// SPDX-License-Identifier: GPL-2.0-only
/*
 *  rtk-usb-manager.c RTK Manager Driver for All USB.
 *
 * Copyright (C) 2017 Realtek Semiconductor Corporation
 * Copyright (C) 2020 Realtek Semiconductor Corporation
 *
 */

//#define DEBUG

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/usb.h>
#include <linux/usb/otg.h>
#include <linux/usb/role.h>
#include <linux/usb/gadget.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/gpio/consumer.h>
#include <linux/of_platform.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/suspend.h>
#include <linux/regulator/consumer.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/sys_soc.h>
#include <linux/extcon.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#define ISO_USB_CTRL_REG 0xfb0 /* 0x98007fb0 */
#define ISO_USB_U2PHY_REG_LDO_PW (BIT(20) | BIT(21) | BIT(22) | BIT(23))
#define ISO_USB0_MAC_POWER (BIT(0))
#define ISO_USB0_PHY_POWER (BIT(4) | BIT(6))
#define ISO_USB0_POWER_MASK (ISO_USB0_MAC_POWER | ISO_USB0_PHY_POWER)
#define ISO_USB0_POWER_ENABLE ISO_USB0_POWER_MASK
#define ISO_USB1_MAC_POWER (BIT(1))
#define ISO_USB1_PHY_POWER (BIT(5))
#define ISO_USB1_POWER_MASK (ISO_USB1_MAC_POWER | ISO_USB1_PHY_POWER)
#define ISO_USB1_POWER_ENABLE ISO_USB1_POWER_MASK
#define ISO_USB_ISOLATION_CELL_MASK (BIT(8) | BIT(9))
#define ISO_USB_ISOLATION_CELL ISO_USB_ISOLATION_CELL_MASK

#define ISO_USB0_SRAM_PWR_REG 0xf7c /* 0x98007f7c */
#define ISO_USB0_SRAM_PWR_CTRL_REG 0xf80 /* 0x98007f80 */
#define ISO_USB1_SRAM_PWR_REG 0xf9c /* 0x98007f9c */
#define ISO_USB1_SRAM_PWR_CTRL_REG 0xfa0 /* 0x98007fa0 */
#define ISO_USB_SRAM_PWR_MASK 0xffffffff
#define ISO_USB_SRAM_PWR_CTRL_MASK 0xffffffff
#define ISO_USB_SRAM_PWR_CTRL_ENABLE 0xf00
#define ISO_USB_SRAM_PWR_CTRL_DISABLE 0xf01

#define ISO_PCIE_USB3PHY_SEL_REG 0x5c /* 0x9800705c */
#define ISO_PCIE_USB3PHY_ENABLE_MASK BIT(6)
#define ISO_PCIE_USB3PHY_ENABLE ISO_PCIE_USB3PHY_ENABLE_MASK

/* define usb register */
#define WRAP_REG_USB2_PHY_UTMI 0x8
#define WRAP_RESET_UTMI_P0 BIT(0)
#define WRAP_REG_USB3_PHY_PIPE 0xc
#define WRAP_CLK_EN_PIPE3_P0 BIT(1)
#define WRAP_REG_USB_HMAC_CTR0 0x60
#define WRAP_HOST_U3_PORT_DIABLE BIT(8)
#define WRAP_REG_USB3_PHY0_ST 0x84
#define WRAP_USB3_CLK_RDY BIT(19)
#define WRAP_REG_USB_TC_MUX_CTRL (0x194 - 0x164)
#define WRAP_USB3_TC_MODE BIT(0)
#define WRAP_USB3_CC_DET BIT(1)
#define WRAP_USB3_CC_SWITCH_INV BIT(2)

struct gpio_data {
	struct gpio_desc *gpio_desc;

	int active_port;
	/* active_port_mask bit 0~3 mapping to port 0~3*/
	int active_port_mask;

	/* port GPIO power on off */
	bool power_on;
};

struct type_c_data {
	/* node for type_c driver */
	struct device_node *node;
	bool is_enable;

	/* for extcon type c connector */
	struct extcon_dev *edev;
	struct notifier_block edev_nb;
	bool is_attach;

	struct gpio_desc *connector_switch_gpio;
	struct gpio_desc *plug_side_switch_gpio;

	struct reset_control *reset_type_c;
};

struct manager_data;

struct port_data {
	int port_num;
	struct manager_data *manager_data;

	struct gpio_data *power_gpio;

	/* port mapping to host */
	struct device_node *port_node;
	struct device_node *slave_mac_node; /* for ohci */
	bool support_usb3;

	struct type_c_data type_c;

	/* for auto switch role to host or device */
	bool is_support_role_sw;
	struct usb_role_switch *role_sw;
	struct delayed_work auto_role_swap_work;
#define DEFAULT_AUTO_ROLE_SWAP_TIME 30
	int auto_role_swap_time;
	int is_linked_on_host_mode;

	/* active for port power */
	int active;
	/* enable for port status */
	int enable;
	int enable_mask; /* bit 0: master; bit 1: slave */
	/* host_add_count for usb host notify */
	int host_add_count;
	int bus_add_count;
	bool host_ready;

#define MAX_CLK_RESET_NUM 5
	/* clock and reset */
	struct clk *clk_mac[MAX_CLK_RESET_NUM];
	struct reset_control *reset_mac[MAX_CLK_RESET_NUM];
	struct reset_control *reset_u2phy[MAX_CLK_RESET_NUM];
	struct reset_control *reset_u3phy[MAX_CLK_RESET_NUM];
};

struct manager_data {
	struct device *dev;

	struct port_data port0;
	struct port_data port1;
	struct port_data port2;
	struct port_data port3;

	/* usb power gpio */
	struct gpio_data gpio0;
	struct gpio_data gpio1;
	struct gpio_data gpio2;
	struct gpio_data gpio3;

	struct regmap *iso_regs;

	bool disable_usb;

	bool usb_iso_mode;

	struct workqueue_struct *wq_usb_manager;
	struct delayed_work delayed_work;
	bool child_node_initiated;

	struct class_compat *usb_ctrl_compat_class;

	struct dentry *debug_dir;

	struct mutex lock;

	/* clock and reset */
	struct clk *clk_usb;
	struct reset_control *reset_usb;

	/* notify */
	struct notifier_block power_nb;
};

static inline bool port_can_enable(struct port_data *port)
{
	bool can_enable = true;

	if (port->port_num < 0)
		can_enable = false;
	if (port->enable_mask == 0)
		can_enable = false;

	return can_enable;
}

/* set usb power domain */

/* isolate UPHY A->D */
static int __isolate_phy_from_a_to_d(struct manager_data *data)
{
	void __iomem *wrap_base;
	void __iomem *reg;

	dev_info(data->dev, "set USB Analog phy power off\n");

	regmap_update_bits(data->iso_regs, ISO_USB_CTRL_REG,
			   ISO_USB0_MAC_POWER | ISO_USB1_MAC_POWER,
			   ISO_USB0_MAC_POWER | ISO_USB1_MAC_POWER);

	wrap_base = of_iomap(data->port0.port_node, 0);
	reg = wrap_base + WRAP_REG_USB2_PHY_UTMI;
	writel(WRAP_RESET_UTMI_P0 | readl(reg), reg);
	iounmap(wrap_base);

	wrap_base = of_iomap(data->port1.port_node, 0);
	reg = wrap_base + WRAP_REG_USB2_PHY_UTMI;
	writel(WRAP_RESET_UTMI_P0 | readl(reg), reg);
	iounmap(wrap_base);

	wrap_base = of_iomap(data->port2.port_node, 0);
	reg = wrap_base + WRAP_REG_USB2_PHY_UTMI;
	writel(WRAP_RESET_UTMI_P0 | readl(reg), reg);
	iounmap(wrap_base);

#define WRAP_REG_USB2_PHY_UTMI_LEGACY 0x8

	wrap_base = of_iomap(data->port3.port_node, 0);
	reg = wrap_base + WRAP_REG_USB2_PHY_UTMI_LEGACY;
	writel(WRAP_RESET_UTMI_P0 | readl(reg), reg);
	iounmap(wrap_base);

	regmap_update_bits(data->iso_regs, ISO_USB_CTRL_REG,
			   0,
			   ISO_USB0_PHY_POWER | ISO_USB1_PHY_POWER);

	return 0;
}

static void rtk_usb_set_pd_power_rtd1295(struct manager_data *data, bool power_on)
{
	struct soc_device_attribute rtk_soc_rtd1296[] = {
		{ .family = "Realtek Kylin", .soc_id = "RTD1296",},{ /* empty */ }};
	static struct soc_device_attribute rtk_soc_a00[] = {
		{ .revision = "A00", }, { /* empty */ }};
	static struct soc_device_attribute rtk_soc_bxx[] = {
		{ .revision = "B0*", }, { /* empty */ }};
	int val;

	if (!data->iso_regs)
		return;

	regmap_read(data->iso_regs, ISO_USB_CTRL_REG, &val);
	pr_info("%s power_%s (iso:[0x%x=0x%08x)\n", __func__,
		    power_on?"on":"off", ISO_USB_CTRL_REG, val);

	if (power_on) {
		pr_debug("%s power_on\n", __func__);

		pr_debug("set usb_power_domain usb0 on\n");
		/* set power gating control */
		regmap_update_bits(data->iso_regs, ISO_USB_CTRL_REG,
			ISO_USB0_POWER_ENABLE, ISO_USB0_POWER_MASK);

		regmap_update_bits(data->iso_regs, ISO_USB0_SRAM_PWR_CTRL_REG,
				   ISO_USB_SRAM_PWR_CTRL_ENABLE,
				   ISO_USB_SRAM_PWR_CTRL_MASK);

		/* port3 sram power */
		if (soc_device_match(rtk_soc_rtd1296)) {
			int usb_sram_pwr_ctrl_enable = ISO_USB_SRAM_PWR_CTRL_ENABLE;

			pr_info("set usb_power_domain usb1 on\n");

			regmap_update_bits(data->iso_regs, ISO_USB_CTRL_REG,
					   ISO_USB1_POWER_ENABLE,
					   ISO_USB1_POWER_MASK);

			if (soc_device_match(rtk_soc_a00))
				usb_sram_pwr_ctrl_enable |= 0x5,

			regmap_update_bits(data->iso_regs, ISO_USB1_SRAM_PWR_CTRL_REG,
					   usb_sram_pwr_ctrl_enable,
					   ISO_USB_SRAM_PWR_CTRL_MASK);
		}

		/* disable isolation cell */
		regmap_update_bits(data->iso_regs, ISO_USB_CTRL_REG,
			0, ISO_USB_ISOLATION_CELL_MASK);
	} else {
		pr_debug("%s power_off\n", __func__);

		__isolate_phy_from_a_to_d(data);

		if (soc_device_match(rtk_soc_bxx)) {

			regmap_update_bits(data->iso_regs, ISO_USB_CTRL_REG,
					   ISO_USB_ISOLATION_CELL,
					   ISO_USB_ISOLATION_CELL_MASK);

			/* port0-port2 sram */
			regmap_update_bits(data->iso_regs, ISO_USB0_SRAM_PWR_REG,
				   0, ISO_USB_SRAM_PWR_MASK);

			regmap_update_bits(data->iso_regs, ISO_USB0_SRAM_PWR_CTRL_REG,
					   ISO_USB_SRAM_PWR_CTRL_DISABLE,
					   ISO_USB_SRAM_PWR_CTRL_MASK);

			/* port3 sram */
			regmap_update_bits(data->iso_regs, ISO_USB1_SRAM_PWR_REG,
				   0, ISO_USB_SRAM_PWR_MASK);

			regmap_update_bits(data->iso_regs, ISO_USB1_SRAM_PWR_CTRL_REG,
					   ISO_USB_SRAM_PWR_CTRL_DISABLE,
					   ISO_USB_SRAM_PWR_CTRL_MASK);

			regmap_update_bits(data->iso_regs, ISO_USB_CTRL_REG,
					   0, ISO_USB0_POWER_MASK | ISO_USB1_POWER_MASK);
		}
	}

	regmap_read(data->iso_regs, ISO_USB_CTRL_REG, &val);
	pr_info("set power_domain %s (iso:[0x%x]=0x%08x)\n",
		    power_on?"on":"off", ISO_USB_CTRL_REG, val);
}

static void rtk_usb_set_pd_power_rtd1395(struct manager_data *data, bool power_on)
{
	int val;

	if (!data->iso_regs)
		return;

	regmap_read(data->iso_regs, ISO_USB_CTRL_REG, &val);
	pr_info("%s power_%s (iso:[0x%x=0x%08x)\n", __func__,
		    power_on?"on":"off", ISO_USB_CTRL_REG, val);

	if (power_on) {
		regmap_update_bits(data->iso_regs, ISO_USB_CTRL_REG,
				   ISO_USB0_POWER_ENABLE | ISO_USB1_POWER_ENABLE,
				   ISO_USB0_POWER_MASK | ISO_USB1_POWER_MASK);

		regmap_update_bits(data->iso_regs, ISO_USB0_SRAM_PWR_CTRL_REG,
				   ISO_USB_SRAM_PWR_CTRL_ENABLE,
				   ISO_USB_SRAM_PWR_CTRL_MASK);

		/* disable isolation cell */
		regmap_update_bits(data->iso_regs, ISO_USB_CTRL_REG,
			0, ISO_USB_ISOLATION_CELL_MASK);
	} else {
		regmap_update_bits(data->iso_regs, ISO_USB_CTRL_REG,
				   ISO_USB_ISOLATION_CELL,
				   ISO_USB_ISOLATION_CELL_MASK);

		/* port0-port2 sram */
		regmap_update_bits(data->iso_regs, ISO_USB0_SRAM_PWR_REG,
			   0, ISO_USB_SRAM_PWR_MASK);

		regmap_update_bits(data->iso_regs, ISO_USB0_SRAM_PWR_CTRL_REG,
				   ISO_USB_SRAM_PWR_CTRL_DISABLE,
				   ISO_USB_SRAM_PWR_CTRL_MASK);

		regmap_update_bits(data->iso_regs, ISO_USB_CTRL_REG,
				   0, ISO_USB0_POWER_MASK | ISO_USB1_POWER_MASK);
	}

	regmap_read(data->iso_regs, ISO_USB_CTRL_REG, &val);
	pr_info("set power_domain %s ([0x%x]=0x%08x)\n",
		    power_on?"on":"off", ISO_USB_CTRL_REG, val);
}

static void rtk_usb_set_pd_power(struct manager_data *data, bool power_on)
{
	struct soc_device_attribute kylin_soc[] = {
		{ .family = "Realtek Kylin", }, { /* empty */ } };
	struct soc_device_attribute hercules_soc[] = {
		{ .family = "Realtek Hercules", }, { /* empty */ } };

	if (soc_device_match(kylin_soc))
		rtk_usb_set_pd_power_rtd1295(data, power_on);
	else if (soc_device_match(hercules_soc))
		rtk_usb_set_pd_power_rtd1395(data, power_on);
}

/* control usb clock and reset */
static inline struct reset_control *usb_reset_get(struct device_node *node,
	    const char *str)
{
	struct reset_control *reset;

	reset = of_reset_control_get_exclusive(node, str);
	if (IS_ERR(reset))
		reset = NULL;

	return reset;
}

static inline void usb_reset_put(struct reset_control *reset)
{
	if (reset)
		reset_control_put(reset);
}

static inline int usb_reset_deassert(struct reset_control *reset)
{
	if (!reset)
		return 0;

	return reset_control_deassert(reset);
}

static inline int usb_reset_assert(struct reset_control *reset)
{
	if (!reset)
		return 0;

	return reset_control_assert(reset);
}

static int usb_port_reset_deassert(struct port_data *port)
{
	int i;

	for (i = 0; i < MAX_CLK_RESET_NUM; i++)
		usb_reset_deassert(port->reset_mac[i]);

	usb_reset_deassert(port->type_c.reset_type_c);

	return 0;
}

static int usb_port_reset_assert(struct port_data *port)
{
	int i;

	for (i = 0; i < MAX_CLK_RESET_NUM; i++)
		usb_reset_assert(port->reset_mac[i]);

	usb_reset_deassert(port->type_c.reset_type_c);

	return 0;
}

static int usb_port_phy_reset_deassert(struct port_data *port)
{
	int i;

	for (i = 0; i < MAX_CLK_RESET_NUM; i++)
		usb_reset_deassert(port->reset_u2phy[i]);

	if (port->support_usb3) {
		for (i = 0; i < MAX_CLK_RESET_NUM; i++)
			usb_reset_deassert(port->reset_u3phy[i]);
	}

	return 0;
}

static int usb_port_phy_reset_assert(struct port_data *port)
{
	int i;

	for (i = 0; i < MAX_CLK_RESET_NUM; i++)
		usb_reset_assert(port->reset_u2phy[i]);

	if (port->support_usb3) {
		for (i = 0; i < MAX_CLK_RESET_NUM; i++)
			usb_reset_assert(port->reset_u3phy[i]);
	}

	return 0;
}

static inline struct clk *usb_clk_get(struct device_node *node, const char *str)
{
	struct clk *clk;

	clk = of_clk_get_by_name(node, str);
	if (IS_ERR(clk))
		clk = NULL;

	return clk;
}

static inline void usb_clk_put(struct clk *clk)
{
	if (clk)
		clk_put(clk);
}

static void usb_port_clk_enable(struct port_data *port)
{
	int i;

	for (i = 0; i < MAX_CLK_RESET_NUM; i++)
		clk_prepare_enable(port->clk_mac[i]);
}

static void usb_port_clk_disable(struct port_data *port)
{
	int i;

	for (i = 0; i < MAX_CLK_RESET_NUM; i++)
		clk_disable_unprepare(port->clk_mac[i]);
}

static bool __usb_workaround_port0_enable(struct manager_data *data)
{
	const struct soc_device_attribute *soc_att_match = NULL;
	struct soc_device_attribute rtk_soc[] = {
			{ .family = "Realtek Phoenix", },
			{ .family = "Realtek Kylin", },
			{ .family = "Realtek Hercules", },
			{ .family = "Realtek Thor", },
			{ .family = "Realtek Hank", },
			{ /* empty */ } };

	soc_att_match = soc_device_match(rtk_soc);
	if (soc_att_match) {
		pr_info("%s Workaround port 0 reset enable match chip: %s\n",
			    __func__,
			    soc_att_match->family);
		return true;
	}
	return false;
}

static int rtk_usb_init_clock_reset(struct manager_data *data)
{
	struct device *dev = data->dev;

	struct reset_control *reset_usb_apply = usb_reset_get(dev->of_node, "apply");

	dev_dbg(dev, "Realtek USB init\n");

	usb_port_phy_reset_assert(&data->port0);
	usb_port_phy_reset_assert(&data->port1);
	usb_port_phy_reset_assert(&data->port2);
	usb_port_phy_reset_assert(&data->port3);
	usb_port_reset_assert(&data->port0);
	usb_port_reset_assert(&data->port1);
	usb_port_reset_assert(&data->port2);
	usb_port_reset_assert(&data->port3);
	usb_reset_assert(data->reset_usb);
	/* Do wmb */
	wmb();

	/* Enable usb phy reset */
	dev_dbg(dev, "Realtek USB init: Set phy reset to 1\n");
	if (port_can_enable(&data->port0))
		usb_port_phy_reset_deassert(&data->port0);
	if (port_can_enable(&data->port1))
		usb_port_phy_reset_deassert(&data->port1);
	if (port_can_enable(&data->port2))
		usb_port_phy_reset_deassert(&data->port2);
	if (port_can_enable(&data->port3))
		usb_port_phy_reset_deassert(&data->port3);

	usb_reset_deassert(reset_usb_apply);

	/* wait hardware clock stable */
	udelay(300);

	dev_dbg(dev, "Realtek USB init: Trigger usb clk\n");
	/* Trigger USB clk (enable -> disable) */
	clk_prepare_enable(data->clk_usb);
	clk_disable_unprepare(data->clk_usb);

	dev_dbg(dev, "Realtek USB init: Set usb reset to 1\n");

	/* port0 mac's reset bit must always enable for some platforms */
	if (port_can_enable(&data->port0) || __usb_workaround_port0_enable(data))
		usb_port_reset_deassert(&data->port0);
	if (port_can_enable(&data->port1))
		usb_port_reset_deassert(&data->port1);
	if (port_can_enable(&data->port2))
		usb_port_reset_deassert(&data->port2);
	if (port_can_enable(&data->port3))
		usb_port_reset_deassert(&data->port3);

	usb_reset_deassert(data->reset_usb);
	dev_dbg(dev, "Realtek USB init: enable usb clk\n");

	/* Enable USB clk */
	clk_prepare_enable(data->clk_usb);
	if (port_can_enable(&data->port0))
		usb_port_clk_enable(&data->port0);
	if (port_can_enable(&data->port1))
		usb_port_clk_enable(&data->port1);
	if (port_can_enable(&data->port2))
		usb_port_clk_enable(&data->port2);
	if (port_can_enable(&data->port3))
		usb_port_clk_enable(&data->port3);

	dev_info(dev, "Realtek USB init OK\n");

	usb_reset_put(reset_usb_apply);

	return 0;
}

static int rtk_usb_clear_clock_reset(struct manager_data *data)
{
	struct device *dev = data->dev;
	struct reset_control *reset_usb_apply = usb_reset_get(dev->of_node, "apply");

	dev_dbg(dev, "Realtek USB clear clock and reset\n");

	usb_port_phy_reset_assert(&data->port0);
	usb_port_phy_reset_assert(&data->port1);
	usb_port_phy_reset_assert(&data->port2);
	usb_port_phy_reset_assert(&data->port3);
	usb_port_reset_assert(&data->port0);
	usb_port_reset_assert(&data->port1);
	usb_port_reset_assert(&data->port2);
	usb_port_reset_assert(&data->port3);
	usb_reset_assert(data->reset_usb);

	usb_reset_assert(reset_usb_apply);
	/* Do wmb */
	wmb();

	clk_disable_unprepare(data->clk_usb);
	if (port_can_enable(&data->port0))
		usb_port_clk_disable(&data->port0);
	if (port_can_enable(&data->port1))
		usb_port_clk_disable(&data->port1);
	if (port_can_enable(&data->port2))
		usb_port_clk_disable(&data->port2);
	if (port_can_enable(&data->port3))
		usb_port_clk_disable(&data->port3);

	dev_info(dev, "Realtek USB clear clock and reset... OK\n");

	usb_reset_put(reset_usb_apply);

	return 0;
}

static int __usb_port_clock_reset_enable(struct manager_data *data,
	    struct port_data *port)
{
	struct device *dev = data->dev;

	dev_info(dev, "%s: port%d clock/reset enable\n",
		    __func__, port->port_num);

	/* Enable USB port reset */
	if (port_can_enable(port))
		usb_port_phy_reset_deassert(port);

	/* wait hardware clock stable */
	udelay(300);

	if (port_can_enable(port))
		usb_port_reset_deassert(port);

	/* wait hardware clock stable */
	udelay(300);
	/* Enable USB port clock */
	if (port_can_enable(port))
		usb_port_clk_enable(port);

	return 0;
}

static int __usb_port_clock_reset_disable(struct manager_data *data,
	    struct port_data *port)
{
	struct device *dev = data->dev;

	dev_info(dev, "%s: port%d clock/reset disable\n",
		    __func__, port->port_num);

	usb_port_clk_disable(port);

	/* Do wmb */
	wmb();

	usb_port_phy_reset_assert(port);

	/* wait hardware clock stable */
	udelay(300);

	if (port->port_num !=0 ||
		    !__usb_workaround_port0_enable(data))
		usb_port_reset_assert(port);

	return 0;
}

/* Type C port control */
#define disable_cc 0x0
#define enable_cc1 0x1
#define enable_cc2 0x2

static int thor_type_c_plug_config(struct manager_data* data, int dr_mode, int cc)
{
	struct device_node *dwc3_node;
	void __iomem *port0_wrap_base, *port0_wrap_base_1, *port2_wrap_base;
	void __iomem *port0_typec_mux_ctrl;
	void __iomem *port0_u3phy_pipe, *port2_u3phy_pipe;
	void __iomem *port0_pclk_u3phy, *port2_pclk_u3phy;
	void __iomem *port0_mac_base, *port0_u3_portsc;
	int wait_count = 1000;
	struct soc_device_attribute thor_soc[] = {
		{ .family = "Realtek Thor", }, { /* empty */ } };

	if (!soc_device_match(thor_soc))
		return 0;

	dwc3_node = of_get_compatible_child(data->port0.port_node, "snps,dwc3");
	if (!dwc3_node)
		dwc3_node = of_get_compatible_child(data->port0.port_node, "synopsys,dwc3");

	if (!dwc3_node)
		return -ENODEV;

	pr_info("%s: dr_mode=%d cc=0x%x\n", __func__, dr_mode, cc);

	port0_wrap_base = of_iomap(data->port0.port_node, 0);
	port0_wrap_base_1 = of_iomap(data->port0.port_node, 1);
	port2_wrap_base = of_iomap(data->port2.port_node, 0);

	port0_u3phy_pipe = port0_wrap_base + WRAP_REG_USB3_PHY_PIPE;
	port2_u3phy_pipe = port2_wrap_base + WRAP_REG_USB3_PHY_PIPE;
	port0_pclk_u3phy = port0_wrap_base + WRAP_REG_USB3_PHY0_ST;
	port2_pclk_u3phy = port2_wrap_base + WRAP_REG_USB3_PHY0_ST;

	port0_typec_mux_ctrl = port0_wrap_base_1 + WRAP_REG_USB_TC_MUX_CTRL;

#define MAC_REG_USB3_PORTSC 0x430
#define MAC_PORT_DISABLE BIT(1)
#define MAC_PORT_RXDETECT 0xa0
#define MAC_PORT_RXDETECT_MASK 0x1E0
#define MAC_PORT_STATE_WRITE BIT(16)

	port0_mac_base = of_iomap(dwc3_node, 0);
	port0_u3_portsc = port0_mac_base + MAC_REG_USB3_PORTSC;

	/* check u3phy clock if resume */
	while (wait_count-- > 0 &&
	       (!(readl(port0_pclk_u3phy) & WRAP_USB3_CLK_RDY) ||
		!(readl(port2_pclk_u3phy) & WRAP_USB3_CLK_RDY))) {
		pr_info("wait port0/port2 u3phy clock\n");
		mdelay(1);
	};

	if (wait_count < 0) {
		pr_err("wait u3phy clock fail. port0=0x%x port2=0x%x\n",
			    readl(port0_pclk_u3phy), readl(port2_pclk_u3phy));
	}

	if (cc != disable_cc) {
		int val_port0_pipe, val_port2_pipe;
		int val_port0_tc_mux_ctrl;

		/* push drd u3 port status to disable */
		writel(MAC_PORT_DISABLE | readl(port0_u3_portsc), port0_u3_portsc);

		pr_info("%s: disable port0_u3_portsc=0x%x\n", __func__, readl(port0_u3_portsc));

		/* disable u3phy clock */
		val_port0_pipe = readl(port0_u3phy_pipe);
		val_port2_pipe = readl(port2_u3phy_pipe);

		writel(~WRAP_CLK_EN_PIPE3_P0 & val_port0_pipe, port0_u3phy_pipe);
		writel(~WRAP_CLK_EN_PIPE3_P0 & val_port2_pipe, port2_u3phy_pipe);

		val_port0_tc_mux_ctrl = readl(port0_typec_mux_ctrl);
		if (cc == enable_cc1) {
			writel(~WRAP_USB3_CC_DET & val_port0_tc_mux_ctrl, port0_typec_mux_ctrl);
			pr_info("%s: enable_cc1 port0 u3phy for CHIP_ID_RTD1619\n", __func__);
		} else if (cc == enable_cc2) {
			writel(WRAP_USB3_CC_DET | val_port0_tc_mux_ctrl, port0_typec_mux_ctrl);
			pr_info("%s: enable_cc2 port0 u3phy for CHIP_ID_RTD1619\n", __func__);
		}
		/* enable u3phy clock */
		writel(WRAP_CLK_EN_PIPE3_P0 | val_port0_pipe, port0_u3phy_pipe);
		writel(WRAP_CLK_EN_PIPE3_P0 | val_port2_pipe, port2_u3phy_pipe);

		/* push port0 u3 port status to rxDetect */
		writel(MAC_PORT_STATE_WRITE | MAC_PORT_RXDETECT |
			    (readl(port0_u3_portsc) & (~MAC_PORT_RXDETECT_MASK)),
			    port0_u3_portsc);
		pr_info("%s: port0_u3_portsc=0x%x\n",
			    __func__, readl(port0_u3_portsc));
	}

	iounmap(port0_wrap_base);
	iounmap(port0_wrap_base_1);
	iounmap(port2_wrap_base);
	iounmap(port0_mac_base);
	of_node_put(dwc3_node);

	return 0;
}

static int rtk_usb_type_c_plug_config(struct manager_data *data,
				      struct type_c_data *type_c,
				      int dr_mode, int cc)
{
	bool high;

	/* host / device */
	if (dr_mode == USB_DR_MODE_PERIPHERAL)
		high = true;
	else if (dr_mode == USB_DR_MODE_HOST)
		high = false;
	else
		goto cc_config;

	if (!IS_ERR(type_c->connector_switch_gpio)) {
		dev_info(data->dev, "%s Set connector to %s by gpio %d\n",
			    __func__, high?"device":"host",
			    desc_to_gpio(type_c->connector_switch_gpio));
		if (gpiod_direction_output(type_c->connector_switch_gpio, high))
			dev_err(data->dev, "%s ERROR connector-switch-ctrl-gpio fail\n",
				    __func__);
	}

cc_config:
	/* cc pin */
	/* host / device */
	if (cc == enable_cc1)
		high = true;
	else if (cc == enable_cc2)
		high = false;
	else
		goto out;

	if (!IS_ERR(type_c->plug_side_switch_gpio)) {
		dev_info(data->dev, "%s type plug to %s by gpio %d\n",
			    __func__, high?"cc1":"cc2",
			    desc_to_gpio(type_c->plug_side_switch_gpio));
		if (gpiod_direction_output(type_c->plug_side_switch_gpio, high))
			dev_err(data->dev, "%s set ERROR plug-side-switch-gpio fail\n",
				    __func__);
	}

	thor_type_c_plug_config(data, dr_mode, cc);

out:
	return 0;
}

static int __usb_port_power_on_off(struct device *dev,
	    struct port_data *port, bool on,
	    struct device_node *node);

static int type_c_port_power_on_off(struct manager_data *data,
				    struct port_data *_port, bool on)
{
	struct device_node *usb_node;
	struct port_data *port = _port;

	mutex_lock(&data->lock);

	if (!port->host_ready && on)
		port = NULL;

	if (port) {
		usb_node = port->port_node;
		__usb_port_power_on_off(data->dev, port, on, usb_node);
		usb_node = port->slave_mac_node;
		__usb_port_power_on_off(data->dev, port, on, usb_node);
	}

	mutex_unlock(&data->lock);

	return 0;
}

static int __rtk_usb_type_c_update(struct manager_data *data,
				   struct type_c_data *type_c,
				   int device_state)
{
	struct port_data *port = container_of(type_c, struct port_data, type_c);
	int dr_mode = USB_DR_MODE_UNKNOWN;
	enum usb_role usb_role = USB_ROLE_NONE;
	int polarity = 0;
	int cc = 0;
	int vbus = 0;
	int host_state = 0;
	bool on = false;
	bool is_attach = false;

	/* notify for EXTCON_USB */
	/* device_state = 1: device mode is attach and set dr_mode is device mode */
	/* device_state = 0: device mode is detach and set dr_mode is host mode  */
	/* host_state = 1: dr_mode is host mode and is attach */
	/* host_state = 0: dr_mode is host mode and is detach */
	/* vbus = 1: device mode have power */
	/* vbus = 0: device mode no power */

	if (device_state) {
		dr_mode = USB_DR_MODE_PERIPHERAL;
		usb_role = USB_ROLE_DEVICE;
		extcon_get_property(type_c->edev, EXTCON_USB,
			    EXTCON_PROP_USB_TYPEC_POLARITY,
			    (union extcon_property_value *)&polarity);
		extcon_get_property(type_c->edev, EXTCON_USB,
			    EXTCON_PROP_USB_VBUS,
			    (union extcon_property_value *)&vbus);
		is_attach = true;
	} else {
		dr_mode = USB_DR_MODE_HOST;
		usb_role = USB_ROLE_HOST;
		extcon_get_property(type_c->edev, EXTCON_USB_HOST,
			    EXTCON_PROP_USB_TYPEC_POLARITY,
			    (union extcon_property_value *)&polarity);
		extcon_get_property(type_c->edev, EXTCON_USB_HOST,
			    EXTCON_PROP_USB_VBUS,
			    (union extcon_property_value *)&vbus);
		host_state = extcon_get_state(type_c->edev, EXTCON_USB_HOST);
		if (host_state > 0)
			is_attach = true;
	}

	dev_dbg(data->dev, "%s EXTCON_USB state=%d\n", __func__, device_state);
	dev_dbg(data->dev, "%s EXTCON_USB_HOST state=%d\n", __func__, host_state);

	if (polarity == 0)
		cc = enable_cc1;
	else if (polarity == 1)
		cc = enable_cc2;
	else
		cc = disable_cc;

	dev_dbg(data->dev, "%s polarity=%d cc=%d dr_mode=%d vbus=%d is_attach=%d\n",
		    __func__, polarity, cc, dr_mode, vbus, is_attach);

	rtk_usb_type_c_plug_config(data, type_c, dr_mode, cc);

	/* enable current mode */
	usb_role_switch_set_role(port->role_sw, usb_role);

	if (is_attach && !vbus)
		on = true;

	type_c->is_attach = is_attach;

	type_c_port_power_on_off(data, port, on);

	return NOTIFY_DONE;
}

static void __rtk_usb_type_c_check(struct manager_data *data,
				   struct type_c_data *type_c)
{
	int state;

	if (!type_c->edev)
		return;

	state = extcon_get_state(type_c->edev, EXTCON_USB);
	if (state < 0)
		state = 0;

	dev_dbg(data->dev, "%s EXTCON_USB state=%d\n", __func__, state);

	__rtk_usb_type_c_update(data, type_c, state);
}

static int __rtk_usb_type_c_notifier(struct notifier_block *nb,
			     unsigned long event, void *ptr)
{
	struct type_c_data *type_c = container_of(nb, struct type_c_data, edev_nb);
	struct port_data *port = container_of(type_c, struct port_data, type_c);
	struct manager_data *data = port->manager_data;
	int state = event;

	dev_dbg(data->dev, "%s EXTCON_USB state=%d\n", __func__, state);

	__rtk_usb_type_c_update(data, type_c, state);

	return NOTIFY_DONE;
}

static int rtk_usb_setup_type_c_device(struct manager_data *data)
{
	struct device_node *dev_node;
	struct device_node *port_node;
	struct type_c_data *type_c;
	struct port_data *port;
	int ret = 0;

	port = &data->port0;
	if (!port->type_c.node)
		port = &data->port1;
	if (!port->type_c.node)
		port = &data->port2;
	if (!port->type_c.node)
		port = &data->port3;
	if (!port->type_c.node)
		return 0;

	type_c = &port->type_c;
	if (!type_c->is_enable) {
		if (port->auto_role_swap_time > 0) {
			/**
			 * For special case, some boards use type c power and
			 * need use host mode. If type c node is disable, then
			 * check the mode after 30s. We switch to host mode if
			 * in device mode but no host connect.
			 */
			dev_info(data->dev, "%s: type c Auto role swap check after %ds\n",
				 __func__, port->auto_role_swap_time);
			queue_delayed_work(data->wq_usb_manager, &port->auto_role_swap_work,
				    msecs_to_jiffies(port->auto_role_swap_time * 1000));
		}

		return 0;
	}

	dev_node = type_c->node;
	port_node = port->port_node;

	type_c->edev = extcon_find_edev_by_node(dev_node);
	if (IS_ERR(type_c->edev)) {
		dev_warn(data->dev, "Get extcon type_c fail (ret=%ld)\n",
			    PTR_ERR(type_c->edev));
		ret = (int)PTR_ERR(type_c->edev);
		type_c->edev = NULL;
		return ret;
	}

	if (port_node && !port->role_sw)
		port->role_sw = usb_role_switch_find_by_fwnode(&port_node->fwnode);

	type_c->edev_nb.notifier_call = __rtk_usb_type_c_notifier;
	ret = extcon_register_notifier(type_c->edev, EXTCON_USB, &type_c->edev_nb);
	if (ret < 0)
		dev_err(data->dev, "couldn't register cable notifier\n");
	else
		__rtk_usb_type_c_check(data, type_c);

	return 0;
}

/* usb port control */
static void __gpio_on_off(struct device *dev, int port,
	    struct gpio_data *gpio, bool on)
{
	int active_port_mask = gpio->active_port_mask;
	struct gpio_desc *gpio_desc = gpio->gpio_desc;

	if (!gpio_desc) {
		dev_dbg(dev, "%s No gpio config\n", __func__);
		return;
	}

	if (on) {
		gpio->active_port |= BIT(port);
		if ((gpio->active_port & active_port_mask) !=
			    active_port_mask) {
			dev_info(dev, "gpio_num=%d active_port=0x%x active_port_mask=0x%x  ==> No turn on\n",
				    desc_to_gpio(gpio_desc), gpio->active_port,
				    active_port_mask);
			return;
		}
	} else {
		gpio->active_port &= ~BIT(port);
		if (gpio->active_port != 0) {
			dev_info(dev, "gpio_num=%d active_port=0x%x active_port_mask=0x%x  ==> No turn off\n",
				    desc_to_gpio(gpio_desc), gpio->active_port,
				    gpio->active_port_mask);
			return;
		}
	}

	if (gpiod_direction_output(gpio_desc, on)) {
		dev_err(dev, "%s ERROR set gpio fail\n",
				__func__);
	} else {
		dev_info(dev, "%s to set power%s %s by gpio (id=%d) OK\n",
					__func__,
					gpiod_is_active_low(gpio_desc) ?
					" (low active)":"",
					on?"on":"off", desc_to_gpio(gpio_desc));
		gpio->power_on = on;
	}
}

static int __usb_port_power_on_off(struct device *dev,
	    struct port_data *port, bool on,
	    struct device_node *node)
{
	struct device_node *usb_node = node;
	int enable_mask = port->enable_mask;

	if (port->port_node && usb_node &&
		    of_node_name_eq(port->port_node, usb_node->name)) {
		if (on)
			port->active |= BIT(0);
		else
			port->active &= ~BIT(0);
	} else if (port->slave_mac_node && usb_node &&
		    of_node_name_eq(port->slave_mac_node, usb_node->name)) {
		if (on)
			port->active |= BIT(1);
		else
			port->active &= ~BIT(1);
	} else {
		dev_dbg(dev, "%s port%d is not match\n",
			    __func__, port->port_num);
		return -ENODEV;
	}

	dev_dbg(dev, "%s port%d power=%s active=0x%x enable_mask=0x%x host_add_count=%d bus_add_count=%d\n",
		    __func__, port->port_num, on?"on":"off",
		    port->active, port->enable_mask, port->host_add_count, port->bus_add_count);

	if (((port->active & enable_mask) == enable_mask) ||
		    (!port->active && !on)) {

		if (port->power_gpio)
			__gpio_on_off(dev, port->port_num,
				    port->power_gpio, on);
		else
			dev_info(dev, "%s port%d no gpio\n",
				    __func__, port->port_num);
	}
	return 0;
}

static int usb_port_power_on_off(struct manager_data *data, bool on)
{
	struct device *dev = data->dev;
	struct device_node *usb_node;

	dev_info(dev, "%s", __func__);
	mutex_lock(&data->lock);

	usb_node = data->port0.port_node;
	__usb_port_power_on_off(data->dev, &data->port0, on, usb_node);
	usb_node = data->port0.slave_mac_node;
	__usb_port_power_on_off(data->dev, &data->port0, on, usb_node);

	usb_node = data->port1.port_node;
	__usb_port_power_on_off(data->dev, &data->port1, on, usb_node);
	usb_node = data->port1.slave_mac_node;
	__usb_port_power_on_off(data->dev, &data->port1, on, usb_node);

	usb_node = data->port2.port_node;
	__usb_port_power_on_off(data->dev, &data->port2, on, usb_node);
	usb_node = data->port2.slave_mac_node;
	__usb_port_power_on_off(data->dev, &data->port2, on, usb_node);

	usb_node = data->port3.port_node;
	__usb_port_power_on_off(data->dev, &data->port3, on, usb_node);
	usb_node = data->port3.slave_mac_node;
	__usb_port_power_on_off(data->dev, &data->port3, on, usb_node);

	mutex_unlock(&data->lock);

	return 0;
}

static int __usb_port_enable(struct manager_data *data, struct port_data *port,
	    struct device_node *usb_node, bool enable)
{
	if (port->port_node && usb_node &&
		    of_node_name_eq(port->port_node, usb_node->name)) {
		if (enable)
			port->enable |= BIT(0);
		else
			port->enable &= ~BIT(0);
	} else if (port->slave_mac_node && usb_node &&
		    of_node_name_eq(port->slave_mac_node, usb_node->name)) {
		if (enable)
			port->enable |= BIT(1);
		else
			port->enable &= ~BIT(1);
	}
	return 0;
}

static int __usb_port_bus_add_remove_notify(struct manager_data *data,
	    struct port_data *port, bool add, struct device_node *node)
{
	struct device_node *usb_node = node;

	if (port->port_node && usb_node &&
		    of_node_name_eq(port->port_node, usb_node->name)) {
		if (add)
			port->bus_add_count++;
		else
			port->bus_add_count--;
	} else if (port->slave_mac_node && usb_node &&
		    of_node_name_eq(port->slave_mac_node, usb_node->name)) {
		if (add)
			port->bus_add_count++;
		else
			port->bus_add_count--;
	} else {
		dev_dbg(data->dev, "%s port%d is not match\n",
			    __func__, port->port_num);
		return -ENODEV;
	}

	return 0;
}

static int __usb_port_host_add_remove_notify(struct manager_data *data,
	    struct port_data *port, bool add,
	    struct device_node *node)
{
	struct device_node *usb_node = node;
	bool run_power_on_off = false;
	bool on = add ? true : false;


	if (port->port_node && usb_node &&
		    of_node_name_eq(port->port_node, usb_node->name)) {
		if (add)
			port->host_add_count++;
		else
			port->host_add_count--;
	} else if (port->slave_mac_node && usb_node &&
		    of_node_name_eq(port->slave_mac_node, usb_node->name)) {
		if (add)
			port->host_add_count++;
		else
			port->host_add_count--;
	} else {
		dev_dbg(data->dev, "%s port%d is not match\n",
			    __func__, port->port_num);
		return -ENODEV;
	}

	if (port->host_add_count && port->host_add_count == port->bus_add_count)
		port->host_ready = true;
	else
		port->host_ready = false;

	/* power on/off for host add and ready, or host remove finish*/
	if (!port->host_add_count || port->host_ready)
		run_power_on_off = true;

	if (on && port->type_c.is_enable && !port->type_c.is_attach)
		run_power_on_off = false;

	if (run_power_on_off)
		__usb_port_power_on_off(data->dev, port, on, usb_node);

	/* set type c port */
	if (port->type_c.node) {
		if (port->host_add_count == port->bus_add_count)
			rtk_usb_type_c_plug_config(data, &port->type_c,
				    USB_DR_MODE_HOST, 0);
		else if (port->host_add_count == 0)
			rtk_usb_type_c_plug_config(data, &port->type_c,
				    USB_DR_MODE_PERIPHERAL, 0);
	}

	return 0;
}

static int __usb_port_host_client_add_remove_notify(struct manager_data *data,
	    struct port_data *port, bool add,
	    struct device_node *node)
{
	struct device_node *usb_node = node;

	if (port->port_node && usb_node &&
		    of_node_name_eq(port->port_node, usb_node->name)) {
		if (add)
			port->is_linked_on_host_mode++;
		else
			port->is_linked_on_host_mode--;
	}

	return 0;
}

/**
 * usb-port 0~2 (dwc3-rtk)
 *    |-> dwc3
 *         |-> xhci
 *
 * usb-port 3 (legacy usb simple-bus)
 *    |-> ehci/ohci
 */
struct device *__get_usb_port_device_from_notify(struct manager_data *data,
	    struct device *usb_dev, bool is_bus)
{
	struct device *port_dev = NULL;
	struct soc_device_attribute kylin_soc[] = {
		{ .family = "Realtek Kylin", }, { /* empty */ } };

	/**
	 * Get device of the xhci or ehci/ochi
	 * is_bus true: port_dev is xhci or ehci
	 * is_bus false: parent of port_dev is xhci or ehci
	 */
	if (is_bus)
		port_dev = usb_dev;
	else
		port_dev = usb_dev->parent;

	/* Special case for kylin usb port 3 is ehci/ohci */
	if (port_dev && soc_device_match(kylin_soc)) {
		struct device_node *usb_node = port_dev->of_node;

		if (of_node_name_eq(usb_node, "ehci")) {
			port_dev = port_dev->parent;
			goto out;
		} else if (of_node_name_eq(usb_node, "ohci")) {
			/* for slave_mac_node */
			goto out;
		}
	}

	/* port_dev is dwc3  */
	if (port_dev)
		port_dev = port_dev->parent;

	/* port_dev is usb_port */
	if (port_dev)
		port_dev = port_dev->parent;

out:
	dev_dbg(data->dev, "%s usb_dev %s usb_port %s\n", __func__,
		    dev_name(usb_dev), dev_name(port_dev));

	return port_dev;
}

static int rtk_usb_port_add_remove_notify(struct notifier_block *self,
		     unsigned long action, void *usb)
{
	struct manager_data *data =
		    container_of(self, struct manager_data, power_nb);
	struct usb_device *udev = NULL;
	struct usb_bus *ubus = NULL;
	struct device *port_dev = NULL;
	bool add = false;

	switch (action) {
	case USB_BUS_ADD:
		/* USB bus add */
		add = true;

		fallthrough;
	case USB_BUS_REMOVE:
		ubus = (struct usb_bus *)usb;

		/* ubus to usb port */
		port_dev = __get_usb_port_device_from_notify(data, ubus->controller, true);
		if (port_dev) {
			struct device_node *usb_node = port_dev->of_node;

			dev_dbg(data->dev, "%s bus %s for %s", __func__,
				    add?"add":"remove", dev_name(port_dev));
			mutex_lock(&data->lock);

			__usb_port_bus_add_remove_notify(data, &data->port0, add, usb_node);
			__usb_port_bus_add_remove_notify(data, &data->port1, add, usb_node);
			__usb_port_bus_add_remove_notify(data, &data->port2, add, usb_node);
			__usb_port_bus_add_remove_notify(data, &data->port3, add, usb_node);

			mutex_unlock(&data->lock);
		}

		break;
	case USB_DEVICE_ADD:
		/* USB host add */
		add = true;

		fallthrough;
	case USB_DEVICE_REMOVE:
		udev = (struct usb_device *)usb;

		/* udev is usbX, usbX to usb port*/
		if (!udev->parent)
			port_dev = __get_usb_port_device_from_notify(data, &udev->dev, false);

		if (port_dev) {
			struct device_node *usb_node = port_dev->of_node;

			dev_dbg(data->dev, "%s host %s for %s", __func__,
				    add?"add":"remove", dev_name(port_dev));
			mutex_lock(&data->lock);

			__usb_port_host_add_remove_notify(data, &data->port0, add, usb_node);
			__usb_port_host_add_remove_notify(data, &data->port1, add, usb_node);
			__usb_port_host_add_remove_notify(data, &data->port2, add, usb_node);
			__usb_port_host_add_remove_notify(data, &data->port3, add, usb_node);

			mutex_unlock(&data->lock);
		}

		port_dev = NULL;
		if (udev->parent && !udev->parent->parent)
			port_dev = __get_usb_port_device_from_notify(data, &udev->parent->dev, false);
		if (port_dev) {
			struct device_node *usb_node = port_dev->of_node;

			dev_dbg(data->dev, "%s host %s a client for %s", __func__,
				    add?"add":"remove", dev_name(port_dev));
			mutex_lock(&data->lock);

			__usb_port_host_client_add_remove_notify(data, &data->port0, add, usb_node);
			__usb_port_host_client_add_remove_notify(data, &data->port1, add, usb_node);
			__usb_port_host_client_add_remove_notify(data, &data->port2, add, usb_node);
			__usb_port_host_client_add_remove_notify(data, &data->port3, add, usb_node);

			mutex_unlock(&data->lock);
		}

		break;
	}

	return NOTIFY_OK;
}

/* debugfs and sysfs */
#ifdef CONFIG_DEBUG_FS
static void __debug_dump_gpio_info(struct seq_file *s, struct gpio_data *gpio)
{
	if (!gpio)
		return;

	seq_puts(s, "    gpio info:\n");
	if (!gpio->gpio_desc) {
		seq_puts(s, "        No gpio\n");
		return;
	}
	seq_printf(s, "       gpio_num=%d\n", desc_to_gpio(gpio->gpio_desc));
	seq_printf(s, "       active_port=0x%x active_port_mask=0x%x\n",
		    gpio->active_port, gpio->active_port_mask);
	seq_printf(s, "       power %s%s\n",
		    gpio->power_on?"on":"off",
		    gpiod_is_active_low(gpio->gpio_desc)?" (low active)":"");
}

static void __debug_dump_port_info(struct seq_file *s, struct port_data *port)
{
	if (port->port_num < 0)
		return;

	seq_printf(s, "port%d info:\n", port->port_num);
	seq_printf(s, "    port_num=%d\n", port->port_num);
	seq_printf(s, "    power_gpio@%p\n", port->power_gpio);
	__debug_dump_gpio_info(s, port->power_gpio);
	seq_printf(s, "    port_node=%s\n",
		    port->port_node?port->port_node->name:"NULL");
	seq_printf(s, "    slave_mac_node=%s\n",
		    port->slave_mac_node?port->slave_mac_node->name:"NULL");
	if (port->type_c.node) {
		struct type_c_data *type_c = &port->type_c;

		seq_printf(s, "    type_c_node=%s\n",
			    type_c->node?type_c->node->name:"NULL");
	}
	seq_printf(s, "    active=0x%x (enable_mask=0x%x)\n",
		    port->active, port->enable_mask);
	seq_printf(s, "    is_linked_on_host_mode=%d\n",
		    port->is_linked_on_host_mode);
}

static int rtk_usb_debug_show(struct seq_file *s, void *unused)
{
	struct manager_data		*data = s->private;

	seq_puts(s, "rtk usb manager info:\n");
	__debug_dump_port_info(s, &data->port0);
	__debug_dump_port_info(s, &data->port1);
	__debug_dump_port_info(s, &data->port2);
	__debug_dump_port_info(s, &data->port3);

	return 0;
}

static int rtk_usb_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtk_usb_debug_show, inode->i_private);
}

static const struct file_operations rtk_usb_debug_fops = {
	.open			= rtk_usb_debug_open,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static inline void create_debug_files(struct manager_data *data)
{
	data->debug_dir = debugfs_create_dir("usb_manager", usb_debug_root);

	debugfs_create_file("debug", 0444, data->debug_dir, data,
			    &rtk_usb_debug_fops);
}

static inline void remove_debug_files(struct manager_data *data)
{
	debugfs_remove_recursive(data->debug_dir);
}
#else
static inline void create_debug_files(struct manager_data *data) {}
static inline void remove_debug_files(struct manager_data *data) {}
#endif /* CONFIG_DEBUG_FS */

static ssize_t port0_power_show(struct device *dev,
	    struct device_attribute *attr, char *buf)
{
	struct manager_data *data = dev_get_drvdata(dev);
	char *ptr = buf;
	int count = PAGE_SIZE;
	int n;

	n = scnprintf(ptr, count, "To Control Port 0\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count, "echo \"on\" > port0_power\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count, "echo \"off\" > port0_power\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count, "\n");
	ptr += n;
	count -= n;

	if (data->port0.power_gpio) {
		struct gpio_data *gpio = data->port0.power_gpio;

		n = scnprintf(ptr, count, "Now port0 %s\n",
			    gpio->power_on?"on":"off");
		ptr += n;
		count -= n;
	} else {
		n = scnprintf(ptr, count, "port0 No control gpio\n");
		ptr += n;
		count -= n;
	}

	return ptr - buf;
}

static ssize_t port0_power_store(struct device *dev,
	    struct device_attribute *attr,
	    const char *buf, size_t count)
{
	struct manager_data *data = dev_get_drvdata(dev);
	struct device_node *usb_node;

	mutex_lock(&data->lock);
	if (!strncmp(buf, "on", 2)) {
		usb_node = data->port0.port_node;
		__usb_port_power_on_off(data->dev, &data->port0, true,
			    usb_node);
		usb_node = data->port0.slave_mac_node;
		__usb_port_power_on_off(data->dev, &data->port0, true,
			    usb_node);
	} else if (!strncmp(buf, "off", 3)) {
		usb_node = data->port0.port_node;
		__usb_port_power_on_off(data->dev, &data->port0, false,
			    usb_node);
		usb_node = data->port0.slave_mac_node;
		__usb_port_power_on_off(data->dev, &data->port0, false,
			    usb_node);
	} else
		dev_err(data->dev, "UNKNOWN input (%s)", buf);

	mutex_unlock(&data->lock);
	return count;
}
static DEVICE_ATTR_RW(port0_power);

static ssize_t port1_power_show(struct device *dev,
	    struct device_attribute *attr, char *buf)
{
	struct manager_data *data = dev_get_drvdata(dev);
	char *ptr = buf;
	int count = PAGE_SIZE;
	int n;

	n = scnprintf(ptr, count, "To Control Port 1\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count, "echo \"on\" > port1_power\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count, "echo \"off\" > port1_power\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count, "\n");
	ptr += n;
	count -= n;

	if (data->port1.power_gpio) {
		struct gpio_data *gpio = data->port1.power_gpio;

		n = scnprintf(ptr, count, "Now port1 %s\n",
			    gpio->power_on?"on":"off");
		ptr += n;
		count -= n;
	} else {
		n = scnprintf(ptr, count, "port1 No control gpio\n");
		ptr += n;
		count -= n;
	}

	return ptr - buf;
}

static ssize_t port1_power_store(struct device *dev,
	    struct device_attribute *attr,
	    const char *buf, size_t count)
{
	struct manager_data *data = dev_get_drvdata(dev);
	struct device_node *usb_node;

	mutex_lock(&data->lock);
	if (!strncmp(buf, "on", 2)) {
		usb_node = data->port1.port_node;
		__usb_port_power_on_off(data->dev, &data->port1, true,
			    usb_node);
		usb_node = data->port1.slave_mac_node;
		__usb_port_power_on_off(data->dev, &data->port1, true,
			    usb_node);
	} else if (!strncmp(buf, "off", 3)) {
		usb_node = data->port1.port_node;
		__usb_port_power_on_off(data->dev, &data->port1, false,
			    usb_node);
		usb_node = data->port1.slave_mac_node;
		__usb_port_power_on_off(data->dev, &data->port1, false,
			    usb_node);
	} else
		dev_err(data->dev, "UNKNOWN input (%s)", buf);

	mutex_unlock(&data->lock);
	return count;
}
static DEVICE_ATTR_RW(port1_power);

static ssize_t port2_power_show(struct device *dev,
	    struct device_attribute *attr, char *buf)
{
	struct manager_data *data = dev_get_drvdata(dev);
	char *ptr = buf;
	int count = PAGE_SIZE;
	int n;

	n = scnprintf(ptr, count, "To Control Port 2\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count, "echo \"on\" > port2_power\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count, "echo \"off\" > port2_power\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count, "\n");
	ptr += n;
	count -= n;

	if (data->port2.power_gpio) {
		struct gpio_data *gpio = data->port2.power_gpio;

		n = scnprintf(ptr, count, "Now port2 %s\n",
			    gpio->power_on?"on":"off");
		ptr += n;
		count -= n;
	} else {
		n = scnprintf(ptr, count, "port2 No control gpio\n");
		ptr += n;
		count -= n;
	}

	return ptr - buf;
}

static ssize_t port2_power_store(struct device *dev,
	    struct device_attribute *attr,
	    const char *buf, size_t count)
{
	struct manager_data *data = dev_get_drvdata(dev);
	struct device_node *usb_node;

	mutex_lock(&data->lock);
	if (!strncmp(buf, "on", 2)) {
		usb_node = data->port2.port_node;
		__usb_port_power_on_off(data->dev, &data->port2, true,
			    usb_node);
		usb_node = data->port2.slave_mac_node;
		__usb_port_power_on_off(data->dev, &data->port2, true,
			    usb_node);

	} else if (!strncmp(buf, "off", 3)) {
		usb_node = data->port2.port_node;
		__usb_port_power_on_off(data->dev, &data->port2, false,
			    usb_node);
		usb_node = data->port2.slave_mac_node;
		__usb_port_power_on_off(data->dev, &data->port2, false,
			    usb_node);
	} else
		dev_err(data->dev, "UNKNOWN input (%s)", buf);

	mutex_unlock(&data->lock);
	return count;
}
static DEVICE_ATTR_RW(port2_power);

static ssize_t port3_power_show(struct device *dev,
	    struct device_attribute *attr, char *buf)
{
	struct manager_data *data = dev_get_drvdata(dev);
	char *ptr = buf;
	int count = PAGE_SIZE;
	int n;

	n = scnprintf(ptr, count, "To Control Port 3\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count, "echo \"on\" > port3_power\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count, "echo \"off\" > port3_power\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count, "\n");
	ptr += n;
	count -= n;

	if (data->port3.power_gpio) {
		struct gpio_data *gpio = data->port3.power_gpio;

		n = scnprintf(ptr, count, "Now port3 %s\n",
			    gpio->power_on?"on":"off");
		ptr += n;
		count -= n;
	} else {
		n = scnprintf(ptr, count, "port3 No control gpio\n");
		ptr += n;
		count -= n;
	}

	return ptr - buf;
}

static ssize_t port3_power_store(struct device *dev,
	    struct device_attribute *attr,
	    const char *buf, size_t count)
{
	struct manager_data *data = dev_get_drvdata(dev);
	struct device_node *usb_node;

	mutex_lock(&data->lock);
	if (!strncmp(buf, "on", 2)) {
		usb_node = data->port3.port_node;
		__usb_port_power_on_off(data->dev, &data->port3, true,
			    usb_node);
		usb_node = data->port3.slave_mac_node;
		__usb_port_power_on_off(data->dev, &data->port3, true,
			    usb_node);
	} else if (!strncmp(buf, "off", 3)) {
		usb_node = data->port3.port_node;
		__usb_port_power_on_off(data->dev, &data->port3, false,
			    usb_node);
		usb_node = data->port3.slave_mac_node;
		__usb_port_power_on_off(data->dev, &data->port3, false,
			    usb_node);
	} else
		dev_err(data->dev, "UNKNOWN input (%s)", buf);

	mutex_unlock(&data->lock);
	return count;
}
static DEVICE_ATTR_RW(port3_power);

static ssize_t iso_mode_show(struct device *dev,
	    struct device_attribute *attr, char *buf)
{
	struct manager_data *data = dev_get_drvdata(dev);
	char *ptr = buf;
	int count = PAGE_SIZE;
	int n;

	mutex_lock(&data->lock);

	n = scnprintf(ptr, count, "usb_iso_mode %s\n",
		    data->usb_iso_mode?"Enable":"Disable");
	ptr += n;
	count -= n;

	mutex_unlock(&data->lock);

	return ptr - buf;
}

static ssize_t iso_mode_store(struct device *dev,
	    struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct manager_data *data = dev_get_drvdata(dev);

	mutex_lock(&data->lock);
	if (!strncmp(buf, "enable", 6))
		data->usb_iso_mode = true;
	else if (!strncmp(buf, "disable", 7))
		data->usb_iso_mode = false;
	else
		dev_err(data->dev, "UNKNOWN input (%s)", buf);

	mutex_unlock(&data->lock);
	return count;
}
DEVICE_ATTR_RW(iso_mode);

static int __do_usb_bind_driver(struct manager_data *data,
	    struct device_node *usb_node)
{
	int ret = 0;

	if (usb_node != NULL) {
		struct platform_device *pdev = NULL;
		struct device *dev = NULL;

		pdev = of_find_device_by_node(usb_node);
		if (pdev != NULL)
			dev = &pdev->dev;

		if (dev && !dev->driver) {

			if (dev->parent && dev->bus->need_parent_lock)
				device_lock(dev->parent);
			ret = device_attach(dev);
			if (dev->parent && dev->bus->need_parent_lock)
				device_unlock(dev->parent);
			if (ret < 0)
				dev_err(data->dev,
					    "%s Error: device_attach fail (ret=%d)",
					    __func__, ret);
			else
				dev_info(data->dev,
					    "%s device %s attach OK\n",
					    __func__, dev_name(dev));
			put_device(dev);
		}
	}

	return ret;
}

static int __usb_bind_driver(struct manager_data *data,
	    struct port_data *port)
{
	int ret = 0;

	dev_info(data->dev, "%s: To bind usb port%d\n",
		    __func__, port->port_num);

	ret = __do_usb_bind_driver(data, port->port_node);
	if (ret > 0)
		__usb_port_enable(data, port, port->port_node, true);
	ret = __do_usb_bind_driver(data, port->slave_mac_node);
	if (ret > 0)
		__usb_port_enable(data, port, port->slave_mac_node, true);
	if (port->type_c.node && port->type_c.is_enable) {
		ret = __do_usb_bind_driver(data, port->type_c.node);
		if (ret > 0)
			rtk_usb_setup_type_c_device(data);
	}

	return 0;
}

static int __do_usb_unbind_driver(struct manager_data *data,
	    struct device_node *usb_node)
{
	if (usb_node != NULL) {
		struct platform_device *pdev = NULL;
		struct device *dev = NULL;

		pdev = of_find_device_by_node(usb_node);
		if (pdev != NULL)
			dev = &pdev->dev;

		if (dev && dev->driver) {
			if (dev->parent && dev->bus->need_parent_lock)
				device_lock(dev->parent);
			device_release_driver(dev);
			if (dev->parent && dev->bus->need_parent_lock)
				device_unlock(dev->parent);
			put_device(dev);
		}
	}

	return 0;
}

static int __usb_unbind_driver(struct manager_data *data,
	    struct port_data *port)
{
	dev_info(data->dev, "%s: To unbind usb port%d\n",
		    __func__, port->port_num);

	if (port->type_c.node && port->type_c.is_enable)
		__do_usb_unbind_driver(data, port->type_c.node);
	__do_usb_unbind_driver(data, port->port_node);
	__do_usb_unbind_driver(data, port->slave_mac_node);

	__usb_port_enable(data, port, port->port_node, false);
	__usb_port_enable(data, port, port->slave_mac_node, false);
	return 0;
}

static ssize_t port0_show(struct device *dev,
	      struct device_attribute *attr, char *buf)
{
	struct manager_data *data = dev_get_drvdata(dev);
	char *ptr = buf;
	int count = PAGE_SIZE;
	int n;

	n = scnprintf(ptr, count,
		     "Now Port0 is %s\n",
		    data->port0.enable?"enable":"disable");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count,
		     "echo disable > port0 ==> to disable port0\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count,
		     "echo enable > port0 ==> to enable port0\n");
	ptr += n;
	count -= n;

	return ptr - buf;
}

static ssize_t port0_store(struct device *dev,
	       struct device_attribute *attr, const char *buf, size_t count)
{
	struct manager_data *data = dev_get_drvdata(dev);
	struct port_data *port = &data->port0;

	if (!strncmp(buf, "disable", 7)) {
		if ((port->enable & port->enable_mask) == port->enable_mask) {
			__usb_unbind_driver(data, port);
			__usb_port_clock_reset_disable(data, port);
			dev_info(data->dev, "port0 disable OK\n");
		} else {
			dev_info(data->dev, "port0 is disable\n");
		}
	} else if (!strncmp(buf, "enable", 6)) {
		if (!port->enable && port->enable_mask) {
			__usb_port_clock_reset_enable(data, port);
			__usb_bind_driver(data, port);
			dev_info(data->dev, "port0 enable OK\n");
		} else {
			dev_info(data->dev, "port0 is enable\n");
		}
	}

	return count;
}
static DEVICE_ATTR_RW(port0);

static ssize_t port1_show(struct device *dev,
	      struct device_attribute *attr, char *buf)
{
	struct manager_data *data = dev_get_drvdata(dev);
	char *ptr = buf;
	int count = PAGE_SIZE;
	int n;

	n = scnprintf(ptr, count,
		     "Now Port1 is %s\n",
		    data->port1.enable?"enable":"disable");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count,
		     "echo disable > port1 ==> to disable port1\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count,
		     "echo enable > port1 ==> to enable port1\n");
	ptr += n;
	count -= n;

	return ptr - buf;
}

static ssize_t port1_store(struct device *dev,
	       struct device_attribute *attr, const char *buf, size_t count)
{
	struct manager_data *data = dev_get_drvdata(dev);
	struct port_data *port = &data->port1;

	if (!strncmp(buf, "disable", 7)) {
		if ((port->enable & port->enable_mask) == port->enable_mask) {
			__usb_unbind_driver(data, port);
			__usb_port_clock_reset_disable(data, port);
			dev_info(data->dev, "port1 disable OK\n");
		} else {
			dev_info(data->dev, "port1 is disable\n");
		}
	} else if (!strncmp(buf, "enable", 6)) {
		if (!port->enable && port->enable_mask) {
			__usb_port_clock_reset_enable(data, port);
			__usb_bind_driver(data, port);
			dev_info(data->dev, "port1 enable OK\n");
		} else {
			dev_info(data->dev, "port1 is enable\n");
		}
	}

	return count;
}
static DEVICE_ATTR_RW(port1);

static ssize_t port2_show(struct device *dev,
	      struct device_attribute *attr, char *buf)
{
	struct manager_data *data = dev_get_drvdata(dev);
	char *ptr = buf;
	int count = PAGE_SIZE;
	int n;

	n = scnprintf(ptr, count,
		     "Now Port2 is %s\n",
		    data->port2.enable?"enable":"disable");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count,
		     "echo disable > port2 ==> to disable port2\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count,
		     "echo enable > port2 ==> to enable port2\n");
	ptr += n;
	count -= n;

	return ptr - buf;
}

static ssize_t port2_store(struct device *dev,
	       struct device_attribute *attr, const char *buf, size_t count)
{
	struct manager_data *data = dev_get_drvdata(dev);
	struct port_data *port = &data->port2;

	if (!strncmp(buf, "disable", 7)) {
		if ((port->enable & port->enable_mask) == port->enable_mask) {
			__usb_unbind_driver(data, port);
			__usb_port_clock_reset_disable(data, port);
			dev_info(data->dev, "port2 disable OK\n");
		} else {
			dev_info(data->dev, "port2 is disable\n");
		}
	} else if (!strncmp(buf, "enable", 6)) {
		if (!port->enable && port->enable_mask) {
			__usb_port_clock_reset_enable(data, port);
			__usb_bind_driver(data, port);
			dev_info(data->dev, "port2 enable OK\n");
		} else {
			dev_info(data->dev, "port2 is enable\n");
		}
	}

	return count;
}
static DEVICE_ATTR_RW(port2);

static ssize_t port3_show(struct device *dev,
	      struct device_attribute *attr, char *buf)
{
	struct manager_data *data = dev_get_drvdata(dev);
	char *ptr = buf;
	int count = PAGE_SIZE;
	int n;

	n = scnprintf(ptr, count,
		     "Now Port3 is %s\n",
		    data->port3.enable?"enable":"disable");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count,
		     "echo disable > port3 ==> to disable port3\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count,
		     "echo enable > port3 ==> to enable port3\n");
	ptr += n;
	count -= n;

	return ptr - buf;
}

static ssize_t port3_store(struct device *dev,
	       struct device_attribute *attr, const char *buf, size_t count)
{
	struct manager_data *data = dev_get_drvdata(dev);
	struct port_data *port = &data->port3;

	if (!strncmp(buf, "disable", 7)) {
		if ((port->enable & port->enable_mask) == port->enable_mask) {
			__usb_unbind_driver(data, port);
			__usb_port_clock_reset_disable(data, port);
			dev_info(data->dev, "port3 disable OK\n");
		} else {
			dev_info(data->dev, "port3 is disable\n");
		}
	} else if (!strncmp(buf, "enable", 6)) {
		if (!port->enable && port->enable_mask) {
			__usb_port_clock_reset_enable(data, port);
			__usb_bind_driver(data, port);
			dev_info(data->dev, "port3 enable OK\n");
		} else {
			dev_info(data->dev, "port3 is enable\n");
		}
	}

	return count;
}
static DEVICE_ATTR_RW(port3);

static void run_auto_role_swap(struct manager_data *data,
				  struct port_data *port, int _timeout)
{
	struct device *dev = data->dev;
	struct device_node *usb_node;
	struct usb_role_switch *role_sw = NULL;
	int timeout;

	usb_node = port->port_node;

	if (!port->role_sw && usb_node && of_device_is_available(usb_node))
		port->role_sw = usb_role_switch_find_by_fwnode(&usb_node->fwnode);

	role_sw = port->role_sw;
	if (!role_sw)
		return;

	if (usb_role_switch_get_role(role_sw) == USB_ROLE_DEVICE) {
		dev_info(dev, "port%d: Now is device mode switch host mode\n",
			 port->port_num);
		usb_role_switch_set_role(role_sw, USB_ROLE_HOST);
	} else if (usb_role_switch_get_role(role_sw) == USB_ROLE_HOST) {
		dev_info(dev, "port%d: Now is host mode switch device mode\n",
			 port->port_num);
		usb_role_switch_set_role(role_sw, USB_ROLE_DEVICE);
	} else {
		dev_err(dev, "port%d: %s Error mode %d\n",
			port->port_num, __func__, usb_role_switch_get_role(role_sw));
		return;
	}

	if (_timeout > 0)
		timeout = _timeout;
	else
		timeout = port->auto_role_swap_time;

	dev_info(dev, "%s: Auto role swap check after %ds\n", __func__, timeout);
	queue_delayed_work(data->wq_usb_manager, &port->auto_role_swap_work,
			   msecs_to_jiffies(timeout * 1000));
}

static ssize_t port0_auto_role_swap_show(struct device *dev,
	      struct device_attribute *attr, char *buf)
{
	char *ptr = buf;
	int count = PAGE_SIZE;
	int n;

	n = scnprintf(ptr, count,
		     "echo time > auto_role_swap ==> to enable auto role swap\n");
	ptr += n;
	count -= n;

	return ptr - buf;
}

static ssize_t port0_auto_role_swap_store(struct device *dev,
	       struct device_attribute *attr, const char *buf, size_t count)
{
	struct manager_data *data = dev_get_drvdata(dev);
	struct port_data *port;
	int value;

	if (sscanf(buf, "%d", &value) != 1 || value >= INT_MAX || value <= -INT_MAX)
		return -EINVAL;

	port = &data->port0;
	run_auto_role_swap(data, port, value);

	return count;
}
static DEVICE_ATTR_RW(port0_auto_role_swap);

static ssize_t port1_auto_role_swap_show(struct device *dev,
	      struct device_attribute *attr, char *buf)
{
	char *ptr = buf;
	int count = PAGE_SIZE;
	int n;

	n = scnprintf(ptr, count,
		     "echo time > auto_role_swap ==> to enable auto role swap\n");
	ptr += n;
	count -= n;

	return ptr - buf;
}

static ssize_t port1_auto_role_swap_store(struct device *dev,
	       struct device_attribute *attr, const char *buf, size_t count)
{
	struct manager_data *data = dev_get_drvdata(dev);
	struct port_data *port;
	int value;

	if (sscanf(buf, "%d", &value) != 1 || value >= INT_MAX || value <= -INT_MAX)
		return -EINVAL;

	port = &data->port1;
	run_auto_role_swap(data, port, value);

	return count;
}
static DEVICE_ATTR_RW(port1_auto_role_swap);

static ssize_t port2_auto_role_swap_show(struct device *dev,
	      struct device_attribute *attr, char *buf)
{
	char *ptr = buf;
	int count = PAGE_SIZE;
	int n;

	n = scnprintf(ptr, count,
		     "echo time > auto_role_swap ==> to enable auto role swap\n");
	ptr += n;
	count -= n;

	return ptr - buf;
}

static ssize_t port2_auto_role_swap_store(struct device *dev,
	       struct device_attribute *attr, const char *buf, size_t count)
{
	struct manager_data *data = dev_get_drvdata(dev);
	struct port_data *port;
	int value;

	if (sscanf(buf, "%d", &value) != 1 || value >= INT_MAX || value <= -INT_MAX)
		return -EINVAL;

	port = &data->port2;
	run_auto_role_swap(data, port, value);

	return count;
}
static DEVICE_ATTR_RW(port2_auto_role_swap);

static ssize_t port3_auto_role_swap_show(struct device *dev,
	      struct device_attribute *attr, char *buf)
{
	char *ptr = buf;
	int count = PAGE_SIZE;
	int n;

	n = scnprintf(ptr, count,
		     "echo time > auto_role_swap ==> to enable auto role swap\n");
	ptr += n;
	count -= n;

	return ptr - buf;
}

static ssize_t port3_auto_role_swap_store(struct device *dev,
	       struct device_attribute *attr, const char *buf, size_t count)
{
	struct manager_data *data = dev_get_drvdata(dev);
	struct port_data *port;
	int value;

	if (sscanf(buf, "%d", &value) != 1 || value >= INT_MAX || value <= -INT_MAX)
		return -EINVAL;

	port = &data->port3;
	run_auto_role_swap(data, port, value);

	return count;
}
static DEVICE_ATTR_RW(port3_auto_role_swap);

static inline void create_device_files(struct manager_data *data)
{
	struct device *dev = data->dev;

	dev_dbg(data->dev, "%s", __func__);

	if (data->port0.port_num >= 0) {
		device_create_file(dev, &dev_attr_port0_power);
		device_create_file(dev, &dev_attr_port0);
		if (data->port0.is_support_role_sw)
			device_create_file(dev, &dev_attr_port0_auto_role_swap);
	}

	if (data->port1.port_num >= 0) {
		device_create_file(dev, &dev_attr_port1_power);
		device_create_file(dev, &dev_attr_port1);
		if (data->port1.is_support_role_sw)
			device_create_file(dev, &dev_attr_port1_auto_role_swap);
	}

	if (data->port2.port_num >= 0) {
		device_create_file(dev, &dev_attr_port2_power);
		device_create_file(dev, &dev_attr_port2);
		if (data->port2.is_support_role_sw)
			device_create_file(dev, &dev_attr_port2_auto_role_swap);
	}

	if (data->port3.port_num >= 0) {
		device_create_file(dev, &dev_attr_port3_power);
		device_create_file(dev, &dev_attr_port3);
		if (data->port3.is_support_role_sw)
			device_create_file(dev, &dev_attr_port3_auto_role_swap);
	}

	device_create_file(dev, &dev_attr_iso_mode);
}

static inline void remove_device_files(struct manager_data *data)
{
	struct device *dev = data->dev;

	dev_dbg(data->dev, "%s", __func__);

	if (data->port0.port_num >= 0) {
		if (data->port0.is_support_role_sw)
			device_remove_file(dev, &dev_attr_port0_auto_role_swap);
		device_remove_file(dev, &dev_attr_port0_power);
		device_remove_file(dev, &dev_attr_port0);
	}

	if (data->port1.port_num >= 0) {
		if (data->port1.is_support_role_sw)
			device_remove_file(dev, &dev_attr_port1_auto_role_swap);
		device_remove_file(dev, &dev_attr_port1_power);
		device_remove_file(dev, &dev_attr_port1);
	}

	if (data->port2.port_num >= 0) {
		if (data->port2.is_support_role_sw)
			device_remove_file(dev, &dev_attr_port2_auto_role_swap);
		device_remove_file(dev, &dev_attr_port2_power);
		device_remove_file(dev, &dev_attr_port2);
	}

	if (data->port3.port_num >= 0) {
		if (data->port3.is_support_role_sw)
			device_remove_file(dev, &dev_attr_port3_auto_role_swap);
		device_remove_file(dev, &dev_attr_port3_power);
		device_remove_file(dev, &dev_attr_port3);
	}

	device_remove_file(dev, &dev_attr_iso_mode);
}

/* Init */
static void __usb_gpio_init(struct device *dev, struct gpio_data *gpio)
{
	bool off = false;

	gpio->active_port = 0;
	__gpio_on_off(dev, -1, gpio, off);

}

static int rtk_usb_init_gpio(struct manager_data *data)
{
	__usb_gpio_init(data->dev, &data->gpio0);
	__usb_gpio_init(data->dev, &data->gpio1);
	__usb_gpio_init(data->dev, &data->gpio2);
	__usb_gpio_init(data->dev, &data->gpio3);

	return 0;
}

static int rtk_usb_manager_init(struct manager_data *data)
{
	struct device *dev = data->dev;
	struct soc_device_attribute hank_soc[] = {
		{ .family = "Realtek Hank", }, { /* empty */ } };
	struct soc_device_attribute stark_soc[] = {
		{ .family = "Realtek Stark", }, { /* empty */ } };
	struct soc_device_attribute thor_soc[] = {
		{ .family = "Realtek Thor", }, { /* empty */ } };
	struct soc_device_attribute ldo_use_soc[] = {
		{ .family = "Realtek Hank", },
		{ .family = "Realtek Groot", },
		{ .family = "Realtek Stark", }, { /* empty */ } };

	if (soc_device_match(ldo_use_soc) && data->iso_regs)
		regmap_update_bits(data->iso_regs, ISO_USB_CTRL_REG,
				   (unsigned int)ISO_USB_U2PHY_REG_LDO_PW,
				   (unsigned int)ISO_USB_U2PHY_REG_LDO_PW);

#define TP1CK_MEM_TEST1_REG 0x9804ef14
	if (soc_device_match(stark_soc)) {
		void __iomem *reg = ioremap(TP1CK_MEM_TEST1_REG, 0x4);
		int val = readl(reg);

		writel(val | BIT(2), reg);
		udelay(1);
		writel(val & ~(BIT(2)), reg);

		dev_info(data->dev, "TP1CK_MEM_TEST1_REG=%x to toggle bit 2 (val=0x%x)\n",
			 TP1CK_MEM_TEST1_REG, val);

		iounmap(reg);
	}

	if (soc_device_match(hank_soc) && data->iso_regs
		    && data->port2.support_usb3)
		regmap_update_bits(data->iso_regs, ISO_PCIE_USB3PHY_SEL_REG,
				   (unsigned int)ISO_PCIE_USB3PHY_ENABLE,
				   (unsigned int)ISO_PCIE_USB3PHY_ENABLE);

	rtk_usb_set_pd_power(data, 1);

	if (data->disable_usb) {
		dev_err(dev, "Realtek USB No any usb be enabled ....\n");
		return 0;
	}

	rtk_usb_init_clock_reset(data);

	/**
	 * For Thor platform, if type c node enable, then we switch port2 u3phy
	 * to port0.
	 */
	if (soc_device_match(thor_soc) &&
	    (data->port0.type_c.node && data->port0.type_c.is_enable)) {
		void __iomem *port0_wrap_base;
		void __iomem *port2_wrap_base;
		void __iomem *port0_typec_mux_ctrl;
		void __iomem *port2_mac_ctrl;
		int val1, val2;

		port0_wrap_base = of_iomap(data->port0.port_node, 1);
		port2_wrap_base = of_iomap(data->port2.port_node, 0);
		port0_typec_mux_ctrl = port0_wrap_base + WRAP_REG_USB_TC_MUX_CTRL;
		port2_mac_ctrl = port2_wrap_base + WRAP_REG_USB_HMAC_CTR0;

		val1 = readl(port0_typec_mux_ctrl);
		val1 |= WRAP_USB3_TC_MODE;
		val2 = readl(port2_mac_ctrl);
		val2 |= WRAP_HOST_U3_PORT_DIABLE;

		writel(val1, port0_typec_mux_ctrl);
		writel(val2, port2_mac_ctrl);

		pr_info("%s: set type_c mode (0x%x) and disable u3host u3 port (0x%x) for RTD1619\n",
			    __func__, readl(port0_typec_mux_ctrl), readl(port2_mac_ctrl));

		iounmap(port0_wrap_base);
		iounmap(port2_mac_ctrl);
	}

	rtk_usb_init_gpio(data);

	dev_dbg(dev, "Realtek USB init Done\n");

	return 0;
}

static int usb_port_clk_reset_get_legacy(struct manager_data *data,
				     struct port_data *port,
				     struct device_node *port_node,
				     int port_num)
{
	struct device_node *ehci_node;
	struct device_node *u2phy_node;
	int index;
	struct property *prop;
	const char *name;

	ehci_node = of_get_child_by_name(port_node, "ehci");
	if (!ehci_node)
		return -ENODEV;

	index = of_property_match_string(ehci_node, "phy-names", "usb2-phy");
	u2phy_node = of_parse_phandle(ehci_node, "phys", index);
	if (!u2phy_node)
		u2phy_node = of_parse_phandle(ehci_node, "usb-phy", 0);

	index = 0;
	prop = of_find_property(port_node, "clock-names", NULL);
	for (name = of_prop_next_string(prop, NULL);
	     name && index < MAX_CLK_RESET_NUM;
	     name = of_prop_next_string(prop, name), index++) {
		port->clk_mac[index] = usb_clk_get(port_node, name);
	}

	index = 0;
	prop = of_find_property(port_node, "reset-names", NULL);
	for (name = of_prop_next_string(prop, NULL);
	     name && index < MAX_CLK_RESET_NUM;
	     name = of_prop_next_string(prop, name), index++) {
		port->reset_mac[index] = usb_reset_get(port_node, name);
	}

	index = 0;
	prop = of_find_property(u2phy_node, "reset-names", NULL);
	for (name = of_prop_next_string(prop, NULL);
	     name && index < MAX_CLK_RESET_NUM;
	     name = of_prop_next_string(prop, name), index++) {
		port->reset_u2phy[index] = usb_reset_get(u2phy_node, name);
	}

	of_node_put(u2phy_node);
	of_node_put(ehci_node);

	return 0;
}

static int usb_port_clk_reset_get(struct manager_data *data,
				     struct port_data *port,
				     struct device_node *port_node,
				     int port_num)
{
	struct device_node *u2phy_node, *u3phy_node;
	struct device_node *dwc3_node;
	int index;
	struct property *prop;
	const char *name;

	dwc3_node = of_get_compatible_child(port_node, "snps,dwc3");
	if (!dwc3_node)
		dwc3_node = of_get_compatible_child(port_node, "synopsys,dwc3");

	if (!dwc3_node)
		return -ENODEV;

	index = of_property_match_string(dwc3_node, "phy-names", "usb2-phy");
	u2phy_node = of_parse_phandle(dwc3_node, "phys", index);

	index = of_property_match_string(dwc3_node, "phy-names", "usb3-phy");
	u3phy_node = of_parse_phandle(dwc3_node, "phys", index);

	index = 0;
	prop = of_find_property(port_node, "clock-names", NULL);
	for (name = of_prop_next_string(prop, NULL);
	     name && index < MAX_CLK_RESET_NUM;
	     name = of_prop_next_string(prop, name), index++) {
		port->clk_mac[index] = usb_clk_get(port_node, name);
	}

	index = 0;
	prop = of_find_property(port_node, "reset-names", NULL);
	for (name = of_prop_next_string(prop, NULL);
	     name && index < MAX_CLK_RESET_NUM;
	     name = of_prop_next_string(prop, name), index++) {
		port->reset_mac[index] = usb_reset_get(port_node, name);
	}

	index = 0;
	prop = of_find_property(u2phy_node, "reset-names", NULL);
	for (name = of_prop_next_string(prop, NULL);
	     name && index < MAX_CLK_RESET_NUM;
	     name = of_prop_next_string(prop, name), index++) {
		port->reset_u2phy[index] = usb_reset_get(u2phy_node, name);
	}

	index = 0;
	prop = of_find_property(u3phy_node, "reset-names", NULL);
	for (name = of_prop_next_string(prop, NULL);
	     name && index < MAX_CLK_RESET_NUM;
	     name = of_prop_next_string(prop, name), index++) {
		port->reset_u3phy[index] = usb_reset_get(u3phy_node, name);
	}

	of_node_put(u2phy_node);
	of_node_put(u3phy_node);
	of_node_put(dwc3_node);

	return 0;
}

static inline struct gpio_data *__of_parse_port_gpio_mapping(
	    struct manager_data *data,
	    struct device_node *port_node, int port_num)
{
	struct gpio_data *gpio;
	struct gpio_desc *gpio_desc;

	gpio_desc = fwnode_gpiod_get_index(of_fwnode_handle(port_node), "realtek,power", 0,
					   GPIOD_OUT_HIGH | GPIOD_FLAGS_BIT_NONEXCLUSIVE,
					   "usb-power-gpio");
	if (IS_ERR(gpio_desc)) {
		dev_err(data->dev, "port%d power-gpio no found (err=%d)\n",
			 port_num, (int)PTR_ERR(gpio_desc));
		if ((int)PTR_ERR(gpio_desc) == -EPROBE_DEFER)
			return (struct gpio_data *)gpio_desc;
		else
			return NULL;
	}

	gpio = &data->gpio0;
	if (!gpio->gpio_desc)
		gpio->gpio_desc = gpio_desc;

	if (gpio_desc == gpio->gpio_desc) {
		gpio->active_port_mask |= BIT(port_num);
		dev_err(data->dev, "port%d mapping gpio_num=%d on gpio0\n",
			 port_num, desc_to_gpio(gpio->gpio_desc));
		return gpio;
	}

	gpio = &data->gpio1;
	if (!gpio->gpio_desc)
		gpio->gpio_desc = gpio_desc;

	if (gpio_desc == gpio->gpio_desc) {
		gpio->active_port_mask |= BIT(port_num);
		dev_err(data->dev, "port%d mapping gpio_num=%d on gpio1\n",
			 port_num, desc_to_gpio(gpio->gpio_desc));
		return gpio;
	}

	gpio = &data->gpio2;
	if (!gpio->gpio_desc)
		gpio->gpio_desc = gpio_desc;

	if (gpio_desc == gpio->gpio_desc) {
		gpio->active_port_mask |= BIT(port_num);
		dev_err(data->dev, "port%d mapping gpio_num=%d on gpio2\n",
			 port_num, desc_to_gpio(gpio->gpio_desc));
		return gpio;
	}

	gpio = &data->gpio3;
	if (!gpio->gpio_desc)
		gpio->gpio_desc = gpio_desc;

	if (gpio_desc == gpio->gpio_desc) {
		gpio->active_port_mask |= BIT(port_num);
		dev_err(data->dev, "port%d mapping gpio_num=%d on gpio3\n",
			 port_num, desc_to_gpio(gpio->gpio_desc));
		return gpio;
	}

	dev_err(data->dev, "%s port%d No mapping GPIO\n", __func__, port_num);
	return NULL;
}

static void __put_port_gpio(struct manager_data *data)
{
	struct gpio_data *gpio;

	gpio = &data->gpio0;
	if (gpio->gpio_desc)
		gpiod_put(gpio->gpio_desc);
	gpio->gpio_desc = NULL;

	gpio = &data->gpio1;
	if (gpio->gpio_desc)
		gpiod_put(gpio->gpio_desc);
	gpio->gpio_desc = NULL;

	gpio = &data->gpio2;
	if (gpio->gpio_desc)
		gpiod_put(gpio->gpio_desc);
	gpio->gpio_desc = NULL;

	gpio = &data->gpio3;
	if (gpio->gpio_desc)
		gpiod_put(gpio->gpio_desc);
	gpio->gpio_desc = NULL;
}

static const char *const speed_names[] = {
	[USB_SPEED_UNKNOWN] = "UNKNOWN",
	[USB_SPEED_LOW] = "low-speed",
	[USB_SPEED_FULL] = "full-speed",
	[USB_SPEED_HIGH] = "high-speed",
	[USB_SPEED_WIRELESS] = "wireless",
	[USB_SPEED_SUPER] = "super-speed",
	[USB_SPEED_SUPER_PLUS] = "super-speed-plus",
};

static enum usb_device_speed __get_dwc3_maximum_speed(struct device_node *np)
{
	struct device_node *dwc3_np;
	const char *maximum_speed;
	int ret;

	dwc3_np = of_get_compatible_child(np, "snps,dwc3");
	if (!dwc3_np)
		dwc3_np = of_get_compatible_child(np, "synopsys,dwc3");

	if (!dwc3_np)
		return USB_SPEED_UNKNOWN;

	ret = of_property_read_string(dwc3_np, "maximum-speed", &maximum_speed);
	if (ret < 0)
		goto out;

	ret = match_string(speed_names, ARRAY_SIZE(speed_names), maximum_speed);

out:
	of_node_put(dwc3_np);

	return (ret < 0) ? USB_SPEED_UNKNOWN : ret;
}

static bool __is_support_role_sw(struct device_node *np)
{
	struct device_node *dwc3_np;
	bool is_support = false;

	dwc3_np = of_get_compatible_child(np, "snps,dwc3");
	if (!dwc3_np)
		dwc3_np = of_get_compatible_child(np, "synopsys,dwc3");

	if (!dwc3_np)
		return is_support;

	if (of_property_read_bool(dwc3_np, "usb-role-switch"))
		is_support = true;

	of_node_put(dwc3_np);

	return is_support;
}

static int __put_port_setting(struct manager_data *data,
				     struct port_data *port)
{
	int i;

	if (port->is_support_role_sw) {
		cancel_delayed_work_sync(&port->auto_role_swap_work);
		flush_delayed_work(&port->auto_role_swap_work);
		BUG_ON(delayed_work_pending(&port->auto_role_swap_work));
	}

	for (i = 0; i < MAX_CLK_RESET_NUM; i++) {
		usb_clk_put(port->clk_mac[i]);
		usb_reset_put(port->reset_mac[i]);
		usb_reset_put(port->reset_u2phy[i]);
		usb_reset_put(port->reset_u3phy[i]);
	}

	return 0;
}

/* parse for ehci/ohci host port */
static inline int __of_parse_port_setting_legacy(struct manager_data *data,
	    struct port_data *port,
	    struct device_node *port_node, int port_num)
{
	if (!port_node) {
		port->port_num = -1;
		return 0;
	}

	port->port_num = port_num;
	port->manager_data = data;
	port->port_node = port_node;
	/* slave_mac_node for ohci */
	port->slave_mac_node = of_get_child_by_name(port_node, "ohci");

	/* parse the clk and reset of port */
	usb_port_clk_reset_get_legacy(data, port, port_node, port_num);

	port->active = 0;
	port->enable_mask = 0;
	port->enable = 0;
	port->host_add_count = 0;
	port->bus_add_count = 0;

	if (port->port_node &&
		    of_device_is_available(port->port_node)) {
		pr_info("port_node: %s status is okay\n",
			    port->port_node->name);
		port->enable_mask |= BIT(0);

		if (port->slave_mac_node &&
			    of_device_is_available(port->slave_mac_node)) {
			pr_info("slave_mac_node: %s status is okay\n",
				    port->slave_mac_node->name);
			port->enable_mask |= BIT(1);
		}
	}

	if (port->enable_mask) {
		struct gpio_data *power_gpio;

		power_gpio = __of_parse_port_gpio_mapping(data, port_node,
							        port->port_num);
		if (IS_ERR(power_gpio))
			return (int)PTR_ERR(power_gpio);
		else
			port->power_gpio = power_gpio;
	}

	/* set port status is enable by enable_mask */
	port->enable = port->enable_mask;

	return 0;
}

static void auto_role_swap_func(struct work_struct *work);

static inline int __of_parse_port_setting(struct manager_data *data,
	    struct port_data *port,
	    struct device_node *port_node, int port_num)
{
	struct device_node *dev_node;
	enum usb_device_speed maximum_speed;

	if (!port_node) {
		port->port_num = -1;
		return 0;
	}

	port->port_num = port_num;
	port->manager_data = data;
	port->port_node = port_node;
	port->slave_mac_node = NULL;

	dev_node = of_get_child_by_name(data->dev->of_node, "type-c");
	if (dev_node) {
		struct device_node *port_node;

		port_node = of_parse_phandle(dev_node, "realtek,usb-port", 0);
		if (port_node && port_node == port->port_node) {
			struct type_c_data *type_c= &port->type_c;

			type_c->node = dev_node;
			if (of_device_is_available(dev_node))
				type_c->is_enable = true;
			type_c->reset_type_c = usb_reset_get(dev_node, "type-c");

			type_c->connector_switch_gpio =
				fwnode_gpiod_get_index(of_fwnode_handle(dev_node), "realtek,connector-switch", 0,
						       GPIOD_OUT_HIGH, "usb-connector_switch");
			if (IS_ERR(type_c->connector_switch_gpio))
				dev_info(data->dev, "type c connector-switch-gpio no found");
			else
				dev_info(data->dev, "type c get connector-switch-gpio (id=%d) OK\n",
					 desc_to_gpio(type_c->connector_switch_gpio));
			type_c->plug_side_switch_gpio =
				fwnode_gpiod_get_index(of_fwnode_handle(dev_node), "realtek,plug-side-switch", 0,
						       GPIOD_OUT_HIGH, "usb-plug-side-switch");
			if (IS_ERR(type_c->plug_side_switch_gpio))
				dev_info(data->dev, "type c plug-side-switch-gpio no found");
			else
				dev_info(data->dev, "type c get plug-side-switch-gpio (id=%d) OK\n",
					 desc_to_gpio(type_c->plug_side_switch_gpio));
		}
		of_node_put(port_node);
	}

	port->is_support_role_sw = __is_support_role_sw(port_node);
	if (port->is_support_role_sw) {
		INIT_DELAYED_WORK(&port->auto_role_swap_work, auto_role_swap_func);
		port->auto_role_swap_time = DEFAULT_AUTO_ROLE_SWAP_TIME;
		dev_dbg(data->dev, "Set port%d auto_role_swap_time %ds\n",
			 port_num, port->auto_role_swap_time);
	}

	/* parse the clk and reset of port */
	usb_port_clk_reset_get(data, port, port_node, port_num);

	port->active = 0;
	port->enable_mask = 0;
	port->enable = 0;
	port->host_add_count = 0;
	port->bus_add_count = 0;

	if (port->port_node &&
		    of_device_is_available(port->port_node)) {
		pr_info("port_node: %s status is okay\n",
			    port->port_node->name);
		port->enable_mask |= BIT(0);

		if (port->slave_mac_node &&
			    of_device_is_available(port->slave_mac_node)) {
			pr_info("slave_mac_node: %s status is okay\n",
				    port->slave_mac_node->name);
			port->enable_mask |= BIT(1);
		}
	}

	if (port->enable_mask) {
		struct gpio_data *power_gpio;

		power_gpio = __of_parse_port_gpio_mapping(data, port_node,
							        port->port_num);
		if (IS_ERR(power_gpio))
			return (int)PTR_ERR(power_gpio);
		else
			port->power_gpio = power_gpio;
	}

	/* set port status is enable by enable_mask */
	port->enable = port->enable_mask;

	maximum_speed = __get_dwc3_maximum_speed(port_node);
	if (maximum_speed != USB_SPEED_UNKNOWN && maximum_speed <= USB_SPEED_HIGH)
		port->support_usb3 = false;
	else
		port->support_usb3 = true;

	return 0;
}

static int parse_port_setting(struct manager_data *data,
	    struct device_node *node)
{
	struct device_node	*sub_node;
	int ret = 0;

	data->disable_usb = true;

	sub_node = of_get_child_by_name(node, "usb-port0");
	ret = __of_parse_port_setting(data, &data->port0, sub_node, 0);
	if (ret)
		goto out;
	sub_node = of_get_child_by_name(node, "usb-port1");
	ret = __of_parse_port_setting(data, &data->port1, sub_node, 1);
	if (ret)
		goto out;
	sub_node = of_get_child_by_name(node, "usb-port2");
	ret = __of_parse_port_setting(data, &data->port2, sub_node, 2);
	if (ret)
		goto out;
	sub_node = of_get_child_by_name(node, "usb-port3");
	ret = __of_parse_port_setting_legacy(data, &data->port3, sub_node, 3);
	if (ret)
		goto out;

	if (port_can_enable(&data->port0) ||
		    port_can_enable(&data->port1) ||
		    port_can_enable(&data->port2) ||
		    port_can_enable(&data->port3))
		data->disable_usb = false;

out:
	return ret;
}

static int put_port_setting(struct manager_data *data)
{
	__put_port_setting(data, &data->port0);
	__put_port_setting(data, &data->port1);
	__put_port_setting(data, &data->port2);
	__put_port_setting(data, &data->port3);

	__put_port_gpio(data);

	return 0;
}

static int gadget_match(struct device *dev, void *data)
{
	char *name = data;

	return (sysfs_streq(dev_name(dev), name));
}

static bool __check_udc_configured(struct manager_data *data, struct device *dev)
{
	struct device *gadget_dev;
	struct usb_gadget *gadget;
	bool configured = false;

	gadget_dev = device_find_child(dev, "gadget", gadget_match);

	if (!gadget_dev)
		return configured;

	gadget = dev_to_usb_gadget(gadget_dev);

	if (gadget && gadget->state == USB_STATE_CONFIGURED)
		configured = true;

	put_device(gadget_dev);
	return configured;
}

static void auto_role_swap_func(struct work_struct *work)
{
	struct port_data *port = container_of(work, struct port_data,
					      auto_role_swap_work.work);
	struct manager_data *data = port->manager_data;
	struct device *dev = data->dev;
	struct device_node *usb_node;
	struct device *usb_dev = NULL;
	struct usb_role_switch *role_sw = NULL;

	usb_node = port->port_node;
	if (usb_node && of_device_is_available(usb_node)) {
		struct platform_device *pdev = NULL;

		pdev = of_find_device_by_node(usb_node);
		if (pdev != NULL)
			usb_dev = &pdev->dev;

		if (!port->role_sw)
			port->role_sw = usb_role_switch_find_by_fwnode(&usb_node->fwnode);
	}

	role_sw = port->role_sw;

	if (!usb_dev)
		return;
	if (!role_sw)
		return;

	if (usb_role_switch_get_role(role_sw) == USB_ROLE_DEVICE) {
		struct device_node *dwc3_node;
		struct platform_device *dwc3_pdev = NULL;
		struct device *dwc3_dev;

		dwc3_node = of_get_compatible_child(usb_node, "snps,dwc3");

		if (!dwc3_node)
			dwc3_node = of_get_compatible_child(usb_node, "synopsys,dwc3");
		if (!dwc3_node) {
			dev_err(dev, "failed to find dwc3 core node\n");
			return;
		}

		dwc3_pdev = of_find_device_by_node(dwc3_node);

		if (!dwc3_pdev) {
			dev_err(dev, "failed to find dwc3 core platform_device\n");
			of_node_put(dwc3_node);
			return;
		}

		of_node_put(dwc3_node);
		dwc3_dev = &dwc3_pdev->dev;
		if (!__check_udc_configured(data, dwc3_dev)) {
			dev_info(dev, "port%d %s: In device mode, NO host connect. Switch to Host mode\n",
				 port->port_num, dev_name(dwc3_dev));
			usb_role_switch_set_role(role_sw, USB_ROLE_HOST);
		}
	} else if (usb_role_switch_get_role(role_sw) == USB_ROLE_HOST) {
		if (!port->is_linked_on_host_mode) {
			dev_info(dev, "port%d: In host mode, NO device connect. Switch to Device mode\n",
				 port->port_num);
			usb_role_switch_set_role(role_sw, USB_ROLE_DEVICE);
		}
	} else {
		dev_dbg(dev, "%s: %s is unknown mode (%d)\n",
			__func__, dev_name(usb_dev), usb_role_switch_get_role(role_sw));
	}
}

static const struct of_device_id of_bus_match_table[] = {
	{ .compatible = "simple-bus", },
	{} /* Empty terminated list */
};

static void rtk_usb_subnode_probe_work(struct work_struct *work)
{
	struct manager_data *data = container_of(work, struct manager_data, delayed_work.work);
	struct device		*dev = data->dev;
	struct device_node	*node = dev->of_node;
	int    ret = 0;
	unsigned long probe_time = jiffies;

	dev_info(dev, "%s Start ...\n", __func__);

	if (!data->child_node_initiated) {
		dev_info(dev, "%s populate subnode device\n", __func__);
		ret = of_platform_populate(node, of_bus_match_table, NULL, dev);
		if (ret)
			dev_err(dev, "%s failed to add subnode device (ret=%d)\n",
				     __func__, ret);
		else
			data->child_node_initiated = true;
	}

	ret = rtk_usb_setup_type_c_device(data);
	if (ret == -EPROBE_DEFER)
		queue_delayed_work(data->wq_usb_manager, &data->delayed_work,
			msecs_to_jiffies(1000));

	dev_info(dev, "%s ... End (take %d ms)\n", __func__,
		    jiffies_to_msecs(jiffies - probe_time));
}

static int rtk_usb_manager_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct manager_data *data;
	int ret = 0;
	unsigned long probe_time = jiffies;

	dev_info(dev, "ENTER %s", __func__);

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;

	mutex_init(&data->lock);

	data->iso_regs = syscon_regmap_lookup_by_phandle(node, "realtek,iso");
	if (IS_ERR(data->iso_regs)) {
		dev_dbg(dev, "%s: DTS no set iso regs syscon\n", __func__);
		data->iso_regs = NULL;
	}

	data->clk_usb = usb_clk_get(dev->of_node, "usb");
	data->reset_usb = usb_reset_get(dev->of_node, "usb");

	ret = parse_port_setting(data, node);
	if (ret)
		goto err;

	ret = rtk_usb_manager_init(data);
	if (ret)
		goto err;

	data->wq_usb_manager = create_singlethread_workqueue("rtk_usb_manager");

	/* Notifications */
	data->power_nb.notifier_call = rtk_usb_port_add_remove_notify;
	usb_register_notify(&data->power_nb);

	create_debug_files(data);

	create_device_files(data);

	data->usb_ctrl_compat_class = class_compat_register("usb_ctrl");
	if (!data->usb_ctrl_compat_class) {
		dev_warn(dev, "Failed to register class_compat for usb_ctrl\n");
	} else {
		ret = class_compat_create_link(
			    data->usb_ctrl_compat_class, dev, NULL);
		if (ret)
			dev_warn(dev, "Failed to create compatibility class link\n");
	}

	platform_set_drvdata(pdev, data);

	INIT_DELAYED_WORK(&data->delayed_work, rtk_usb_subnode_probe_work);
	if (!of_property_read_bool(node, "realtek,non-delay-subnode-probe"))
		queue_delayed_work(data->wq_usb_manager, &data->delayed_work, 0);
	else
		rtk_usb_subnode_probe_work(&data->delayed_work.work);

	dev_info(&pdev->dev, "%s OK (take %d ms)\n", __func__,
		    jiffies_to_msecs(jiffies - probe_time));
	return 0;

err:
	put_port_setting(data);
	usb_clk_put(data->clk_usb);
	usb_reset_put(data->reset_usb);

	devm_kfree(dev, data);

	dev_info(&pdev->dev, "%s Fail (ret=%d) (take %d ms)\n", __func__,
		    ret, jiffies_to_msecs(jiffies - probe_time));

	return ret;
}

static int rtk_usb_manager_remove(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct manager_data *data = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&data->delayed_work);
	flush_delayed_work(&data->delayed_work);
	BUG_ON(delayed_work_pending(&data->delayed_work));

	if (data->usb_ctrl_compat_class) {
		class_compat_remove_link(data->usb_ctrl_compat_class, dev, NULL);
		class_compat_unregister(data->usb_ctrl_compat_class);
	}

	remove_device_files(data);

	remove_debug_files(data);

	usb_unregister_notify(&data->power_nb);

	put_port_setting(data);
	usb_clk_put(data->clk_usb);
	usb_reset_put(data->reset_usb);

	return 0;
}

static void rtk_usb_manager_shutdown(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct manager_data *data = dev_get_drvdata(dev);

	dev_info(dev, "[USB] Enter %s S5 (shutdown)\n", __func__);

	cancel_delayed_work_sync(&data->delayed_work);
	flush_delayed_work(&data->delayed_work);
	BUG_ON(delayed_work_pending(&data->delayed_work));

	rtk_usb_init_gpio(data);
	rtk_usb_set_pd_power(data, 0);
	rtk_usb_clear_clock_reset(data);

	dev_info(dev, "[USB] Exit %s\n", __func__);
}

static const struct of_device_id rtk_usb_manager_match[] = {
	{ .compatible = "realtek,usb-manager" },
	{},
};
MODULE_DEVICE_TABLE(of, rtk_usb_manager_match);

#ifdef CONFIG_PM_SLEEP

static int rtk_usb_manager_prepare(struct device *dev)
{
	return 0;
}

static void rtk_usb_manager_complete(struct device *dev)
{
	/* nothing */
}

static int rtk_usb_manager_suspend(struct device *dev)
{
	struct manager_data *data = dev_get_drvdata(dev);

	dev_info(dev, "[USB] Enter %s\n", __func__);

	if (!data->usb_iso_mode) {
		usb_port_power_on_off(data, false);
		rtk_usb_set_pd_power(data, 0);
		rtk_usb_clear_clock_reset(data);
	}

	dev_info(dev, "[USB] Exit %s\n", __func__);
	return 0;
}

static int rtk_usb_manager_resume(struct device *dev)
{
	struct manager_data *data = dev_get_drvdata(dev);

	dev_info(dev, "[USB] Enter %s\n", __func__);

	if (!data->usb_iso_mode) {
		rtk_usb_set_pd_power(data, 1);

		rtk_usb_init_clock_reset(data);
	}
	usb_port_power_on_off(data, true);

	dev_info(dev, "[USB] Exit %s\n", __func__);
	return 0;
}

static const struct dev_pm_ops rtk_usb_manager_pm_ops = {
	.prepare = rtk_usb_manager_prepare,
	.complete = rtk_usb_manager_complete,
	SET_LATE_SYSTEM_SLEEP_PM_OPS(rtk_usb_manager_suspend,
	    rtk_usb_manager_resume)
};

#define DEV_PM_OPS	(&rtk_usb_manager_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver rtk_usb_manager_driver = {
	.probe		= rtk_usb_manager_probe,
	.remove		= rtk_usb_manager_remove,
	.shutdown	= rtk_usb_manager_shutdown,
	.driver		= {
		.name	= "rtk-usb-manager",
		.of_match_table = rtk_usb_manager_match,
		.pm = DEV_PM_OPS,
	},
};

static int __init rtk_usb_manager_driver_init(void)
{
	return platform_driver_register(&(rtk_usb_manager_driver));
}
module_init(rtk_usb_manager_driver_init);

static void __exit rtk_usb_manager_driver_exit(void)
{
	platform_driver_unregister(&(rtk_usb_manager_driver));
}
module_exit(rtk_usb_manager_driver_exit);

MODULE_AUTHOR("Stanley Chang <stanley_chang@realtek.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek rtd SoC usb manager driver");
