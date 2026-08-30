#pragma once
#include <limits.h>
#include <picofuse/sys.h>
#include <runtime/mbedtls.h>

#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_MS_TIME_ALT
#define MBEDTLS_PLATFORM_PRINTF_MACRO sys_printf
#define MBEDTLS_PLATFORM_CALLOC_MACRO sys_mbedtls_calloc
#define MBEDTLS_PLATFORM_FREE_MACRO sys_mbedtls_free
#define MBEDTLS_MD5_C
#define MBEDTLS_SHA256_C

#if LIB_PICO_SHA256
#define MBEDTLS_SHA256_ALT
#endif

#include <mbedtls/mbedtls_config.h>

#undef MBEDTLS_HAVE_TIME_DATE
#undef MBEDTLS_FS_IO
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_SELF_TEST
#undef MBEDTLS_DEBUG_C
