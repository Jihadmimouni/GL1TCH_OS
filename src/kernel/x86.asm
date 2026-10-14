bits 16

section _TEXT class=CODE

;
; BIOS wrappers callable from C. All use the cdecl convention: arguments
; are pushed right to left as 16-bit words, the result is returned in ax
; and the caller cleans up the stack.
;

;
; void _cdecl x86_putc(char c);
;
global _x86_putc
_x86_putc:
    push bp
    mov bp, sp
    push bx

    mov ah, 0x0E                        ; teletype output
    mov al, [bp + 4]
    mov bh, 0                           ; page 0
    int 0x10

    pop bx
    mov sp, bp
    pop bp
    ret

;
; void _cdecl x86_clear_screen(void);
;
global _x86_clear_screen
_x86_clear_screen:
    mov ax, 0x0003                      ; set 80x25 text mode, clears screen
    int 0x10
    ret

;
; uint16_t _cdecl x86_getkey(void);
; Waits for a key press. Returns scan code in the high byte, ASCII in the low byte.
;
global _x86_getkey
_x86_getkey:
    mov ah, 0
    int 0x16
    ret

;
; void _cdecl x86_reboot(void);
;
global _x86_reboot
_x86_reboot:
    jmp 0FFFFh:0

;
; int _cdecl x86_disk_reset(uint8_t drive);
; Returns 1 on success, 0 on failure.
;
global _x86_disk_reset
_x86_disk_reset:
    push bp
    mov bp, sp

    mov ah, 0
    mov dl, [bp + 4]
    stc
    int 0x13
    mov ax, 1
    jnc .ok
    xor ax, ax
.ok:
    mov sp, bp
    pop bp
    ret

;
; int _cdecl x86_disk_params(uint8_t drive, uint16_t *sectors_per_track, uint16_t *heads);
; Returns 1 on success, 0 on failure.
;
global _x86_disk_params
_x86_disk_params:
    push bp
    mov bp, sp
    push bx
    push si
    push di
    push es

    mov ah, 0x08
    mov dl, [bp + 4]
    xor di, di                          ; es:di = 0:0 works around buggy BIOSes
    mov es, di
    stc
    int 0x13
    mov ax, 0
    jc .done

    and cl, 0x3F                        ; sectors per track in bits 0-5
    xor ch, ch
    mov si, [bp + 6]
    mov [si], cx

    mov cl, dh                          ; dh = last head index
    xor ch, ch
    inc cx
    mov si, [bp + 8]
    mov [si], cx

    mov ax, 1
.done:
    pop es
    pop di
    pop si
    pop bx
    mov sp, bp
    pop bp
    ret

;
; int _cdecl x86_disk_io(uint8_t op, uint8_t drive, uint16_t cylinder,
;                        uint8_t head, uint8_t sector, void *buffer);
; Reads (op = 2) or writes (op = 3) one sector at CHS into ds:buffer.
; Returns 1 on success, 0 on failure.
;
global _x86_disk_io
_x86_disk_io:
    push bp
    mov bp, sp
    push bx
    push si
    push di
    push es

    push ds
    pop es                              ; es:bx = ds:buffer

    mov ax, [bp + 8]                    ; cylinder
    mov ch, al                          ; ch = cylinder bits 0-7
    mov cl, ah
    shl cl, 6                           ; cl bits 6-7 = cylinder bits 8-9
    mov al, [bp + 12]                   ; sector (1-based)
    and al, 0x3F
    or cl, al

    mov dh, [bp + 10]                   ; head
    mov dl, [bp + 6]                    ; drive
    mov bx, [bp + 14]                   ; buffer
    mov ah, [bp + 4]                    ; 02h = read, 03h = write
    mov al, 1                           ; one sector

    stc
    int 0x13
    mov ax, 1
    jnc .done
    xor ax, ax
.done:
    pop es
    pop di
    pop si
    pop bx
    mov sp, bp
    pop bp
    ret

;
; void _cdecl x86_far_copy(uint16_t dst_seg, uint16_t dst_off, const void *src, uint16_t len);
; Copies len bytes from the kernel's own near pointer src (relative to
; the caller's current ds - always true when called from kernel C code,
; since the whole kernel lives in one 64KB segment) to dst_seg:dst_off.
; Standard real-mode technique: point es:di at the destination, leave
; ds:si pointing at the source (ds is already correct and is never
; touched), and let rep movsb do the copy - source segment defaults to
; ds, destination to es. es is saved/restored; si/di/cx are caller-saved
; scratch under this kernel's convention (see x86_disk_io above, which
; only saves the registers it doesn't already clobber as return values)
; and are not restored.
;
global _x86_far_copy
_x86_far_copy:
    push bp
    mov bp, sp
    push si
    push di
    push es

    mov es, [bp + 4]                    ; dst_seg
    mov di, [bp + 6]                    ; dst_off
    mov si, [bp + 8]                    ; src (near; ds:src)
    mov cx, [bp + 10]                   ; len
    cld
    rep movsb

    pop es
    pop di
    pop si
    mov sp, bp
    pop bp
    ret

