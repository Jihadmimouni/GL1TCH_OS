#include "console.h"
#include "kstring.h"
#include "x86.h"

/* See con_capture_start() in console.h. Not reentrant/nested on purpose -
 * this kernel is single-threaded and the shell only ever has one capture
 * active at a time. */
static char *g_cap_buf = 0;
static uint16_t g_cap_max = 0;
static uint16_t g_cap_len = 0;
static int g_cap_overflow = 0;

void _cdecl con_capture_start(char *buf, uint16_t max_len) {
    g_cap_buf = buf;
    g_cap_max = max_len;
    g_cap_len = 0;
    g_cap_overflow = 0;
}

uint16_t _cdecl con_capture_stop(void) {
    uint16_t len = g_cap_len;
    g_cap_buf = 0;
    g_cap_max = 0;
    g_cap_len = 0;
    return len;
}

int _cdecl con_capture_overflowed(void) {
    return g_cap_overflow;
}

void _cdecl con_putc(char c) {
    if (g_cap_buf != 0) {
        if (g_cap_len < g_cap_max) {
            g_cap_buf[g_cap_len++] = c;
        } else {
            g_cap_overflow = 1;
        }
        return;
    }
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
