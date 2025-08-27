/*
 * Copyright (c) 2022 Realtek Semiconductor Corp.
 */

#ifndef __SCD_MARS_CHILI__
#define __SCD_MARS_CHILI__

#define SCD_CHILI_SESSION_FLAG_none         (0x00000000)
#define SCD_CHILI_SESSION_FLAG_to_card      (0x00000001)  // determine by Cas lib
#define SCD_CHILI_SESSION_FLAG_null_filter  (0x00000010)  // by sesion
#define SCD_CHILI_SESSION_FLAG_flow_ctrl    (0x00000020)  // by sesion
#define SCD_CHILI_SESSION_FLAG_tx_rx_hack   (0x00010000)  // internal hack

int32_t Chili_TimeoutWwtSetup(uint64_t wwt_ms, uint64_t timeout_ms);

int32_t Chili_TxRxSession(mars_scd *p_this,
                        uint8_t* xmit_ptr,
                        uint32_t xmit_length,
                        uint8_t* rcv_ptr,
                        uint32_t rcv_length,
                        uint32_t flags);

int32_t Chili_CardIoCommand(mars_scd *p_this,
                        uint8_t *to_card,
                        uint32_t to_card_len,
                        uint8_t  *from_card,
                        uint32_t *from_card_len,
                        uint32_t flags);
#endif // __SCD_MARS_CHILI__
