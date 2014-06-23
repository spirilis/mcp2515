/* STE2007 LCD text driver
 * Compatible with Nokia 1202 B&W LCD display
 * Basic driver I/O primitives for TI MSP430 microcontrollers
 * Copyright (C) 2013 Eric Brundick <spirilis@linux.com>
 *
 * References code and examples from John Honniball, Bristol UK and:
 * Copyright (c) 2012, Greg Davill
 *    All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *     Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the
 *     distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <msp430.h>
#include <stdint.h>
#include "ste2007.h"
#include "msp430_spi.h"


/* This is a generic and will require tweaking for each specific environment
 *   where this library is used.
 *
 * It is assumed the function takes an argument of type uint16_t and the return
 *   value isn't used (so it can be void).
 *
 * Please add the necessary #include to this source file so the C compiler can
 *   resolve this function.
 */
#define STE2007_SPI_TRANSFER9(x) spi_transfer9(x)
// Chip Select line drive; takes 0 or 1 to set the CS line low or high
#define STE2007_CHIPSELECT(x) lcd_chipselect(x)
void lcd_chipselect(uint8_t);  // Declared in the main .c file we'll be using


/* SPI command I/O functions */
void ste2007_issuecmd(uint8_t cmd, uint8_t arg, uint8_t argmask)
{
	STE2007_CHIPSELECT(0);
	STE2007_SPI_TRANSFER9( (uint16_t) (cmd | (arg & argmask)) );
	STE2007_CHIPSELECT(1);
}

void ste2007_issue_compoundcmd(uint8_t cmd, uint8_t arg, uint8_t argmask)
{
	STE2007_CHIPSELECT(0);
	STE2007_SPI_TRANSFER9( (uint16_t)cmd );
	STE2007_SPI_TRANSFER9( (uint16_t) (arg & argmask) );
	STE2007_CHIPSELECT(1);
}

/* A convenience function so the user can configure the Chip Select I/O in one place and use
 * it portable in other libraries stacked on top of this one.
 */
void ste2007_chipselect(uint8_t onoff)
{
	STE2007_CHIPSELECT(onoff);
}

/* Logical LCD operations */
void ste2007_init()
{
	ste2007_issuecmd(STE2007_CMD_RESET, 0, STE2007_MASK_RESET);  // Soft RESET
	ste2007_issuecmd(STE2007_CMD_DPYALLPTS, 0, STE2007_MASK_DPYALLPTS); // Powersave ALLPOINTS-ON mode turned OFF
	ste2007_issuecmd(STE2007_CMD_PWRCTL, 7, STE2007_MASK_PWRCTL); // Power control set to max
	ste2007_issuecmd(STE2007_CMD_ONOFF, 1, STE2007_MASK_ONOFF); // Display ON
	ste2007_issuecmd(STE2007_CMD_COMDIR, 0, STE2007_MASK_COMDIR); // Display common driver = NORMAL
	ste2007_issuecmd(STE2007_CMD_SEGMENTDIR, 0, STE2007_MASK_SEGMENTDIR); // Lines start at the left
	ste2007_issuecmd(STE2007_CMD_ELECTVOL, 16, STE2007_MASK_ELECTVOL); // Electronic volume set to 16

	ste2007_clear();

	ste2007_issue_compoundcmd(STE2007_CMD_REFRESHRATE, 3, STE2007_MASK_REFRESHRATE); // Refresh rate = 65Hz
	ste2007_issue_compoundcmd(STE2007_CMD_CHARGEPUMP, 0, STE2007_MASK_CHARGEPUMP); // Charge Pump multiply factor = 5x
	ste2007_issuecmd(STE2007_CMD_SETBIAS, 6, STE2007_MASK_SETBIAS); // Bias ratio = 1/4
	ste2007_issue_compoundcmd(STE2007_CMD_VOP, 0, STE2007_MASK_VOP);
	ste2007_issuecmd(STE2007_CMD_DPYREV, 0, STE2007_MASK_DPYREV); // Display normal (not inverted)
}

