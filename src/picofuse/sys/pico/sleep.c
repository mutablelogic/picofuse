#include <pico/stdlib.h>
#include <pico/time.h>

/**
 * @brief Pauses the execution of the current thread for a specified time.
 *
 * Busy-waits to the exact target time rather than calling the SDK's
 * sleep_ms()/sleep_us(): with PICO_TIME_DEFAULT_ALARM_POOL_DISABLED set (see
 * CMakeLists.txt), the SDK's own busy-wait path shortens the wait by
 * PICO_TIME_SLEEP_OVERHEAD_ADJUST_US and never makes it up, which can return
 * early enough to violate the "at least the requested duration" contract.
 */
void sys_sleep_ms(uint32_t ms) {
  busy_wait_until(make_timeout_time_ms(ms));
}
