// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * Copyright (c) 2008 Kevin Wang <kevin_wang@realtek.com.tw>
 * Copyright (c) 2019 Realtek Semiconductor Corp.
 * Copyright (c) 2022 Realtek Semiconductor Corp.
 */

#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <asm/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/pinctrl/consumer.h>
#include "scd_mars_priv.h"
#include "scd_mars_chili.h"


#define ME1_OK          (0x90)  /* ME1 = 9X is valid. */
#define NULL_BYTE       (0x60)  // ISO7816-3 2006 table 11

//static uint64_t g_wwt_ms;
static uint64_t g_wwt_jiffies;
static uint64_t g_end_jiffies;
static uint64_t g_start_jiffies;
/*
static uint64_t __inline MS_TO_JIFFIES(uint32_t ms)
{
    return (ms*HZ)/1000;
}

static uint32_t __inline JIFFIES_TO_MS(uint64_t j)
{
    return (j*1000)/HZ;
}*/

// it willl return non-zero value.....
static int ReceieveCharacters(
    mars_scd *p_this,
    unsigned char*          pBuff,
    unsigned int            Len)
{
    int rx_len = 0;
    uint32_t this_to;
    uint64_t cur_jiffies;//unsigned long cur_stamp;
    bool bUseWWT=false;
    int ret;

    while(Len) {
        cur_jiffies = jiffies;
        if(cur_jiffies >= g_end_jiffies) {
            SC_WARNING("timeout(>%dj)\n", (int)(cur_jiffies-g_end_jiffies));
            rx_len = SC_ERR_TIMEOUT; // let caller to exit loop even Rx some bytes in this call.
            break;
        }
        if( (cur_jiffies + g_wwt_jiffies) > g_end_jiffies) {
            this_to = jiffies_to_msecs(g_end_jiffies-cur_jiffies);
        }
        else {
            this_to= jiffies_to_msecs(g_wwt_jiffies);
            bUseWWT = true;
        }
        SC_INFO("Try Rx %d byte with to(%lu ms), b_wwt=%d\n", Len, this_to, bUseWWT);

        mars_scd_set_rx_timeout(p_this, this_to);

        ret = mars_scd_read(p_this, pBuff, Len);
        if (ret<=0) {
            SC_WARNING("Rx %d byte to(%d ms)(%d)\n", Len, this_to, ret);
            if(0==ret) //oo>> Rx 0 byte means timeout
                ret = SC_ERR_TIMEOUT;
            return ret;
        }

        Len    -= ret;
        pBuff  += ret;
        rx_len += ret;

        SC_INFO("Try Rx %d byte with to(%lu), b_wwt=%d\n", Len, this_to, bUseWWT);

        if(bUseWWT && Len) { // case 31, step4
            //SC_DBG("Rx %d byte, remain(%d), timeout(%d), cost:%d\n", ret, Len, this_to, SC_GetSystemTimeMs()- cur_stamp);
            SC_WARNING("wwt to(%dms,%dj),get %d,reamin(%d)\n", this_to, (int)g_wwt_jiffies, rx_len, Len);
            rx_len = SC_ERR_WWT_TIMEOUT; // let caller to exit loop even Rx some bytes in this call.
            break;
        }

		SC_INFO("Rx(%d)remain(%d) with to(%dms), cost:%llums\n", ret, Len, this_to, jiffies_to_msecs(jiffies-cur_jiffies) );
    }

    return rx_len;
}

