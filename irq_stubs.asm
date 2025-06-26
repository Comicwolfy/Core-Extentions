; irq_stubs.asm

section .text

; Common handler for IRQs
%macro IRQ_COMMON 1
    cli             ; Clear interrupts
    pusha           ; Push all general-purpose registers
    push ds
    push es
    push fs
    push gs

    ; Set up kernel data segment (assuming 0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Push interrupt number onto stack
    push %1
    call generic_isr_handler ; Call the C handler for generic interrupts
    add esp, 4              ; Pop interrupt number

    pop gs
    pop fs
    pop es
    pop ds
    popa            ; Pop all general-purpose registers
    sti             ; Set interrupts
    iret            ; Return from interrupt
%endmacro

; Specific IRQ handlers (you'll need more for other IRQs)
global irq0
irq0:
    IRQ_COMMON 0x20
    ; Specific EOI for IRQ0 if needed, but the C handler usually does it.
    ; This EOI is for the Master PIC (offset 0x20)
    ; mov al, 0x20
    ; out 0x20, al

global irq1
irq1:
    IRQ_COMMON 0x21
    call keyboard_handler_c ; Call the specific keyboard handler
    ; This EOI is for the Master PIC (offset 0x20)
    ; mov al, 0x20
    ; out 0x20, al


; Generic ISRs for exceptions (for testing, these just print the interrupt number)
; You'd need to define more of these for all 32 Intel reserved exceptions
global isr0
isr0:
    cli
    push byte 0     ; Dummy error code
    push byte 0     ; Interrupt number
    jmp common_isr_stub

global isr1
isr1:
    cli
    push byte 0     ; Dummy error code
    push byte 1     ; Interrupt number
    jmp common_isr_stub

; Common stub for general ISRs/Exceptions
common_isr_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10 ; Load the kernel data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; The interrupt number is already on the stack
    call generic_isr_handler
    add esp, 8      ; Pop interrupt number and dummy error code/padding

    pop gs
    pop fs
    pop es
    pop ds
    popa
    sti
    iret
