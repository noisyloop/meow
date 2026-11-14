; usb/boot.asm — meow-usb boot stage (legacy BIOS, USB stick / raw disk)
;
; The first 2048 bytes of build/meow-usb.img, laid out to mirror the CD
; build so the *same* kernel link (0x8400) is reused untouched:
;
;   sector 0      0x7C00  MBR: BPB + loader + partition table + 0x55AA
;   sectors 1-3   0x7E00  stage 2: A20, GDT, protected mode
;   sectors 4..   0x8400  kernel (flat binary)
;
; The BIOS loads only sector 0. It pulls sectors 1..N to 0x7E00 one
; sector at a time (so no read can ever straddle a 64 KB boundary or a
; track), using INT 13h extensions when the BIOS has them and plain CHS
; otherwise, then jumps into stage 2.
;
; Progress on screen (INT 10h teletype), one character per stage:
;   M  boot sector is running
;   L  disk mode: INT 13h extensions (LBA)     \ one of
;   C  disk mode: CHS with BIOS geometry       / these two
;   K  stage 2 + kernel loaded
;   A  A20 line is on
;   P  entering protected mode (kernel clears the screen right after)
; Errors print '!' and a letter, then halt:
;   !G  BIOS gave no usable geometry and has no LBA extensions
;   !D  disk read failed (3 tries, with resets, in every available mode)
;   !S  stage 2 signature missing (image truncated or wrongly flashed)
;   !A  could not enable the A20 line

BITS 16
ORG 0x7C00

%ifndef KERNEL_SECTORS
%error "KERNEL_SECTORS must be defined (-DKERNEL_SECTORS=n)"
%endif
%ifndef IMAGE_SECTORS
%define IMAGE_SECTORS 2048              ; 1 MiB
%endif

STAGE_SECTORS equ 4                     ; this file: 4 * 512 = 2048 bytes
STAGE2_SEG    equ 0x07E0                ; sector 1 lands at 0x7E00
KERNEL_ADDR   equ 0x8400                ; 0x7C00 + 2048, same as the CD build
LOAD_SECTORS  equ STAGE_SECTORS - 1 + KERNEL_SECTORS
STAGE2_MAGIC  equ 0x774D                ; "Mw"

; everything we load must stay in conventional memory below the stack
%if 0x7E00 + LOAD_SECTORS * 512 > 0x80000
%error "kernel too big for the meow-usb loader"
%endif
%if STAGE_SECTORS + KERNEL_SECTORS > IMAGE_SECTORS
%error "kernel does not fit in the image"
%endif

; partition-table CHS triple for an LBA, using the usual 255/63 translation
%macro CHS 1
    db ((%1) / 63) % 255
    db ((((%1) / (63 * 255)) >> 8) << 6) | ((%1) % 63 + 1)
    db ((%1) / (63 * 255)) & 0xFF
%endmacro

; =====================================================================
; sector 0
; =====================================================================
start:
    jmp short begin
    nop

; --- FAT-style BIOS parameter block --------------------------------
; Not a real filesystem. It is here because BIOSes that present a USB
; stick as a floppy or ZIP drive expect one, and some of them write
; into these bytes before jumping to us — so no code may live here.
            db "MEOWUSB "               ; 0x03 OEM name
            dw 512                      ; 0x0B bytes per sector
            db 1                        ; 0x0D sectors per cluster
            dw IMAGE_SECTORS - 8        ; 0x0E reserved sectors (boot + kernel)
            db 2                        ; 0x10 FATs
            dw 16                       ; 0x11 root entries
            dw IMAGE_SECTORS            ; 0x13 total sectors
            db 0xF8                     ; 0x15 media descriptor
            dw 1                        ; 0x16 sectors per FAT
            dw 63                       ; 0x18 sectors per track
            dw 255                      ; 0x1A heads
            dd 0                        ; 0x1C hidden sectors
            dd 0                        ; 0x20 total sectors (32-bit)
            db 0x80                     ; 0x24 drive number
            db 0                        ; 0x25 reserved
            db 0x29                     ; 0x26 extended boot signature
            dd 0x3E5E5E3D               ; 0x27 volume id
            db "MEOW USB   "            ; 0x2B volume label
            db "FAT12   "               ; 0x36 filesystem type
