/**
 * @file sys/hash.h
 * @brief Defines incremental hash generation APIs.
 * @defgroup SystemDataHash Hashes
 * @ingroup SystemData
 *
 * Incremental MD5 and SHA-256 hash generation, sometimes with hardware
 * acceleration.
 *
 * MD5 is not considered secure, but it is still useful for checksums and
 * other non-security purposes. SHA-256 is recommended for security-sensitive
 * uses and may have hardware acceleration on some platforms.
 */

#pragma once
#include "io.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @def SYS_HASH_SIZE
 * @ingroup SystemDataHash
 * @brief Maximum digest buffer size in bytes.
 */
#ifndef SYS_HASH_SIZE
#define SYS_HASH_SIZE 32
#endif

/**
 * @def SYS_HASH_CAPACITY
 * @ingroup SystemDataHash
 * @brief Maximum number of concurrent hash contexts available.
 */
#ifndef SYS_HASH_CAPACITY
#ifdef SYSTEM_NAME_PICO
#define SYS_HASH_CAPACITY 1
#else
#define SYS_HASH_CAPACITY 4
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Hash algorithm identifiers.
 * @ingroup SystemDataHash
 */
typedef enum sys_hash_algorithm_t {
  sys_hash_md5 = 1, ///< MD5 digest.
  sys_hash_sha256,  ///< SHA-256 digest.
} sys_hash_algorithm_t;

/**
 * @brief Hash context.
 * @ingroup SystemDataHash
 * @headerfile hash.h picofuse/sys.h
 */
typedef struct sys_hash_t sys_hash_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize a new hash context.
 * @ingroup SystemDataHash
 * @param algorithm The hash algorithm to use.
 * @return Initialized hash context, or `NULL` if the algorithm is unsupported
 * or no implementation capacity remains.
 *
 * Allocates and initializes a hash context from an implementation-defined
 * static capacity. The returned handle is ready for use with
 * sys_hash_update() and sys_hash_finalize(). Call sys_hash_deinit() to
 * release the slot for reuse.
 */
sys_hash_t *sys_hash_init(sys_hash_algorithm_t algorithm);

/**
 * @brief Release a hash context.
 * @ingroup SystemDataHash
 * @param hash Pointer to the hash context to release.
 *
 * Releases all resources associated with the hash context and renders it
 * unusable. After this call, the handle must not be used again.
 */
void sys_hash_deinit(sys_hash_t *hash);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @name Methods
 * @{ */

/**
 * @brief Return the digest size in bytes for a hash context.
 * @ingroup SystemDataHash
 * @param hash Pointer to the hash context.
 * @return The digest size in bytes, or 0 if the context is invalid.
 */
size_t sys_hash_size(const sys_hash_t *hash);

/**
 * @brief Update a hash context by reading more data from a stream.
 * @ingroup SystemDataHash
 * @param hash Pointer to the hash context.
 * @param stream The stream to read from.
 * @param size Number of bytes to read from stream and feed into the
 * hash, or 0 to read until end of stream. Call this repeatedly (e.g.
 * once per chunk as it becomes available, each with an explicit size)
 * to hash a stream incrementally, or once with size 0 to hash all of
 * its remaining bytes in one call.
 * @return true on success, false on error - including a short read
 * (stream had fewer than size bytes left, when size is nonzero).
 */
bool sys_hash_update(sys_hash_t *hash, sys_iostream_t *stream, size_t size);

/**
 * @brief Finalize the hash computation.
 * @ingroup SystemDataHash
 * @param hash Pointer to the hash context.
 * @return Pointer to the computed digest stored inside the hash context, or
 * `NULL` on failure.
 *
 * The returned pointer remains owned by the hash context and is valid until
 * sys_hash_deinit() is called for that handle.
 */
const uint8_t *sys_hash_finalize(sys_hash_t *hash);

/**
 * @brief Compute a djb2 hash for a NULL-terminated string.
 * @ingroup SystemDataHash
 * @param str The NULL-terminated string to hash.
 * @return The computed hash value.
 */
uintptr_t sys_hash_djb2(const char *str);

/** @} */

#ifdef __cplusplus
}
#endif