static int SendAndReceiveBytesSession(mars_scd *p_this,
                            uint8_t* xmit_ptr,
                            uint32_t xmit_length,
                            uint8_t* rcv_ptr,
                            uint32_t rcv_length,
                            uint32_t flags)
{
    int ret = 0;
	unsigned long final_fc = (flags & SCD_CHILI_SESSION_FLAG_flow_ctrl)? 1:0;

    // oo>> rcv_length should be 1 byte.....
    SC_TRACE("flags(0x%x) len(%d, %d)\n", flags, xmit_length, rcv_length);

#ifdef SCD_CHILI_SESSION_FLAG_tx_rx_hack
	if( flags & SCD_CHILI_SESSION_FLAG_tx_rx_hack )
		final_fc |= SCD_CHILI_SESSION_FLAG_tx_rx_hack;
#endif
    mars_scd_set_flowcontrol(p_this, final_fc);

	mars_scd_xmit(p_this, xmit_ptr, xmit_length);

	do {
		ret = ReceieveCharacters(p_this, rcv_ptr, rcv_length);
        if(ret <=0)
        {
            SC_WARNING("get %d-byte failed(%d) @ flags(0x%d)\n", rcv_length, ret, (flags & SCD_CHILI_SESSION_FLAG_null_filter)? 1:0);
            break;
        }
        SC_TRACE("ACK=0x%02x @ flags(%d)\n", rcv_ptr[0], (flags & SCD_CHILI_SESSION_FLAG_null_filter)? 1:0); // test 62-02, 63-02, 64-02
    } while (  (flags & SCD_CHILI_SESSION_FLAG_null_filter) && (NULL_BYTE ==rcv_ptr[0]) );

    // oo>> Rx rcv_length byte without to.
    if(ret>0)
        ret = SC_SUCCESS;

    return ret;
}

static int ReceiveBytesSession(mars_scd *p_this,
                            unsigned char* rcv_ptr,
                            unsigned int   rcv_length,
                            unsigned int flags)
{
	int ret;
	unsigned long final_fc = (flags & SCD_CHILI_SESSION_FLAG_flow_ctrl)? 1:0;

	SC_TRACE("flags(0x%x) len=%d\n", flags, rcv_length);

#ifdef SCD_CHILI_SESSION_FLAG_tx_rx_hack
	if( flags & SCD_CHILI_SESSION_FLAG_tx_rx_hack )
		final_fc |= SCD_CHILI_SESSION_FLAG_tx_rx_hack;
#endif
	mars_scd_set_flowcontrol(p_this, final_fc);

    if(1== rcv_length) {
        do {
            ret = ReceieveCharacters(p_this, rcv_ptr, rcv_length);
            if(ret <=0) {
                SC_WARNING("get %d-byte failed(%d) @ flags(0x%x)\n", rcv_length, ret, flags);
                break;
            }
            SC_TRACE("ACK=0x%02x @ flags(0x%x)\n", rcv_ptr[0], flags);
        } while ( (flags & SCD_CHILI_SESSION_FLAG_null_filter) && (NULL_BYTE ==rcv_ptr[0]) );
    }
    else {
        ret = ReceieveCharacters(p_this, rcv_ptr, rcv_length);
        if(ret <=0) {
            SC_INFO("get %d-byte failed(%d)\n", rcv_length, ret);
        }
    }

    //ret = (ret <=0) ? -1:0;
    // oo>> Rx rcv_length byte without to.
    if(ret>0)
        ret = SC_SUCCESS;

    return ret;
}

int32_t Chili_TxRxSession(mars_scd *p_this,
                        uint8_t* xmit_ptr,
                        uint32_t xmit_length,
                        uint8_t* rcv_ptr,
                        uint32_t rcv_length,
                        uint32_t flags)
{
    int ret;
    if(xmit_length) {
        ret = SendAndReceiveBytesSession(p_this, xmit_ptr, xmit_length, rcv_ptr, rcv_length, flags);
        //SC_WARNING("oo>> Tx(0x%x), Len(%d,%d), flag=0x%x, to(0x%lx, 0x%lx)\n", p_tx[0], tx_len, rx_len, flags, wwt, end_stamp_ms);
    }
    else {
        ret = ReceiveBytesSession(p_this, rcv_ptr, rcv_length, flags);
        //SC_WARNING("oo>> Len(%d,%d), flag=0x%x, to(0x%d, 0x%d)\n", tx_len, rx_len, flags, wwt, end_stamp_ms);
    }

    return ret;
}

