;
; hello_rm.asm - a minimal real-mode test program for GL1TCH OS's
; program loader (see src/kernel/program.c/program.h and
; src/kernel/x86.asm's x86_call_far). This replaces the old hello.c,
; which was a native Linux ELF binary (compiled with the host gcc) that
; could never actually run on this real-mode OS - a leftover test
; artifact, not a real loaded program.
;
; Assembled as a flat binary (nasm -f bin), with no header of any kind:
; the kernel's loader streams this file's raw bytes straight into a
; fresh segment at offset 0 and far-calls offset 0 in that segment.
;
; ABI this program implements (see program.h for the full contract):
;   - On entry, cs:ip = <load segment>:0000. ds/es are whatever the
;     kernel had (its own segment) at the moment of the call - NOT this
;     program's own segment - so the very first thing done here is
;     pointing ds/es at our own segment via cs, exactly mirroring what
;     src/kernel/entry.asm does for the kernel itself.
;   - ss/sp are left completely untouched for this program's entire
;     run: it executes on the kernel's own existing stack, and never
;     pushes/pops anything that isn't balanced by a matching pop/push
;     within this same file (the one "call tt_print" below is a normal
;     near call/ret pair, fully balanced).
;   - All output goes straight through the BIOS (int 10h teletype),
;     the same technique the bootloader/kernel already use in
;     src/kernel/x86.asm's x86_putc - this program never calls back
;     into any kernel C function, it is fully self-contained.
;   - Control returns to the kernel with a plain retf.
;
bits 16

section .text

start:
    ; ds/es on entry belong to the KERNEL's segment, not ours - point
    ; them at our own segment (cs) before touching any of our own data,
    ; same as entry.asm does for the kernel at boot.
    mov ax, cs
    mov ds, ax
    mov es, ax

    mov si, msg
    call tt_print

    retf                                 ; hand control back to the kernel

; Prints the NUL-terminated string at ds:si via BIOS int 10h teletype
; output (ah=0x0Eh), one character at a time. Clobbers ax/bx only;
; preserves si (advances it, which is fine - this program never uses it
; again) and never touches ss/sp.
tt_print:
    push ax
    push bx
.next:
    mov al, [si]
    cmp al, 0
    je .done
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    inc si
    jmp .next
.done:
    pop bx
    pop ax
    ret

msg: db "Hello from a LOADED real-mode program! (hello_rm.bin via GL1TCH OS's program loader)", 0x0D, 0x0A, 0
