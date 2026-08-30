#include <picofuse/sys.h>
#include <test/test.h>

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  ///////////////////////////////////////////////////////////////////////
  // sys_string_bytes

  test_assert(sys_string_bytes(NULL) == 0);
  test_assert(sys_string_bytes("") == 0);
  test_assert(sys_string_bytes("hello") == 5);

  // Byte length counts UTF-8 bytes, not runes.
  test_assert(sys_string_bytes("caf\xC3\xA9") == 5); // "café", 4 runes
  test_assert(sys_string_bytes("\xC3\x80\xC3\x89") == 4); // "ÀÉ", 2 runes

  // Malformed bytes still count as bytes - this is a raw byte length, not
  // a validity check.
  test_assert(sys_string_bytes("\x80\x80\x80") == 3);

  ///////////////////////////////////////////////////////////////////////
  // sys_string_runes - thin wrapper over sys_rune_count()

  test_assert(sys_string_runes(NULL) == 0);
  test_assert(sys_string_runes("") == 0);
  test_assert(sys_string_runes("hello") == 5);
  test_assert(sys_string_runes("caf\xC3\xA9") == 4); // "café"
  test_assert(sys_string_runes("\x80\x80\x80") == 3); // each byte its own error rune

  // Cross-check against sys_rune_count() directly for a spread of inputs.
  static const char *fixtures[] = {
      NULL,          "",   "a",
      "hello world", "\xE2\x82\xAC\xF0\x9F\x98\x80", "ab\x80without",
  };
  for (size_t i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); i++) {
    test_assert(sys_string_runes(fixtures[i]) == sys_rune_count(fixtures[i]));
  }

  sys_exit();
  return 0;
}
