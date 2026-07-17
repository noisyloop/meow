; boot/boot.asm — meow os boot stage
;
; This is the first 2048 bytes of the El Torito no-emulation boot image.
; The BIOS loads exactly these 2048 bytes at 0000:7C00. The kernel binary
; is concatenated right after this stage in the boot image file, so we use
; the boot info table (patched in by xorriso -boot-info-table) to find our
; own file on the CD and pull the rest of it to 0x8400 with INT 13h
; extended reads. Then: A20, GDT, protected mode, jump to the kernel.

BITS 16
ORG 0x7C00

STAGE_SIZE   equ 2048          ; bytes the BIOS already loaded (boot-load-size 4)
KERNEL_ADDR  equ 0x8400        ; 0x7C00 + 2048

start:
    jmp short begin
    nop

; --- El Torito boot info table (offset 8, filled in by xorriso) ---
times 8-($-$$) db 0
bi_pvd:     dd 0               ; LBA of the primary volume descriptor
bi_file:    dd 0               ; LBA of this boot file (2048-byte sectors)
bi_length:  dd 0               ; length of this boot file in bytes
bi_csum:    dd 0
bi_reserved: times 40 db 0

begin:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [drive], dl            ; BIOS hands us the boot drive in DL

    mov si, msg_boot
    call print

    ; how many 2048-byte sectors past the first do we need?
    mov eax, [bi_length]
    add eax, 2047
    shr eax, 11                ; total sectors in boot file
    dec eax                    ; minus the one the BIOS loaded
    jz  enter_pm               ; kernel missing? just try anyway
    mov [remaining], eax

    mov eax, [bi_file]
    inc eax                    ; first sector we still need
    mov [next_lba], eax
    mov word [dap_seg], KERNEL_ADDR >> 4

.read_loop:
    mov eax, [remaining]
    test eax, eax
    jz  enter_pm
    cmp eax, 16                ; read at most 16 sectors (32 KiB) per call
    jbe .count_ok
    mov eax, 16
.count_ok:
    mov [chunk], eax
    mov [dap_count], ax
    mov eax, [next_lba]
    mov [dap_lba], eax

    mov si, dap
    mov dl, [drive]
    mov ah, 0x42               ; INT 13h extended read
    int 0x13
    jc  disk_error

    mov eax, [chunk]
    sub [remaining], eax
    add [next_lba], eax
    shl eax, 7                 ; sectors * 2048 / 16 = paragraphs
    add [dap_seg], ax
    jmp .read_loop

disk_error:
    mov si, msg_err
    call print
.halt:
    hlt
    jmp .halt

; --- print SI (zero-terminated) via BIOS teletype ---
print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print
.done:
    ret

enter_pm:
    ; enable A20: BIOS first, fast gate as fallback
    mov ax, 0x2401
    int 0x15
    in  al, 0x92
    or  al, 2
    and al, 0xFE
    out 0x92, al

    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 0x08:pm_start

BITS 32
pm_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    jmp KERNEL_ADDR

; --- data ---
BITS 16
drive:     db 0
remaining: dd 0
next_lba:  dd 0
chunk:     dd 0

align 4
dap:
    db 0x10, 0                 ; size, reserved
dap_count:
    dw 0                       ; sectors to read
dap_off:
    dw 0                       ; buffer offset
dap_seg:
    dw 0                       ; buffer segment
dap_lba:
    dd 0                       ; LBA low
    dd 0                       ; LBA high

msg_boot: db "meow?", 13, 10, 0
msg_err:  db "hiss! disk read failed", 13, 10, 0

align 8
gdt:
    dq 0                       ; null
    dq 0x00CF9A000000FFFF      ; 0x08: code, base 0, limit 4G, 32-bit
    dq 0x00CF92000000FFFF      ; 0x10: data, base 0, limit 4G, 32-bit
gdt_desc:
    dw gdt_desc - gdt - 1
    dd gdt

times STAGE_SIZE-($-$$) db 0
