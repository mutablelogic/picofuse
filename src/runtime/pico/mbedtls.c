#include <mbedtls/platform_time.h>
#include <picofuse/sys.h>
#include <runtime/mbedtls.h>
#include <stdlib.h>

/**
 * Custom calloc function for mbedtls, using the standard C library allocator.
 */
void *sys_mbedtls_calloc(size_t n, size_t size) { return calloc(n, size); }

/**
 * Custom free function for mbedtls, using the standard C library allocator.
 */
void sys_mbedtls_free(void *ptr) { free(ptr); }

/**
 * Custom function to get the current time in milliseconds for mbedtls.
 */
mbedtls_ms_time_t mbedtls_ms_time(void) {
  return (mbedtls_ms_time_t)sys_timestamp_ms();
}
