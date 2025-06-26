#include <stdint.h>
#include <stddef.h> // For size_t
#include "base_kernel.h" // Assuming this header contains terminal, kmalloc, and extension APIs

// Define necessary I/O port functions (assuming these will be provided by your kernel's core or a low-level module)
// For x86:
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "dN"(port) );
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ( "inb %1, %0" : "=a"(ret) : "dN"(port) );
    return ret;
}

// Global Extension ID
static int irq_kb_ext_id = -1;

// IDT Entry Structure
struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t always0;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed));

// IDT Pointer Structure
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256]; // Array of IDT entries
static struct idt_ptr idt_p;      // Pointer to the IDT

// Keyboard buffer
#define KB_BUFFER_SIZE 256
static char keyboard_buffer[KB_BUFFER_SIZE];
static volatile size_t kb_buffer_head = 0;
static volatile size_t kb_buffer_tail = 0;

// Scan code to ASCII conversion (simplified US QWERTY)
// This is a minimal set. A full keyboard driver would be much larger.
static const unsigned char kbd_us[128] =
{
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',   // 0-14
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',     // 15-28
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',             // 29-41
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,               // 42-54
    '*',                                                                        // 55
    0,                                                                          // 56 (Alt)
    ' ',                                                                        // 57 (Spacebar)
    0,                                                                          // 58 (Caps Lock)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                               // 59-68 (F1-F10)
    0,                                                                          // 69 (Num Lock)
    0,                                                                          // 70 (Scroll Lock)
    0,                                                                          // 71 (Home)
    0,                                                                          // 72 (Up Arrow)
    0,                                                                          // 73 (Page Up)
    0,                                                                          // 74
    0,                                                                          // 75 (Left Arrow)
    0,                                                                          // 76
    0,                                                                          // 77 (Right Arrow)
    0,                                                                          // 78
    0,                                                                          // 79 (End)
    0,                                                                          // 80 (Down Arrow)
    0,                                                                          // 81 (Page Down)
    0,                                                                          // 82 (Insert)
    0,                                                                          // 83 (Delete)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0                      // 84-127 (remaining unused)
};

// Extern declarations for assembly interrupt stubs
extern void isr0();
extern void isr1();
// ... you'd need to declare all 256 ISRs if you implement them individually
// For simplicity, we'll focus on IRQ1 (keyboard) and a generic handler.
extern void irq0(); // Timer
extern void irq1(); // Keyboard

// Generic Interrupt Service Routine (ISR) handler
void generic_isr_handler(int int_no) {
    terminal_writestring("Interrupt: ");
    char num_str[10]; // Buffer for number to string conversion
    // Simple integer to string conversion (for demonstration)
    int i = 0;
    if (int_no == 0) {
        num_str[0] = '0';
        i = 1;
    } else {
        int temp = int_no;
        while (temp > 0) {
            num_str[i++] = (temp % 10) + '0';
            temp /= 10;
        }
    }
    num_str[i] = '\0';

    // Reverse the string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = num_str[start];
        num_str[start] = num_str[end];
        num_str[end] = temp;
        start++;
        end--;
    }

    terminal_writestring(num_str);
    terminal_writestring("\n");
}

// Keyboard Interrupt Handler
void keyboard_handler_c() {
    uint8_t scancode = inb(0x60); // Read scan code from keyboard data port

    if (scancode & 0x80) {
        // Key released, ignore for now
    } else {
        char ascii = kbd_us[scancode];
        if (ascii != 0) {
            // Add to keyboard buffer
            size_t next_head = (kb_buffer_head + 1) % KB_BUFFER_SIZE;
            if (next_head != kb_buffer_tail) { // Check if buffer is full
                keyboard_buffer[kb_buffer_head] = ascii;
                kb_buffer_head = next_head;
            }
        }
    }

    // Acknowledge the interrupt to the PIC
    outb(0x20, 0x20); // EOI to Master PIC
}

// Function to read a character from the keyboard buffer (non-blocking)
char read_char_from_kb_buffer() {
    if (kb_buffer_head == kb_buffer_tail) {
        return 0; // Buffer empty
    }
    char c = keyboard_buffer[kb_buffer_tail];
    kb_buffer_tail = (kb_buffer_tail + 1) % KB_BUFFER_SIZE;
    return c;
}

// Function to wait for and read a character from the keyboard buffer (blocking)
char wait_for_char_from_kb_buffer() {
    while (kb_buffer_head == kb_buffer_tail) {
        // Wait (you might want to add a HLT instruction here in a real kernel loop)
        asm volatile("hlt");
    }
    char c = keyboard_buffer[kb_buffer_tail];
    kb_buffer_tail = (kb_buffer_tail + 1) % KB_BUFFER_SIZE;
    return c;
}


// IDT Setup
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

