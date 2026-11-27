#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

// Hash bytes in memory, in pieces of the given size (0 for all at once),
// and compare with a digest computed with Python's hashlib
static void expect_bytes(sys_hash_algorithm_t algorithm, const void *bytes,
                         size_t length, size_t piece, const uint8_t *want,
                         size_t want_size) {
  sys_hash_t *hash = sys_hash_init(algorithm);
  test_assert(hash != NULL);

  const uint8_t *at = bytes;
  size_t remaining = length;
  do {
    size_t n = piece == 0 || piece > remaining ? remaining : piece;
    test_assert(sys_hash_update_bytes(hash, at, n));
    at += n;
    remaining -= n;
  } while (remaining > 0);

  const uint8_t *got = sys_hash_finalize(hash);
  test_assert(got != NULL);
  test_assert(memcmp(got, want, want_size) == 0);
  sys_hash_deinit(hash);
}

test_main_sys(0) {
  const uint8_t md5_abc[] = {0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
                             0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72};
  const uint8_t sha256_abc[] = {
      0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
      0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
      0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};

  ///////////////////////////////////////////////////////////////////////
  // Bytes, all at once and one at a time

  expect_bytes(sys_hash_md5, "abc", 3, 0, md5_abc, sizeof(md5_abc));
  expect_bytes(sys_hash_md5, "abc", 3, 1, md5_abc, sizeof(md5_abc));
  expect_bytes(sys_hash_sha256, "abc", 3, 0, sha256_abc, sizeof(sha256_abc));
  expect_bytes(sys_hash_sha256, "abc", 3, 1, sha256_abc, sizeof(sha256_abc));

  ///////////////////////////////////////////////////////////////////////
  // Zero bytes are hashed, not taken as the end

  {
    const uint8_t zeros[] = {'a', 0, 'b', 0};
    const uint8_t md5[] = {0xaa, 0x3a, 0x79, 0x1e, 0x27, 0x3b, 0xce, 0x9c,
                           0xf4, 0xa2, 0xa7, 0xca, 0xa9, 0x02, 0x8b, 0x36};
    const uint8_t sha256[] = {
        0x8f, 0xb2, 0x0e, 0xf6, 0x3c, 0xed, 0x41, 0x45, 0xfc, 0x2e, 0x98,
        0x3f, 0xfe, 0x59, 0x7d, 0x1d, 0xcf, 0xf3, 0x91, 0x54, 0xc3, 0xbf,
        0x21, 0xf0, 0xfa, 0x9d, 0xde, 0x6a, 0x0c, 0x50, 0xfd, 0xc9};
    expect_bytes(sys_hash_md5, zeros, sizeof(zeros), 0, md5, sizeof(md5));
    expect_bytes(sys_hash_sha256, zeros, sizeof(zeros), 0, sha256,
                 sizeof(sha256));
  }

  ///////////////////////////////////////////////////////////////////////
  // More than a block, in uneven pieces: bytes i % 256 for i < 1000

  {
    static uint8_t all[1000];
    for (size_t i = 0; i < sizeof(all); i++) {
      all[i] = (uint8_t)i;
    }
    const uint8_t md5[] = {0xcb, 0xec, 0xbd, 0xb0, 0xfd, 0xd5, 0xce, 0xc1,
                           0xe2, 0x42, 0x49, 0x3b, 0x60, 0x08, 0xcc, 0x79};
    const uint8_t sha256[] = {
        0xa8, 0xaf, 0x09, 0x9b, 0xf2, 0xe8, 0x78, 0x60, 0x95, 0x58, 0xdb,
        0xf6, 0x9d, 0x8f, 0x88, 0xf4, 0xa3, 0x10, 0x40, 0xa8, 0xcf, 0x84,
        0xb5, 0x49, 0xa0, 0xcf, 0xa9, 0x12, 0xf1, 0x2f, 0xfc, 0x3f};
    expect_bytes(sys_hash_md5, all, sizeof(all), 0, md5, sizeof(md5));
    expect_bytes(sys_hash_md5, all, sizeof(all), 37, md5, sizeof(md5));
    expect_bytes(sys_hash_sha256, all, sizeof(all), 0, sha256,
                 sizeof(sha256));
    expect_bytes(sys_hash_sha256, all, sizeof(all), 37, sha256,
                 sizeof(sha256));
  }

  ///////////////////////////////////////////////////////////////////////
  // Mixed with a stream: "a" from a stream, then "bc" as bytes

  {
    sys_hash_t *hash = sys_hash_init(sys_hash_sha256);
    test_assert(hash != NULL);
    sys_iostream_t *stream = sys_string_read("a");
    test_assert(stream != NULL);
    test_assert(sys_hash_update(hash, stream, 0));
    sys_iostream_close(stream);
    test_assert(sys_hash_update_bytes(hash, "bc", 2));
    const uint8_t *got = sys_hash_finalize(hash);
    test_assert(got != NULL && memcmp(got, sha256_abc, sizeof(sha256_abc)) == 0);

    // Not after it's finalized
    test_assert(!sys_hash_update_bytes(hash, "x", 1));
    sys_hash_deinit(hash);
  }

  ///////////////////////////////////////////////////////////////////////
  // NULL is only allowed with no bytes, and changes nothing

  {
    sys_hash_t *hash = sys_hash_init(sys_hash_md5);
    test_assert(hash != NULL);
    test_assert(!sys_hash_update_bytes(hash, NULL, 1));
    test_assert(sys_hash_update_bytes(hash, NULL, 0));
    test_assert(sys_hash_update_bytes(hash, "abc", 3));
    const uint8_t *got = sys_hash_finalize(hash);
    test_assert(got != NULL && memcmp(got, md5_abc, sizeof(md5_abc)) == 0);
    sys_hash_deinit(hash);
  }
}
