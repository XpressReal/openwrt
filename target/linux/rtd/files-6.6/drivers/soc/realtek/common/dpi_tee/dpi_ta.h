/*
 * Copyright (c) 2023 Realtek Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __DPI_TA_H
#define __DPI_TA_H

enum dpi_ta_cmd {
	/* DPI_TA_CMD_SETUP_PARAMS
	 *
	 * params[0].memref: struct dpi_params
	 */
	DPI_TA_CMD_SETUP_PARAMS = 0,

	/* DPI_TA_CMD_UPDATE_THERMAL_STATE
	*
	 * params[0].value.a: [in] new thermal state
	 */
	DPI_TA_CMD_UPDATE_THERMAL_STATE = 1,

	/* DPI_TA_CMD_UPDATE_THERMAL_DELTA
	 */
	DPI_TA_CMD_UPDATE_THERMAL_DELTA = 2
};

struct dpi_rx_dq_cal_params {
	uint32_t enabled;
	uint32_t delta_width;
	uint32_t cal_time;
	uint32_t flow_type;
};

#define DPI_RX_ODT_SEL_NUM_MAX  10

struct dpi_rx_odt_sel_params {
	uint32_t enabled;
	uint32_t odt_idx_q[DPI_RX_ODT_SEL_NUM_MAX];
	uint32_t odt_val_q_0[DPI_RX_ODT_SEL_NUM_MAX];
	uint32_t odt_val_q_1[DPI_RX_ODT_SEL_NUM_MAX];
	uint32_t num_q;
};

#define DPI_TX_OCD_SEL_NUM_MAX 10

struct dpi_tx_ocd_sel_params {
	uint32_t enabled;
	uint32_t ocd_idx_q[DPI_TX_OCD_SEL_NUM_MAX];
	uint32_t ocd_val_q_0[DPI_TX_OCD_SEL_NUM_MAX];
	uint32_t ocd_val_q_1[DPI_TX_OCD_SEL_NUM_MAX];
	uint32_t num_q;
};

#define DPI_TX_PHASE_PI_NUM_MAX 10

struct dpi_tx_phase_params {
	uint32_t enabled;
	int32_t pi_idx_q[DPI_TX_PHASE_PI_NUM_MAX];
	int32_t pi_val_q_0[DPI_TX_PHASE_PI_NUM_MAX];
	int32_t pi_val_q_1[DPI_TX_PHASE_PI_NUM_MAX];
	uint32_t num_q;
};

struct dpi_zq_cal_params {
	uint32_t enabled;
	uint32_t zq_vref[3];
	uint32_t zprog[5];
	uint32_t zq_nocd2_en[5];
	uint32_t zprog_nocd2[5];
	uint32_t zq_vref_num;
	uint32_t zprog_num;
	uint32_t zq_nocd2_en_num;
	uint32_t zprog_nocd2_num;
};

struct dpi_temp_params {
	int temp_hot;
	int temp_cold;
	int temp_delta;
};

struct dpi_init_pararms {
	uint32_t ddr_speed_enabled;
	uint32_t ddr_speed;
};

struct dpi_params {
	struct dpi_init_pararms        init;
	struct dpi_temp_params         temp;
	struct dpi_rx_dq_cal_params    rx_dq_cal;
	struct dpi_rx_odt_sel_params   rx_odt_sel;
	struct dpi_tx_ocd_sel_params   tx_ocd_sel;
	struct dpi_tx_phase_params     tx_phase;;
	struct dpi_zq_cal_params       zq_cal;
};

#define DPI_THERMAL_STATE_NORMAL   0
#define DPI_THERMAL_STATE_HOT      1
#define DPI_THERMAL_STATE_COLD     2

#endif /*__DPI_TA_H */
