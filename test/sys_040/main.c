#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  ///////////////////////////////////////////////////////////////////////
  // NULL is a no-op, doesn't crash

  test_assert(sys_string_to_upper(NULL) == NULL);
  test_assert(sys_string_to_lower(NULL) == NULL);

  ///////////////////////////////////////////////////////////////////////
  // Empty string

  {
    char buf[] = "";
    test_assert(sys_string_to_upper(buf) == buf);
    test_assert_strequal(buf, "");
  }

  ///////////////////////////////////////////////////////////////////////
  // ASCII, in place, non-letters untouched, return value is the same
  // pointer (for chaining)

  {
    char buf[] = "Hello, World! 123";
    char *ret = sys_string_to_upper(buf);
    test_assert(ret == buf);
    test_assert_strequal(buf, "HELLO, WORLD! 123");
  }
  {
    char buf[] = "Hello, World! 123";
    char *ret = sys_string_to_lower(buf);
    test_assert(ret == buf);
    test_assert_strequal(buf, "hello, world! 123");
  }

  ///////////////////////////////////////////////////////////////////////
  // Round trip

  {
    char buf[] = "MixedCase";
    sys_string_to_lower(buf);
    test_assert_strequal(buf, "mixedcase");
    sys_string_to_upper(buf);
    test_assert_strequal(buf, "MIXEDCASE");
  }

  ///////////////////////////////////////////////////////////////////////
  // Latin-1 Supplement: 2-byte UTF-8 rewritten in place, byte length
  // unchanged

  {
    // "café" (e-acute precomposed, C3 A9) -> "CAFÉ" (C3 89)
    char buf[] = "caf\xC3\xA9";
    size_t before = strlen(buf);
    sys_string_to_upper(buf);
    test_assert(strlen(buf) == before);
    test_assert(memcmp(buf, "CAF\xC3\x89", before) == 0);

    sys_string_to_lower(buf);
    test_assert(strlen(buf) == before);
    test_assert(memcmp(buf, "caf\xC3\xA9", before) == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // No single-codepoint uppercase form (sharp s) - left byte-for-byte
  // unchanged, not corrupted

  {
    char buf[] = "\xC3\x9F"; // sharp s, U+00DF
    char copy[sizeof(buf)];
    memcpy(copy, buf, sizeof(buf));
    sys_string_to_upper(buf);
    test_assert(memcmp(buf, copy, sizeof(buf)) == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // Malformed bytes pass through untouched, letters around them still
  // convert

  {
    char buf[] = "ab\x80"
                 "cd";
    sys_string_to_upper(buf);
    test_assert(memcmp(buf, "AB\x80"
                            "CD",
                        5) == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // Idempotence

  {
    char buf[] = "Already UPPER 123";
    sys_string_to_upper(buf);
    char once[sizeof(buf)];
    memcpy(once, buf, sizeof(buf));
    sys_string_to_upper(buf);
    test_assert(memcmp(buf, once, sizeof(buf)) == 0);
  }

  sys_exit();
  return 0;
}
