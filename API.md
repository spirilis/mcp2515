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
the return value may have 0x40 OR'd to its value indicating the frame was a Remote Transfer Request; called RTR in Extended frames or SRR in Standard
frames.  As a result, the return value of this function should be AND'ed with 0x0F before trusting it as an actual data length.
Should an RTR or SRR come through, if this node is the expected manager of that message ID's logical function then it will be your firmware's job
to send a message with this same msgid containing the appropriate data.  Note that this feature is no longer recommended for use (per the book
"Controller Area Network Projects" by Dogan Ibrahim).  If you never expect to see or use this feature, just be sure to AND the
return value with 0x0F every time.

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

Data transmission is designed to be simple with this library; while there are 3 separate TX buffers available, the library
will choose the next available one.  Note that transmits are considered asynchronous; while a TX buffer might be filled
and the Request to Send command was submitted to the controller, the controller is at the mercy of the CAN bus state as to
when it can actually send.  Once messages are sent, the IRQ handler must be executed upon receiving the IRQ signal so that it
may free the affected TX buffer to make room for new outgoing messages.

Transmit buffers have a 2-bit priority which gives the MCP2515 a way to sort the pending TX messages to determine which should
go first; the user must provide this.  Higher numbers mean higher priority.

Messages are sent with a message ID, Extended vs. Standard mode selected, a data payload and priority setting.  Optionally,
a second function called _can_query()_ can use the RTR or SRR feature to request that a remote node managing a particular message ID
provide an update (another frame should be received shortly with that same message ID and the requisite data contents).

* int can_send(uint32_t msg, uint8_t is_ext, void *buf, uint8_t len, uint8_t prio)

    > Send a message on the next available TX buffer.  Up to 29-bit message ID, Std. vs Ext. mode supported,
    > length can be from 0 to 8 and priority from 0 to 3.  Upon data transmission, the TX IRQ may be set and you must
    > execute the IRQ handler (_can_irq_handler()_) to free that buffer for a new message.  If an error occurs, the buffer
    > will be freed too.
    >
    > Return value: TX buffer# if success, -1 if no available TX buffer slots

* int can_query(uint32_t msg, uint8_t is_ext, uint8_t prio)

    > Send an RTR or SRR (Remote Transfer Request) frame for the indicated message ID.  There is no data payload; a 0-byte
    > frame is sent with the RTR (extended message) or SRR (standard message) bit set.  The recipient of this message is
    > supposed to transmit a followup message with this same message ID containing a data payload.  It is used to passively
    > query the state of a remote node's data.  Note this feature is no longer recommended for use (per the book "Controller Area
    > Network Projects" by Dogan Ibrahim).
    >
    > Return value: TX buffer# if success, -1 if no available TX buffer slots

## IRQ Handling ##

IRQ handling is a critical part of using this library and the _can_irq_handler()_ function is a jack-of-many-trades that handles
most errors and successful events itself, communicating with the user by way of its return value and a global _mcp2515_irq_ variable,
which notifies you that all IRQ events have been handled by way of the _MCP2515_IRQ_FLAGGED_ bit.

Upon receiving a HIGH-to-LOW transition on the MCP2515's IRQ pin, your firmware must bitwise-OR the mcp2515_irq variable with MCP2515_IRQ_FLAGGED.
It should wake up the main CPU if it is asleep, and part of your main loop should include a check to see if (bitwise-AND) mcp2515_irq & MCP2515_IRQ_FLAGGED
is non-zero.  If it is, run _can_irq_handler()_ right away and analyze its return value.  This function will only handle 1 event at a time,
and multiple events might be pending which require subsequent repeated calls to _can_irq_handler()_.  The way you deal with this is by
checking the state of "mcp2515_irq & MCP2515_IRQ_FLAGGED" to see if it has cleared before entering any Low-Power sleep modes.  Once you
run _can_irq_handler()_ with no events actually pending, this flag will be cleared and your app will know it is free to go to sleep.
It is critically important that your firmware does NOT clear the MCP2515_IRQ_FLAGGED bit from _mcp2515_irq_.

The return value of _can_irq_handler()_ contains a bitmap of values.  The very first condition that should be watched is whether
the value has MCP2515_IRQ_ERROR _cleared_ but has MCP2515_IRQ_RX _set_.  This indicates a received message is pending; you should run
_can_recv()_ right away to grab it before a new message comes through and overwrites the old buffer contents.

After verifying no pending RX events are here, it is possible (for lazy & simple apps) to merely check if the MCP2515_IRQ_HANDLED bit is
set.  If it is, quit looking at the IRQ handler return value and continue on with your main loop.  You should run _can_irq_handler()_ again
to check if other events are pending or if it's possible to put the CPU to sleep.  Be sure MCP2515_IRQ_FLAGGED is not still set in mcp2515_irq
before entering any Low-Power sleep modes.

However, if you want to know further information, test the MCP2515_IRQ_TX and MCP2515_IRQ_ERROR bits.  MCP2515_IRQ_TX without a corresponding
MCP2515_IRQ_ERROR bit indicates that one of the TX buffers has successfully transmitted its message and that buffer is now available.  You
may send another message after this.  If you are curious, the variable _mcp2515_buf_ contains the TX buffer# that completed its transmit.

The MCP2515_IRQ_ERROR bit indicates one of many error conditions are present.  A breakdown of these is provided:

* MCP2515_IRQ_ERROR alone means Bus Error; use _can_read_error(MCP2515_EFLG)_ to get more detail.  See the Errors section below for more detail.
* MCP2515_IRQ_ERROR with MCP2515_IRQ_RX means a message was in the process of being received but an error occurred.  Nothing more needs to be
  done here.
* MCP2515_IRQ_ERROR with MCP2515_IRQ_TX means a transmit failed.  _mcp2515_buf_ contains the TXB# affected.

