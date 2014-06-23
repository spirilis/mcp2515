#ifndef CAN_PRINTF_H
#define CAN_PRINTF_H

/* Printf() derived from oPossum's code - http://forum.43oh.com/topic/1289-tiny-printf-c-version/
 * Doctored up to make GCC happy and function names converted to use an can_ prefix so they don't conflict
 * with MSPGCC's shipped libc.
 */
#include <stdint.h>

void can_printf(uint32_t, uint8_t, uint8_t *, char *format, ...);

#endif
