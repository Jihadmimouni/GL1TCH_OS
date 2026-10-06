bits 16

section _ENTRY class=CODE

extern _kmain
global entry

;
; Kernel entry point. stage2 jumps here (KERNEL_LOAD_SEGMENT:0) with the
; boot drive in dl. Code, data and stack all share the kernel's single
; 64KB segment, which is what Open Watcom's small memory model expects
; (ds == ss).
;
entry:
    cli
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0xFFF0                      ; stack at the top of the segment
    mov bp, sp
    sti

    xor dh, dh
    push dx                             ; kmain(boot_drive)
    call _kmain
    add sp, 2

.halt:
    cli
    hlt
    jmp .halt
