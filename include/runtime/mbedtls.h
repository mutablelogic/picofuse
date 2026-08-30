#pragma once
#include <stddef.h>

// calloc/free hooks for pico_mbedtls (MBEDTLS_PLATFORM_MEMORY).
// These currently just forward to the C library allocator (backed by
// pico_malloc) - a placeholder until picofuse has its own allocator to
// wire in here instead.
extern void *sys_mbedtls_calloc(size_t n, size_t size);
extern void sys_mbedtls_free(void *ptr);
