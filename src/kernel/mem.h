#pragma once
#include "stdint.h"

/*
 * Kernel heap allocator.
 *
 * This is a classic bounded first-fit allocator over a single static
 * arena embedded in the kernel's own data segment. Everything here
 * deals in ordinary near (16-bit) pointers, because this whole kernel
 * is one 64KB real-mode segment (ds == ss, Open Watcom small model -
 * see entry.asm) - there is no far-pointer/segment-switching code
 * anywhere, and this allocator does not add any. It is bounded (a
 * fixed-size arena, not real virtual memory) and has no notion of
 * pages or protection; a wild pointer can still corrupt it like any
 * other kernel data structure. See mem.c for the arena size and the
 * reasoning behind it (stack-margin check against entry.asm's
 * sp = 0xFFF0).
 */

/*
 * Sets up the free list over the static heap arena. Must be called
 * exactly once, before any kmalloc()/kfree() call - kmain() calls this
 * first, before fat_init()/shell_run(). Does not assume the arena
 * starts out zeroed (stage2 only loads as many bytes as are actually
 * present in KERNEL.BIN, so BSS past the end of the file happens to
 * read as zero in practice under QEMU/BIOS-zeroed low RAM, but this
 * init does not rely on that - it writes every header field it needs).
 */
void _cdecl mem_init(void);

/*
 * Returns a pointer to at least `size` usable, uninitialized bytes, or
 * 0 if no free block is large enough. size == 0 always returns 0.
 * Never panics and never corrupts state on failure - a caller must
 * check for 0, same as any other fallible call in this kernel.
 */
void* _cdecl kmalloc(uint16_t size);

/*
 * Frees a block previously returned by kmalloc(). Freeing 0 is a
 * no-op. Freeing a pointer that didn't come from kmalloc(), or
 * freeing the same pointer twice, is undefined - this is a simple
 * kernel heap, not a hardened allocator, and this kernel has no
 * fault/panic handling yet to catch misuse (see readme.md's other
 * still-open architectural items).
 */
void _cdecl kfree(void *ptr);

/*
 * Reports heap usage in bytes. *total is the arena's fixed size;
 * *used + *free_bytes always equals *total exactly (the free list
 * exactly tiles the arena, block headers counted on whichever side
 * they currently belong to).
 */
void _cdecl mem_get_stats(uint32_t *total, uint32_t *used, uint32_t *free_bytes);

/*
 * Runs a short in-kernel self-test exercising allocation, splitting,
 * freeing, free-slot reuse, clean out-of-memory failure, and
 * neighbor coalescing. Prints "meminfo self-test: PASS" or
 * "meminfo self-test: FAIL: <reason>" to the console. Returns 1 on
 * success, 0 on failure. Intended to run once, early, during boot
 * (see kernel.c) so a boot-test script driving the console can
 * observe the result before the shell prompt appears.
 */
int _cdecl mem_self_test(void);
