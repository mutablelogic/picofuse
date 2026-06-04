#include "pico/clocks.h"
#include "pico/gpio.h"
#include "pico/irq.h"
#include "pico/uart.h"

#ifndef LED_PIN
#error "LED_PIN not defined — set PICOFUSE_LED_PIN in the board cmake"
#endif

#ifdef USER_SW_PIN
static void on_button(uint gpio, uint32_t events) {
    uart_puts(0, "button pressed\r\n");
}
#endif

int main(void) {
    clocks_init_xosc(PICOFUSE_XOSC_HZ);
    irq_init();

    uart_init(0, 0, 1, 9600);
    uart_puts(0, "hello, world\r\n");

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

#ifdef USER_SW_PIN
    gpio_init(USER_SW_PIN);
    gpio_disable_pulls(USER_SW_PIN);
    gpio_set_irq_callback(on_button);
    gpio_set_irq_enabled(USER_SW_PIN, GPIO_IRQ_EDGE_FALL, true);
#endif

    while (1) {
        gpio_toggle(LED_PIN);
        for (volatile uint i = 50000; i > 0; i--);
    }
}
