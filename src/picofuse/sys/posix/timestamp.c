#define _POSIX_C_SOURCE 199309L
#include <picofuse/sys.h>
#include <stdint.h>
#include <time.h>

/**
 * @brief Gets the number of milliseconds since the process launched.
 */
uint64_t sys_timestamp_ms(void) {
  static uint64_t process_start_time_ms = 0;
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
    // Use explicit 64-bit arithmetic to prevent overflow
    uint64_t current_time_ms =
        (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;

    // Initialize process start time on first call (when it's zero)
    if (process_start_time_ms == 0) {
      process_start_time_ms = current_time_ms;
    }

    // Return time elapsed since process start
    return current_time_ms - process_start_time_ms;
  }
  return 0;
}
