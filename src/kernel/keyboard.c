#include "keyboard.h"
#include "console.h"
#include "x86.h"

#define KEY_ENTER     0x0D
#define KEY_BACKSPACE 0x08

void _cdecl read_line(char *buf, uint16_t max_len) {
    uint16_t len = 0;
    uint16_t key;
    char ascii;

    for (;;) {
        key = x86_getkey();
        ascii = (char)(key & 0xFF);

        if (ascii == KEY_ENTER) {
            break;
        }

        if (ascii == KEY_BACKSPACE) {
            if (len > 0) {
                len--;
                con_putc(KEY_BACKSPACE);
                con_putc(' ');
                con_putc(KEY_BACKSPACE);
            }
            continue;
        }

        if (ascii >= 0x20 && ascii <= 0x7E) {
            if (len + 1 < max_len) {
                buf[len++] = ascii;
                con_putc(ascii);
            }
            continue;
        }
        /* ignore other control / extended keys */
    }

    buf[len] = '\0';
    con_puts("\r\n");
}
