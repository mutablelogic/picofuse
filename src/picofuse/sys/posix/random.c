#include <picofuse/sys.h>
#include <stdint.h>
#include <stdlib.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Returns a random number as a 32-bit unsigned integer.
 */
uint32_t sys_random_uint32(void) { return arc4random(); }

/**
 * @brief Returns a random number as a 64-bit unsigned integer.
 */
uint64_t sys_random_uint64(void) {
  uint64_t value;
  arc4random_buf(&value, sizeof(value));
  return value;
}
