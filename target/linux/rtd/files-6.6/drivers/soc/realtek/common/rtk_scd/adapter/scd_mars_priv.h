// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * Copyright (c) 2019 Realtek Semiconductor Corp.
 */

#ifndef __SCD_MARS_PRIV_H__
#define __SCD_MARS_PRIV_H__

#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/kfifo.h>
#include "venus_gpio.h"
#include "../core/scd.h"
#include "../core/scd_atr.h"

#define ID_MARS_SCD(x)          (0x12830000 | (x & 0x3))
#define MARS_SCD_CHANNEL(id)    (id & 0x03)

#define MISC_IRQ	3
#define RX_RING_LENGTH  32
#define TX_RING_LENGTH  32

// oo>> enable one of these, TX polling if all disable.
//#define SCD_MARS_TX_INT	// note: this macro need fined tune to fit melon ICC 3452, 3462, 3472, 3482.
#define SCD_MARS_TX_INT_TXDONE	// SC_TX_DONE means one byte sent


#define ENABLE_ATR_DYNAMIC_TIMEOUT

#ifdef ENABLE_ATR_DYNAMIC_TIMEOUT
typedef enum {
	ATR_PHASE_ap_init = 0,             // set it in reset api, timeout_ap_1
	ATR_PHASE_ap_reset,                // set it if in FSM_reset, timeout_int_1
	ATR_PHASE_isr_get,                 // ISR gets some pieces of ATR, timeout_int_2
	ATR_PHASE_ap_ack,                  // set it in reset api, timeout_ap_2
	ATR_PHASE_unused = 0xFF,           // set it in reset api when exit.
} ATR_PHASE_e;

#define ATR_PHASE_TIMEOUT_JIFFIES_ap_1         (HZ<<1)
#define ATR_PHASE_TIMEOUT_JIFFIES_ap_1_chili   (HZ)			// SA5-2419
#define ATR_PHASE_TIMEOUT_JIFFIES_int_1        (HZ)
#define ATR_PHASE_TIMEOUT_JIFFIES_int_2        (HZ*13)
#define ATR_PHASE_TIMEOUT_JIFFIES_ap_2         (HZ*14)


#endif // ENABLE_ATR_DYNAMIC_TIMEOUT

//#define CONFIG_SMARTCARD_FAST_HOTPLUG_PROTECTION  determined in final linux-kernel/.config

////////////////////////////////////////////////
// oo>> trace for sa5-2612, for clk/etu trace, should by sync with SCD
//#define CLK_ETU_TRACE_mask          (0xFF)
#ifdef CLK_ETU_TRACE_mask
#define CLK_ETU_TRACE_shift_bit     (24)

#define CLK_ETU_TRACE_cas_lib       (0x01)
#define CLK_ETU_TRACE_atr           (0x02)
#define CLK_ETU_TRACE_middle_reset  (0x04)
#define CLK_ETU_TRACE_scctrl        (0x08)
#define CLK_ETU_TRACE_scd           (0x10)  // used only in SCD

#define CLK_ETU_TRACE_set(_val, _from, _type) \
            do { (_val) = (_type)((_val) | ((_from)<<CLK_ETU_TRACE_shift_bit)) ; } while(0)

#define CLK_ETU_TRACE_get(_val, _from) \
            do { \
                (_from) =  ( (_val) >> CLK_ETU_TRACE_shift_bit ); \
                (_val)  =  (_val) & (~(CLK_ETU_TRACE_mask<<CLK_ETU_TRACE_shift_bit)); \
            } while(0)

#endif // CLK_ETU_TRACE_mask

//////////////////////////////////////////////// for clk/etu change....
#define CLK_ETU_FLAG_flush					(0x00000001)
//#define CLK_ETU_FLAG_clr_act_seq			(0x00000002)
////////////////////////////////////////////////

typedef enum {
	IFD_FSM_UNKNOWN,
	IFD_FSM_DISABLE,        // function disabled
	IFD_FSM_DEACTIVATE,     // function enabled but not reset the card yet
	IFD_FSM_RESET,          // reset the card and waiting for ATR
	IFD_FSM_ACTIVE,         // card activated
} IFD_FSM;

//#define ISR_POLLING

struct clk;
struct reset_control;