// PIC Remapping
void pic_remap(void) {
    outb(0x20, 0x11); // Start initialization sequence (ICW1)
    outb(0xA0, 0x11);
    outb(0x21, 0x20); // Master PIC vector offset (0x20 - 0x27)
    outb(0xA1, 0x28); // Slave PIC vector offset (0x28 - 0x2F)
    outb(0x21, 0x04); // Tell Master PIC that there is a slave PIC at IRQ2
    outb(0xA1, 0x02); // Tell Slave PIC its cascade identity (0x02)
    outb(0x21, 0x01); // 8086 mode
    outb(0xA1, 0x01);
    outb(0x21, 0x0); // Mask all interrupts on Master PIC
    outb(0xA1, 0x0); // Mask all interrupts on Slave PIC
}

// CLI Command Handler
void cmd_cli_input(const char* args) {
    terminal_writestring("Enter command (press Enter to execute, Ctrl+C to exit):\n");
    terminal_writestring("NOTE: Basic input, no backspace yet.\n");

    char input_buffer[VGA_WIDTH + 1]; // Max line length + null terminator
    size_t input_idx = 0;

    terminal_writestring("$ "); // Prompt

    while (1) {
        char c = wait_for_char_from_kb_buffer();

        if (c == '\n' || c == '\r') {
            input_buffer[input_idx] = '\0';
            terminal_putchar('\n');
            if (input_idx > 0) {
                // Here, you would call your kernel's process_command
                // For now, let's just echo and then exit this temporary loop
                terminal_writestring("You typed: ");
                terminal_writestring(input_buffer);
                terminal_writestring("\n");
                // In a real shell, you'd call process_command(input_buffer);
                // Then reset for next command. For this example, we'll exit.
            }
            break; // Exit the loop for this example
        } else if (c == '\b' || c == 0x7F) { // Handle backspace
            if (input_idx > 0) {
                input_idx--;
                terminal_putchar('\b'); // Move cursor back
                terminal_putchar(' ');  // Overwrite char
                terminal_putchar('\b'); // Move cursor back again
            }
        }
        else if (input_idx < VGA_WIDTH) { // Prevent buffer overflow on screen
            input_buffer[input_idx++] = c;
            terminal_putchar(c);
        }
    }
}

// Initialization function for IRQ & Keyboard Extension
int irq_kb_extension_init(void) {
    terminal_writestring("IRQ & Keyboard Extension: Initializing...\n");

    // Initialize IDT pointer and base address
    idt_p.limit = (sizeof(struct idt_entry) * 256) - 1;
    idt_p.base = (uint32_t)&idt;

    // Zero out the IDT
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0x08, 0x8E); // Default empty gate
    }

    // Set up interrupt handlers for IRQs
    // PIC remap makes IRQ0-7 map to 0x20-0x27, IRQ8-15 map to 0x28-0x2F
    // Kernel code segment is usually 0x08 for a flat model.
    idt_set_gate(0x20, (uint32_t)irq0, 0x08, 0x8E); // IRQ0 - Timer
    idt_set_gate(0x21, (uint32_t)irq1, 0x08, 0x8E); // IRQ1 - Keyboard

    // Load the IDT
    asm volatile("lidt %0" : : "m"(idt_p));

    // Remap the PIC
    pic_remap();

    // Mask all IRQs initially, then unmask keyboard (IRQ1) and timer (IRQ0)
    outb(0x21, 0xFC); // Mask all except IRQ0 (timer) and IRQ1 (keyboard) on Master PIC
    outb(0xA1, 0xFF); // Mask all on Slave PIC

    // Enable interrupts
    asm volatile("sti");

    terminal_writestring("IRQ & Keyboard Extension: IDT loaded, PIC remapped, Interrupts enabled.\n");
    terminal_writestring("IRQ & Keyboard Extension: Keyboard ready.\n");

    // Register a command to test keyboard input
    register_command("cli_test", cmd_cli_input, "Test basic keyboard input", irq_kb_ext_id);

    return 0; // Success
}

// Cleanup function for IRQ & Keyboard Extension
void irq_kb_extension_cleanup(void) {
    terminal_writestring("IRQ & Keyboard Extension: Cleaning up...\n");
    // Disable interrupts before unloading
    asm volatile("cli");
    // Potentially unmask PICs or reset IDT entries to safer states
    terminal_writestring("IRQ & Keyboard Extension: Cleanup complete.\n");
}

// Function to register this extension from the kernel's main entry point
void register_irq_kb_extension(void) {
    irq_kb_ext_id = register_extension("IRQ_KB", "1.0",
                                       irq_kb_extension_init,
                                       irq_kb_extension_cleanup);
    if (irq_kb_ext_id >= 0) {
        load_extension(irq_kb_ext_id);
    } else {
        terminal_writestring("Failed to register IRQ & Keyboard Extension!\n");
    }
}