/* Fully erase DDRAM */
void ste2007_clear()
{
	int i;

	ste2007_setxy(0, 0);
	STE2007_CHIPSELECT(0);
	for (i=0; i < 16*6*9; i++) {
		STE2007_SPI_TRANSFER9( 0x100 );  // Write 0
	}
	STE2007_CHIPSELECT(1);
}

/* Set DDRAM cursor */
void ste2007_setxy(uint8_t x, uint8_t y)
{
	ste2007_issuecmd(STE2007_CMD_LINE, y, STE2007_MASK_LINE);
	ste2007_issuecmd(STE2007_CMD_COLMSB, x >> 4, STE2007_MASK_COLMSB);
	ste2007_issuecmd(STE2007_CMD_COLLSB, x, STE2007_MASK_COLLSB);
}

/* Bulk-write data to DDRAM
 * Note: This function does not drive the Chip Select line but assumes that you will.
 */
void ste2007_write(const void *buf, uint16_t len)
{
	int i;
	uint8_t *ubuf = (uint8_t *)buf;

	for (i=0; i < len; i++) {
		STE2007_SPI_TRANSFER9( (uint16_t)(*ubuf++) | 0x100 );
	}
}

/* Just for fun... */
void ste2007_invert(uint8_t onoff)
{
	ste2007_issuecmd(STE2007_CMD_DPYREV, onoff, STE2007_MASK_DPYREV);
}

/* STE2007 datasheet lists ONOFF=0, DPYALLPTS=1 as a "Power saver" mode. */
void ste2007_powersave(uint8_t onoff)  // 1 = power-saver mode, 0 = normal mode
{
	ste2007_issuecmd(STE2007_CMD_DPYALLPTS, onoff, STE2007_MASK_DPYALLPTS);
	ste2007_issuecmd(STE2007_CMD_ONOFF, !onoff, STE2007_MASK_ONOFF);
}

/* Set contrast; val is a scale from 0-31 and configures the Electronic Volume setting.
 * 16 is the default.
 */
void ste2007_contrast(uint8_t val)
{
	ste2007_issuecmd(STE2007_CMD_ELECTVOL, val, STE2007_MASK_ELECTVOL);
}

/* Set LCD refresh rate, just for the heck of it.
 * Supported values: 65, 70, 75, 80 (Hz)
 */
void ste2007_refreshrate(uint8_t val)
{
	switch (val) {
		case 80:
			ste2007_issue_compoundcmd(STE2007_CMD_REFRESHRATE, 0, STE2007_MASK_REFRESHRATE);
			break;

		case 75:
			ste2007_issue_compoundcmd(STE2007_CMD_REFRESHRATE, 1, STE2007_MASK_REFRESHRATE);
			break;

		case 70:
			ste2007_issue_compoundcmd(STE2007_CMD_REFRESHRATE, 2, STE2007_MASK_REFRESHRATE);
			break;

		default:
			ste2007_issue_compoundcmd(STE2007_CMD_REFRESHRATE, 3, STE2007_MASK_REFRESHRATE);
	}
}

/* Helper function; write a single 5x8 character at the specified location with the 6th line blank for
 * character spacing.
 * Each byte in *buf is a vertical slice of the row, MSB/LSB orientation depends on SEGMENTDIR parameter.
 *
 * If either x or y are <0, forget setting the cursor and just write it to the current
 *   DDRAM cursor location.
 * x coordinate specified as character position (0-15), not pixel position.  y specified
 *   as character row (page row).
 *
 * This function will drive the Chip Select line since it's meant to be a one-off helper function.
 */
void ste2007_putchar(int16_t x, int16_t y, const void *buf)
{
	if (x > -1 && y > -1) {
		ste2007_setxy(x * 6, y);
	}
	STE2007_CHIPSELECT(0);
	ste2007_write(buf, 5);
	STE2007_SPI_TRANSFER9( 0x100 );  // 6th column is blank for character spacing
	STE2007_CHIPSELECT(1);
}
