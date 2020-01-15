/* MCP2515 SPI CAN device driver for MSP430
 *
 * Copyright (c) 2020 Eric Brundick <spirilis [at] linux dot com>
 *  Permission is hereby granted, free of charge, to any person 
 *  obtaining a copy of this software and associated documentation 
 *  files (the "Software"), to deal in the Software without 
 *  restriction, including without limitation the rights to use, copy, 
 *  modify, merge, publish, distribute, sublicense, and/or sell copies 
 *  of the Software, and to permit persons to whom the Software is 
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be 
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
 *  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
 *  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
 *  DEALINGS IN THE SOFTWARE.
 */

#include <msp430.h>
#include <stdint.h>
#include <string.h>
#include "mcp2515.h"
#include "msp430_spi.h"

/* Global variables used internally */
uint8_t mcp2515_txb, mcp2515_ctrl, mcp2515_exmask;

/* Global variable exposed externally for IRQ handling */
volatile uint8_t mcp2515_irq, mcp2515_buf;

/* SPI I/O */

#define CAN_CS_LOW CAN_SPI_CS_PORTOUT &= ~CAN_SPI_CS_PORTBIT
#define CAN_CS_HIGH CAN_SPI_CS_PORTOUT |= CAN_SPI_CS_PORTBIT

void can_spi_command(uint8_t cmd)
{
	CAN_CS_LOW;
	spi_transfer(cmd);
	CAN_CS_HIGH;
}

uint8_t can_spi_query(uint8_t cmd)
{
	uint8_t ret;

	CAN_CS_LOW;
	spi_transfer(cmd);
	ret = spi_transfer(0xFF);
	CAN_CS_HIGH;
	return ret;
}

void can_r_reg(uint8_t addr, void *buf, uint8_t len)
{
	uint16_t i;
	uint8_t *sbuf = (uint8_t *)buf;

	CAN_CS_LOW;
	spi_transfer(MCP2515_SPI_READ);
	spi_transfer(addr);
	for (i=0; i < len; i++) {
		sbuf[i] = spi_transfer(0xFF);
	}
	CAN_CS_HIGH;
}

void can_w_reg(uint8_t addr, void *buf, uint8_t len)
{
	uint16_t i;
	uint8_t *sbuf = (uint8_t *)buf;

	CAN_CS_LOW;
	spi_transfer(MCP2515_SPI_WRITE);
	spi_transfer(addr);
	for (i=0; i < len; i++) {
		spi_transfer(sbuf[i]);
	}
	CAN_CS_HIGH;
}

void can_w_bit(uint8_t addr, uint8_t mask, uint8_t val)
{
	CAN_CS_LOW;
	spi_transfer(MCP2515_SPI_BITMOD);
	spi_transfer(addr);
	spi_transfer(mask);
	spi_transfer(val);
	CAN_CS_HIGH;
}

void can_w_txbuf(uint8_t bufid, void *buf, uint8_t len)
{
	uint16_t i;
	uint8_t *sbuf = (uint8_t *)buf;

	CAN_CS_LOW;
	spi_transfer(MCP2515_SPI_LOAD_TXBUF | (bufid & 0x07));
	for (i=0; i < len; i++) {
		spi_transfer(sbuf[i]);
	}
	CAN_CS_HIGH;
}

void can_r_rxbuf(uint8_t bufid, void *buf, uint8_t len)
{
	uint16_t i;
	uint8_t *sbuf = (uint8_t *)buf;

	CAN_CS_LOW;
	spi_transfer(MCP2515_SPI_READ_RXBUF | (bufid & 0x06));
	for (i=0; i < len; i++) {
		sbuf[i] = spi_transfer(0xFF);
	}
	CAN_CS_HIGH;
}

/* Main library - Maintenance functions */

