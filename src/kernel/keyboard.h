#pragma once
#include "stdint.h"

/*
 * Reads a line of input, echoing characters and handling backspace.
 * Stops on Enter. Result is NUL-terminated and never exceeds
 * max_len - 1 characters (not counting the NUL).
 */
void _cdecl read_line(char *buf, uint16_t max_len);
