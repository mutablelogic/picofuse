#include <pico/time.h>
#include <stdbool.h>

static uint64_t _sys_timestamp_now_ms(void) {
  return to_us_since_boot(get_absolute_time()) / 1000;
}

/**
 * @brief Gets the number of milliseconds since the process launched.
 */
uint64_t sys_timestamp_ms(void) {
  static uint64_t process_start_time_ms;
  static bool initialized = false;
  uint64_t current_time_ms = _sys_timestamp_now_ms();
  if (!initialized) {
    process_start_time_ms = current_time_ms;
    initialized = true;
  }

  return current_time_ms - process_start_time_ms;
}