void can_init()
{
	uint8_t ie;

	// CS pin - inactive HIGH, active LOW
	CAN_SPI_CS_PORTOUT |= CAN_SPI_CS_PORTBIT;
	CAN_SPI_CS_PORTDIR |= CAN_SPI_CS_PORTBIT;

	// IRQ pin
	CAN_IRQ_PORTIE &= ~CAN_IRQ_PORTBIT;
	CAN_IRQ_PORTDIR &= ~CAN_IRQ_PORTBIT;
	CAN_IRQ_PORTREN |= CAN_IRQ_PORTBIT;
	CAN_IRQ_PORTOUT |= CAN_IRQ_PORTBIT;
	CAN_IRQ_PORTIES |= CAN_IRQ_PORTBIT;
	CAN_IRQ_PORTIFG &= ~CAN_IRQ_PORTBIT;
	CAN_IRQ_PORTIE |= CAN_IRQ_PORTBIT;

	spi_init();
	can_spi_command(MCP2515_SPI_RESET);
	__delay_cycles(160000);

	mcp2515_ctrl = MCP2515_CANCTRL_REQOP_CONFIGURATION;
	can_w_reg(MCP2515_CANCTRL, &mcp2515_ctrl, 1);

	ie = MCP2515_CANINTE_RX0IE | MCP2515_CANINTE_RX1IE | MCP2515_CANINTE_ERRIE | MCP2515_CANINTE_MERRE;
	can_w_reg(MCP2515_CANINTE, &ie, 1);

	mcp2515_irq = 0x00;
	mcp2515_txb = 0x00;
	mcp2515_exmask = 0x00;

	_EINT();
}

/* Bitrate in Hz
 * propseg_hint in Time Quanta, 1-8
 * syncjump in Time Quanta, 1-4
 */
int can_speed(uint32_t bitrate, uint8_t propseg_hint, uint8_t syncjump)
{
	uint32_t a;
	uint16_t brp = 0, tq_prop, tq_ps1, tq_ps2;
	uint8_t c;

	// Sanity check
	if (!bitrate || bitrate > 1000000)
		return -1;
	if (syncjump > 4)
		return -1;
	if (!propseg_hint)
		propseg_hint = 1;
	if (!syncjump)
		syncjump = 1;

	// Resolve appropriate bitrate prescaler
	do {
		brp++;
		a = CAN_OSC_FREQUENCY / 2 / brp;  // TQ (Time Quanta) = tOSC / 2
		a /= bitrate;
	} while (a > 25);

	if (a < 8)
		return -1;  // Invalid speed

	a -= 1;  // Sync Seg fixed at 1 TQ
	if ( (a - propseg_hint) < 3 )
		propseg_hint = a - 3;
	if (propseg_hint > 8)
		propseg_hint = 8;

	// Propagation segment is hinted by the user (can be tweaked to account for long cable runs)
	tq_prop = propseg_hint;
	a -= tq_prop;
	// Split PS1 and PS2 evenly; give bias to PS2 (it needs to be at least 2xTQ)
	tq_ps1 = a / 2;
	a -= tq_ps1;
	tq_ps2 = a;

	if (syncjump >= tq_ps2)
		syncjump = tq_ps2 - 1;

	// Configure BRP, SJW, TQ_PropSeg, TQ_PS1, TQ_PS2
	if ( (mcp2515_ctrl & MCP2515_CANCTRL_REQOP_MASK) != MCP2515_CANCTRL_REQOP_CONFIGURATION )
		can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, MCP2515_CANCTRL_REQOP_CONFIGURATION);

	c = ((brp - 1) & 0x3F) | ((syncjump - 1) << 6);
	can_w_reg(MCP2515_CNF1, &c, 1);
	can_w_bit(MCP2515_CNF2, MCP2515_CNF2_PRSEG_MASK | MCP2515_CNF2_PHSEG_MASK | MCP2515_CNF2_BTLMODE,
			  MCP2515_CNF2_BTLMODE | (tq_prop-1) | ((tq_ps1-1) << 3));
	can_w_bit(MCP2515_CNF3, MCP2515_CNF3_PHSEG_MASK, tq_ps2-1);

	if ( (mcp2515_ctrl & MCP2515_CANCTRL_REQOP_MASK) != MCP2515_CANCTRL_REQOP_CONFIGURATION )
		can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, mcp2515_ctrl);
	return 0;
}

