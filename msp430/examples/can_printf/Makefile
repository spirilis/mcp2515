TARGETMCU	?= msp430f5529

CROSS		:= msp430-
CC		:= $(CROSS)gcc
MSPDEBUG	:= mspdebug
CFLAGS		:= -Os -Wall -Werror -g -mmcu=$(TARGETMCU)
CFLAGS += -fdata-sections -ffunction-sections -Wl,--gc-sections

LIBSRCS			:= msp430_spi.c mcp2515.c can_printf.c clockinit.c vcore.c
PROG			:= main

all:			$(PROG).elf

$(PROG).elf:	$(OBJS)
	$(CC) $(CFLAGS) -o $(PROG).elf $(LIBSRCS) $(PROG).c

clean:
	-rm -f *.elf

install: $(PROG).elf
	$(MSPDEBUG) -n tilib "prog $(PROG).elf"
