#include <openssl/evp.h>
#include <picofuse/sys.h>
#include <pthread.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct sys_hash_t {
  EVP_MD_CTX *ctx;
  sys_hash_algorithm_t algorithm;
  uint8_t digest[SYS_HASH_SIZE];
  size_t size;
  bool init;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static pthread_mutex_t _sys_hash_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static sys_hash_t _sys_hash_pool[SYS_HASH_CAPACITY];
static size_t _sys_hash_pool_next_index = 0;

// Bytes read from the stream per sys_iostream_read() call inside
// sys_hash_update() - a stack buffer, not a limit on how much data can
// be hashed in one call (size may be arbitrarily large, or 0 for "until
// EOF"; this only bounds how much of it is in memory at once).
#define _SYS_HASH_UPDATE_CHUNK_SIZE 256

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Returns the OpenSSL EVP_MD corresponding to the given hash algorithm,
 * and sets the digest size. Returns NULL if the algorithm is unsupported. */
static inline const EVP_MD *_sys_hash_evp_md(sys_hash_algorithm_t algorithm,
                                             size_t *size) {
  switch (algorithm) {
  case sys_hash_md5:
    *size = 16;
    return EVP_md5();
  case sys_hash_sha256:
    *size = 32;
    return EVP_sha256();
  default:
    *size = 0;
    return NULL;
  }
}

/** @brief Returns true if the given hash is owned by the pool, false otherwise.
 */
static inline bool _sys_hash_owned(const sys_hash_t *hash) {
  if (hash == NULL) {
    return false;
  }

  for (size_t index = 0; index < SYS_HASH_CAPACITY; index++) {
    if (hash == &_sys_hash_pool[index]) {
      return true;
    }
  }

  return false;
}

/** @brief Returns true if the given hash is valid (owned by the pool and
 * initialized), false otherwise.
 */
static inline bool _sys_hash_valid(const sys_hash_t *hash) {
  return _sys_hash_owned(hash) && hash->init;
}

