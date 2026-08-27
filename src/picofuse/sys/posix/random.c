#include <picofuse/sys.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static inline void _sys_random_init(void) {
  static bool init = false;
  if (!init) {
    srand((unsigned int)time(NULL));
    init = true;
  }
}

static inline uint32_t _sys_random_rand32(void) {
  uint32_t result = 0;

  for (int i = 0; i < 4; i++) {
    result = (result << 8) | ((uint32_t)rand() & 0xFFu);
  }

  return result;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Returns a random number as a 32-bit unsigned integer.
 * @note This function is not thread-safe.
 */
uint32_t sys_random_uint32(void) {
  _sys_random_init();
  return _sys_random_rand32();
}

/**
 * @brief Returns a random number as a 64-bit unsigned integer.
 * @note This function is not thread-safe.
 */
uint64_t sys_random_uint64(void) {
  _sys_random_init();
  return ((uint64_t)_sys_random_rand32() << 32) |
         (uint64_t)_sys_random_rand32();
}
