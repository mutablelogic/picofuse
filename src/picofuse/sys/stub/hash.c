#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

// No algorithm is actually implemented here - returning NULL for every
// request is a conforming answer to "unsupported", per sys_hash_init()'s
// own documented contract, not a placeholder shortcut. A real backend
// (e.g. one built on OpenSSL's EVP digest API, matching how
// sys/openssl/random.c supplements sys/posix/random.c) can replace this
// file per-platform without changing anything that calls sys_hash_*().
sys_hash_t *sys_hash_init(sys_hash_algorithm_t algorithm) {
  (void)algorithm;
  return NULL;
}

void sys_hash_deinit(sys_hash_t *hash) { (void)hash; }

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

size_t sys_hash_size(const sys_hash_t *hash) {
  (void)hash;
  return 0;
}

bool sys_hash_update(sys_hash_t *hash, sys_iostream_t *stream, size_t size) {
  (void)hash;
  (void)stream;
  (void)size;
  return false;
}

const uint8_t *sys_hash_finalize(sys_hash_t *hash) {
  (void)hash;
  return NULL;
}
