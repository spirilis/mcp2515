/* call.c
 * Very basic I/O test of the MCP2515 on MSP430
 * Sends a message every second, responds if it finds the right response
 * Intended for MSP430 Value Line (G2xxx) chips

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
#include "mcp2515.h"
#include "ste2007.h"
#include "chargen.h"
#include "msp430_spi.h"

uint32_t rid;
uint8_t mext;
uint8_t irq, inbuf[19];
volatile uint16_t sleep_counter;
#define SLEEP_COUNTER 20

int main()
{
	uint8_t do_lpm, is_ext;
	int i, j, k;
	uint32_t msgid;

	WDTCTL = WDTPW | WDTHOLD;
	DCOCTL = CALDCO_16MHZ;
	BCSCTL1 = CALBC1_16MHZ;
	BCSCTL2 = DIVS_1;
	BCSCTL3 = LFXT1S_2;
	while (BCSCTL3 & LFXT1OF)
		;
	
	P1SEL &= ~BIT0;
	P1SEL2 &= ~BIT0;
	P1DIR |= BIT0;
	P1OUT &= ~BIT0;
	sleep_counter = SLEEP_COUNTER;
	inbuf[8] = '\0';

	can_init();
	if (can_speed(500000, 1, 3) < 0) {
		P1OUT |= BIT0;
		LPM4;
	}

	// LCD init
	P2SEL &= ~(BIT0 | BIT5);
	P2SEL2 &= ~(BIT0 | BIT5);
	P2DIR |= BIT0 | BIT5;
	P2OUT |= BIT0 | BIT5;
	msp1202_init();
	msp1202_puts("CAN printer\n0x00000080-\n");

	can_rx_setmask(0, 0xFFFFFFFF, 1);
	can_rx_setmask(1, 0xFFFFFFFF, 1);
	can_rx_setfilter(0, 0, 0x00000080);
	can_rx_mode(0, MCP2515_RXB0CTRL_MODE_RECV_STD_OR_EXT);

	can_ioctl(MCP2515_OPTION_LOOPBACK, 0);
	can_ioctl(MCP2515_OPTION_ONESHOT, 0);
	can_ioctl(MCP2515_OPTION_ROLLOVER, 1);

	/* Flowchart for program flow:
	 *
	 *        START
	 *          |
	 *          +---------<-------------------+-----------<-----------------------+
	 *          |                             |                                   |
	 *         \|/                           /|\                                  |
	 *   --------------                       |                                   |
	 *  /               \         +------+    |                                   |
	 * < Is IRQ pending? > No->---| LPM4 |->--+                                   |
	 *  \               /         +------+                                       /|\
	 *    -------------                                                           |
	 *         Yes                                                                |
	 *          |                                                                 |
	 *         \_/                                                                |
	 *    -----------              -------------                                  |
	 *  /             \          /               \                                |
	 * < Is IRQ_ERROR? > Yes->--< Is IRQ_HANDLED? > Yes->-------------------------+
	 *  \             /          \               /                                |
	 *    -----------              -------------                                  |
	 *         No                        No                                       |
	 *         |                         |                                       /|\
	 *        \_/                       \_/                                       |
	 *     --------                   --------           +-----------------+      |
	 *   /          \               /          \         | Cancel TX using |      |
	 *  < Is IRQ_RX? > No->---->---< Is IRQ_TX? > Yes->--| can_tx_cancel() |->----+
	 *   \          /               \          /         +-----------------+      |
	 *     --------                   --------                                    |
	 *        Yes                        No                                       |
	 *         |                         |                                       /|\
	 *        \_/                       \_/                                       |
	 *  +----------------+      +-------------------+                             |
	 *  | Pull data      |      | Bus error, wait   |                             |
	 *  | using can_recv |      | 100ms and recheck |->---------------------------+
	 *  +----------------+      | IRQ.              |                             |
	 *          |               +-------------------+                             |
	 *         \_/                                                                |
	 *   +--------------+                                                        /|\
	 *   | Write buffer |                                                         |
	 *   | contents to  |                                                         |
	 *   | LCD using    |->--------------------------->---------------------------+
	 *   | msp1202_puts |
	 *   +--------------+
	 *
	 *
	 *
	 */
	while(1) {
		do_lpm = 1;
		if (mcp2515_irq) {
			irq = can_irq_handler();
			if (irq & MCP2515_IRQ_ERROR) {
				if ( !(irq & MCP2515_IRQ_HANDLED) ) {
					if (irq & MCP2515_IRQ_TX) {
						can_tx_cancel();
					} else {
						// Bus error; wait 100ms, recheck
						P1OUT |= BIT0;
						__delay_cycles(1600000);
						P1OUT &= ~BIT0;
						do_lpm = 0;
					}
				} else {
					// RX overflow, most likely
					msp1202_puts("RX OVERFLOW\n");
				}
			}
			if (irq & MCP2515_IRQ_RX) {
				j = 0;
				k = can_rx_pending();
				do {
					i = can_recv(&msgid, &is_ext, inbuf+j);
					if ( !(i & 0x40) ) {
						j += i;
					}
					//msp1202_putc(k + '0', 0); msp1202_putc(' ', 0);
				} while ( (k = can_rx_pending()) >= 0 );
				//inbuf[j++] = '\n'; inbuf[j] = '\0';
				inbuf[j] = '\0';
				msp1202_puts((char*)inbuf);
				//msp1202_putc('\n', 1);
			} else {
				if (irq & MCP2515_IRQ_TX) {
					can_tx_cancel();
				}
			}
		}

		if ( do_lpm && !(mcp2515_irq & MCP2515_IRQ_FLAGGED) ) {
			LPM4;
		}
	}
	return 0;
}

// ISR for PORT1
#pragma vector=PORT1_VECTOR
__interrupt void P1_ISR(void)
{
	if (P1IFG & CAN_IRQ_PORTBIT) {
		P1IFG &= ~CAN_IRQ_PORTBIT;
		mcp2515_irq |= MCP2515_IRQ_FLAGGED;
		__bic_SR_register_on_exit(LPM4_bits);
	}
}

void lcd_chipselect(uint8_t onoff)
{
	if (onoff)
		P2OUT |= BIT0;
	else
		P2OUT &= ~BIT0;
}
