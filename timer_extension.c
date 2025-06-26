#include <stdint.h>
#include <stddef.h>
#include "base_kernel.h" // For terminal, kmalloc, and extension APIs

// Assumed I/O port functions
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "dN"(port) );
}

// Global Extension ID
static int timer_ext_id = -1;

// Global tick counter
static volatile uint64_t ticks = 0;

// PIT interrupt handler (called from assembly stub)
void timer_handler_c() {
    ticks++;
    // Acknowledge the interrupt to the Master PIC
    outb(0x20, 0x20);
}

// Command handler for 'uptime'
void cmd_uptime(const char* args) {
    terminal_writestring("System Uptime: ");
    char num_str[20]; // Buffer for number to string conversion
    uint64_t current_ticks = ticks;

    // Simple uint64_t to string conversion
    int i = 0;
    if (current_ticks == 0) {
        num_str[0] = '0';
        i = 1;
    } else {
        uint64_t temp = current_ticks;
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
    terminal_writestring(" ticks\n");

    // Convert to seconds for better readability (assuming ~18.2 Hz)
    uint64_t seconds = current_ticks / 18; // Approx. 18.2 ticks per second
    terminal_writestring(" (~");
    
    // Simple seconds to string conversion
    i = 0;
    if (seconds == 0) {
        num_str[0] = '0';
        i = 1;
    } else {
        uint64_t temp = seconds;
        while (temp > 0) {
            num_str[i++] = (temp % 10) + '0';
            temp /= 10;
        }
    }
    num_str[i] = '\0';
    start = 0; end = i - 1;
    while (start < end) { char temp = num_str[start]; num_str[start] = num_str[end]; num_str[end] = temp; start++; end--; }
    terminal_writestring(num_str);
    terminal_writestring(" seconds)\n");
}

// Initialization function for Timer Extension
int timer_extension_init(void) {
    terminal_writestring("Timer Extension: Initializing...\n");

    // PIT frequency is 1193180 Hz.
    // To get ~18.2 Hz (default for IRQ0), use divisor 65536.
    // For 100 Hz, divisor = 1193180 / 100 = 11931.8 -> use 11932
    uint16_t divisor = 11932; // For ~100 Hz

    // Command Register (0x43):
    // Channel 0 (00), Access Mode: Low/High byte (11), Operating Mode: Square Wave Generator (011), BCD: Binary (0)
    // 00110110b = 0x36
    outb(0x43, 0x36); // Set our command byte.

    // Data Register (0x40): Send divisor (low byte then high byte)
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));

    // Ensure IRQ0 is unmasked in the PIC (handled by IRQ & KB Ext, but good to ensure here)
    // You'd typically only unmask IRQ0 here if you weren't using a separate IRQ_KB extension
    // uint8_t mask = inb(0x21);
    // outb(0x21, mask & 0xFE); // Clear bit 0 for IRQ0

    terminal_writestring("Timer Extension: PIT configured for ~100 Hz.\n");
    terminal_writestring("Timer Extension: Uptime counter active.\n");

    // Register uptime command
    register_command("uptime", cmd_uptime, "Display system uptime", timer_ext_id);

    return 0; // Success
}

// Cleanup function for Timer Extension
void timer_extension_cleanup(void) {
    terminal_writestring("Timer Extension: Cleaning up...\n");
    // Optionally disable PIT interrupts or set it to a safe default mode
    terminal_writestring("Timer Extension: Cleanup complete.\n");
}

// Function to register this extension from the kernel's main entry point
void register_timer_extension(void) {
    timer_ext_id = register_extension("Timer", "1.0",
                                      timer_extension_init,
                                      timer_extension_cleanup);
    if (timer_ext_id >= 0) {
        load_extension(timer_ext_id);
    } else {
        terminal_writestring("Failed to register Timer Extension!\n");
    }
}
