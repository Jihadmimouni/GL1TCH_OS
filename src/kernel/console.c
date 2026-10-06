#include "console.h"
#include "kstring.h"
#include "x86.h"

void _cdecl con_putc(char c) {
    x86_putc(c);
}

void _cdecl con_puts(const char *s) {
    while (*s != '\0') {
        con_putc(*s);
        s++;
    }
}

void _cdecl con_print_u32(uint32_t v, uint8_t base) {
    char buf[12];
    u32_to_str(v, buf, base);
    con_puts(buf);
}

void _cdecl con_print_i32(int32_t v) {
    char buf[13];
    i32_to_str(v, buf);
    con_puts(buf);
}
