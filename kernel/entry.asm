; kernel/entry.asm — 32-bit kernel entry, first bytes at 0x8400
BITS 32
section .entry
global _start
extern kmain
extern __bss_start
extern __bss_end

_start:
    mov esp, 0x90000
    cld
    mov edi, __bss_start        ; zero .bss — the loader only copies the file
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb
    call kmain
.hang:
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
