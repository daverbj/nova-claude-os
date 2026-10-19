/* Nova Claude OS boot stub: Multiboot1 header (requests 1024x768x32 linear framebuffer),
   GDT, FPU init, interrupt entry stubs. */
.ifndef MB_W
.set MB_W, 1024
.set MB_H, 768
.endif
.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x00000007
.set MB_CHECK, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECK
.long 0, 0, 0, 0, 0
.long 0          /* linear graphics mode */
.long MB_W
.long MB_H
.long 32

.section .text
.global _start
_start:
    cli
    mov $stack_top, %esp
    mov %eax, %esi          /* magic */
    lgdt gdt_ptr
    ljmp $0x08, $1f
1:  mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov %cr0, %eax
    and $0xFFFFFFF3, %eax    /* clear EM, TS */
    or  $0x22, %eax          /* MP, NE */
    mov %eax, %cr0
    fninit
    push %ebx                /* multiboot info */
    push %esi                /* magic */
    call kmain
2:  cli
    hlt
    jmp 2b

.global idt_load
idt_load:
    mov 4(%esp), %eax
    lidt (%eax)
    ret

/* ---- interrupt stubs ---- */
.macro ISR_NOERR n
isr\n:
    push $0
    push $\n
    jmp isr_common
.endm
.macro ISR_ERR n
isr\n:
    push $\n
    jmp isr_common
.endm

.irp n,0,1,2,3,4,5,6,7,9,15,16,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    ISR_NOERR \n
.endr
.irp n,8,10,11,12,13,14,17
    ISR_ERR \n
.endr
.irp n,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
    ISR_NOERR \n
.endr

isr_common:
    pusha
    push %esp
    call isr_handler
    add $4, %esp
    popa
    add $8, %esp
    iret

.section .data
.align 8
gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt_ptr:
    .word gdt_ptr - gdt - 1
    .long gdt

.global isr_table
isr_table:
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
    .long isr\n
.endr

.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:
