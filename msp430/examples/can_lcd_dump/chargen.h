/* Nokia 1202 LCD terminal library
 *
    Copyright (c) 2013 Eric Brundick <spirilis [at] linux dot com>

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy,
    modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
 */

#ifndef CHARGEN_H
#define CHARGEN_H

#include <stdint.h>
#include "ste2007.h"


/* Enable the optional cursor showing current position
 * Comment-out this #define in order to disable it
 */
#define MSP1202_USE_CURSOR 1
#define MSP1202_CURSOR 0x80

/* LCD configuration */
#define MSP1202_LINES 8
#define MSP1202_CHAR_WIDTH 6
#define MSP1202_COLUMNS 16
#define MSP1202_TAB_SPACING 4

/* Character handling */
void msp1202_init();
void msp1202_putc(uint8_t c, uint8_t doflush);
void msp1202_puts(const char *str);
void msp1202_move(uint8_t x, uint8_t y);
void msp1202_flush();

extern uint8_t msp1202_framebuffer[];
extern uint16_t msp1202_dirtybits[];

#endif /* OKAYA_H */
