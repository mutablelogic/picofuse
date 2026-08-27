#define _POSIX_C_SOURCE 199309L
#include <errno.h>
#include <picofuse/sys.h>
#include <stdint.h>
#include <time.h>

/**
 * @brief Pauses the execution of the current thread for a specified time.
 */
void sys_sleep_ms(uint32_t ms) {
  if (ms == 0) {
    return;
  }

  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000;

  int res;
  do {
    res = nanosleep(&ts, &ts);
  } while (res && errno == EINTR);
}