/* Standard IDs can contain extended bits, but EXIDE is cleared.  This is to support
 * masks on standard IDs that allow the Extended ID portion to act as a filter on the first
 * 16-bits of the data.
 *
 *
 *  CAN message ID chart
 *  +-----------------------------------------------------------------------------------------------+
 *  |31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0|
 *  +-----------------------------------------------------------------------------------------------+
 *    8  4  2  1  8  4  2  1  8  4  2  1  8  4  2  1  8  4  2  1  8  4  2  1  8  4  2  1  8  4  2  1
 *  
 *                                                                 +--------------------------------+
 *                                                                 | CAN Standard ID                |  < CAN 2.0B Standard Message
 *                                                                 +--------------------------------+
 *                                                                 | SIDH                  | SIDL   |  < MCP2515 Register Assignments
 *                                                                 +--------------------------------+
 *
 *           +--------------------------------+-----------------------------------------------------+
 *           | CAN Standard ID                | CAN Extended ID                                     |  < CAN 2.0B Extended Message
 *           +-----------------------+--------+-----+-----------------------+-----------------------+
 *           | SIDH                  | SIDL   | SIDL| EID8                  | EID0                  |  < MCP2515 Register Assignments
 *           +-----------------------+--------+-----+-----------------------+-----------------------+
 *
 */
void can_compose_msgid_std(uint32_t id, uint8_t *bytebuf)
{
	bytebuf[0] = (uint8_t) ((id & 0x000007F8UL) >> 3);
	bytebuf[1] = (uint8_t) ((id & 0x00000007UL) << 5);
	bytebuf[2] = (uint8_t) ((id & 0xFF000000UL) >> 24);  // Upper 16 bits assumed to be potential byte-filter information
	bytebuf[3] = (uint8_t) ((id & 0x00FF0000UL) >> 16);  // for mask & filters to filter on the first 16 bits of the frame data.
}

void can_compose_msgid_ext(uint32_t id, uint8_t *bytebuf)
{
	bytebuf[0] = (uint8_t) ((id & 0x1FE00000UL) >> 21);
	bytebuf[1] = (uint8_t) ((id & 0x001C0000UL) >> 13) | (uint8_t) ((id & 0x00030000UL) >> 16) | 0x08;  // EID
	bytebuf[2] = (uint8_t) ((id & 0x0000FF00UL) >> 8);
	bytebuf[3] = (uint8_t) (id & 0x000000FFUL);
}

uint32_t can_parse_msgid(uint8_t *buf)
{
	uint32_t ret = 0;

	if (buf[1] & 0x08) {  // Extended message ID
		ret = ((uint32_t)(buf[0]) << 21) | ((uint32_t)(buf[1] & 0xE0) << 12) |
			  (((uint32_t)(buf[1]) & 0x03) << 16) |
			  ((uint32_t)(buf[2]) << 8) |
			  (uint32_t)(buf[3]);
	} else {
		ret = ((uint32_t)(buf[0]) << 3) | ((uint32_t)(buf[1]) >> 5);
	}
	return ret;
}

/* CAN message transmission */

int can_send(uint32_t msg, uint8_t is_ext, void *buf, uint8_t len, uint8_t prio)
{
	int txb;
	uint8_t outbuf[13];

	if (len > 8 || prio > 3)
		return -1;

	// Choose an available TX buffer
	if ( (txb = can_tx_available()) < 0 )
		return -1;
	mcp2515_txb |= 1 << txb;

	// Make sure we're in the right operational mode
	if ( (mcp2515_ctrl & MCP2515_CANCTRL_REQOP_MASK) != MCP2515_CANCTRL_REQOP_NORMAL &&
		 (mcp2515_ctrl & MCP2515_CANCTRL_REQOP_MASK) != MCP2515_CANCTRL_REQOP_LOOPBACK ) {
		mcp2515_ctrl &= ~MCP2515_CANCTRL_REQOP_MASK;
		can_w_reg(MCP2515_CANCTRL, &mcp2515_ctrl, 1);
	}
	
	// Sending an Extended message?
	if (is_ext)
		can_compose_msgid_ext(msg, outbuf);
	else
		can_compose_msgid_std(msg, outbuf);
	
	// Load buffer & send
	outbuf[4] = len;
	memcpy(outbuf+5, (uint8_t *)buf, len);

	can_w_reg(MCP2515_TXB0CTRL + 0x10*txb, &prio, 1);
	can_w_txbuf(MCP2515_TXBUF_TXB0SIDH + 2*txb, outbuf, 5+len);
	can_w_bit(MCP2515_CANINTE, MCP2515_CANINTE_TX0IE << txb, MCP2515_CANINTE_TX0IE << txb);
	//can_w_bit(MCP2515_TXB0CTRL + 0x10*txb, MCP2515_TXBCTRL_TXREQ, MCP2515_TXBCTRL_TXREQ);
	can_spi_command(MCP2515_SPI_RTS | mcp2515_txb);  // Initiate transmission

	return txb;
}