%if ($-$$) != 0x3E
%error "BPB must end at offset 0x3E"
%endif

begin:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    jmp 0x0000:.flat                    ; some BIOSes enter at 07C0:0000
.flat:
    mov [drive], dl                     ; the BIOS boot drive; used for every read

    mov al, 'M'
    call putc

    ; --- geometry for CHS (AH=08h). Not fatal yet: LBA may still work.
    mov ah, 0x08
    mov dl, [drive]
    xor di, di                          ; ES:DI = 0:0 guards against BIOS bugs
    int 0x13
    push ds
    pop es                              ; AH=08h may have changed ES
    sti
    jc .no_geom
    and cx, 0x003F                      ; sectors per track
    jz .no_geom
    mov [spt], cx
    mov dl, dh
    xor dh, dh
    inc dx                              ; heads = max head + 1
    mov [heads], dx
.no_geom:

    ; --- INT 13h extensions? (AH=41h)
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [drive]
    int 0x13
    sti
    jc .no_lba
    cmp bx, 0xAA55
    jne .no_lba
    test cl, 1                          ; AH=42h supported
    jz .no_lba
    mov byte [use_lba], 1
    mov al, 'L'
    jmp short .mode
.no_lba:
    cmp word [spt], 0
    je err_geom
    mov al, 'C'
.mode:
    call putc

    ; --- load sectors 1..LOAD_SECTORS to 07E0:0000, one at a time
.next:
    call read_sector
    inc word [dap_lba]
    add word [dap_seg], 512 >> 4
    dec word [remaining]
    jnz .next

    cmp word [stage2_magic], STAGE2_MAGIC
    jne err_sig
    mov al, 'K'
    call putc
    jmp stage2

; --- read one sector: LBA [dap_lba] -> [dap_seg]:0000 -----------------
read_sector:
    cmp byte [use_lba], 0
    je .chs

    mov byte [tries], 3
.lba_try:
    mov si, dap
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    sti
    jnc .ok
    call disk_reset
    dec byte [tries]
    jnz .lba_try
    mov byte [use_lba], 0               ; extensions lied; fall back to CHS

.chs:
    cmp word [spt], 0
    je err_disk
    mov byte [tries], 3
.chs_try:
    mov ax, [dap_lba]
    xor dx, dx
    div word [spt]                      ; ax = lba / spt, dx = sector - 1
    mov cl, dl
    inc cl                              ; CL = sector (1-based)
    xor dx, dx
    div word [heads]                    ; ax = cylinder, dx = head
    mov ch, al                          ; CH = cylinder low 8 bits
    shl ah, 6
    or  cl, ah                          ; CL bits 6-7 = cylinder high bits
    mov dh, dl                          ; DH = head
    mov dl, [drive]
    les bx, [dap_off]                   ; ES:BX = buffer
    mov ax, 0x0201                      ; read 1 sector
    int 0x13
    push ds
    pop es
    sti
    jnc .ok
    call disk_reset
    dec byte [tries]
    jnz .chs_try
    jmp short err_disk
.ok:
    ret

disk_reset:
    xor ah, ah
    mov dl, [drive]
    int 0x13
    sti
    ret

; --- errors: "!<letter>" then halt ------------------------------------
err_geom:
    mov al, 'G'
    jmp short fail
err_sig:
    mov al, 'S'
    jmp short fail
err_disk:
    mov al, 'D'
fail:
    push ax
    mov al, '!'
    call putc
    pop ax
    call putc
.halt:
    hlt
    jmp short .halt

; --- print AL via BIOS teletype ---------------------------------------
putc:
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    ret

; --- data ---------------------------------------------------------------
drive:     db 0
use_lba:   db 0
tries:     db 0
spt:       dw 0
heads:     dw 0
remaining: dw LOAD_SECTORS

align 4
dap:
    db 0x10, 0                          ; size, reserved
    dw 1                                ; sectors per read
dap_off:
    dw 0                                ; buffer offset
dap_seg:
    dw STAGE2_SEG                       ; buffer segment
dap_lba:
    dd 1                                ; next LBA (low)
    dd 0                                ; (high)