/** @brief Clears the digest buffer of the given hash. */
static inline void _sys_hash_clear_digest(sys_hash_t *hash) {
  for (size_t i = 0; i < sizeof(hash->digest); i++) {
    hash->digest[i] = 0;
  }
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @brief Initializes a hash context for the given algorithm.
 * Returns a pointer to the hash context on success, or NULL on failure.
 */
sys_hash_t *sys_hash_init(sys_hash_algorithm_t algorithm) {
  size_t digest_size = 0;
  const EVP_MD *md = _sys_hash_evp_md(algorithm, &digest_size);
  if (md == NULL || digest_size > SYS_HASH_SIZE) {
    return NULL;
  }

  if (pthread_mutex_lock(&_sys_hash_pool_lock) != 0) {
    return NULL;
  }

  for (size_t offset = 0; offset < SYS_HASH_CAPACITY; offset++) {
    size_t index = (_sys_hash_pool_next_index + offset) % SYS_HASH_CAPACITY;
    sys_hash_t *hash = &_sys_hash_pool[index];
    if (hash->init) {
      continue;
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
      pthread_mutex_unlock(&_sys_hash_pool_lock);
      return NULL;
    }

    if (EVP_DigestInit_ex(ctx, md, NULL) != 1) {
      EVP_MD_CTX_free(ctx);
      pthread_mutex_unlock(&_sys_hash_pool_lock);
      return NULL;
    }

    hash->ctx = ctx;
    hash->algorithm = algorithm;
    hash->size = digest_size;
    hash->init = true;
    _sys_hash_clear_digest(hash);

    _sys_hash_pool_next_index = (index + 1) % SYS_HASH_CAPACITY;
    pthread_mutex_unlock(&_sys_hash_pool_lock);
    return hash;
  }

  pthread_mutex_unlock(&_sys_hash_pool_lock);
  return NULL;
}

/** @brief Deinitializes the given hash context, freeing any associated
 * resources. */
void sys_hash_deinit(sys_hash_t *hash) {
  sys_assert(_sys_hash_owned(hash));
  if (!_sys_hash_owned(hash)) {
    return;
  }

  int lock_result = pthread_mutex_lock(&_sys_hash_pool_lock);
  sys_assert(lock_result == 0);
  if (lock_result != 0) {
    return;
  }

  if (!_sys_hash_valid(hash)) {
    int unlock_result = pthread_mutex_unlock(&_sys_hash_pool_lock);
    sys_assert(unlock_result == 0);
    return;
  }

  if (hash->ctx != NULL) {
    EVP_MD_CTX_free(hash->ctx);
  }

  hash->ctx = NULL;
  hash->algorithm = 0;
  hash->size = 0;
  hash->init = false;
  _sys_hash_clear_digest(hash);

  int unlock_result = pthread_mutex_unlock(&_sys_hash_pool_lock);
  sys_assert(unlock_result == 0);
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Returns the size of the digest for the given hash context. */
size_t sys_hash_size(const sys_hash_t *hash) {
  if (!_sys_hash_owned(hash)) {
    return 0;
  }

  int lock_result = pthread_mutex_lock(&_sys_hash_pool_lock);
  sys_assert(lock_result == 0);
  if (lock_result != 0) {
    return 0;
  }

  size_t size = _sys_hash_valid(hash) ? hash->size : 0;

  int unlock_result = pthread_mutex_unlock(&_sys_hash_pool_lock);
  sys_assert(unlock_result == 0);
  return size;
}

/** @brief Updates the given hash context with data read from the given stream.
 * Returns true on success, false on failure. If size is 0, reads until EOF.
 */
bool sys_hash_update(sys_hash_t *hash, sys_iostream_t *stream, size_t size) {
  sys_assert(_sys_hash_owned(hash));

  if (!_sys_hash_owned(hash) || stream == NULL) {
    return false;
  }

  int lock_result = pthread_mutex_lock(&_sys_hash_pool_lock);
  sys_assert(lock_result == 0);
  if (lock_result != 0) {
    return false;
  }

  if (!_sys_hash_valid(hash) || hash->ctx == NULL) {
    int unlock_result = pthread_mutex_unlock(&_sys_hash_pool_lock);
    sys_assert(unlock_result == 0);
    return false;
  }

  char buf[_SYS_HASH_UPDATE_CHUNK_SIZE];
  bool ok = true;

  if (size == 0) {
    // Read until end of stream.
    for (;;) {
      size_t got = sys_iostream_read(stream, buf, sizeof(buf));
      if (got == 0) {
        break;
      }
      if (EVP_DigestUpdate(hash->ctx, buf, got) != 1) {
        ok = false;
        break;
      }
    }
  } else {
    // Read exactly size bytes - a short read (stream ran out first) is
    // a failure, not a partial success.
    size_t remaining = size;
    while (remaining > 0) {
      size_t want = remaining < sizeof(buf) ? remaining : sizeof(buf);
      size_t got = sys_iostream_read(stream, buf, want);
      if (got == 0) {
        ok = false;
        break;
      }
      if (EVP_DigestUpdate(hash->ctx, buf, got) != 1) {
        ok = false;
        break;
      }
      remaining -= got;
    }
  }

  int unlock_result = pthread_mutex_unlock(&_sys_hash_pool_lock);
  sys_assert(unlock_result == 0);
  return ok;
}

/** @brief Finalizes the given hash context and returns a pointer to the digest.
 * Returns NULL on failure.
 */
const uint8_t *sys_hash_finalize(sys_hash_t *hash) {
  sys_assert(_sys_hash_owned(hash));

  if (!_sys_hash_owned(hash)) {
    return NULL;
  }

  int lock_result = pthread_mutex_lock(&_sys_hash_pool_lock);
  sys_assert(lock_result == 0);
  if (lock_result != 0) {
    return NULL;
  }

  if (!_sys_hash_valid(hash)) {
    int unlock_result = pthread_mutex_unlock(&_sys_hash_pool_lock);
    sys_assert(unlock_result == 0);
    return NULL;
  }

  if (hash->ctx == NULL) {
    const uint8_t *digest = hash->digest;
    int unlock_result = pthread_mutex_unlock(&_sys_hash_pool_lock);
    sys_assert(unlock_result == 0);
    return digest;
  }

  unsigned int digest_size = 0;
  bool ok = EVP_DigestFinal_ex(hash->ctx, hash->digest, &digest_size) == 1;
  EVP_MD_CTX_free(hash->ctx);
  hash->ctx = NULL;

  if (!ok || digest_size != hash->size) {
    _sys_hash_clear_digest(hash);
    int unlock_result = pthread_mutex_unlock(&_sys_hash_pool_lock);
    sys_assert(unlock_result == 0);
    return NULL;
  }

  const uint8_t *digest = hash->digest;
  int unlock_result = pthread_mutex_unlock(&_sys_hash_pool_lock);
  sys_assert(unlock_result == 0);
  return digest;
}