;
; void _cdecl x86_call_far(uint16_t segment);
;
; The kernel-side trampoline for program loading (see program.h/program.c
; for the loader that calls this). Transfers control to a loaded program
; at segment:0000 via a genuine far call - so the program's own retf
; returns control right here - then UNCONDITIONALLY restores ds/es to
; the kernel's own values afterward, no matter what the program did to
; them. This is the single most safety-critical function in the whole
; loader: every other kernel function assumes ds never moves out from
; under it, and this is the one place that intentionally changes it.
;
; Full ABI contract with the loaded program:
;   - On entry, cs:ip = segment:0000 (set by the far call below) and
;     ds/es = whatever the kernel had (its own segment) at the moment
;     of the call. The program's very first instructions must set its
;     own ds/es from cs before touching any of its own data (mirroring
;     what entry.asm does for the kernel itself).
;   - ss/sp are NOT switched here, and the loaded program must NEVER
;     touch them - it runs on the kernel's existing stack for its
;     entire lifetime. This is what sidesteps the classic real-mode
;     trap of a callee switching ss:sp and needing to restore them
;     perfectly before retf: by simply forbidding stack switches in
;     this ABI, that whole failure class can't happen (assuming the
;     program obeys the contract).
;   - The program returns with a plain retf.
;
; Because segment is a runtime value (not a link-time constant), the
; far-call target can't be written as a literal `call far seg:0` -
; instead a far pointer (offset word, then segment word - x86's
; standard in-memory far-pointer layout) is built in a small stack
; scratch slot and called indirectly via `call far [mem]`.
;
; Defense in depth: even though the ABI forbids the guest from touching
; sp, this also snapshots sp into cx immediately before the far call and
; forcibly re-pins sp to that value immediately after it returns, before
; anything else is popped. In the well-behaved case this is a no-op
; (call far's return address push/pop is self-balancing, so sp is back
; to the same value anyway); if a buggy or malicious guest left the
; stack unbalanced, this stops that corruption from propagating into the
; ds/es restore below (or into the caller's own stack frame).
;
global _x86_call_far
_x86_call_far:
    push bp
    mov bp, sp
    push ds                              ; saved ds at [bp - 2]
    push es                              ; saved es at [bp - 4]
    sub sp, 4                            ; far-ptr scratch at [bp - 8]/[bp - 6]

    mov word [bp - 8], 0                 ; far ptr offset = 0 (program entry point)
    mov ax, [bp + 4]                     ; segment argument
    mov word [bp - 6], ax                ; far ptr segment

    mov cx, sp                           ; snapshot sp = bp - 8, taken right before
                                          ; the far call (see comment above)
    call far [bp - 8]                    ; transfer control to segment:0000; control
                                          ; returns here via the program's retf

    mov sp, cx                           ; force sp back to its pre-call value,
                                          ; regardless of what the guest did
    add sp, 4                            ; drop the far-ptr scratch slot
    pop es                               ; restore the kernel's own es
    pop ds                               ; restore the kernel's own ds

    mov sp, bp
    pop bp
    ret

;
; void _cdecl x86_get_datetime(uint16_t *fat_date, uint16_t *fat_time);
; Reads the current date/time from the BIOS real-time clock (int 1Ah,
; AH=02h for time, AH=04h for date; both return packed BCD) and packs
; the result directly into FAT's on-disk date/time formats:
;   date: bits 15-9 = year-1980, bits 8-5 = month(1-12), bits 4-0 = day(1-31)
;   time: bits 15-11 = hour(0-23), bits 10-5 = minute(0-59), bits 4-0 = second/2
;
global _x86_get_datetime
_x86_get_datetime:
    push bp
    mov bp, sp
    sub sp, 14                          ; locals: hour,min,sec,century,year,month,day
    push bx
    push cx
    push dx
    push si

    ; ---- time: ah=02h -> ch=hour cl=minute dh=second, all packed BCD ----
    mov ah, 0x02
    int 0x1a

    mov al, ch
    call datetime_bcd2bin
    mov [bp - 2], ax                    ; hour

    mov al, cl
    call datetime_bcd2bin
    mov [bp - 4], ax                    ; minute

    mov al, dh
    call datetime_bcd2bin
    mov [bp - 6], ax                    ; second

    ; ---- date: ah=04h -> ch=century cl=year dh=month dl=day, all packed BCD ----
    mov ah, 0x04
    int 0x1a

    mov al, ch
    call datetime_bcd2bin
    mov [bp - 8], ax                    ; century (e.g. 20)

    mov al, cl
    call datetime_bcd2bin
    mov [bp - 10], ax                   ; year of century (0-99)

    mov al, dh
    call datetime_bcd2bin
    mov [bp - 12], ax                   ; month

    mov al, dl
    call datetime_bcd2bin
    mov [bp - 14], ax                   ; day

    ; ---- pack FAT date ----
    mov ax, [bp - 8]                    ; century
    mov bx, 100
    mul bx                              ; dx:ax = century * 100 (fits in ax)
    add ax, [bp - 10]                   ; + year of century = full year
    sub ax, 1980                        ; year - 1980
    and ax, 0x007F
    mov cx, ax
    shl cx, 9                           ; bits 15-9

    mov ax, [bp - 12]                   ; month
    and ax, 0x000F
    mov bx, ax
    shl bx, 5                           ; bits 8-5
    or cx, bx

    mov ax, [bp - 14]                   ; day
    and ax, 0x001F                      ; bits 4-0
    or cx, ax

    mov bx, [bp + 4]                    ; uint16_t *fat_date
    mov [bx], cx

    ; ---- pack FAT time ----
    mov ax, [bp - 2]                    ; hour
    and ax, 0x001F
    mov cx, ax
    shl cx, 11                          ; bits 15-11

    mov ax, [bp - 4]                    ; minute
    and ax, 0x003F
    mov bx, ax
    shl bx, 5                           ; bits 10-5
    or cx, bx

    mov ax, [bp - 6]                    ; second
    shr ax, 1                           ; /2
    and ax, 0x001F                      ; bits 4-0
    or cx, ax

    mov bx, [bp + 6]                    ; uint16_t *fat_time
    mov [bx], cx

    pop si
    pop dx
    pop cx
    pop bx
    mov sp, bp
    pop bp
    ret

; Converts a packed-BCD byte in AL (0x00-0x99) to binary, returned
; zero-extended in AX. Destroys AX and BX only; not a global symbol,
; used only by x86_get_datetime above.
datetime_bcd2bin:
    mov bl, al
    and bl, 0x0F                        ; bl = ones digit
    mov bh, al
    shr bh, 4                           ; bh = tens digit
    mov al, bh
    xor ah, ah
    mov bh, 10
    mul bh                              ; ax = tens * 10 (fits in al, ah = 0)
    add al, bl
    ret

;
; 32-bit arithmetic helpers. Open Watcom emits calls to these for long
; multiply/divide in 16-bit code; they normally come from its C library,
; which the kernel doesn't link. Operands use 386 32-bit registers.
;
; Convention: first operand in dx:ax, second operand in cx:bx.
; Multiply returns the product in dx:ax. Divide returns the quotient in
; dx:ax and the remainder in cx:bx.
;

global __U4M
global __I4M
__U4M:
__I4M:                                  ; low 32 bits are the same for signed
    shl edx, 16
    mov dx, ax
    mov eax, edx                        ; eax = first operand

    shl ecx, 16
    mov cx, bx                          ; ecx = second operand

    mul ecx                             ; edx:eax = product

    mov edx, eax
    shr edx, 16                         ; dx:ax = low 32 bits of product
    ret

global __U4D
__U4D:
    shl edx, 16
    mov dx, ax
    mov eax, edx                        ; eax = dividend
    xor edx, edx

    shl ecx, 16
    mov cx, bx                          ; ecx = divisor

    div ecx                             ; eax = quotient, edx = remainder

    mov ebx, edx
    mov ecx, edx
    shr ecx, 16                         ; cx:bx = remainder

    mov edx, eax
    shr edx, 16                         ; dx:ax = quotient
    ret

global __I4D
__I4D:
    shl edx, 16
    mov dx, ax
    mov eax, edx                        ; eax = dividend
    cdq                                 ; sign-extend into edx

    shl ecx, 16
    mov cx, bx                          ; ecx = divisor

    idiv ecx                            ; eax = quotient, edx = remainder

    mov ebx, edx
    mov ecx, edx
    shr ecx, 16                         ; cx:bx = remainder

    mov edx, eax
    shr edx, 16                         ; dx:ax = quotient
    ret
