/* simple_send.c
 * Very basic I/O test of the MCP2515 on MSP430
 * Intended for MSP430 Value Line (G2xxx) chips
 */
#include <msp430.h>
#include "mcp2515.h"

uint32_t rid;
uint8_t mext;
uint8_t irq, buf[8], buf2[16];
volatile int i;
volatile uint16_t sleep_counter;
#define SLEEP_COUNTER 7

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
	sleep_counter = SLEEP_COUNTER;

	can_init();
	if (can_speed(500000, 1, 1) < 0) {
		P1OUT |= BIT0;
		LPM4;
	}

	can_rx_setmask(0, 0x000000FF, 1);
	can_rx_setfilter(0, 0, 0x00000080);
	can_rx_mode(0, MCP2515_RXB0CTRL_MODE_RECV_STD_OR_EXT);

	can_ioctl(MCP2515_OPTION_LOOPBACK, 1);
	can_ioctl(MCP2515_OPTION_ONESHOT, 1);

	for (i=0; i < 16; i++)
		can_r_reg(i * 16, buf2, 16);

	WDTCTL = WDT_ADLY_16;
	IFG1 &= ~WDTIFG;
	IE1 |= WDTIE;

	while(1) {
		if (mcp2515_irq & MCP2515_IRQ_FLAGGED) {
			irq = can_irq_handler();
			if (irq & MCP2515_IRQ_RX && !(irq & MCP2515_IRQ_ERROR)) {
				i = can_recv(&rid, &mext, buf);
				if (i > 0) {
					if (buf[0] == '1') {
						P1OUT |= BIT0;
						__delay_cycles(8000000);
						P1OUT &= ~BIT0;
						__delay_cycles(8000000);
						P1OUT |= BIT0;
						__delay_cycles(8000000);
						P1OUT &= ~BIT0;
						__delay_cycles(8000000);
					}
				}
			} else if (irq & MCP2515_IRQ_ERROR) {
				while(1) {
					P1OUT |= BIT0;
					__delay_cycles(800000);
					P1OUT &= ~BIT0;
					__delay_cycles(800000);
				}
			}
		}

		if (!sleep_counter) {
			buf[0] = '1';
			buf[1]++;
			if (can_send(0x00000080, 1, buf, 1, 3) < 0) {
				//P1OUT |= BIT0;
			} else {
				//P1OUT &= ~BIT0;
			}
			sleep_counter = SLEEP_COUNTER;
		}

		if ( !(mcp2515_irq & MCP2515_IRQ_FLAGGED) ) {
			P1OUT ^= BIT0;
			LPM3;
		}
	}
	return 0;
}

// WDT overflow/timer
#pragma vector=WDT_VECTOR
__interrupt void WDT_ISR(void)
{
	IFG1 &= ~WDTIFG;
	if (sleep_counter)
		sleep_counter--;
	else
		__bic_SR_register_on_exit(LPM3_bits);
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
