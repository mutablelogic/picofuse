#include <pico/stdlib.h>

/**
 * @brief Pauses the execution of the current thread for a specified time.
 */
void sys_sleep_ms(uint32_t ms) { sleep_ms(ms); }
