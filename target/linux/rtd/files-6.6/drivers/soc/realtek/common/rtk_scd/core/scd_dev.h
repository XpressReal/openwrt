// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * Copyright (c) 2019 Realtek Semiconductor Corp.
 */

#ifndef __SCD_DEV_H__
#define __SCD_DEV_H__

#include <linux/cdev.h>
#include "scd.h"

#define SCD_IFD_ENABLE          0x7300
#define SCD_RESET               0x7301

#define SCD_SET_BAUD_RATE       0x7302
#define SCD_GET_BAUD_RATE       0x7303

#define SCD_DEACTIVE            0x7306
#define SCD_ACTIVE              0x7307
#define SCD_GET_ATR             0x7309
#define SCD_READ                0x730a
#define SCD_WRITE               0x730b
#define SCD_CARD_IO_COMMAND     0x730c // oo>> for card io command in one ioctrl
#define SCD_GET_CARD_STATUS     0x730d
#define SCD_POLL_CARD_STATUS_CHANGE 0x730e

#define SCD_SETPARAM_EX         0x7310
#define SCD_GETPARAM_EX         0x7311

#define SCD_SET_PROTOCOL        (0x7312)
#define SCD_GET_PROTOCOL        (0x7313)

#define SCD_TX_RX_SESSION       (0x7314)  // oo>> for Tx/Rx session in one ioctrl

typedef struct{
	unsigned char* p_data;
	unsigned int length;
} sc_msg_buff;

typedef struct{
	struct cdev cdev;
	struct device* device;
} scd_dev;

typedef struct{
	/*
    unsigned char *p_tx;
    unsigned char *p_rx;
    unsigned int tx_len;
    unsigned int rx_len;
    unsigned int flags;
    unsigned long wwt;
    unsigned long end_stamp_ms;
    */

    //uint8_t *p_tx;
    //uint8_t *p_rx;
    #define MAX_TX_SIZE		(255)
    #define MAX_RX_SIZE		(257)
    uint8_t p_tx[MAX_TX_SIZE];
    uint8_t p_rx[MAX_RX_SIZE];

    uint32_t tx_len;
    uint32_t rx_len;
    uint32_t flags;
    uint64_t wwt;
    uint64_t end_stamp_ms;
} scd_tx_rx_session;

extern int  create_scd_dev_node(scd_device* dev);
extern void remove_scd_dev_node(scd_device* dev);

extern int scd_dev_module_init(void);
extern void scd_dev_module_exit(void);

#endif  //__SCD_DEV_H__