int32_t Chili_TimeoutWwtSetup(uint64_t wwt_ms, uint64_t timeout_ms)
{
    g_wwt_jiffies= msecs_to_jiffies(wwt_ms); //MS_TO_JIFFIES(wwt, g_wwt_jiffies);
    g_start_jiffies = jiffies;
    g_end_jiffies = g_start_jiffies + msecs_to_jiffies(timeout_ms); //MS_TO_JIFFIES(timeout_ms, to_jiffies);

    SC_INFO("wwt(%dms, %llu), to(%dms, %llu)\n", wwt_ms, g_wwt_jiffies, timeout_ms, msecs_to_jiffies(timeout_ms) );

    return 0;
}


// Used to transmit buffer & receive byte with ( PB filter on + No Flow control)
// afterwards (Header for ISO Writes [with PB])
#define NUL_WRITE       (1)

// Used to transmit buffer & receive byte with ( PB filter on + FC turned on )
// afterwards (Header for ISO Reads [with PB],
// data for ISO writes [with SW1])
#define NUL_WRITE_FC    (2)

// Used to receive into buffer & receive byte with (PB filter on + FC off)
// (data for ISO reads [with SW1])
#define NUL_READ_FC     (3)

// Receive into buffer (SW2) (PB filter off + FC off)
#define BYTE_READ       (4)

// read bytes with FC, used in ATR  (PB filter off + FC on)
#define READ_FC         (5)



static int TxRxSession(mars_scd *p_this,
                    uint8_t *receive_buffer,
                    uint8_t *send_buffer,
                    uint32_t  length,
                    uint8_t com_type,
                    uint32_t flags)
{
    int ret = SC_FAIL;

    switch (com_type) {
    case NUL_WRITE:
    /* Used to transmit buffer & receive byte with PB filter on
        No Flow control afterwards (Header for ISO Writes [with PB]) */
        ret = SendAndReceiveBytesSession (p_this, send_buffer,length,receive_buffer,1,
            flags | SCD_CHILI_SESSION_FLAG_null_filter);
        break;

    case NUL_WRITE_FC:
    /* Used to transmit buffer & receive byte with PB filter on.
        Flow control turned on afterwards (Header for ISO Reads [with PB],
        data for ISO writes [with SW1]) */
        ret = SendAndReceiveBytesSession (p_this, send_buffer,length,receive_buffer, 1,
            flags | SCD_CHILI_SESSION_FLAG_null_filter | SCD_CHILI_SESSION_FLAG_flow_ctrl);
        break;

    case NUL_READ_FC:
        /* Used to receive into buffer & receive byte with PB filter on.
        (data for ISO reads [with SW1]) */
        ret = ReceiveBytesSession (p_this, receive_buffer, length,
            flags | SCD_CHILI_SESSION_FLAG_null_filter | SCD_CHILI_SESSION_FLAG_flow_ctrl);
        break;

    case BYTE_READ:
        /* Receive into buffer (SW2) */
        ret = ReceiveBytesSession (p_this, receive_buffer,length, SCD_CHILI_SESSION_FLAG_none);
        break;

    case READ_FC: /* read bytes with FC, used in ATR */
        ret = ReceiveBytesSession (p_this, receive_buffer,length, SCD_CHILI_SESSION_FLAG_flow_ctrl);
        break;

    default:
        SC_WARNING("upsupported com_type(%d)\n", com_type);
        break;
    }

    return ret;
}

