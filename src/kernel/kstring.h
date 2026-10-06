#pragma once
#include "stdint.h"

/*
 * Small freestanding string/number helpers. Deliberately NOT named
 * memcpy/strlen/strcmp/etc: Open Watcom recognizes those names as
 * compiler intrinsics and may emit calls with a different (register
 * based) calling convention than the cdecl one defined here, which
 * would corrupt arguments. Picking distinct names avoids that trap.
 */

void _cdecl mem_set(void *dst, uint8_t value, uint16_t n);
void _cdecl mem_copy(void *dst, const void *src, uint16_t n);

uint16_t _cdecl str_len(const char *s);
int _cdecl str_cmp(const char *a, const char *b);
int _cdecl str_ncmp(const char *a, const char *b, uint16_t n);
int _cdecl str_icmp(const char *a, const char *b); /* case-insensitive */
void _cdecl str_copy(char *dst, const char *src);
char* _cdecl str_chr(const char *s, char c);
char _cdecl to_upper(char c);
int _cdecl is_space(char c);
int _cdecl is_digit(char c);

/* Converts v to a string in the given base (10 or 16), NUL-terminated. */
void _cdecl u32_to_str(uint32_t v, char *out, uint8_t base);
void _cdecl i32_to_str(int32_t v, char *out);

/* Parses a decimal integer; returns 1 on success and advances *s past it. */
int _cdecl str_to_i32(const char **s, int32_t *out);
