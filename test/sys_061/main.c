#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // Exact matches

  {
    bool out = false;
    test_assert(sys_string_parse_bool("true", 0, &out) == true);
    test_assert(out == true);
  }
  {
    bool out = true;
    test_assert(sys_string_parse_bool("false", 0, &out) == true);
    test_assert(out == false);
  }

  ///////////////////////////////////////////////////////////////////////
  // Anything else is a parse error - wrong case, trailing/leading
  // content, empty, and NULL all included.

  {
    bool out = false;
    test_assert(sys_string_parse_bool("True", 0, &out) == false);
    test_assert(sys_string_parse_bool("FALSE", 0, &out) == false);
    test_assert(sys_string_parse_bool("truthy", 0, &out) == false);
    test_assert(sys_string_parse_bool("true ", 0, &out) == false);
    test_assert(sys_string_parse_bool(" true", 0, &out) == false);
    test_assert(sys_string_parse_bool("falsee", 0, &out) == false);
    test_assert(sys_string_parse_bool("", 0, &out) == false);
    test_assert(sys_string_parse_bool(NULL, 0, &out) == false);
    test_assert(sys_string_parse_bool("1", 0, &out) == false);
    test_assert(sys_string_parse_bool("yes", 0, &out) == false);
  }

  // *out is left untouched on a parse error.
  {
    bool out = true;
    test_assert(sys_string_parse_bool("nope", 0, &out) == false);
    test_assert(out == true); // unchanged
  }
  {
    bool out = false;
    test_assert(sys_string_parse_bool("nope", 0, &out) == false);
    test_assert(out == false); // unchanged
  }

  // A NULL out pointer doesn't crash.
  test_assert(sys_string_parse_bool("true", 0, NULL) == true);
  test_assert(sys_string_parse_bool("false", 0, NULL) == true);
  test_assert(sys_string_parse_bool("nope", 0, NULL) == false);

  ///////////////////////////////////////////////////////////////////////
  // With a length: str need not be NUL-terminated, and nothing else may
  // be within len, including a zero byte (#38)

  {
    bool out = false;
    test_assert(sys_string_parse_bool("true!", 4, &out) == true);
    test_assert(out == true);
    test_assert(sys_string_parse_bool("falsetto", 5, &out) == true);
    test_assert(out == false);
    test_assert(sys_string_parse_bool("true!", 5, &out) == false);
    test_assert(sys_string_parse_bool("true\0junk", 9, &out) == false);
    test_assert(sys_string_parse_bool("true\0", 5, &out) == false);
    test_assert(sys_string_parse_bool("tru", 3, &out) == false);
    test_assert(sys_string_parse_bool(NULL, 4, &out) == false);

    // Not NUL-terminated at all: nothing past len is read
    const char bytes[] = {'f', 'a', 'l', 's', 'e'};
    out = true;
    test_assert(sys_string_parse_bool(bytes, sizeof(bytes), &out) == true);
    test_assert(out == false);
  }
}
