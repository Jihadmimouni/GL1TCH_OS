bits 16

section _ENTRY class=CODE

extern _cstart_
extern load_kernel
global entry


entry:
    cli
    mov ax, ds
    mov ss, ax
    mov sp, 0
    mov bp, sp
    sti

    ; save the boot drive (BIOS gives it to us in dl) before it can get
    ; clobbered by cstart_
    mov [boot_drive], dl

    ;expect boot drive in dl, send it as argument to cstart function
    xor dh, dh
    push dx
    call _cstart_
    add sp, 2                           ; cdecl: caller cleans up the pushed word

    ; load and jump to KERNEL.BIN
    mov dl, [boot_drive]
    call load_kernel

    cli
    hlt

boot_drive: db 0
