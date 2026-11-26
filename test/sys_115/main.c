#include <picofuse/sys.h>
#include <test/test.h>

// The vectors of RFC 4648, section 10
static const struct {
  const char *bytes, *base64;
} vectors[] = {
    {"", ""},         {"f", "Zg=="},        {"fo", "Zm8="},
    {"foo", "Zm9v"},  {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="},
    {"foobar", "Zm9vYmFy"},
};

test_main_sys(0) {
  char buf[400];
  uint8_t bytes[300];

  ///////////////////////////////////////////////////////////////////////
  // Encoding and decoding the vectors

  for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
    size_t size = sys_string_bytes(vectors[i].bytes);
    size_t length = sys_string_bytes(vectors[i].base64);
    test_assert(sys_string_base64_encode(vectors[i].bytes, size, buf,
                                         sizeof(buf)) == length);
    test_assert_strequal(buf, vectors[i].base64);
    test_assert(sys_string_base64_encode(vectors[i].bytes, size, NULL, 0) ==
                length);
    if (length > 0) {
      test_assert(sys_string_parse_base64(vectors[i].base64, 0, bytes,
                                          sizeof(bytes)) == (ptrdiff_t)size);
      test_assert(__builtin_memcmp(bytes, vectors[i].bytes, size) == 0);
      test_assert(sys_string_parse_base64(vectors[i].base64, 0, NULL, 0) ==
                  (ptrdiff_t)size);
    }
  }

  ///////////////////////////////////////////////////////////////////////
  // Without padding, and with a length

  test_assert(sys_string_parse_base64("Zg", 0, bytes, sizeof(bytes)) == 1 &&
              bytes[0] == 'f');
  test_assert(sys_string_parse_base64("Zm8", 0, bytes, sizeof(bytes)) == 2 &&
              bytes[0] == 'f' && bytes[1] == 'o');
  test_assert(sys_string_parse_base64("Zm9vYmFyXXXX", 8, bytes, sizeof(bytes)) ==
              6);

  ///////////////////////////////////////////////////////////////////////
  // Every byte, and every length up to 256, both ways

  uint8_t all[256];
  for (size_t i = 0; i < sizeof(all); i++) {
    all[i] = (uint8_t)(255 - i);
  }
  for (size_t size = 0; size <= sizeof(all); size++) {
    size_t length = sys_string_base64_encode(all, size, buf, sizeof(buf));
    test_assert(length == (size + 2) / 3 * 4 && sys_string_bytes(buf) == length);
    if (size > 0) {
      __builtin_memset(bytes, 0, sizeof(bytes));
      test_assert(sys_string_parse_base64(buf, 0, bytes, sizeof(bytes)) ==
                  (ptrdiff_t)size);
      test_assert(__builtin_memcmp(bytes, all, size) == 0);
    }
  }
  test_assert(sys_string_base64_encode("\xff\xfe\x00", 3, buf, sizeof(buf)) == 4);
  test_assert_strequal(buf, "//4A");

  ///////////////////////////////////////////////////////////////////////
  // Too small a buffer

  test_assert(sys_string_base64_encode("foobar", 6, buf, 5) == 8);
  test_assert_strequal(buf, "Zm9v");
  test_assert(sys_string_base64_encode("foobar", 6, buf, 0) == 8);
  __builtin_memset(bytes, 0xAA, sizeof(bytes));
  test_assert(sys_string_parse_base64("Zm9vYmFy", 0, bytes, 4) == 6);
  test_assert(__builtin_memcmp(bytes, "foob", 4) == 0 && bytes[4] == 0xAA);

  ///////////////////////////////////////////////////////////////////////
  // Those which aren't base64

  const char *bad[] = {"Z",       "Zg=",      "Zg===",  "Zm8==",   "Zm9v=",
                       "Zh==",    "Zm9=",     "Zm 9v",  "Zm9v\n",  " Zm9v",
                       "Zm9-",    "Zm9_",     "Zm9v====", "=",     "==",
                       "Z=g=",    "Zm9vY",    "\xC3\xA9xx"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    test_assert(sys_string_parse_base64(bad[i], 0, bytes, sizeof(bytes)) == -1);
  }
  test_assert(sys_string_parse_base64(NULL, 0, bytes, sizeof(bytes)) == -1);
  test_assert(sys_string_base64_encode(NULL, 0, buf, sizeof(buf)) == 0 &&
              buf[0] == '\0');
}
