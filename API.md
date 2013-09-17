# MCP2515 SPI CAN API #

## Initialization ##

Initializing the MCP2515 SPI CAN controller starts with can_init(), which initializes SPI (runs _spi_init()_)
and resets the controller registers.  Necessary I/O ports are initialized, basic interrupts are enabled and the
controller is left in the _CONFIGURATION_ state.

After initialization, the user must configure a speed and necessary masks & filters.

Finally, bringing the controller out of _CONFIGURATION_ mode and into _NORMAL_ mode necessitates running
_can_ioctl(MCP2515_OPTION_SLEEP, 0)_

* void can_init()

    > Initialize controller, SPI and I/O ports related to controller operation

* int can_speed(uint32_t bitrate, uint8_t propagation_segment_hint, uint8_t synchronization_jump)

    > Configure CAN speed.  Bitrate is in Hz, maximum is 1000000.  The other 2 options are advanced
    > CAN options and can be set to '1' (or '0' which is translated to '1' anyhow).  The first option,
    > propagation_segment_hint, can go from '1' to '4' and provides extra time for the signal to propagate
    > to account for longer cable runs.  The second option specifies the maximum number of time slices the
    > controller may shift its receive parser to account for clock skew or jitter in remote controllers.
    >
    > Return value: 0 if success, -1 if error

* int can_rx_setmask(uint8_t maskid, uint32_t msgmask, uint8_t is_ext)

    > Configure one of the two message filter masks.  maskid = 0 is for RXB0, maskid = 1 is for RXB1.
    > msgmask stores up to 29 bits, and is_ext declares whether this is a CAN 2.0B Extended message
    > or Standard 11-bit message.  Keep in mind, on the MCP2515, if is_ext = 0 the device will still use
    > the extended bits (bit# 11-26) as a 16-bit filter against the first 2 bytes in the data field.  This
    > function allows this feature if you provide a >11bit mask.  It's meant to support upper-layer protocols
    > running on top of CAN.
    >
    > The _can_rx_setfilter()_ function will read the RXB#'s mask to determine if its filter is meant to be
    > an Extended message or not.
    >
    > Return value: maskid if success, -1 if error

* int can_rx_setfilter(uint8_t rxb, uint8_t filtid, uint32_t msgid)

    > Set a filter.  rxb is the buffer#, filtid sets the filter# ... rxb0 supports 2 filters (filtid = 0 or 1),
    > rxb1 supports 3 filters (filtid = 0, 1 or 2).  rxb0 is considered a higher-priority buffer, and there is
    > an optional feature (see _can_ioctl()_) to enable ROLLOVER so unread rxb0 contents will get pushed into
    > rxb1 if a new message comes into rxb0 before the user has read rxb0's previous contents.
    >
    > Filter's std. vs ext. setting is derived from the rxb's mask, see _can_rx_setmask()_ above.
    >
    > Return value: filtid if success, -1 if error

* int can_rx_mode(uint8_t rxb, uint8_t mode)

    > Set receive matching mode for the RX buffer#.  Options for _mode_ include:
    > * MCP2515_RXB0CTRL_MODE_RECV_STD_OR_EXT - Receive either STD or EXT messages matching a filter (EXT message ID bits applied to first 2 data bytes for STD messages)
    > * MCP2515_RXB0CTRL_MODE_RECV_STD - Receive only STD messages (EXT message ID bits applied to first 2 data bytes)
    > * MCP2515_RXB0CTRL_MODE_RECV_EXT - Receive only EXT messages
    > * MCP2515_RXB0CTRL_MODE_RECV_ALL - Receive any message, ignoring the filters
    >
    > Return value: 0 if success, -1 if error

## Receiving Data ##

A single function, _can_recv()_ can be used to obtain the next available piece of data.  It scans the RX buffer interrupt flags
in order of 0, 1 (remember; RXB0 is considered "higher priority") in search for pending data.  Upon reading the frame, its contents
are copied to user-supplied buffers & variables and the function returns the length of the data frame.  As an advanced feature,
the return value may have 0x40 OR'd to its value indicating the frame was a Request to Read; RTR in Extended frames or SRR in Standard
frames.  As a result, the return value of this function should be AND'ed with 0x0F before trusting it as an actual data length.

* int can_recv(uint32_t *msgid, uint8_t *is_ext, void *buf)

    > Retrieve the message from the first pending RX buffer, clearing its CANINTF IRQ flag when complete.  The message ID for this
    > message will be stored in the user-supplied uint32_t variable (you must provide a pointer to that) and the Std. vs Ext. message
    > specifier is stored in the user's supplied is_ext variable; 0 = Standard, 1 = Extended.  The data contents are stored in the
    > supplied buffer and the length of that data returned in the lower 4 bits of the function's return value.
    >
    > Return value: Data length possibly OR'd with 0x40 if RTR/SRR was set, -1 if no messages are pending.

* int can_rx_pending()

    > Simple function to determine if any RX IRQs are pending.
    >
    > Return value: RXB ID if any are pending, -1 if none are pending.

## Transmitting Data ##

