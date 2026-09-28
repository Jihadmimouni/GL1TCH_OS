bits 16

section _TEXT class=CODE

;
; Loads KERNEL.BIN from the FAT12 root directory into memory at
; KERNEL_LOAD_SEGMENT:KERNEL_LOAD_OFFSET and jumps to it.
;
; This ports the FAT loader already proven in stage1.asm (which loads
; STAGE2.BIN) to stage2, adjusted for the fixed FAT12 layout this project's
; build always produces (512 bytes/sector, 2 FATs, 9 sectors/fat, 224 root
; entries): root directory LBA = reserved(1) + fats(2)*sectors_per_fat(9)
; = 19, root directory size = ceil(224*32/512) = 14 sectors, so the data
; area starts at sector 1 + 18 + 14 = 33.
;
; Params:
;   - dl: boot drive number
;
global load_kernel
load_kernel:
    mov [drive_number], dl

    ; read drive parameters (sectors per track and head count), instead of
    ; relying on hardcoded/possibly-wrong geometry
    push es
    mov ah, 08h
    int 13h
    jc disk_error
    pop es

    and cl, 0x3F                        ; remove top 2 bits
    xor ch, ch
    mov [sectors_per_track], cx

    inc dh
    mov [heads], dh

    ; read root directory (LBA 19, 14 sectors)
    mov ax, 19
    mov cl, 14
    mov dl, [drive_number]
    mov bx, buffer
    call disk_read

    ; search for KERNEL.BIN
    xor bx, bx
    mov di, buffer

.search_kernel:
    mov si, file_kernel_bin
    mov cx, 11                          ; compare up to 11 characters
    push di
    repe cmpsb
    pop di
    je .found_kernel

    add di, 32
    inc bx
    cmp bx, 224                         ; root directory entry count
    jl .search_kernel

    jmp kernel_not_found_error

.found_kernel:
    ; di should have the address to the entry
    mov ax, [di + 26]                   ; first logical cluster field (offset 26)
    mov [kernel_cluster], ax

    ; load FAT from disk into memory (LBA 1, 9 sectors)
    mov ax, 1
    mov bx, buffer
    mov cl, 9
    mov dl, [drive_number]
    call disk_read

    ; read kernel and process FAT chain
    mov bx, KERNEL_LOAD_SEGMENT
    mov es, bx
    mov bx, KERNEL_LOAD_OFFSET

.load_kernel_loop:

    ; read next cluster
    mov ax, [kernel_cluster]
    add ax, 31                          ; cluster -> LBA: (cluster - 2) + first_data_sector(33)

    mov cl, 1
    mov dl, [drive_number]
    call disk_read

    add bx, 512

    ; compute location of next cluster
    mov ax, [kernel_cluster]
    mov cx, 3
    mul cx
    mov cx, 2
    div cx                              ; ax = index of entry in FAT, dx = cluster mod 2

    mov si, buffer
    add si, ax
    mov ax, [ds:si]                     ; read entry from FAT table at index ax

    or dx, dx
    jz .even

.odd:
    shr ax, 4
    jmp .next_cluster_after

.even:
    and ax, 0x0FFF

.next_cluster_after:
    cmp ax, 0x0FF8                      ; end of chain
    jae .jump_to_kernel

    mov [kernel_cluster], ax
    jmp .load_kernel_loop

.jump_to_kernel:

    ; hand off control to the kernel. Set ds/es to the kernel's own segment
    ; (like stage1 does for stage2) so its org-0 data references resolve
    ; correctly.
    mov dl, [drive_number]              ; leave boot drive in dl for the kernel

    mov ax, KERNEL_LOAD_SEGMENT
    mov ds, ax
    mov es, ax

    jmp KERNEL_LOAD_SEGMENT:KERNEL_LOAD_OFFSET

    ; should never reach here
    jmp wait_key_and_reboot


;
; Error handlers
;

disk_error:
    mov si, msg_read_failed
    call puts
    jmp wait_key_and_reboot

kernel_not_found_error:
    mov si, msg_kernel_not_found
    call puts
    jmp wait_key_and_reboot

wait_key_and_reboot:
    mov ah, 0
    int 16h                             ; wait for keypress
    jmp 0FFFFh:0                        ; jump to beginning of BIOS, should reboot

    cli                                 ; should never reach here
    hlt


;
; Prints a string to the screen
; Params:
;   - ds:si points to string
;
puts:
    push si
    push ax
    push bx

.loop:
    lodsb
    or al, al
    jz .done

    mov ah, 0x0E
    mov bh, 0
    int 0x10

    jmp .loop

.done:
    pop bx
    pop ax
    pop si
    ret


;
; Disk routines (ported from stage1.asm)
;

;
; Converts an LBA address to a CHS address
; Parameters:
;   - ax: LBA address
; Returns:
;   - cx [bits 0-5]: sector number
;   - cx [bits 6-15]: cylinder
;   - dh: head
;
lba_to_chs:
    push ax
    push dx

    xor dx, dx
    div word [sectors_per_track]        ; ax = LBA / SectorsPerTrack
                                         ; dx = LBA % SectorsPerTrack

    inc dx                              ; dx = sector
    mov cx, dx

    xor dx, dx
    div word [heads]                    ; ax = cylinder, dx = head
    mov dh, dl
    mov ch, al
    shl ah, 6
    or cl, ah

    pop ax
    mov dl, al
    pop ax
    ret

;
; Reads sectors from a disk
; Parameters:
;   - ax: LBA address
;   - cl: number of sectors to read (up to 128)
;   - dl: drive number
;   - es:bx: memory address where to store read data
;
disk_read:
    push ax
    push bx
    push cx
    push dx
    push di

    push cx                             ; temporarily save CL (number of sectors to read)
    call lba_to_chs
    pop ax                              ; al = number of sectors to read

    mov ah, 02h
    mov di, 3                           ; retry count

.retry:
    pusha
    stc
    int 13h
    jnc .done

    popa
    call disk_reset

    dec di
    test di, di
    jnz .retry

.fail:
    jmp disk_error

.done:
    popa

    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

;
; Resets disk controller
; Parameters:
;   dl: drive number
;
disk_reset:
    pusha
    mov ah, 0
    stc
    int 13h
    jc disk_error
    popa
    ret


msg_read_failed:        db 'Read from disk failed!', 0x0D, 0x0A, 0
msg_kernel_not_found:   db 'KERNEL.BIN file not found!', 0x0D, 0x0A, 0
file_kernel_bin:        db 'KERNEL  BIN'
kernel_cluster:         dw 0
drive_number:            db 0
sectors_per_track:      dw 0
heads:                   dw 0

KERNEL_LOAD_SEGMENT     equ 0x4000
KERNEL_LOAD_OFFSET      equ 0

; scratch buffer for root directory / FAT reads (14 sectors = 7168 bytes
; needed; reserved explicitly so it can never overlap code from another
; object file linked into this same segment)
buffer:                 resb 8192
