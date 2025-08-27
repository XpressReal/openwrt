#ifndef __RTK_CLK_DET_H
#define __RTK_CLK_DET_H

struct device;
struct clk;

enum {
	CLK_DET_TYPE_GENERIC = 0,
	CLK_DET_TYPE_CRT = 0,     /* for compatible */
	CLK_DET_TYPE_SC_WRAP = 1,
	CLK_DET_TYPE_HDMI_TOP = 2,
};

struct clk_det_initdata {
        const char *name;
        void __iomem *base;
        uint32_t type;
        struct clk *ref;
};

#if IS_ENABLED(CONFIG_RTK_CLK_DET)

struct clk *devm_clk_det_register(struct device *dev, const struct clk_det_initdata *initdata);
void devm_clk_det_unregister(struct clk *clk);

#else
static inline struct clk *devm_clk_det_register(struct device *dev, const struct clk_det_initdata *initdata)
{
	return ERR_PTR(-EINVAL);
}

static inline void devm_clk_det_unregister(struct clk *clk)
{}
#endif /* IS_ENABLED(CONFIG_RTK_CLK_DET) */

#endif /* __RTK_CLK_DET_H */
