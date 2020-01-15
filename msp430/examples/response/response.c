/* response.c
 * Very basic I/O test of the MCP2515 on MSP430
 * Waits to receive a message at 0x80, sends a reply to 0x40
 * Intended for MSP430 Value Line (G2xxx) chips
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
#include "mcp2515.h"

uint32_t rid;
uint8_t mext;
uint8_t irq, buf[8], buf2[16];
volatile int i;
#define SLEEP_COUNTER 20

int main()
{
	WDTCTL = WDTPW | WDTHOLD;
	DCOCTL = CALDCO_16MHZ;
	BCSCTL1 = CALBC1_16MHZ;
	BCSCTL2 = DIVS_1;
	BCSCTL3 = LFXT1S_2;
	while (BCSCTL3 & LFXT1OF)
		;
	
	P1DIR |= BIT0;
	P1OUT &= ~BIT0;

	can_init();
	if (can_speed(500000, 1, 1) < 0) {
		P1OUT |= BIT0;
		LPM4;
	}

	can_rx_setmask(0, 0xFFFFFF4F, 1);
	can_rx_setfilter(0, 0, 0x00000040);
	can_rx_setfilter(0, 1, 0x0000000F);

	can_rx_setmask(1, 0xFFFFFF0F, 1);
	can_rx_setfilter(1, 0, 0x00000000);
	can_rx_setfilter(1, 1, 0x00000000);
	can_rx_setfilter(1, 2, 0x00000000);
	can_rx_setfilter(1, 3, 0x00000000);

	can_rx_mode(0, MCP2515_RXB0CTRL_MODE_RECV_STD_OR_EXT);

	can_ioctl(MCP2515_OPTION_LOOPBACK, 0);
	can_ioctl(MCP2515_OPTION_ONESHOT, 1);

	for (i=0; i < 16; i++)
		can_r_reg(i * 16, buf2, 16);

	while(1) {
		if (mcp2515_irq & MCP2515_IRQ_FLAGGED) {
			irq = can_irq_handler();
			if (irq & MCP2515_IRQ_RX && !(irq & MCP2515_IRQ_ERROR)) {
				i = can_recv(&rid, &mext, buf);
				if (i > 0) {
					if (mext && rid == 0x00000080) {
						can_send(0x00000040, 1, buf, 2, 3);
					}
				}
			} else if (irq & MCP2515_IRQ_ERROR) {
				can_r_reg(MCP2515_CANINTF, &mext, 1);
				can_r_reg(MCP2515_EFLG, &mext, 1);
				while(1) {
					P1OUT |= BIT0;
					__delay_cycles(1600000);
					P1OUT &= ~BIT0;
					__delay_cycles(1600000);
				}
			}
		}

		if ( !(mcp2515_irq & MCP2515_IRQ_FLAGGED) ) {
			//P1OUT ^= BIT0;
			LPM3;
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
		__bic_SR_register_on_exit(LPM3_bits);
	}
}
