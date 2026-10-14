#include "program.h"
#include "fat.h"
#include "x86.h"

/*
 * Segment a loaded program is placed in and run from. Chosen to sit
 * comfortably in real-mode conventional memory (0-640KB / 0x0-0xA0000)
 * well clear of every other segment this kernel uses:
 *   - 0x2000 (physical 0x20000/128KB): the bootloader's old stage2
 *     segment - not reused after the kernel takes over, but left alone
 *     here out of caution.
 *   - 0x4000 (physical 0x40000/256KB): the kernel's own segment
 *     (code+data+heap+stack - see entry.asm/mem.c), ending at 0x50000.
 *   - 0x6000 (physical 0x60000/384KB): this segment. A full, untouched
 *     64KB gap (0x50000-0x60000) separates it from the kernel's segment
 *     for safety margin.
 * Nothing else in this kernel ever touches segment 0x6000, so unlike
 * the kernel's own cramped segment (see mem.c's comments on its ~540
 * bytes of margin below the stack), a loaded program effectively gets
 * the whole 64KB to itself.
 */
#define PROGRAM_LOAD_SEGMENT 0x6000

/*
 * Maximum size of a loaded program's file. The ABI (see program.h)
 * forbids a loaded program from ever switching ss/sp - it runs on the
 * kernel's own stack for its whole lifetime - so, unlike the kernel's
 * own segment, there is no stack sharing this segment and essentially
 * all 64KB of it is fair game for the program's own code+data. This cap
 * still leaves the top 1KB (0xFC00-0xFFFF) of the segment untouched,
 * purely as defensive headroom (e.g. against off-by-one chunk math
 * while streaming the file in below) - not because anything else lives
 * there. There's no need for the kind of tight, verified margin
 * counting mem.c's heap needed, since that was specifically about
 * sharing a segment with a live call stack, which doesn't apply here.
 */
#define PROGRAM_MAX_SIZE 0xFC00UL /* 64512 bytes */

typedef struct {
    uint32_t offset; /* bytes written into the target segment so far */
    int overflow;    /* set if the file turned out to exceed PROGRAM_MAX_SIZE
                       * mid-stream (fat_find_file's entry.size should have
                       * already caught this - this is a defensive backstop,
                       * not the primary check) */
} load_ctx_t;

/* fat_read_file's sink: far-copies each chunk (up to ~512 bytes, see
 * fat.h) straight into PROGRAM_LOAD_SEGMENT at the next free offset. */
static void _cdecl load_sink(const uint8_t *data, uint16_t len, void *ctx) {
    load_ctx_t *c = (load_ctx_t *)ctx;

    if (c->overflow) {
        return;
    }
    if (c->offset + (uint32_t)len > PROGRAM_MAX_SIZE) {
        c->overflow = 1;
        return;
    }

    x86_far_copy(PROGRAM_LOAD_SEGMENT, (uint16_t)c->offset, data, len);
    c->offset += len;
}

int _cdecl program_run(const char *path, const char **err_msg) {
    fat_dirent_t entry;
    load_ctx_t ctx;

    if (!fat_find_file(path, &entry, err_msg)) {
        return 0;
    }
    if (entry.attr & FAT_ATTR_DIRECTORY) {
        *err_msg = "not a file";
        return 0;
    }
    if (entry.size == 0) {
        *err_msg = "file is empty";
        return 0;
    }
    if (entry.size > PROGRAM_MAX_SIZE) {
        *err_msg = "program too large (max 63KB)";
        return 0;
    }

    ctx.offset = 0;
    ctx.overflow = 0;
    if (!fat_read_file(&entry, load_sink, &ctx)) {
        *err_msg = "disk read error";
        return 0;
    }
    if (ctx.overflow) {
        /* Shouldn't happen given the entry.size check above, but a
         * directory's size field can lie in principle - fail cleanly
         * rather than running a half-loaded program. */
        *err_msg = "program too large (max 63KB)";
        return 0;
    }

    /* Transfer control. See x86_call_far's comment in x86.asm for the
     * full ABI and the ds/es save/restore this performs unconditionally
     * around the far call. */
    x86_call_far(PROGRAM_LOAD_SEGMENT);

    return 1;
}
