#include "stdint.h"
#include "x86.h"
#include "console.h"
#include "mem.h"
#include "fat.h"
#include "shell.h"

void _cdecl kmain(uint16_t boot_drive) {
    x86_clear_screen();
    con_puts("GL1TCH OS\r\n");
    con_puts("=========\r\n");

    /* Heap must be ready before anything that might allocate - nothing
     * does yet, but this is the one-time setup point everything else
     * (program loading, etc.) will build on. The self-test runs once,
     * here, so its PASS/FAIL is visible on the console before the
     * shell prompt appears. */
    mem_init();
    mem_self_test();

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
