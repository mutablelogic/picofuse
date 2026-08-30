#include <picofuse/sys.h>
#include <test/test.h>

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  // Empty substring is found at offset 0, always, matching Go's
  // strings.Index semantics.
  test_assert(sys_string_contains(NULL, NULL) == 0);
  test_assert(sys_string_contains("", "") == 0);
  test_assert(sys_string_contains("hello", "") == 0);
  test_assert(sys_string_contains("hello", NULL) == 0);

  // NULL haystack is "" - never contains a non-empty substring.
  test_assert(sys_string_contains(NULL, "x") == -1);
  test_assert(sys_string_contains("", "x") == -1);

  // Not found.
  test_assert(sys_string_contains("hello", "world") == -1);
  test_assert(sys_string_contains("hello", "hello!") == -1); // longer than s

  // Found at the start, middle, end, and as the whole string.
  test_assert(sys_string_contains("hello world", "hello") == 0);
  test_assert(sys_string_contains("hello world", "lo wo") == 3);
  test_assert(sys_string_contains("hello world", "world") == 6);
  test_assert(sys_string_contains("hello", "hello") == 0);

  // First occurrence wins when the substring repeats.
  test_assert(sys_string_contains("abcabc", "bc") == 1);

  // Case-sensitive, byte-level match.
  test_assert(sys_string_contains("Hello", "hello") == -1);

  // Multi-byte substring: the returned offset is a byte offset, not a
  // rune index.
  test_assert(sys_string_contains("caf\xC3\xA9lait", "\xC3\xA9") == 3);
  test_assert(sys_string_contains("caf\xC3\xA9lait", "lait") == 5);

  sys_exit();
  return 0;
}
