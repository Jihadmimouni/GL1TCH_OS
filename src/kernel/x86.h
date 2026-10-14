#pragma once
#include "stdint.h"

void _cdecl x86_putc(char c);
void _cdecl x86_clear_screen(void);
uint16_t _cdecl x86_getkey(void);
void _cdecl x86_reboot(void);

/*
 * Reads the BIOS real-time clock and packs the result directly into
 * FAT's on-disk date/time formats (see fat.c's build_dirent_raw):
 *   *fat_date: bits 15-9 = year-1980, bits 8-5 = month, bits 4-0 = day
 *   *fat_time: bits 15-11 = hour, bits 10-5 = minute, bits 4-0 = second/2
 */
void _cdecl x86_get_datetime(uint16_t *fat_date, uint16_t *fat_time);

int _cdecl x86_disk_reset(uint8_t drive);
int _cdecl x86_disk_params(uint8_t drive, uint16_t *sectors_per_track, uint16_t *heads);
int _cdecl x86_disk_io(uint8_t op, uint8_t drive, uint16_t cylinder,
                       uint8_t head, uint8_t sector, void *buffer);

/*
 * Copies len bytes from the kernel's own near pointer src (interpreted
 * relative to the caller's current ds - always true when called from
 * kernel C code, since the whole kernel lives in one segment) to
 * dst_seg:dst_off. Used by program.c to stream a loaded program's bytes
 * into its own segment. Does not require or touch ds itself - only es
 * (the destination segment) changes, and it is restored before return.
 */
void _cdecl x86_far_copy(uint16_t dst_seg, uint16_t dst_off, const void *src, uint16_t len);

/*
 * Transfers control to a loaded program at segment:0000 via a genuine
 * far call, then unconditionally restores ds/es to the kernel's own
 * values afterward, regardless of what the program did to them. See
 * x86.asm for the full ABI contract this implements and program.h for
 * the loader that uses it - this is the single most safety-critical
 * function in the loader, since a bug here corrupts the kernel's own
 * addressing for everything that runs after it returns.
 */
void _cdecl x86_call_far(uint16_t segment);
