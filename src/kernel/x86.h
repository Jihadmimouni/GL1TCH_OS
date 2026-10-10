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
