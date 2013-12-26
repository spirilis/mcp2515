TARGETMCU	?= msp430g2553

CROSS		:= msp430-
CC		:= $(CROSS)gcc
MSPDEBUG	:= mspdebug
CFLAGS		:= -Os -Wall -Werror -g -mmcu=$(TARGETMCU) -I../../
CFLAGS += -fdata-sections -ffunction-sections -Wl,--gc-sections

LIBSRCS			:= ../../msp430_spi.c ../../mcp2515.c
PROG			:= response

all:			$(PROG).elf

$(PROG).elf:	$(OBJS)
	$(CC) $(CFLAGS) -o $(PROG).elf $(LIBSRCS) $(PROG).c

clean:
	-rm -f *.elf

install: $(PROG).elf
	$(MSPDEBUG) -n rf2500 "prog $(PROG).elf"
