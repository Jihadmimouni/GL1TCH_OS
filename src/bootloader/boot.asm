org 0x7c00
bits 16

%define ENDL 0x0D,0x0A     ;end of line

start:
    
    jmp main


;
; print string
; parms:
;       ds:si -> string
puts:
    ;save registers we will modify  
    push si 
    push ax
.loop:
    lodsb ;load next byte from ds:si into al
    or al, al ;is it zero?
    jz .done ;if so, we are done
    mov ah, 0x0e ;otherwise, print it
    mov bh, 0x00 ;page number
    int 0x10
    jmp .loop ;and repeat
.done:
    pop ax
    pop si
    ret


main:
    ; setup data segment
    mov ax, 0 ;can't write to ds and es directly
    mov ds, ax
    mov es, ax

    ; setup stack
    mov ss, ax
    mov sp, 0x7c00 ;stack grows down

    ; print message
    mov si, msg_hello
    call puts
    
    hlt

.halt:
    jmp .halt

msg_hello: db 'Hello, World!',ENDL, 0


times 510 - ($ - $$) db 0
dw 0AA55h


