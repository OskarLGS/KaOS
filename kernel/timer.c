#include "timer.h"
#include "isr.h"
#include "task.h"

// volatile: incremented by the IRQ0 handler, read by get_tick/uptime and
// the desktop loop's once-per-second redraw check
static volatile uint32_t tick = 0;
static uint32_t frequency = 0;

static void timer_callback(struct registers* regs) {
    (void)regs;
    tick++;
    // Preemptive multitasking: drive the scheduler from the timer. EOI was
    // already sent by irq_handler before we got here, and schedule() is
    // interrupt-safe, so switching tasks from inside this IRQ is sound.
    schedule();
}

void init_timer(uint32_t freq) {
    frequency = freq;
    register_interrupt_handler(0, timer_callback);

    uint32_t divisor = 1193180 / freq;

    __asm__ __volatile__("outb %0, $0x43" : : "a"((uint8_t)0x36));
    __asm__ __volatile__("outb %0, $0x40" : : "a"((uint8_t)(divisor & 0xFF)));
    __asm__ __volatile__("outb %0, $0x40" : : "a"((uint8_t)((divisor >> 8) & 0xFF)));
}

uint32_t get_tick() {
    return tick;
}

uint32_t get_uptime_seconds() {
    if (frequency == 0) return 0;
    return tick / frequency;
}
