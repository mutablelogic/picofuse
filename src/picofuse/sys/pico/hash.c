#include "sync.h"
#include <mbedtls/md5.h>
#include <mbedtls/sha256.h>
#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct sys_hash_t {
  union {
    mbedtls_md5_context md5;
    mbedtls_sha256_context sha256;
  } ctx;
  sys_hash_algorithm_t algorithm;
  uint8_t digest[SYS_HASH_SIZE];
  size_t size;
  bool init;
};

// Bytes read from the stream per sys_iostream_read() call inside
// sys_hash_update() - a stack buffer, not a limit on how much data can
// be hashed in one call (size may be arbitrarily large, or 0 for "until
// EOF"; this only bounds how much of it is in memory at once).
#define _SYS_HASH_UPDATE_CHUNK_SIZE 256

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static sys_hash_t _sys_hash_pool[SYS_HASH_CAPACITY];
static size_t _sys_hash_pool_next_index = 0;

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static size_t _sys_hash_digest_size(sys_hash_algorithm_t algorithm);
static bool _sys_hash_owned(const sys_hash_t *hash);
static bool _sys_hash_valid(const sys_hash_t *hash);
static void _sys_hash_clear_digest(sys_hash_t *hash);
static bool _sys_hash_start(sys_hash_t *hash, sys_hash_algorithm_t algorithm);
static bool _sys_hash_update_chunk(sys_hash_t *hash, const void *data,
                                    size_t size);
static void _sys_hash_free_ctx(sys_hash_t *hash);

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

sys_hash_t *sys_hash_init(sys_hash_algorithm_t algorithm) {
  size_t digest_size = _sys_hash_digest_size(algorithm);
  if (digest_size == 0 || digest_size > SYS_HASH_SIZE) {
    return NULL;
  }

  // Shared with the mutex/cond/waitgroup pools - held only for this
  // array's own bookkeeping, never across the (unbounded) work done by
  // sys_hash_update()/sys_hash_finalize() on an already-claimed handle.
  _sys_sync_pool_lock();

  for (size_t offset = 0; offset < SYS_HASH_CAPACITY; offset++) {
    size_t index = (_sys_hash_pool_next_index + offset) % SYS_HASH_CAPACITY;
    sys_hash_t *hash = &_sys_hash_pool[index];
    if (hash->init) {
      continue;
    }

    if (!_sys_hash_start(hash, algorithm)) {
      _sys_sync_pool_unlock();
      return NULL;
    }

    hash->algorithm = algorithm;
    hash->size = digest_size;
    hash->init = true;
    _sys_hash_clear_digest(hash);

    _sys_hash_pool_next_index = (index + 1) % SYS_HASH_CAPACITY;
    _sys_sync_pool_unlock();
    return hash;
  }

  _sys_sync_pool_unlock();
  return NULL;
}

void sys_hash_deinit(sys_hash_t *hash) {
  sys_assert(_sys_hash_owned(hash));
  if (!_sys_hash_owned(hash)) {
    return;
  }

  _sys_sync_pool_lock();

  if (!_sys_hash_valid(hash)) {
    _sys_sync_pool_unlock();
    return;
  }

  _sys_hash_free_ctx(hash);
  hash->algorithm = 0;
  hash->size = 0;
  hash->init = false;
  _sys_hash_clear_digest(hash);

  _sys_sync_pool_unlock();
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

size_t sys_hash_size(const sys_hash_t *hash) {
  if (!_sys_hash_valid(hash)) {
    return 0;
  }
  return hash->size;
}

bool sys_hash_update(sys_hash_t *hash, sys_iostream_t *stream, size_t size) {
  sys_assert(_sys_hash_owned(hash));

  if (!_sys_hash_valid(hash) || stream == NULL || hash->algorithm == 0) {
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
      if (!_sys_hash_update_chunk(hash, buf, got)) {
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
      if (!_sys_hash_update_chunk(hash, buf, got)) {
        ok = false;
        break;
      }
      remaining -= got;
    }
  }

  return ok;
}

const uint8_t *sys_hash_finalize(sys_hash_t *hash) {
  sys_assert(_sys_hash_owned(hash));

  if (!_sys_hash_valid(hash)) {
    return NULL;
  }

  if (hash->algorithm == 0) {
    // Already finalized - return the same stored digest again.
    return hash->digest;
  }

  int ret = -1;
  switch (hash->algorithm) {
  case sys_hash_md5:
    ret = mbedtls_md5_finish(&hash->ctx.md5, hash->digest);
    break;
  case sys_hash_sha256:
    ret = mbedtls_sha256_finish(&hash->ctx.sha256, hash->digest);
    break;
  default:
    break;
  }

  _sys_hash_free_ctx(hash);
  hash->algorithm = 0;

  if (ret != 0) {
    _sys_hash_clear_digest(hash);
    return NULL;
  }

  return hash->digest;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static size_t _sys_hash_digest_size(sys_hash_algorithm_t algorithm) {
  switch (algorithm) {
  case sys_hash_md5:
    return 16;
  case sys_hash_sha256:
    return 32;
  default:
    return 0;
  }
}

static bool _sys_hash_owned(const sys_hash_t *hash) {
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

static bool _sys_hash_valid(const sys_hash_t *hash) {
  return _sys_hash_owned(hash) && hash->init;
}

static void _sys_hash_clear_digest(sys_hash_t *hash) {
  for (size_t i = 0; i < sizeof(hash->digest); i++) {
    hash->digest[i] = 0;
  }
}

static bool _sys_hash_start(sys_hash_t *hash, sys_hash_algorithm_t algorithm) {
  switch (algorithm) {
  case sys_hash_md5:
    mbedtls_md5_init(&hash->ctx.md5);
    if (mbedtls_md5_starts(&hash->ctx.md5) != 0) {
      mbedtls_md5_free(&hash->ctx.md5);
      return false;
    }
    return true;
  case sys_hash_sha256:
    mbedtls_sha256_init(&hash->ctx.sha256);
    if (mbedtls_sha256_starts(&hash->ctx.sha256, 0) != 0) {
      mbedtls_sha256_free(&hash->ctx.sha256);
      return false;
    }
    return true;
  default:
    return false;
  }
}

static bool _sys_hash_update_chunk(sys_hash_t *hash, const void *data,
                                    size_t size) {
  switch (hash->algorithm) {
  case sys_hash_md5:
    return mbedtls_md5_update(&hash->ctx.md5, data, size) == 0;
  case sys_hash_sha256:
    return mbedtls_sha256_update(&hash->ctx.sha256, data, size) == 0;
  default:
    return false;
  }
}

static void _sys_hash_free_ctx(sys_hash_t *hash) {
  switch (hash->algorithm) {
  case sys_hash_md5:
    mbedtls_md5_free(&hash->ctx.md5);
    break;
  case sys_hash_sha256:
    mbedtls_sha256_free(&hash->ctx.sha256);
    break;
  default:
    break;
  }
}