int32_t Chili_CardIoCommand(mars_scd *p_this,
                        uint8_t *to_card,
                        uint32_t to_card_len,
                        uint8_t  *from_card,
                        uint32_t *from_card_len,
                        uint32_t flags)
{
    int ret = SC_FAIL;
    unsigned char pb;     /* procedure byte */
    uint64_t cur_jiffies;

    SC_INFO("oo>>> in len(to:from)=(%d,%d), f=0x%x(d=%d), (%02x, %02x, %02x, %02x, %02x)\n",
        to_card_len, *from_card_len, flags, (flags & SCD_CHILI_SESSION_FLAG_to_card) ? 1:0,
        to_card[0], to_card[1], to_card[2], to_card[3], to_card[4]);

    //m_wwt = timeout;
    //m_EndStamp = SC_GetSystemTimeMs() + timeout;
    //SC_DBG("m_EndStamp=%lu\n", m_EndStamp);

    *from_card_len = 0;

   /* ISO_SEND_TYPE */
   //if (direction == HD_ISO_SEND_TYPE) {
    if(flags & SCD_CHILI_SESSION_FLAG_to_card) {
        if (to_card_len == 5)  {  // case 2S
            ret = TxRxSession (p_this, &pb, &(to_card[0]), 5, NUL_WRITE_FC, flags);
            if(SC_SUCCESS != ret) {
                SC_WARNING("fail to send header and get ack(%d)\n", ret);
                goto Chili_CardIoCommand_end;
            }
            SC_INFO("pb:0x%02x in Case2S\n", pb);
            if ((pb & 0xF0) == ME1_OK) {     //oo>> 0x9X and 0x6X ?
                SC_INFO("pb:0x%02x as SW1\n", pb);
                from_card[0] = pb;
            }
            else {
                ret = TxRxSession (p_this, &(from_card[0]), NULL, 0+1, NUL_READ_FC, flags);
                if(SC_SUCCESS != ret) {
                    SC_WARNING("fail to send header and get ack(%d)\n", ret);
                    goto Chili_CardIoCommand_end;
                }
            }
        }
        else {
            unsigned char com_type;
            unsigned int TxLen, TxRemain;

            TxRemain = to_card_len;
            TxLen = 5;
            com_type = NUL_WRITE;
            SC_INFO("send header and get pb\n");

            while(TxRemain) {
                ret = TxRxSession (p_this, &pb, &(to_card[to_card_len-TxRemain]), TxLen, com_type, flags);
                if(SC_SUCCESS != ret) {
                    SC_WARNING("fail to send %d ata and get 1 byte(%d)\n", TxLen, ret);
                    goto Chili_CardIoCommand_end;
                }

                SC_INFO("Final pb=0x%02x\n", pb);

                TxRemain -= TxLen;

                // last byte in test 65......
                if ((pb & 0xF0) == ME1_OK) {
                    SC_INFO("pb:0x%02x as SW1, reamin(%d)\n", pb, TxRemain);
                    from_card[0] = pb;
                    break;
                }
                else if (pb == to_card[1]) {
                    TxLen = TxRemain;
                    com_type = NUL_WRITE_FC;
                    SC_INFO("Tx %d byte(FC) and Rx SW1!\n",  TxLen);
                }
                else if (pb == ((unsigned char)~to_card[1]) ) { // test 65-02
                    TxLen = 1;
                    com_type = NUL_WRITE;
                    SC_INFO("Tx %d byte and pb again!\n",  TxLen);
                }
                else { // oo>> test 67-03, 68-04
                    SC_WARNING("pb:0x%02x != INS(0x%02x), !=~INS(0x%02x), !=0x%02x, return error\n", pb, to_card[1], ((unsigned char)~to_card[1]), ME1_OK);
                    ret = SC_FAIL;
                    goto Chili_CardIoCommand_end;
                }
            }
        }

        /* Read SW2 from card */
        ret = TxRxSession (p_this, &(from_card[1]), NULL, 1, BYTE_READ, flags);
        if(SC_SUCCESS != ret) {
            SC_WARNING("fail in Rx SW2! => SW1=0%02x(%d)\n", from_card[1], ret);
            goto Chili_CardIoCommand_end;
        }

        *from_card_len = 2;
        SC_INFO("SW(0x%02x, 0x%02x)\n", from_card[0], from_card[1]);
    }
    else {
        /* from card */
        ret = TxRxSession (p_this, &pb, &(to_card[0]), 5, NUL_WRITE_FC, flags);
        if(SC_SUCCESS != ret) {
            SC_WARNING("fail to send header and get ack(%d)\n", ret);
            goto Chili_CardIoCommand_end;
        }
        SC_INFO("Final pb=0x%02x\n", pb);

        if( (pb != to_card[1]) && (pb != ((unsigned char)~to_card[1]) ) ) { // oo>> test 67-01,
            SC_WARNING("ACK:0x%02x != INS(0x%02x || 0x%02x) \n", pb, to_card[1], ((unsigned char)~to_card[1]) );
            ret = SC_FAIL;
            goto Chili_CardIoCommand_end;
        }

        // Read of (l bytes of data + SW1) of data from card
        #define RX_MAX_BYTE   (200)
        if (to_card[4] < RX_MAX_BYTE) {
            SC_INFO("try to get %d byte\n", to_card[4]);
            ret = TxRxSession (p_this, &(from_card[0]), NULL, to_card[4]+1, NUL_READ_FC, flags);
            if(SC_SUCCESS != ret) {
                SC_WARNING("fail to get data and get SW1(%d)\n", ret);
                goto Chili_CardIoCommand_end;
            }
        }
        else {
            // oo>> in test 21,  byte: 82.7*(12+2)=1.16ms, wwt=238 for Rx timeout: 204 byte
            SC_INFO("try to get FC-%d + %d byte \n", RX_MAX_BYTE, to_card[4]-RX_MAX_BYTE);
            ret = TxRxSession (p_this, &(from_card[0]), NULL, RX_MAX_BYTE, READ_FC, flags);
            if(SC_SUCCESS != ret) {
                SC_WARNING("fail to Rx %d byte(%d)\n", RX_MAX_BYTE, ret);
                goto Chili_CardIoCommand_end;
            }
            ret = TxRxSession (p_this, &(from_card[RX_MAX_BYTE]), NULL, (to_card[4]-RX_MAX_BYTE)+1, NUL_READ_FC, flags);
            if(SC_SUCCESS != ret) {
                SC_WARNING("fail to Rx %d byte + SW1(%d)\n", to_card[4]-RX_MAX_BYTE, ret);
                goto Chili_CardIoCommand_end;
            }
        }

        /* Read SW2 from card */
        ret = TxRxSession (p_this, &(from_card[(int)(to_card[4] + 1)]), NULL, 1, BYTE_READ, flags);
        if(SC_SUCCESS != ret) {
            SC_WARNING("fail to Rx SW2 byte(%d)\n", ret);
            goto Chili_CardIoCommand_end;
        }

        *from_card_len = (int)(to_card[4] + 2);

        // oo >> chili Test.68, Test.65 0x55,0x55 will not reach here!!!
        if( ME1_OK == from_card[(*from_card_len)-2] ) {
            // command normally completed
        }
        else if ( ((( unsigned char)(~(ME1_OK))) == from_card[(*from_card_len)-2]) ) { //test 68-03
            from_card[(*from_card_len)-2] = (unsigned char)~from_card[ (*from_card_len)-2 ];
            from_card[(*from_card_len)-1] = (unsigned char)~from_card[ (*from_card_len)-1 ];
            SC_WARNING("complement to SW(0x%02x, 0x%02x)\n", from_card[(*from_card_len)-2], from_card[(*from_card_len)-1]);
        }
        else { // test 68-01,
            SC_WARNING("unexpected SW(0x%02x, 0x%02x)\n", from_card[(*from_card_len)-2], from_card[(*from_card_len)-1]);
            ret = SC_FAIL;
            *from_card_len = 0;
            goto Chili_CardIoCommand_end;
        }
        SC_INFO("r_len(%d), SW(=0x%02x, 0x%02x)\n", *from_card_len, from_card[(*from_card_len)-2], from_card[(*from_card_len)-1]);
    }

    cur_jiffies = jiffies;
    if(cur_jiffies > g_end_jiffies) {
        SC_WARNING("timeout(>%dj)!!!\n", (int)(cur_jiffies-g_end_jiffies));
        ret = SC_ERR_TIMEOUT;
        goto Chili_CardIoCommand_end;
    }

    //uint64_t cost;
    //jiffies_to_msecs(cur_jiffies-g_start_jiffies, cost);
    SC_WARNING("oo>>> out len=(%d), ret=%d (cost:%d ms)\n", *from_card_len, ret, jiffies_to_msecs(cur_jiffies-g_start_jiffies) );

Chili_CardIoCommand_end:
   return ret;
}
