#pragma once
#include "stdint.h"

void _cdecl x86_putc(char c);
void _cdecl x86_clear_screen(void);
uint16_t _cdecl x86_getkey(void);
void _cdecl x86_reboot(void);

int _cdecl x86_disk_reset(uint8_t drive);
int _cdecl x86_disk_params(uint8_t drive, uint16_t *sectors_per_track, uint16_t *heads);
int _cdecl x86_disk_io(uint8_t op, uint8_t drive, uint16_t cylinder,
                       uint8_t head, uint8_t sector, void *buffer);