// SRR or RTR ... zero-byte frame requesting the specified msg be returned
int can_query(uint32_t msg, uint8_t is_ext, uint8_t prio)
{
	int txb;
	uint8_t outbuf[13];

	if (prio > 3)
		return -1;

	// Choose an available TX buffer
	if ( !(mcp2515_txb & BIT0) )
		txb = 0;
	else if ( !(mcp2515_txb & BIT1) )
		txb = 1;
	else if ( !(mcp2515_txb & BIT2) )
		txb = 2;
	else
		return -1;
	mcp2515_txb |= 1 << txb;

	// Make sure we're in the right operational mode
	if ( (mcp2515_ctrl & MCP2515_CANCTRL_REQOP_MASK) != MCP2515_CANCTRL_REQOP_NORMAL &&
		 (mcp2515_ctrl & MCP2515_CANCTRL_REQOP_MASK) != MCP2515_CANCTRL_REQOP_LOOPBACK ) {
		mcp2515_ctrl &= ~MCP2515_CANCTRL_REQOP_MASK;
		can_w_reg(MCP2515_CANCTRL, &mcp2515_ctrl, 1);
	}
	
	// Sending an Extended message?
	if (is_ext) {
		can_compose_msgid_ext(msg, outbuf);
		outbuf[4] = 0x40;  // RTR=1, data length = 0
	} else {
		can_compose_msgid_std(msg, outbuf);
		outbuf[1] |= 0x10; // SRR=1
	}
	
	// Send
	can_w_reg(MCP2515_TXB0CTRL + 0x10*txb, &prio, 1);
	can_w_txbuf(MCP2515_TXBUF_TXB0SIDH + 2*txb, outbuf, 5);
	can_w_bit(MCP2515_CANINTE, MCP2515_CANINTE_TX0IE << txb, MCP2515_CANINTE_TX0IE << txb);
	//can_w_bit(MCP2515_TXB0CTRL + 0x10*txb, MCP2515_TXBCTRL_TXREQ, MCP2515_TXBCTRL_TXREQ);
	can_spi_command(MCP2515_SPI_RTS);  // Initiate transmission

	return txb;
}

// Returns -1 if no TXB's were active
int can_tx_cancel()
{
	uint8_t i, work_done = -1;
	
	for (i=0; i < 3; i++) {
		if (mcp2515_txb & (1 << i)) {
			// Cancel TXREQ bit
			can_w_bit(MCP2515_TXB0CTRL + 0x10*i, MCP2515_TXBCTRL_TXREQ, 0x00);
			// Disable IRQ for this TXB
			can_w_bit(MCP2515_CANINTF, MCP2515_CANINTF_TX0IF << i, 0x00);
			can_w_bit(MCP2515_CANINTE, MCP2515_CANINTE_TX0IE << i, 0x00);
			work_done = 0;
		}
	}
	return work_done;
}

// Returns available TXB if one is available, else -1 indicating the user must wait to TX.
int can_tx_available()
{
	int txb = -1;

	if ( !(mcp2515_txb & BIT0) )
		txb = 0;
	else if ( !(mcp2515_txb & BIT1) )
		txb = 1;
	else if ( !(mcp2515_txb & BIT2) )
		txb = 2;
	return txb;
}

/* CAN message receive */

// Returns length of packet or -1 if nothing to read
int can_recv(uint32_t *msgid, uint8_t *is_ext, void *buf)
{
	uint8_t msginbuf[13];
	int rxb = -1;

	// Any of them have unread data?
	rxb = can_rx_pending();
	if (rxb < 0)
		return -1;

	// Pull down the message
	can_r_rxbuf(MCP2515_RXBUF_RXB0SIDH + 0x04*rxb, msginbuf, 13);
	// To reduce risk of RXB overflow, acknowledge IRQ right away.
	can_w_bit(MCP2515_CANINTF, MCP2515_CANINTF_RX0IF + rxb, 0x00);

	*msgid = can_parse_msgid(msginbuf);
	if (msginbuf[1] & 0x08)
		*is_ext = 1;
	else
		*is_ext = 0;
	memcpy((uint8_t *)buf, msginbuf+5, msginbuf[4] & 0x0F);

	// Present RTR or SRR bit as 0x40
	if (is_ext)
		return msginbuf[4] & 0x4F;
	else
		return (msginbuf[4] & 0x0F) | ((msginbuf[1] & 0x10) << 2);
}

