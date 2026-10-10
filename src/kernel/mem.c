#include "mem.h"
#include "console.h"
#include "kstring.h"

/*
 * Heap arena size.
 *
 * This kernel is one single 64KB real-mode segment (ds == es == ss,
 * see entry.asm), and the kernel's actual call stack lives at the top
 * of that very same segment, growing downward from sp = 0xFFF0. Every
 * byte handed to kmalloc() therefore has to come out of address space
 * that isn't needed by the stack during normal operation.
 *
 * Measured from build/kernel.map: before this file existed, the
 * kernel's code+data+BSS already ran up to address 0xFAAC - almost
 * entirely because of shell.c's g_cp_buf (a 32KB static cp/mv
 * buffer) plus fat.c's ~4.5KB g_fat_buf and a handful of smaller
 * shell.c buffers. That leaves only 0xFFF0 - 0xFAAC = 1348 bytes of
 * *total* slack in the entire 64KB segment before this allocator
 * adds anything - nowhere near the 4-16KB that would be ideal, and
 * far less than the loaded-file size (~23KB) suggests, since BSS
 * (uninitialized statics like g_cp_buf) isn't physically present in
 * KERNEL.BIN but still reserves address space above it.
 *
 * Given that reality, 512 bytes was chosen deliberately small: it's
 * enough to be a genuinely working, testable heap (the self-test
 * below exercises it with several concurrent allocations, splitting,
 * reuse and coalescing) while leaving 1348 - (512 + 2) = 834 bytes of
 * real margin below sp = 0xFFF0 for the actual call stack - most of
 * the slack that existed before this file, rather than most of it
 * being eaten by the heap. (The "+2" is g_head below.) If this is
 * ever raised, rebuild and check build/kernel.map's "Memory size"
 * against 0xFFF0 again before trusting the new margin - don't assume.
 */
#define HEAP_SIZE 512U

/*
 * Splitting a free block smaller than this many bytes past what the
 * request needs isn't worth it - the leftover sliver would be mostly
 * (or entirely) header overhead, so the whole block is handed to the
 * caller instead of carving off a near-useless fragment that could
 * never satisfy a future allocation anyway.
 */
#define MIN_SPLIT 8U

/*
 * One block header, living immediately before the usable memory it
 * describes. The heap is one physically-ordered doubly linked list of
 * these (prev/next here mean "physically adjacent block", not
 * "adjacent free block" - there is no separate free list; used and
 * free blocks are threaded together in address order, which is what
 * makes O(1) immediate-neighbor coalescing possible in kfree()).
 */
typedef struct mem_block {
    uint16_t size;              /* usable bytes following this header */
    uint8_t used;                /* 0 = free, 1 = allocated */
    struct mem_block *prev;     /* physically previous block, or 0 */
    struct mem_block *next;     /* physically next block, or 0 */
} mem_block_t;

static uint8_t g_heap[HEAP_SIZE];
static mem_block_t *g_head;     /* first block; fixed at g_heap's start */

void _cdecl mem_init(void) {
    g_head = (mem_block_t *)g_heap;
    g_head->size = (uint16_t)(HEAP_SIZE - sizeof(mem_block_t));
    g_head->used = 0;
    g_head->prev = 0;
    g_head->next = 0;
}

void* _cdecl kmalloc(uint16_t size) {
    mem_block_t *b;
    mem_block_t *newb;
    uint16_t remain;

    if (size == 0) {
        return 0;
    }
    /* Round up to even: keeps every block's data area (and so every
     * following header) at an even offset. Not required for
     * correctness on x86 (unaligned access just works), but it's
     * free and avoids ever handing out an odd-sized split remainder
     * that's one byte shy of being useful. */
    if (size & 1) {
        size++;
    }

    for (b = g_head; b != 0; b = b->next) {
        if (!b->used && b->size >= size) {
            break;
        }
    }
    if (b == 0) {
        return 0; /* no free block large enough - clean failure */
    }

    remain = (uint16_t)(b->size - size);
    if (remain >= (uint16_t)sizeof(mem_block_t) + MIN_SPLIT) {
        /* Split: carve the tail of this block off into a new free
         * block, right after the bytes we're handing out. */
        newb = (mem_block_t *)((uint8_t *)(b + 1) + size);
        newb->size = (uint16_t)(remain - sizeof(mem_block_t));
        newb->used = 0;
        newb->prev = b;
        newb->next = b->next;
        if (newb->next != 0) {
            newb->next->prev = newb;
        }
        b->next = newb;
        b->size = size;
    }
    /* else: remainder too small to be worth splitting off - the
     * caller gets a block slightly bigger than asked for. */

    b->used = 1;
    return (void *)(b + 1);
}

void _cdecl kfree(void *ptr) {
    mem_block_t *b;
    mem_block_t *n;
    mem_block_t *p;

    if (ptr == 0) {
        return;
    }
    b = ((mem_block_t *)ptr) - 1;
    b->used = 0;

    /* Coalesce with the physically next block if it's also free. */
    n = b->next;
    if (n != 0 && !n->used) {
        b->size = (uint16_t)(b->size + sizeof(mem_block_t) + n->size);
        b->next = n->next;
        if (b->next != 0) {
            b->next->prev = b;
        }
    }

    /* Coalesce with the physically previous block if it's also free.
     * Done second (and folds b into p) so a free that has both
     * neighbors free merges all three into one block in one pass. */
    p = b->prev;
    if (p != 0 && !p->used) {
        p->size = (uint16_t)(p->size + sizeof(mem_block_t) + b->size);
        p->next = b->next;
        if (p->next != 0) {
            p->next->prev = p;
        }
    }
}

