#include "stdint.h"
#include "x86.h"
#include "console.h"
#include "fat.h"
#include "shell.h"

void _cdecl kmain(uint16_t boot_drive) {
    x86_clear_screen();
    con_puts("GL1TCH OS\r\n");
    con_puts("=========\r\n");

    if (!fat_init((uint8_t)boot_drive)) {
        con_puts("Warning: could not read the filesystem (disk error).\r\n");
    }

    shell_run();

    /* shell_run() never returns, but just in case: */
    for (;;) {
        __asm { cli }
        __asm { hlt }
    }
}
