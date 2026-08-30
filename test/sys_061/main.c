#include <picofuse/sys.h>
#include <test/test.h>

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  ///////////////////////////////////////////////////////////////////////
  // Exact matches

  {
    bool out = false;
    test_assert(sys_string_parse_bool("true", &out) == true);
    test_assert(out == true);
  }
  {
    bool out = true;
    test_assert(sys_string_parse_bool("false", &out) == true);
    test_assert(out == false);
  }

  ///////////////////////////////////////////////////////////////////////
  // Anything else is a parse error - wrong case, trailing/leading
  // content, empty, and NULL all included.

  {
    bool out = false;
    test_assert(sys_string_parse_bool("True", &out) == false);
    test_assert(sys_string_parse_bool("FALSE", &out) == false);
    test_assert(sys_string_parse_bool("truthy", &out) == false);
    test_assert(sys_string_parse_bool("true ", &out) == false);
    test_assert(sys_string_parse_bool(" true", &out) == false);
    test_assert(sys_string_parse_bool("falsee", &out) == false);
    test_assert(sys_string_parse_bool("", &out) == false);
    test_assert(sys_string_parse_bool(NULL, &out) == false);
    test_assert(sys_string_parse_bool("1", &out) == false);
    test_assert(sys_string_parse_bool("yes", &out) == false);
  }

  // *out is left untouched on a parse error.
  {
    bool out = true;
    test_assert(sys_string_parse_bool("nope", &out) == false);
    test_assert(out == true); // unchanged
  }
  {
    bool out = false;
    test_assert(sys_string_parse_bool("nope", &out) == false);
    test_assert(out == false); // unchanged
  }

  // A NULL out pointer doesn't crash.
  test_assert(sys_string_parse_bool("true", NULL) == true);
  test_assert(sys_string_parse_bool("false", NULL) == true);
  test_assert(sys_string_parse_bool("nope", NULL) == false);

  sys_exit();
  return 0;
}
