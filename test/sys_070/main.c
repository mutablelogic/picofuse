#include <picofuse/sys.h>
#include <test/test.h>

static void expect_digest(sys_hash_algorithm_t algorithm, const char *str,
                           const uint8_t *want, size_t want_size) {
  sys_hash_t *hash = sys_hash_init(algorithm);
  test_assert(hash != NULL);
  test_assert(sys_hash_size(hash) == want_size);

  sys_iostream_t *stream = sys_string_read(str);
  test_assert(stream != NULL);
  test_assert(sys_hash_update(hash, stream, 0));
  sys_iostream_close(stream);

  const uint8_t *got = sys_hash_finalize(hash);
  test_assert(got != NULL);
  for (size_t i = 0; i < want_size; i++) {
    test_assert(got[i] == want[i]);
  }

  sys_hash_deinit(hash);
}

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // sys_hash_size(NULL) is safe (no assert on this path) and returns 0,
  // matching the header's documented "0 if the context is invalid".

  test_assert(sys_hash_size(NULL) == 0);

  ///////////////////////////////////////////////////////////////////////
  // An unsupported algorithm value fails to initialize.

  test_assert(sys_hash_init((sys_hash_algorithm_t)0) == NULL);

  ///////////////////////////////////////////////////////////////////////
  // Known MD5 / SHA-256 reference digests, computed independently via
  // Python's hashlib.

  {
    const uint8_t md5_empty[] = {0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2,
                                  0x04, 0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8,
                                  0x42, 0x7e};
    const uint8_t md5_abc[] = {0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f,
                                0xb0, 0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1,
                                0x7f, 0x72};
    const uint8_t md5_hello_world[] = {0x5e, 0xb6, 0x3b, 0xbb, 0xe0, 0x1e,
                                        0xee, 0xd0, 0x93, 0xcb, 0x22, 0xbb,
                                        0x8f, 0x5a, 0xcd, 0xc3};

    expect_digest(sys_hash_md5, "", md5_empty, sizeof(md5_empty));
    expect_digest(sys_hash_md5, "abc", md5_abc, sizeof(md5_abc));
    expect_digest(sys_hash_md5, "hello world", md5_hello_world,
                   sizeof(md5_hello_world));
  }

  {
    const uint8_t sha256_empty[] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4,
        0xc8, 0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b,
        0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};
    const uint8_t sha256_abc[] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
        0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
        0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    const uint8_t sha256_hello_world[] = {
        0xb9, 0x4d, 0x27, 0xb9, 0x93, 0x4d, 0x3e, 0x08, 0xa5, 0x2e, 0x52,
        0xd7, 0xda, 0x7d, 0xab, 0xfa, 0xc4, 0x84, 0xef, 0xe3, 0x7a, 0x53,
        0x80, 0xee, 0x90, 0x88, 0xf7, 0xac, 0xe2, 0xef, 0xcd, 0xe9};

    expect_digest(sys_hash_sha256, "", sha256_empty, sizeof(sha256_empty));
    expect_digest(sys_hash_sha256, "abc", sha256_abc, sizeof(sha256_abc));
    expect_digest(sys_hash_sha256, "hello world", sha256_hello_world,
                   sizeof(sha256_hello_world));
  }

  ///////////////////////////////////////////////////////////////////////
  // Incremental updates across two separate reads-to-EOF must produce
  // the same digest as hashing the concatenation in one call.

  {
    const uint8_t want[] = {0x5e, 0xb6, 0x3b, 0xbb, 0xe0, 0x1e, 0xee,
                             0xd0, 0x93, 0xcb, 0x22, 0xbb, 0x8f, 0x5a,
                             0xcd, 0xc3}; // md5("hello world")

    sys_hash_t *hash = sys_hash_init(sys_hash_md5);
    test_assert(hash != NULL);

    sys_iostream_t *first = sys_string_read("hello ");
    sys_iostream_t *second = sys_string_read("world");
    test_assert(first != NULL && second != NULL);
    test_assert(sys_hash_update(hash, first, 0));
    test_assert(sys_hash_update(hash, second, 0));
    sys_iostream_close(first);
    sys_iostream_close(second);

    const uint8_t *got = sys_hash_finalize(hash);
    test_assert(got != NULL);
    for (size_t i = 0; i < sizeof(want); i++) {
      test_assert(got[i] == want[i]);
    }

    sys_hash_deinit(hash);
  }

  ///////////////////////////////////////////////////////////////////////
  // size > 0 reads an exact prefix of the stream, ignoring what follows.

  {
    const uint8_t want[] = {0x5e, 0xb6, 0x3b, 0xbb, 0xe0, 0x1e, 0xee,
                             0xd0, 0x93, 0xcb, 0x22, 0xbb, 0x8f, 0x5a,
                             0xcd, 0xc3}; // md5("hello world")

    sys_hash_t *hash = sys_hash_init(sys_hash_md5);
    test_assert(hash != NULL);

    sys_iostream_t *stream = sys_string_read("hello world, and more");
    test_assert(stream != NULL);
    test_assert(sys_hash_update(hash, stream, 11)); // just "hello world"
    sys_iostream_close(stream);

    const uint8_t *got = sys_hash_finalize(hash);
    test_assert(got != NULL);
    for (size_t i = 0; i < sizeof(want); i++) {
      test_assert(got[i] == want[i]);
    }

    sys_hash_deinit(hash);
  }

  ///////////////////////////////////////////////////////////////////////
  // A short read (fewer bytes available than requested) is a failure.

  {
    sys_hash_t *hash = sys_hash_init(sys_hash_md5);
    test_assert(hash != NULL);

    sys_iostream_t *stream = sys_string_read("hi");
    test_assert(stream != NULL);
    test_assert(!sys_hash_update(hash, stream, 10));
    sys_iostream_close(stream);

    sys_hash_deinit(hash);
  }

  ///////////////////////////////////////////////////////////////////////
  // The context pool has finite capacity: once exhausted, further
  // sys_hash_init() calls fail until a slot is released.

  {
    sys_hash_t *pool[SYS_HASH_CAPACITY];
    size_t count = 0;
    while (count < SYS_HASH_CAPACITY) {
      sys_hash_t *hash = sys_hash_init(sys_hash_md5);
      if (hash == NULL) {
        break;
      }
      pool[count++] = hash;
    }
    test_assert(count == SYS_HASH_CAPACITY);
    test_assert(sys_hash_init(sys_hash_md5) == NULL);

    sys_hash_deinit(pool[0]);
    sys_hash_t *reused = sys_hash_init(sys_hash_sha256);
    test_assert(reused != NULL);
    sys_hash_deinit(reused);

    for (size_t i = 1; i < count; i++) {
      sys_hash_deinit(pool[i]);
    }
  }

}