typedef struct mars_dts_info
{
	void __iomem        *misc;
	void __iomem        *base;
	unsigned int        irq;
	unsigned int        id;
	unsigned char       cmd_vcc_en;
	unsigned char       cmd_vcc_polarity;
	unsigned char       pwr_sel_en;
	unsigned char       pwr_sel_polarity;
	VENUS_GPIO_ID       pin_cmd_vcc;        // command vcc
	VENUS_GPIO_ID       pin_pwr_sel0;       // power sel
	VENUS_GPIO_ID	    pin_pwr_sel1;       // power sel

	struct clk *clk;
	struct clk *clk_sel;
	struct reset_control *rstc;

	int pwr_sel_pin_x2 : 1;

	struct pinctrl		*pinctrl;
	struct pinctrl_state    *pins_default;
	struct pinctrl_state    *pins_data0;
	struct pinctrl_state    *pins_data1;
	struct pinctrl_state    *pins_data2;

} MARS_DTS_INFO_T;

typedef struct mars_scd_tag   mars_scd;

struct mars_scd_tag
{
	unsigned int        id;         // channel id
	void __iomem 	    *base;
	unsigned int	    irq;
	IFD_FSM             fsm;        // finate state machine of ifd
	unsigned char       card_status_change;

#ifdef CONFIG_SMARTCARD_FAST_HOTPLUG_PROTECTION
	#define THREAD_NAME "SCD_thread"
	#define THREAD_SLEEP_MS		(1000)		// sleep time in this thread
	#define CPRES_THRESHOLD		(4)			// CPRES occurrence to trigger fast_hotplug_mode

	unsigned int        card_status_change_count; // increased by ISR, clear by kthread
	unsigned char       fast_hotplug_mode;  // modified by kthread, read by ISR

	struct task_struct  *kthread;   // handle of scd thread
#endif // CONFIG_SMARTCARD_FAST_HOTPLUG_PROTECTION

	unsigned char       tx_status;
	#define SC_TX_DONE          0x1
	#define SC_TX_PARITY_ERROR  0x2

	struct clk *clk;
	struct clk *clk_sel;
	struct reset_control *rstc;

	// config
	unsigned int	    sys_clk;
	unsigned char       clock_div;          // clock divider
	unsigned char       pre_clock_div;      // pre_clock divider
	unsigned long       baud_div1;
	unsigned long       baud_div2;
	unsigned char       parity;
	unsigned char       pwr_on;

	unsigned char       vcc: 1;
	unsigned char       vcc_temp: 1;
	unsigned char       cmd_vcc_en : 1;
	unsigned char       cmd_vcc_polarity : 1;
	unsigned char       pwr_sel_en : 1;
	unsigned char       pwr_sel_polarity : 1;
	VENUS_GPIO_ID       pin_cmd_vcc;         // command vcc
	VENUS_GPIO_ID       pin_pwr_sel0;         // power sel
	VENUS_GPIO_ID       pin_pwr_sel1;         // power sel

	// atr
	scd_atr             atr;               // current atr
	scd_atr_info        atr_info;          // current atr
	unsigned long       atr_timeout;

#if(1) //#ifdef CONFIG_SMARTCARD_NX_ATR_TIMEOUT_CHECK
	unsigned long       internal_timer_base;
	unsigned long       atr_activation_timeout;
	unsigned long       atr_char2char_timeout;
	unsigned long       atr_max_char2char;           // for debug to dump max char2char after reset
	int                 g_error_activation_timeout;
	int                 g_error_char2char_timeout;
#endif // CONFIG_SMARTCARD_NX_ATR_TIMEOUT_CHECK

	wait_queue_head_t   wq;
	struct kfifo        rx_fifo;
	struct kfifo        tx_fifo;

#if defined (SCD_MARS_TX_INT) || defined (SCD_MARS_TX_INT_TXDONE)
	#define TX_INT_LOG_MAX_LEN         (1024*2)
	struct kfifo        tx_int_log_fifo;
#endif

#ifdef ENABLE_ATR_DYNAMIC_TIMEOUT
	ATR_PHASE_e         e_atr_phase;
#endif // ENABLE_ATR_DYNAMIC_TIMEOUT

	spinlock_t          rx_fifo_lock;

#ifdef ISR_POLLING
	struct timer_list   timer;
#endif

	spinlock_t          lock;
	struct mutex	mutex_lock;
	struct list_head    list;

	int pwr_sel_pin_x2 : 1;

	struct pinctrl		*pinctrl;
	struct pinctrl_state    *pins_default;
	struct pinctrl_state    *pins_data0;
	struct pinctrl_state    *pins_data1;
	struct pinctrl_state    *pins_data2;
	unsigned int		pin;
	unsigned int		flow_en;

	unsigned int        rx_timeout;
	SC_CAS              cas;
};

typedef enum {
	CTL_DISABLE = 0,
	CTL_ENABLE  = 1
} SCD_CLK_CTL;

#define MARS_SCD_SET_CLOCK( _p, _clk, _flags, _ret)	\
	do { \
		(_ret) = mars_scd_set_clock( (_p), (_clk), (_flags) ); \
		SC_INFO("set clk(%lu) done\n", (_clk) ); \
	} while(0)

