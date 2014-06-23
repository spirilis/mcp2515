#include <msp430.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include "mcp2515.h"
#include "can_printf.h"

/* Manage sending arbitrary-length data using 8-byte CAN buffers
 *
 * Flowchart:
 *
 *            START
 *              |------------------<---------------------<-------------------------+
 *             \_/                                                                 |
 *     --------------------                                                        |
 *   /                      \                                                      |
 *  < Is there pending data? > No-> Exit func.                                     |
 *   \                      /                                                     /|\
 *     --------------------                                                        |
 *             Yes                                                                 |
 *              |                                                                  |
 *             \_/                                                                 |
 *       -----------------                                                         |
 *     /                   \        +------------------------+                     |
 *    < Is a TXB available? > Yes-> | Send frame, ERRCOUNT=0 |->--------->---------+
 *     \                   /        +------------------------+                     |
 *       -----------------                                                         |
 *              No                                                                 |
 *              |------------<------------------------------<----------------------------------------<-------------------+
 *             \_/                    (RX IRQs always reported before TX IRQs)     |                                     |
 *     ------------------------              -------------------------             |                                    /|\
 *   /                          \          /                           \           |   +-----------------------------+   |
 *  < Is there a TX IRQ pending? > No->-- < Is there an RX IRQ pending? > Yes->--------| Run can_recv & discard data |->-+
 *   \                          /          \                           /           |   +-----------------------------+   |
 *     ------------------------              -------------------------             |                                     |
 *           Yes                                      No                          /|\                                   /|\
 *            |                                       |   +----------+             |                                     |
 *           \_/                                      +-> | Wait 1ms |------------------------>--------------------------+
 *     --------------                                     +----------+             |
 *   /                \                                         |                  |
 *  < Is it "handled"? > Yes->--------->---------------------------->--------------+
 *   \                / (this should free up a TXB)             |
 *     --------------                                           |
 *           No                                                 |
 *           |                                                  |
 *          \_/                                                 |
 *    +------------+                                           /|\
 *    | ERRCOUNT++ |                                            |
 *    +------------+                                            |
 *           |                                                  |
 *          \_/                                                 |
 *     ---------------                                          |
 *   /                 \                                        |
 *  < Is ERRCOUNT > 8 ? > No->----------------->----------------+
 *   \                 / (assumes ONESHOT=0, allow re-sending but only up to 8 times before we give up)
 *     ---------------
 *          Yes
 *           |
 *          \_/
 *   +---------------+
 *   | Cancel all TX |
 *   +---------------+
 *           |
 *          \_/
 *
 *       Exit func.
 *
 *
 */

int can_managed_tx(uint32_t msgid, uint8_t is_ext, uint8_t *buf, uint16_t len, uint16_t pace_ms)
{
	uint16_t i = 0, j = 0, errcount = 0;
	uint8_t tmp_u8, tmp_buf[8], irq;
	uint32_t tmp_msgid;

	while (i < len) {
		if (can_tx_available() == 0) {
			j = len - i;
			if (j > 8)
				j = 8;
			can_send(msgid, is_ext, buf+i, j, 0);
			i += j;
			errcount = 0;
		} else {
			// TXB not available
			do {
				irq = can_irq_handler();
				if (irq & MCP2515_IRQ_TX) {
					// TX IRQ present
					if (irq & MCP2515_IRQ_HANDLED) {
						irq = 0;  // Bail out, attempt to send another frame
						// Pace ourselves if user requests so
						if (pace_ms) {
							for (j=0; j < pace_ms; j++)
								__delay_cycles(16000);
						}
					} else {
						errcount++;
						if (errcount > 8) {
							can_tx_cancel();
							return -1;
						}
						__delay_cycles(16000);
					}
				} else {
					if (irq & MCP2515_IRQ_RX) {
						can_recv(&tmp_msgid, &tmp_u8, tmp_buf);
					}
					// No TX IRQ, no RX IRQ, pause and re-check IRQ state
					__delay_cycles(16000);
				}
			} while (irq);
		}
	}

	return 0;
}

void can_pf_putc(uint8_t *buf, uint16_t *idx, unsigned int c)
{
	buf[*idx] = (uint8_t)c;
	*idx += 1;
}

void can_pf_puts(uint8_t *buf, uint16_t *idx, uint8_t *str)
{
	uint16_t i = 0;

	while (str[i] != '\0') {
		buf[*idx] = str[i++];
		*idx += 1;
	}
}


static const unsigned long dv[] = {
//  4294967296      // 32 bit unsigned max
    1000000000,     // +0
     100000000,     // +1
      10000000,     // +2
       1000000,     // +3
        100000,     // +4
//       65535      // 16 bit unsigned max     
         10000,     // +5
          1000,     // +6
           100,     // +7
            10,     // +8
             1,     // +9
};

static void can_pf_xtoa(uint8_t *buf, uint16_t *idx, unsigned long x, const unsigned long *dp)
{
    uint8_t c;
    unsigned long d;
    if(x) {
        while(x < *dp) ++dp;
        do {
            d = *dp++;
            c = '0';
            while(x >= d) ++c, x -= d;
            can_pf_putc(buf, idx, c);
        } while(!(d & 1));
    } else
        can_pf_putc(buf, idx, '0');
}

static void can_pf_puth(uint8_t *buf, uint16_t *idx, unsigned int n)
{
    static const uint8_t hex[16] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
    can_pf_putc(buf, idx, hex[n & 15]);
}
 
void can_printf(uint32_t msgid, uint8_t is_ext, uint8_t *buf, char *format, ...)
{
    uint8_t c;
    int i;
    long n;
    uint16_t idx = 0;
    
    va_list a;
    va_start(a, format);
    while( (c = *format++) ) {
        if(c == '%') {
            switch(c = *format++) {
                case 's':                       // String
                    can_pf_puts(buf, &idx, va_arg(a, uint8_t*));
                    break;
                case 'c':                       // Char
                    can_pf_putc(buf, &idx, va_arg(a, int));   // Char gets promoted to Int in args, so it's an int we're looking for (GCC warning)
                    break;
                case 'i':                       // 16 bit Integer
                case 'd':                       // 16 bit Integer
                case 'u':                       // 16 bit Unsigned
                    i = va_arg(a, int);
                    if( (c == 'i' || c == 'd') && i < 0 ) i = -i, can_pf_putc(buf, &idx, '-');
                    can_pf_xtoa(buf, &idx, (unsigned)i, dv + 5);
                    break;
                case 'l':                       // 32 bit Long
                case 'n':                       // 32 bit uNsigned loNg
                    n = va_arg(a, long);
                    if(c == 'l' &&  n < 0) n = -n, can_pf_putc(buf, &idx, '-');
                    can_pf_xtoa(buf, &idx, (unsigned long)n, dv);
                    break;
                case 'x':                       // 16 bit heXadecimal
                    i = va_arg(a, int);
                    can_pf_puth(buf, &idx, i >> 12);
                    can_pf_puth(buf, &idx, i >> 8);
                    can_pf_puth(buf, &idx, i >> 4);
                    can_pf_puth(buf, &idx, i);
                    break;
                case 0: return;
                default: goto bad_fmt;
            }
        } else
bad_fmt:    can_pf_putc(buf, &idx, c);
    }
    va_end(a);

    can_managed_tx(msgid, is_ext, buf, idx, 50);
}
