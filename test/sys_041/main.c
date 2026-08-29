#include <picofuse/sys.h>
#include <test/test.h>

int main(void) {
  sys_init();

  ///////////////////////////////////////////////////////////////////////
  // NULL / empty / all-whitespace

  test_assert(sys_string_trimspace(NULL) == NULL);

  {
    char buf[] = "";
    test_assert(sys_string_trimspace(buf) == buf);
    test_assert_strequal(buf, "");
  }
  {
    char buf[] = "   ";
    char *ret = sys_string_trimspace(buf);
    test_assert_strequal(ret, "");
  }

  ///////////////////////////////////////////////////////////////////////
  // No whitespace to trim - returned pointer equals the input pointer

  {
    char buf[] = "hello";
    test_assert(sys_string_trimspace(buf) == buf);
    test_assert_strequal(buf, "hello");
  }

  ///////////////////////////////////////////////////////////////////////
  // Leading only / trailing only / both

  {
    char buf[] = "  hello";
    char *ret = sys_string_trimspace(buf);
    test_assert(ret == buf); // same pointer - content shifted down, not advanced
    test_assert_strequal(ret, "hello");
  }
  {
    char buf[] = "hello  ";
    test_assert_strequal(sys_string_trimspace(buf), "hello");
  }
  {
    char buf[] = "  hello  ";
    char *ret = sys_string_trimspace(buf);
    test_assert(ret == buf);
    test_assert_strequal(ret, "hello");
  }

  ///////////////////////////////////////////////////////////////////////
  // Other whitespace runes, and internal whitespace preserved

  {
    char buf[] = "\t\n hello world \r\v";
    test_assert_strequal(sys_string_trimspace(buf), "hello world");
  }

  ///////////////////////////////////////////////////////////////////////
  // Multi-byte whitespace (NBSP, U+00A0, encoded C2 A0) trimmed
  // correctly - this only works if trimming decodes runes rather than
  // stripping single bytes >= 0x80.

  {
    char buf[] = "\xC2\xA0hello\xC2\xA0";
    char *ret = sys_string_trimspace(buf);
    test_assert(ret == buf);
    test_assert_strequal(ret, "hello");
  }

  ///////////////////////////////////////////////////////////////////////
  // A malformed byte between whitespace is content, not whitespace - it
  // survives the trim untouched.

  {
    char buf[] = " \x80 ";
    char *ret = sys_string_trimspace(buf);
    test_assert(ret == buf);
    test_assert(sys_string_bytes(ret) == 1);
    test_assert((uint8_t)ret[0] == 0x80);
  }

  ///////////////////////////////////////////////////////////////////////
  // Idempotence

  {
    char buf[] = "  hello  ";
    char *once = sys_string_trimspace(buf);
    char *twice = sys_string_trimspace(once);
    test_assert(once == twice);
    test_assert_strequal(twice, "hello");
  }

  sys_exit();
  return 0;
}
