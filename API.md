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