; --- disk signature + partition table ---------------------------------
times 0x1B8-($-$$) db 0
    dd 0x574F454D                       ; 0x1B8 disk signature "MEOW"
    dw 0                                ; 0x1BC
    ; 0x1BE: one active entry covering everything after the MBR. Some
    ; BIOSes refuse to boot a USB disk without an active partition.
    db 0x80                             ; active
    CHS 1                               ; first sector
    db 0xDA                             ; type: non-filesystem data
    CHS IMAGE_SECTORS - 1               ; last sector
    dd 1                                ; first LBA
    dd IMAGE_SECTORS - 1                ; sector count
    times 3 * 16 db 0                   ; entries 2-4 unused
    dw 0xAA55                           ; 0x1FE boot signature (bytes 55 AA)

; =====================================================================
; sectors 1-3: stage 2, loaded at 0x7E00
; =====================================================================
stage2_magic:
    dw STAGE2_MAGIC

stage2:
    call a20_enable
    mov al, 'A'
    jc fail                             ; "!A"
    call putc

    mov dx, 0x3F2                       ; floppy-emulated boots: motor off
    xor al, al
    out dx, al

    mov al, 'P'
    call putc

    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 0x08:pm_start

; --- A20: check, BIOS, keyboard controller, port 0x92 -------------------
; Each method is verified before the next (more invasive) one is tried.
; Returns CF=0 if A20 is on, CF=1 if nothing worked.
a20_enable:
    call a20_check                      ; already on? (most BIOSes)
    jnz .on

    mov ax, 0x2401                      ; 1) ask the BIOS
    int 0x15
    sti
    call a20_poll
    jnz .on

    in  al, 0x64                        ; 2) keyboard controller
    cmp al, 0xFF                        ;    0xFF = no controller there
    je  .fast
    call kbc_wait
    mov al, 0xD1                        ;    write output port
    out 0x64, al
    call kbc_wait
    mov al, 0xDF                        ;    A20 on, reset line high
    out 0x60, al
    call kbc_wait
    mov al, 0xFF                        ;    null command (settles some chipsets)
    out 0x64, al
    call kbc_wait
    call a20_poll
    jnz .on

.fast:
    in  al, 0x92                        ; 3) "fast A20" system control port
    or  al, 2
    and al, 0xFE                        ;    never touch the reset bit
    out 0x92, al
    call a20_poll
    jnz .on

    stc
    ret
.on:
    clc
    ret

; wait (bounded) for the keyboard controller input buffer to drain
kbc_wait:
    xor cx, cx
.loop:
    out 0x80, al                        ; ~1 us I/O delay
    in  al, 0x64
    test al, 2
    jz  .done
    loop .loop
.done:
    ret

; a20_check repeated for a while: the keyboard controller is slow
a20_poll:
    mov cx, 2000
.loop:
    call a20_check
    jnz .done
    out 0x80, al
    loop .loop
    xor ax, ax                          ; ZF=1: still off
.done:
    ret

; ZF=0 if A20 is on, ZF=1 if 0x100500 still aliases 0x000500
a20_check:
    push ds
    push es
    push si
    push di
    pushf
    cli
    xor ax, ax
    mov ds, ax
    mov si, 0x0500                      ; 0000:0500 = 0x000500
    dec ax
    mov es, ax
    mov di, 0x0510                      ; FFFF:0510 = 0x100500
    mov al, [si]
    push ax
    mov al, [es:di]
    push ax
    mov byte [si], 0x00
    mov byte [es:di], 0xFF
    out 0x80, al
    cmp byte [si], 0xFF                 ; equal => wrapped => A20 off
    pop ax
    mov [es:di], al
    pop ax
    mov [si], al
    mov ax, 0
    je  .off
    inc ax
.off:
    popf
    pop di
    pop si
    pop es
    pop ds
    test ax, ax
    ret

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

BITS 16
align 8
gdt:
    dq 0                                ; null
    dq 0x00CF9A000000FFFF               ; 0x08: code, base 0, limit 4G, 32-bit
    dq 0x00CF92000000FFFF               ; 0x10: data, base 0, limit 4G, 32-bit
gdt_desc:
    dw gdt_desc - gdt - 1
    dd gdt

times STAGE_SECTORS*512-($-$$) db 0
