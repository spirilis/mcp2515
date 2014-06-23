/* chargen.c - Framebuffer and character display manager for STE2007 96x68 LCD display
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

#include <msp430.h>
#include <string.h>
#include <stdint.h>
#include "font_5x7.h"
#include "chargen.h"

uint8_t msp1202_framebuffer[MSP1202_COLUMNS*MSP1202_LINES];
uint16_t msp1202_dirtybits[ (MSP1202_COLUMNS / 16) * MSP1202_LINES ];

uint8_t msp1202_x, msp1202_y;

void msp1202_init()
{
	memset(msp1202_framebuffer, ' ', MSP1202_LINES*MSP1202_COLUMNS);
	msp1202_x = 0;
	msp1202_y = 0;
	ste2007_init();

#ifdef MSP1202_USE_CURSOR
	msp1202_framebuffer[msp1202_y * MSP1202_COLUMNS + msp1202_x] = MSP1202_CURSOR;
	msp1202_dirtybits[msp1202_y] |= 1 << msp1202_x;
	msp1202_flush();
#endif
}

void msp1202_putc(uint8_t c, uint8_t doflush)
{
	uint8_t i = 0;

	// Process the character as is
	if (c > 0x80)  // High-bit characters treated as spaces (except 0x80 which is the cursor)
		c = 0x20;

	if (c >= 0x20) {
		msp1202_framebuffer[msp1202_y * MSP1202_COLUMNS + msp1202_x] = c;

		// Flag dirty buffer
		msp1202_dirtybits[msp1202_y] |= 1 << msp1202_x;
		msp1202_x++;
	} else {
		// Process control character
		switch (c) {
			case '\n':
#ifdef MSP1202_USE_CURSOR
				// Erase the cursor presently at msp1202_x,msp1202_y before moving it
				msp1202_framebuffer[msp1202_y * MSP1202_COLUMNS + msp1202_x] = ' ';
				msp1202_dirtybits[msp1202_y] |= 1 << msp1202_x;
#endif
				msp1202_x = 0;
				msp1202_y++;
				break;
			case '\t':
#ifdef MSP1202_USE_CURSOR
				// Erase the cursor presently at msp1202_x,msp1202_y before moving it
				msp1202_framebuffer[msp1202_y * MSP1202_COLUMNS + msp1202_x] = ' ';
				msp1202_dirtybits[msp1202_y] |= 1 << msp1202_x;
#endif
				if (msp1202_x % MSP1202_TAB_SPACING == 0) {
					msp1202_x += MSP1202_TAB_SPACING;
				} else {
					msp1202_x += msp1202_x % MSP1202_TAB_SPACING;
				}
				break;
			case '\b':
				if (msp1202_x > 0) {  // Nothing happens if @ beginning of line
					msp1202_x--;
					// Otherwise, the previous character gets erased.
					msp1202_framebuffer[msp1202_y * MSP1202_COLUMNS + msp1202_x] = ' ';
					msp1202_dirtybits[msp1202_y] |= 1 << msp1202_x;
				}
				break;
			// No default section; any other ctrl char is ignored
		}
	}

	if (msp1202_x >= MSP1202_COLUMNS) {
		// Shift down one row
		msp1202_y++;
		msp1202_x = 0;
	}

	if (msp1202_y >= MSP1202_LINES) {
		// Ut oh, we must scroll...
		for (i=1; i < MSP1202_LINES; i++) {
			memcpy(msp1202_framebuffer+(i-1)*MSP1202_COLUMNS,
			       msp1202_framebuffer+i*MSP1202_COLUMNS,
			       MSP1202_COLUMNS);
			msp1202_dirtybits[i-1] = 0xFFFF;
		}
		msp1202_y = MSP1202_LINES-1;
		memset(msp1202_framebuffer + msp1202_y*MSP1202_COLUMNS,
		       ' ',
		       MSP1202_COLUMNS);  // Clear last line
		msp1202_dirtybits[msp1202_y] = 0xFFFF;
	}

#ifdef MSP1202_USE_CURSOR
	msp1202_framebuffer[msp1202_y * MSP1202_COLUMNS + msp1202_x] = MSP1202_CURSOR;
	msp1202_dirtybits[msp1202_y] |= 1 << msp1202_x;
#endif

	if (doflush)
		msp1202_flush();
}

void msp1202_flush()
{
	uint16_t i, j;

	for (i=0; i < MSP1202_LINES; i++) {
		if (msp1202_dirtybits[i]) {
			if (msp1202_dirtybits[i] == 0xFFFF) {  // Quick optimization for refreshing a whole line
				ste2007_setxy(0, i);
				ste2007_chipselect(0);
				for (j=0; j < MSP1202_COLUMNS; j++) {
					ste2007_write(font_5x7[msp1202_framebuffer[i*MSP1202_COLUMNS+j]-' '], MSP1202_CHAR_WIDTH);
				}
				ste2007_chipselect(1);
			} else {
				for (j=0; j < MSP1202_COLUMNS; j++) {
					if (msp1202_dirtybits[i] & (1 << j)) {
						ste2007_setxy(j*MSP1202_CHAR_WIDTH, i);
						ste2007_chipselect(0);
						ste2007_write(font_5x7[msp1202_framebuffer[i*MSP1202_COLUMNS+j]-' '], MSP1202_CHAR_WIDTH);
						ste2007_chipselect(1);
						msp1202_dirtybits[i] &= ~(1 << j);  // Flushed; clear bit
					}
				}
			}
		}
	}
}

void msp1202_move(uint8_t x, uint8_t y)
{
#ifdef MSP1202_USE_CURSOR
	msp1202_framebuffer[msp1202_y * MSP1202_COLUMNS + msp1202_x] = ' ';
	msp1202_dirtybits[msp1202_y] |= 1 << msp1202_x;
#endif
	msp1202_x = x;
	msp1202_y = y;
#ifdef MSP1202_USE_CURSOR
	msp1202_framebuffer[msp1202_y * MSP1202_COLUMNS + msp1202_x] = MSP1202_CURSOR;
	msp1202_dirtybits[msp1202_y] |= 1 << msp1202_x;
	msp1202_flush();  // Flush only necessary if we're using a cursor.
#endif
}

void msp1202_puts(const char *str)
{
	uint16_t i, j;

	j = strlen(str);
	for (i=0; i < j; i++)
		msp1202_putc((uint8_t)str[i], 0);
	msp1202_flush();
}

void lcd_putc(const unsigned int c)
{
	msp1202_putc((uint8_t)c, 1);
}

void lcd_puts(const char *str)
{
	msp1202_puts(str);
}