// Returns RXBID of first full buffer or -1 if nothing is waiting.
int can_rx_pending()
{
	uint8_t canintf;

	can_r_reg(MCP2515_CANINTF, &canintf, 1);
	if (canintf & MCP2515_CANINTF_RX0IF)
		return 0;
	if (canintf & MCP2515_CANINTF_RX1IF)
		return 1;
	return -1;
}

// Set one of the 2 RX masks.  maskid=0 is for RXB0, maskid=1 is for RXB1.
int can_rx_setmask(uint8_t maskid, uint32_t msgmask, uint8_t is_ext)
{
	uint8_t maskbuf[4];

	if (maskid > 1)
		return -1;
	
	if ( (mcp2515_ctrl & MCP2515_CANCTRL_REQOP_MASK) != MCP2515_CANCTRL_REQOP_CONFIGURATION )
		can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, MCP2515_CANCTRL_REQOP_CONFIGURATION);

	if (is_ext) {
		can_compose_msgid_ext(msgmask, maskbuf);
		maskbuf[1] &= ~0x08;  // EXIDE is unimplemented in the MASK registers
		mcp2515_exmask |= 1 << maskid;
	} else {
		can_compose_msgid_std(msgmask, maskbuf);
		mcp2515_exmask &= ~(1 << maskid);
	}
	
	can_w_reg(MCP2515_RXM0SIDH + maskid * 0x04, maskbuf, 4);

	if ( (mcp2515_ctrl & MCP2515_CANCTRL_REQOP_MASK) != MCP2515_CANCTRL_REQOP_CONFIGURATION )
		can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, mcp2515_ctrl);

	return maskid;
}

/* Configure filter.  filtid is from 0-3 (0-1 when rxb=0, 0-3 when rxb=1)
 * Standard vs. Extended ID is determined by mcp2515_exmask (whether a mask was specified
 * for std or ext operation)
 */
int can_rx_setfilter(uint8_t rxb, uint8_t filtid, uint32_t msgid)
{
	uint8_t idbuf[4];

	if (rxb > 1)
		return -1;
	if (filtid > 5 || (filtid > 1 && rxb == 0))
		return -1;
	
	if ( (mcp2515_ctrl & MCP2515_CANCTRL_REQOP_MASK) != MCP2515_CANCTRL_REQOP_CONFIGURATION )
		can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, MCP2515_CANCTRL_REQOP_CONFIGURATION);

	if (mcp2515_exmask & (1 << rxb)) // Extended ID
		can_compose_msgid_ext(msgid, idbuf);
	else
		can_compose_msgid_std(msgid, idbuf);
	
	filtid += 2*rxb;
	if (filtid < 3)
		can_w_reg(MCP2515_RXF0SIDH + filtid * 0x04, idbuf, 4);
	else
		can_w_reg(MCP2515_RXF3SIDH + (filtid-3) * 0x04, idbuf, 4);
	
	if ( (mcp2515_ctrl & MCP2515_CANCTRL_REQOP_MASK) != MCP2515_CANCTRL_REQOP_CONFIGURATION )
		can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, mcp2515_ctrl);

	return filtid;
}

// RX mode for the specified RXB.  See MCP2515_RXB0CTRL_MODE_* for details.
int can_rx_mode(uint8_t rxb, uint8_t mode)
{
	if (rxb > 1)
		return -1;

	can_w_bit(MCP2515_RXB0CTRL + rxb*0x10, MCP2515_RXB0CTRL_RXM0 | MCP2515_RXB0CTRL_RXM0, mode);

	return 0;
}

