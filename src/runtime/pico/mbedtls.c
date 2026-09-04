#include <mbedtls/platform_time.h>
#include <picofuse/sys.h>

/**
 * Custom function to get the current time in milliseconds for mbedtls.
 */
mbedtls_ms_time_t mbedtls_ms_time(void) {
  return (mbedtls_ms_time_t)sys_timestamp_ms();
}
