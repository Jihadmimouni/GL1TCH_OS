#pragma once
#include "stdint.h"

/*
 * Program loading: runs a flat real-mode binary straight off the FAT
 * filesystem, in its own segment, separate from the kernel.
 *
 * This is deliberately a minimal "load one program, run it to
 * completion, get control back" model - not a real process model. What
 * it actually provides:
 *
 *   - The program is loaded whole into a fixed, fresh segment
 *     (PROGRAM_LOAD_SEGMENT in program.c, currently 0x6000) separate
 *     from the kernel's own segment (0x4000) and from the bootloader's
 *     old segment (0x2000) - see program.c for the memory-map reasoning.
 *   - Control is transferred via a genuine far call to
 *     PROGRAM_LOAD_SEGMENT:0000 (x86_call_far in x86.asm). On entry,
 *     cs:ip is that address; ds/es are whatever the kernel had (its own
 *     segment) at the moment of the call. A loaded program's very first
 *     instructions must set its own ds/es from cs before touching any
 *     of its own data (exactly mirroring what entry.asm does for the
 *     kernel itself) - see tools/hello_rm.asm for a worked example.
 *   - ss/sp are NEVER switched and the loaded program must never touch
 *     them - it runs on the kernel's own existing stack for its entire
 *     lifetime. This is what makes the ABI safe without any of the
 *     usual segmented-real-mode stack-restore gymnastics: there is no
 *     stack switch, so there is nothing to get wrong restoring it.
 *   - The program returns control with a plain retf. x86_call_far
 *     unconditionally restores the kernel's own ds/es right after that,
 *     regardless of what the program did to them in the meantime.
 *   - Exactly one program loaded and run at a time, synchronously -
 *     program_run() doesn't return to its caller (the shell) until the
 *     loaded program has already run to completion and returned.
 *
 * What this explicitly does NOT provide (still open items - see
 * readme.md): no memory protection or isolation between the kernel and
 * a loaded program (real mode has none to offer); no concurrent/multiple
 * programs; no argument passing to the loaded program; no dynamic
 * loading of further programs from within a loaded program; nothing
 * resembling virtual memory. A loaded program is exactly as trusted as
 * the kernel itself - it runs with the same privilege (there is no
 * other privilege level in real mode) and can corrupt kernel memory
 * just as easily as a kernel bug could.
 */

/*
 * Loads the file at path and runs it (see the ABI contract above).
 * Returns 1 if the program was successfully loaded and transferred
 * control to, and that control later returned here via retf - this is
 * NOT a judgment on whatever the program itself did while it ran, only
 * on the load-and-run mechanics themselves succeeding.
 *
 * Returns 0 with *err_msg set to a static string on failure: path not
 * found, path names a directory, the file is empty, the file is larger
 * than this loader supports, or a disk read error occurs partway
 * through loading. In all failure cases nothing is run and control
 * returns to the caller exactly as any other failed shell command
 * would.
 */
int _cdecl program_run(const char *path, const char **err_msg);
