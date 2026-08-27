#include <pico/time.h>

static uint64_t _sys_timestamp_now_ms(void) {
  return to_us_since_boot(get_absolute_time()) / 1000;
}

/**
 * @brief Gets the number of milliseconds since the process launched.
 */
uint64_t sys_timestamp_ms(void) {
  static uint64_t process_start_time_ms = 0;
  uint64_t current_time_ms = _sys_timestamp_now_ms();
  if (process_start_time_ms == 0) {
    process_start_time_ms = current_time_ms;
  }

  return current_time_ms - process_start_time_ms;
}
