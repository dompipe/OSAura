.section .text

.global osaura_arch_load_gdt
.type osaura_arch_load_gdt,@function
osaura_arch_load_gdt:
    subq $16, %rsp
    movw $23, 0(%rsp)
    leaq osaura_gdt(%rip), %rax
    movq %rax, 2(%rsp)
    lgdt 0(%rsp)
    addq $16, %rsp

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    ret

.global osaura_arch_load_idt
.type osaura_arch_load_idt,@function
osaura_arch_load_idt:
    subq $16, %rsp
    movw %si, 0(%rsp)
    movq %rdi, 2(%rsp)
    lidt 0(%rsp)
    addq $16, %rsp
    ret

.global osaura_arch_enable_interrupts
.type osaura_arch_enable_interrupts,@function
osaura_arch_enable_interrupts:
    sti
    ret

.global osaura_arch_disable_interrupts
.type osaura_arch_disable_interrupts,@function
osaura_arch_disable_interrupts:
    cli
    ret

.global osaura_isr_anchor
osaura_isr_anchor:

.macro ISR_NOERR n
.global osaura_isr_\n
.type osaura_isr_\n,@function
osaura_isr_\n:
    pushq $0
    pushq $\n
    jmp osaura_isr_common
.endm

.macro ISR_ERR n
.global osaura_isr_\n
.type osaura_isr_\n,@function
osaura_isr_\n:
    pushq $\n
    jmp osaura_isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

# IRQ0 is deliberately kept as a minimal assembly path. It proves the
# descriptor/PIC/iret path independently of the C interrupt ABI and gives the
# scheduler clock a very small hot path.
.global osaura_isr_32
.type osaura_isr_32,@function
osaura_isr_32:
    pushq %rax
    incq osaura_ticks(%rip)
    movb $0x20, %al
    outb %al, $0x20
    popq %rax
    iretq

ISR_NOERR 33
ISR_NOERR 34
ISR_NOERR 35
ISR_NOERR 36
ISR_NOERR 37
ISR_NOERR 38
ISR_NOERR 39
ISR_NOERR 40
ISR_NOERR 41
ISR_NOERR 42
ISR_NOERR 43
ISR_NOERR 44
ISR_NOERR 45
ISR_NOERR 46
ISR_NOERR 47

.type osaura_isr_common,@function
osaura_isr_common:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq 120(%rsp), %rdi
    movq 128(%rsp), %rsi
    movq %rsp, %r12
    andq $-16, %rsp
    call osaura_interrupt_dispatch
    movq %r12, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq

.section .rodata
.align 8
osaura_gdt:
    .quad 0x0000000000000000
    .quad 0x00af9a000000ffff
    .quad 0x00cf92000000ffff

# These are signed offsets from osaura_isr_anchor, not absolute pointers.
# That keeps the table valid after the PE/COFF image is relocated by UEFI.
.align 4
.global osaura_isr_offsets
osaura_isr_offsets:
    .long osaura_isr_0 - osaura_isr_anchor
    .long osaura_isr_1 - osaura_isr_anchor
    .long osaura_isr_2 - osaura_isr_anchor
    .long osaura_isr_3 - osaura_isr_anchor
    .long osaura_isr_4 - osaura_isr_anchor
    .long osaura_isr_5 - osaura_isr_anchor
    .long osaura_isr_6 - osaura_isr_anchor
    .long osaura_isr_7 - osaura_isr_anchor
    .long osaura_isr_8 - osaura_isr_anchor
    .long osaura_isr_9 - osaura_isr_anchor
    .long osaura_isr_10 - osaura_isr_anchor
    .long osaura_isr_11 - osaura_isr_anchor
    .long osaura_isr_12 - osaura_isr_anchor
    .long osaura_isr_13 - osaura_isr_anchor
    .long osaura_isr_14 - osaura_isr_anchor
    .long osaura_isr_15 - osaura_isr_anchor
    .long osaura_isr_16 - osaura_isr_anchor
    .long osaura_isr_17 - osaura_isr_anchor
    .long osaura_isr_18 - osaura_isr_anchor
    .long osaura_isr_19 - osaura_isr_anchor
    .long osaura_isr_20 - osaura_isr_anchor
    .long osaura_isr_21 - osaura_isr_anchor
    .long osaura_isr_22 - osaura_isr_anchor
    .long osaura_isr_23 - osaura_isr_anchor
    .long osaura_isr_24 - osaura_isr_anchor
    .long osaura_isr_25 - osaura_isr_anchor
    .long osaura_isr_26 - osaura_isr_anchor
    .long osaura_isr_27 - osaura_isr_anchor
    .long osaura_isr_28 - osaura_isr_anchor
    .long osaura_isr_29 - osaura_isr_anchor
    .long osaura_isr_30 - osaura_isr_anchor
    .long osaura_isr_31 - osaura_isr_anchor
    .long osaura_isr_32 - osaura_isr_anchor
    .long osaura_isr_33 - osaura_isr_anchor
    .long osaura_isr_34 - osaura_isr_anchor
    .long osaura_isr_35 - osaura_isr_anchor
    .long osaura_isr_36 - osaura_isr_anchor
    .long osaura_isr_37 - osaura_isr_anchor
    .long osaura_isr_38 - osaura_isr_anchor
    .long osaura_isr_39 - osaura_isr_anchor
    .long osaura_isr_40 - osaura_isr_anchor
    .long osaura_isr_41 - osaura_isr_anchor
    .long osaura_isr_42 - osaura_isr_anchor
    .long osaura_isr_43 - osaura_isr_anchor
    .long osaura_isr_44 - osaura_isr_anchor
    .long osaura_isr_45 - osaura_isr_anchor
    .long osaura_isr_46 - osaura_isr_anchor
    .long osaura_isr_47 - osaura_isr_anchor

.section .note.GNU-stack,"",@progbits