// Miscellaneous option-setting goes here.
int can_ioctl(uint8_t option, uint8_t val)
{
	switch (option) {
		// Allows RXB0 to shove its contents over to RXB1 if a new RXB0 frame comes in.
		case MCP2515_OPTION_ROLLOVER:
			if (val)
				can_w_bit(MCP2515_RXB0CTRL, MCP2515_RXB0CTRL_BUKT, MCP2515_RXB0CTRL_BUKT);
			else
				can_w_bit(MCP2515_RXB0CTRL, MCP2515_RXB0CTRL_BUKT, 0);
			break;

		case MCP2515_OPTION_ONESHOT:
			if (val) {
				can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_OSM, MCP2515_CANCTRL_OSM);
				mcp2515_ctrl |= MCP2515_CANCTRL_OSM;
			} else {
				can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_OSM, 0);
				mcp2515_ctrl &= ~MCP2515_CANCTRL_OSM;
			}
			break;

		// Abort all pending transmissions.
		case MCP2515_OPTION_ABORT:
			if (val) {
				can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_ABAT, MCP2515_CANCTRL_ABAT);
				mcp2515_ctrl |= MCP2515_CANCTRL_ABAT;
			} else {
				can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_ABAT, 0);
				mcp2515_ctrl &= ~MCP2515_CANCTRL_ABAT;
			}
			break;

		// CLKOUT pin shows the clock signal divided by 2^(val-1) (1=/1, 2=/2, 3=/4, 4=/8)
		case MCP2515_OPTION_CLOCKOUT:
			if (val) {
				can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_CLKEN | MCP2515_CANCTRL_CLKPRE_MASK, MCP2515_CANCTRL_CLKEN | ((val-1) & 0x03));
				mcp2515_ctrl |= MCP2515_CANCTRL_CLKEN | ((val-1) & 0x03);
			} else {
				can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_CLKEN, 0);
				mcp2515_ctrl &= ~(MCP2515_CANCTRL_ABAT | MCP2515_CANCTRL_CLKPRE_MASK);
			}
			break;

		case MCP2515_OPTION_LOOPBACK:
			if (val) {
				can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, MCP2515_CANCTRL_REQOP_LOOPBACK);
				mcp2515_ctrl &= ~MCP2515_CANCTRL_REQOP_MASK; mcp2515_ctrl |= MCP2515_CANCTRL_REQOP_LOOPBACK;
			} else {
				can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, MCP2515_CANCTRL_REQOP_NORMAL);
				mcp2515_ctrl &= ~MCP2515_CANCTRL_REQOP_MASK; mcp2515_ctrl |= MCP2515_CANCTRL_REQOP_NORMAL;
			}
			break;

		case MCP2515_OPTION_LISTEN_ONLY:
			if (val) {
				can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, MCP2515_CANCTRL_REQOP_LISTEN_ONLY);
				mcp2515_ctrl &= ~MCP2515_CANCTRL_REQOP_MASK; mcp2515_ctrl |= MCP2515_CANCTRL_REQOP_LISTEN_ONLY;
			} else {
				can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, MCP2515_CANCTRL_REQOP_NORMAL);
				mcp2515_ctrl &= ~MCP2515_CANCTRL_REQOP_MASK; mcp2515_ctrl |= MCP2515_CANCTRL_REQOP_NORMAL;
			}
			break;

		// See MCP2515_OPTION_WAKE* for ways to come out of this.
		case MCP2515_OPTION_SLEEP:
			if (val) {
				can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, MCP2515_CANCTRL_REQOP_SLEEP);
				mcp2515_ctrl &= ~MCP2515_CANCTRL_REQOP_MASK; mcp2515_ctrl |= MCP2515_CANCTRL_REQOP_SLEEP;
			} else {
				can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, MCP2515_CANCTRL_REQOP_NORMAL);
				mcp2515_ctrl &= ~MCP2515_CANCTRL_REQOP_MASK; mcp2515_ctrl |= MCP2515_CANCTRL_REQOP_NORMAL;
			}
			break;

		// Sample 3 times around the sample point instead of 1.
		case MCP2515_OPTION_MULTISAMPLE:
			can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, MCP2515_CANCTRL_REQOP_CONFIGURATION);
			if (val)
				can_w_bit(MCP2515_CNF2, MCP2515_CNF2_SAM, MCP2515_CNF2_SAM);
			else
				can_w_bit(MCP2515_CNF2, MCP2515_CNF2_SAM, 0);
			can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, mcp2515_ctrl);
			break;

		// CLKOUT pin produces Start of Frame edge signal instead of CLKOUT.
		case MCP2515_OPTION_SOFOUT:
			can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, MCP2515_CANCTRL_REQOP_CONFIGURATION);
			if (val)
				can_w_bit(MCP2515_CNF3, MCP2515_CNF3_SOF, MCP2515_CNF3_SOF);
			else
				can_w_bit(MCP2515_CNF3, MCP2515_CNF3_SOF, 0);
			can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, mcp2515_ctrl);
			break;

		// Enable low-pass filter on CAN_RX to reduce the likelihood of waking due to random noise.
		case MCP2515_OPTION_WAKE_GLITCH_FILTER:
			can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, MCP2515_CANCTRL_REQOP_CONFIGURATION);
			if (val)
				can_w_bit(MCP2515_CNF3, MCP2515_CNF3_WAKFIL, MCP2515_CNF3_WAKFIL);
			else
				can_w_bit(MCP2515_CNF3, MCP2515_CNF3_WAKFIL, 0);
			can_w_bit(MCP2515_CANCTRL, MCP2515_CANCTRL_REQOP_MASK, mcp2515_ctrl);
			break;

		// Enable WAKIE to activate IRQ line in the event of received data.
		case MCP2515_OPTION_WAKE:
			if (val)
				can_w_bit(MCP2515_CANINTE, MCP2515_CANINTE_WAKIE, MCP2515_CANINTE_WAKIE);
			else
				can_w_bit(MCP2515_CANINTE, MCP2515_CANINTE_WAKIE, 0);
			break;

		default:
			return -1;
	}
	return 0;
}