void _cdecl mem_get_stats(uint32_t *total, uint32_t *used, uint32_t *free_bytes) {
    mem_block_t *b;
    uint32_t u = 0;
    uint32_t f = 0;

    for (b = g_head; b != 0; b = b->next) {
        uint32_t chunk = (uint32_t)sizeof(mem_block_t) + (uint32_t)b->size;
        if (b->used) {
            u += chunk;
        } else {
            f += chunk;
        }
    }

    if (total != 0) {
        *total = (uint32_t)HEAP_SIZE;
    }
    if (used != 0) {
        *used = u;
    }
    if (free_bytes != 0) {
        *free_bytes = f;
    }
}

/* ------------------------------------------------------------------ */
/* Self-test                                                          */
/* ------------------------------------------------------------------ */

static void _cdecl self_test_fail(const char *reason) {
    con_puts("meminfo self-test: FAIL: ");
    con_puts(reason);
    con_puts("\r\n");
}

/*
 * Verifies that a just-written byte pattern is intact over the given
 * span. Returns 1 if the whole span matches, 0 if any byte differs
 * (meaning something else clobbered it - exactly the bug class a
 * broken header/offset calculation would cause).
 */
static int _cdecl pattern_intact(const uint8_t *p, uint16_t len, uint8_t pattern) {
    uint16_t i;
    for (i = 0; i < len; i++) {
        if (p[i] != pattern) {
            return 0;
        }
    }
    return 1;
}

/*
 * Allocation sizes below are deliberately small (HEAP_SIZE is only
 * 512 bytes - see the comment above it) but still chosen so each step
 * genuinely exercises its intended behavior: distinct non-overlapping
 * allocations, a reused freed slot, a clean out-of-memory failure,
 * and a coalesce that only succeeds if two neighbors actually merged.
 */
int _cdecl mem_self_test(void) {
    uint8_t *p1, *p2, *p3, *p4, *p5;
    uint8_t *a, *b, *c;
    uint32_t total, used, free_bytes;

    /* ---- distinct allocations, non-overlapping ---- */
    p1 = (uint8_t *)kmalloc(40);
    p2 = (uint8_t *)kmalloc(80);
    p3 = (uint8_t *)kmalloc(20);
    if (p1 == 0 || p2 == 0 || p3 == 0) {
        self_test_fail("initial allocations returned 0");
        return 0;
    }
    mem_set(p1, 0x11, 40);
    mem_set(p2, 0x22, 80);
    mem_set(p3, 0x33, 20);
    if (!pattern_intact(p1, 40, 0x11) || !pattern_intact(p2, 80, 0x22) ||
        !pattern_intact(p3, 20, 0x33)) {
        self_test_fail("distinct allocations overlapped (pattern clobbered)");
        return 0;
    }

    /* ---- free the middle block, then reuse its slot ---- */
    kfree(p2);
    p4 = (uint8_t *)kmalloc(60); /* must fit inside the freed 80-byte slot */
    if (p4 != p2) {
        self_test_fail("freed slot was not reused by a fitting allocation");
        return 0;
    }
    mem_set(p4, 0x44, 60);
    if (!pattern_intact(p1, 40, 0x11) || !pattern_intact(p3, 20, 0x33) ||
        !pattern_intact(p4, 60, 0x44)) {
        self_test_fail("reused-slot allocation clobbered a neighbor");
        return 0;
    }

    /* ---- an allocation bigger than the whole remaining heap fails
     * cleanly, and the heap stays usable afterward ---- */
    mem_get_stats(&total, &used, &free_bytes);
    if (kmalloc((uint16_t)(free_bytes + 64)) != 0) {
        self_test_fail("oversized allocation should have failed");
        return 0;
    }
    p5 = (uint8_t *)kmalloc(8);
    if (p5 == 0) {
        self_test_fail("heap unusable after a failed oversized allocation");
        return 0;
    }
    mem_set(p5, 0x55, 8);
    if (!pattern_intact(p5, 8, 0x55)) {
        self_test_fail("post-failure allocation is corrupted");
        return 0;
    }
    kfree(p5);

    /* ---- immediate-neighbor coalescing ----
     * a and b are allocated back-to-back with nothing freed in
     * between, and sized (60 bytes each) larger than any fragment
     * left over by the steps above, so both are guaranteed to be
     * carved fresh out of the heap's tail - i.e. physically adjacent.
     * Freeing both must merge them into one free block big enough for
     * an allocation neither 60-byte piece could satisfy alone. */
    a = (uint8_t *)kmalloc(60);
    b = (uint8_t *)kmalloc(60);
    if (a == 0 || b == 0) {
        self_test_fail("coalesce-test setup allocation failed");
        return 0;
    }
    kfree(a);
    kfree(b);
    c = (uint8_t *)kmalloc(100); /* > either 60-byte block alone */
    if (c == 0) {
        self_test_fail("neighbor free blocks did not coalesce");
        return 0;
    }
    if (c != a) {
        self_test_fail("coalesced block did not start at the first neighbor");
        return 0;
    }
    kfree(c);

    /* Leave the heap clean (everything above freed) before the shell
     * starts using it for real work. */
    kfree(p1);
    kfree(p3);
    kfree(p4);

    con_puts("meminfo self-test: PASS\r\n");
    return 1;
}
