#pragma once
#include "stdint.h"

void _cdecl con_putc(char c);
void _cdecl con_puts(const char *s);
void _cdecl con_print_u32(uint32_t v, uint8_t base);
void _cdecl con_print_i32(int32_t v);