// Report error counters; valid registers include MCP2515_TEC (TX error count) and MCP2515_REC (RX error count)
int can_read_error(uint8_t reg)
{
	uint8_t e;

	if (reg != MCP2515_TEC && reg != MCP2515_REC && reg != MCP2515_EFLG)
		return -1;
	can_r_reg(reg, &e, 1);
	return e;
}

/* IRQ handling
 *
 * Handler architecture:
 * IRQ line sets mcp2515_irq |= MCP2515_IRQ_FLAGGED
 * Main loop uses this as a hint to run can_irq_handler() which returns:
 * MCP2515_IRQ_RX
 * MCP2515_IRQ_TX
 * MCP2515_IRQ_ERROR
 * MCP2515_IRQ_WAKEUP
 * Any errors that are automatically handled by can_irq_handler() will also return MCP2515_IRQ_HANDLED bitwise-OR'd
 * If all interrupts have been handled, MCP2515_IRQ_FLAGGED is cleared automatically from mcp2515_irq.  It should not
 * be cleared by the user's firmware.
 */

int can_irq_handler()
{
	int i;
	uint8_t ifg, eflg, ie, txbctrl;

	mcp2515_irq &= MCP2515_IRQ_FLAGGED;  // Clear everything but the flagged bit.
	// Read CANINTF to get started
	can_r_reg(MCP2515_CANINTF, &ifg, 1);

	// RX success IRQ?
	if (ifg & (MCP2515_CANINTF_RX0IF | MCP2515_CANINTF_RX1IF)) {
		if (ifg & MCP2515_CANINTF_RX0IF)
			mcp2515_buf = 0;
		else
			mcp2515_buf = 1;
		mcp2515_irq |= MCP2515_IRQ_RX;
		return MCP2515_IRQ_RX;
	}

	// TX success IRQ?
	if (ifg & (MCP2515_CANINTF_TX0IF | MCP2515_CANINTF_TX1IF | MCP2515_CANINTF_TX2IF)) {
		for (i=0; i <= 2; i++) {
			if (ifg & (MCP2515_CANINTF_TX0IF << i)) {
				can_w_bit(MCP2515_CANINTF, MCP2515_CANINTF_TX0IF << i, 0);  // Clear IFG
				can_w_bit(MCP2515_CANINTE, MCP2515_CANINTE_TX0IE << i, 0);  // Disable interrupt (will be re-enabled on next TX)
				mcp2515_txb &= ~(1 << i);
				mcp2515_buf = i;
				mcp2515_irq |= MCP2515_IRQ_TX | MCP2515_IRQ_HANDLED;
				return MCP2515_IRQ_TX | MCP2515_IRQ_HANDLED;
			}
		}
	}

	// Wake up?
	if (ifg & MCP2515_CANINTF_WAKIF) {
		can_w_bit(MCP2515_CANINTF, MCP2515_CANINTF_WAKIF, 0);
		mcp2515_irq |= MCP2515_IRQ_WAKEUP | MCP2515_IRQ_HANDLED;
		return MCP2515_IRQ_WAKEUP | MCP2515_IRQ_HANDLED;
	}

	// Message error?
	if (ifg & MCP2515_CANINTF_MERRF) {
		can_r_reg(MCP2515_CANINTE, &ie, 1);
		// See if it's a TX error
		for (i=0; i <= 2; i++) {
			if (ie & (MCP2515_CANINTE_TX0IE << i)) {
				can_r_reg(MCP2515_TXB0CTRL + 0x10*i, &txbctrl, 1);
				if (txbctrl & MCP2515_TXBCTRL_TXERR) {
					mcp2515_buf = i;
					can_w_bit(MCP2515_CANINTF, MCP2515_CANINTF_MERRF, 0);  // Clear MERRF
					// Are we in OneShot mode?
					if (mcp2515_ctrl & MCP2515_CANCTRL_OSM) {
						can_w_bit(MCP2515_CANINTE, MCP2515_CANINTE_TX0IE << i, 0);  // Disable interrupt (will be re-enabled on next TX)
						mcp2515_txb &= ~(1 << i);
						mcp2515_irq |= MCP2515_IRQ_TX | MCP2515_IRQ_ERROR | MCP2515_IRQ_HANDLED;
						return MCP2515_IRQ_TX | MCP2515_IRQ_ERROR | MCP2515_IRQ_HANDLED;
					} else {
					// If not, notify that TX error occurred but it "hasn't" been handled.  MCU intervention may be required
					// in order to monitor and validate the # of retries that have occurred & failed and whether the request should
					// be cancelled.
						mcp2515_irq |= MCP2515_IRQ_TX | MCP2515_IRQ_ERROR;
						return MCP2515_IRQ_TX | MCP2515_IRQ_ERROR;
					}
				}
			}
		}
		// Not TX?  Must be an RX error.
		can_w_bit(MCP2515_CANINTF, MCP2515_CANINTF_MERRF, 0);
		mcp2515_irq |= MCP2515_IRQ_RX | MCP2515_IRQ_ERROR | MCP2515_IRQ_HANDLED;
		return MCP2515_IRQ_RX | MCP2515_IRQ_ERROR | MCP2515_IRQ_HANDLED;
	}

	// All other errors are expressed in EFLG.
	if (ifg & MCP2515_CANINTF_ERRIF) {
		// ERRIF ... Read EFLG.
		can_r_reg(MCP2515_EFLG, &eflg, 1);

		if (eflg & (MCP2515_EFLG_RX0OVR | MCP2515_EFLG_RX1OVR)) {
			// RX overflow
			can_w_bit(MCP2515_EFLG, MCP2515_EFLG_RX0OVR | MCP2515_EFLG_RX1OVR, 0);
			eflg &= ~(MCP2515_EFLG_RX0OVR | MCP2515_EFLG_RX1OVR);
			if (!eflg)
				can_w_bit(MCP2515_CANINTF, MCP2515_CANINTF_ERRIF, 0);
			mcp2515_irq |= MCP2515_IRQ_RX | MCP2515_IRQ_ERROR | MCP2515_IRQ_HANDLED;
			return MCP2515_IRQ_RX | MCP2515_IRQ_ERROR | MCP2515_IRQ_HANDLED;
		}

		if (eflg & ~(MCP2515_EFLG_RX0OVR | MCP2515_EFLG_RX1OVR)) {
			// Warning; TEC or REC too high
			mcp2515_irq |= MCP2515_IRQ_ERROR;
			return MCP2515_IRQ_ERROR;
		}
	}

	/* If we reach this far, it means the user ran this function when no IRQ existed.
	 * At this point we can clear the MCP2515_IRQ_FLAGGED bit.
	 */
	mcp2515_irq &= ~MCP2515_IRQ_FLAGGED;
	return 0;
}

int can_clear_buserror()
{
	uint8_t intf, eflg;

	can_r_reg(MCP2515_CANINTF, &intf, 1);
	if (intf & MCP2515_CANINTF_ERRIF) {
		can_r_reg(MCP2515_EFLG, &eflg, 1);
		can_w_bit(MCP2515_EFLG, MCP2515_EFLG_RX0OVR | MCP2515_EFLG_RX1OVR, 0);  // The only bits that can be written
		can_w_bit(MCP2515_CANINTF, MCP2515_CANINTF_ERRIF, 0);
		return eflg;
	}

	return -1;  // No bus error found
}
