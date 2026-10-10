bits 16

section _ENTRY class=CODE

extern load_kernel
global entry


entry:
    cli
    mov ax, ds
    mov ss, ax
    mov sp, 0
    mov bp, sp
    sti

    ; save the boot drive (BIOS gives it to us in dl)
    mov [boot_drive], dl

    ; load and jump to KERNEL.BIN
    mov dl, [boot_drive]
    call load_kernel

    cli
    hlt

boot_drive: db 0
