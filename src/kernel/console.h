#pragma once
#include "stdint.h"

void _cdecl con_putc(char c);
void _cdecl con_puts(const char *s);
void _cdecl con_print_u32(uint32_t v, uint8_t base);
void _cdecl con_print_i32(int32_t v);

/*
 * Output capture, used by the shell to implement `>`/`>>` redirection and
 * `|` piping: while active, con_putc()/con_puts() append to buf instead of
 * writing to the real screen. Captures do not nest - starting a new one
 * while another is active simply replaces it.
 */
void _cdecl con_capture_start(char *buf, uint16_t max_len);

/* Stops capturing and returns the number of bytes written into the
 * buffer passed to con_capture_start (not counting any that overflowed
 * it - see con_capture_overflowed). */
uint16_t _cdecl con_capture_stop(void);

/* Whether the most recent capture tried to write past its buffer's end.
 * Valid until the next con_capture_start() call. */
int _cdecl con_capture_overflowed(void);