#define MARS_SCD_SET_ETU( _p, _etu, _flags, _ret)	\
	do { \
		(_ret) = mars_scd_set_etu( (_p), (_etu), (_flags) ); \
		SC_INFO("set etu(%lu) done\n", (_etu) ); \
	} while(0)

#define MARS_SCD_SET_BAUD_RATE( _p, _clk, _etu, _flags, _ret)	\
	do { \
		(_ret) = mars_scd_set_baud_rate( (_p), (_clk), (_etu), (_flags) ); \
		SC_INFO("set baud(%d, %d) done\n", (_clk), (_etu) ); \
	} while(0)


extern mars_scd* mars_scd_open(unsigned char id);
extern void mars_scd_close(mars_scd* p_this);

extern int mars_scd_enable(mars_scd* p_this, unsigned char on_off);

extern int mars_scd_set_vcc_lvl(mars_scd *p_this, unsigned long vcc);
extern int mars_scd_get_vcc_lvl(mars_scd *p_this, unsigned long *vcc);

extern int mars_scd_set_clock(mars_scd* p_this, unsigned long clock, unsigned int flags);
extern int mars_scd_set_etu(mars_scd* p_this, unsigned long etu, unsigned int flags);
extern int mars_scd_set_baud_rate(mars_scd* p_this, unsigned long clk, unsigned long etu, uint32_t flags);
extern int mars_scd_set_parity(mars_scd* p_this, SC_PARITY parity);

extern int mars_scd_get_clock(mars_scd* p_this, unsigned long* p_clock);
extern int mars_scd_get_etu(mars_scd* p_this, unsigned long* p_etu);
extern int mars_scd_get_baud_rate(mars_scd* p_this, unsigned long *p_clk, unsigned long *p_etu);
extern int mars_scd_get_parity(mars_scd* p_this, SC_PARITY* p_parity);

extern int mars_scd_set_bgt(mars_scd* p_this, unsigned long bgt);
extern int mars_scd_get_bgt(mars_scd* p_this, unsigned long* p_bgt);

extern int mars_scd_set_pin(mars_scd* p_this, unsigned long pin);
extern int mars_scd_get_pin(mars_scd* p_this, unsigned long *pin);

extern int mars_scd_set_protocol(mars_scd* p_this, unsigned long protocol);
extern int mars_scd_get_protocol(mars_scd* p_this, unsigned long* p_protocol);

extern int mars_scd_set_bwi(mars_scd* p_this, unsigned long bwi);
extern int mars_scd_get_bwi(mars_scd* p_this, unsigned long* p_bwi);
extern int mars_scd_set_cwi(mars_scd* p_this, unsigned long cwi);
extern int mars_scd_get_cwi(mars_scd* p_this, unsigned long* p_cwi);
extern int mars_scd_set_guard_interval(mars_scd* p_this, unsigned long guard_interval);
extern int mars_scd_get_guard_interval(mars_scd* p_this, unsigned long* p_guard_interval);

extern int inline mars_scd_set_convention(mars_scd* p_this, SC_CONV conv);
extern int inline mars_scd_get_convention(mars_scd* p_this, SC_CONV* p_conv);

extern int mars_scd_set_pwr(mars_scd* p_this, unsigned long pwr);
extern int mars_scd_get_pwr(mars_scd* p_this, unsigned long *pwr);

extern int mars_scd_set_rx_timeout(mars_scd* p_this, unsigned long to_ms);
extern int mars_scd_get_rx_timeout(mars_scd* p_this, unsigned long* p_to_ms);

extern int mars_scd_set_flowcontrol(mars_scd* p_this, unsigned long flowctl);
extern int mars_scd_get_flowcontrol(mars_scd* p_this, unsigned long* flowctl);

extern int mars_scd_set_cas(mars_scd* p_this, SC_CAS cas);
extern int mars_scd_get_cas(mars_scd* p_this, SC_CAS* p_cas);

extern int mars_scd_activate         (mars_scd* p_this);
extern int mars_scd_deactivate       (mars_scd* p_this);
extern int mars_scd_reset            (mars_scd* p_this);
extern int mars_scd_get_atr          (mars_scd* p_this, scd_atr* p_atr);
extern int mars_scd_card_detect      (mars_scd* p_this);
extern int mars_scd_get_card_status  (mars_scd* p_this);
extern int mars_scd_poll_card_status (mars_scd* p_this);
extern int mars_scd_xmit             (mars_scd* p_this, unsigned char* p_data, unsigned int len);
extern int mars_scd_read             (mars_scd* p_this, unsigned char* p_data, unsigned int len);

#endif //__SCD_MARS_PRIV_H__
