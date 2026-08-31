#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // NULL / not an escape at all - len 0 throughout this section (NUL-
  // terminated mode, not checking an exact length)

  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape(NULL, 0, &r) == false);
    test_assert(r == RUNE_ERROR);
  }
  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("n", 0, &r) == false); // no leading backslash
    test_assert(r == RUNE_ERROR);
  }
  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("", 0, &r) == false);
    test_assert(r == RUNE_ERROR);
  }

  // A lone backslash at the end of the string.
  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\", 0, &r) == false);
    test_assert(r == RUNE_ERROR);
  }

  // An unrecognized escape character.
  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\q", 0, &r) == false);
    test_assert(r == RUNE_ERROR);
  }

  // A NULL rune pointer doesn't crash.
  test_assert(sys_string_parse_escape("\\n", 0, NULL) == true);
  test_assert(sys_string_parse_escape("\\q", 0, NULL) == false);

  ///////////////////////////////////////////////////////////////////////
  // Every single-character JSON escape

  {
    struct {
      const char *str;
      rune_t want;
    } cases[] = {
        {"\\\"", '"'},  {"\\\\", '\\'}, {"\\/", '/'},  {"\\b", '\b'},
        {"\\f", '\f'},  {"\\n", '\n'},  {"\\r", '\r'}, {"\\t", '\t'},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      rune_t r = 0;
      test_assert(sys_string_parse_escape(cases[i].str, 0, &r) == true);
      test_assert(r == cases[i].want);
    }
  }

  ///////////////////////////////////////////////////////////////////////
  // \uXXXX - exactly 4 hex digits, either case

  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\u0041", 0, &r) == true);
    test_assert(r == 0x0041);
  }
  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\uABCD", 0, &r) == true);
    test_assert(r == 0xABCD);
  }
  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\uabcd", 0, &r) == true);
    test_assert(r == 0xABCD);
  }
  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\uAbC1", 0, &r) == true);
    test_assert(r == 0xABC1);
  }

  ///////////////////////////////////////////////////////////////////////
  // Malformed \u

  {
    // Only 2 hex digits, then the string ends.
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\u12", 0, &r) == false);
    test_assert(r == RUNE_ERROR);
  }
  {
    // A non-hex character among the 4 digits.
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\uZZZZ", 0, &r) == false);
    test_assert(r == RUNE_ERROR);
  }
  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\u", 0, &r) == false);
    test_assert(r == RUNE_ERROR);
  }

  ///////////////////////////////////////////////////////////////////////
  // Lone UTF-16 surrogates (D800-DFFF) are rejected - not a valid
  // standalone rune - but the codepoints immediately outside that range
  // are fine.

  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\uD800", 0, &r) == false); // first high surrogate
    test_assert(r == RUNE_ERROR);
  }
  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\uDFFF", 0, &r) == false); // last low surrogate
    test_assert(r == RUNE_ERROR);
  }
  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\uD7FF", 0, &r) == true); // just below
    test_assert(r == 0xD7FF);
  }
  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\uE000", 0, &r) == true); // just above
    test_assert(r == 0xE000);
  }

  ///////////////////////////////////////////////////////////////////////
  // len == 0: str's own NUL terminator stands in for len - str must
  // still contain exactly one escape and nothing else, same as with an
  // explicit len. Trailing content is a parse error, not ignored.

  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\nrest", 0, &r) == false);
    test_assert(r == RUNE_ERROR);
  }
  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\u0041rest", 0, &r) == false);
    test_assert(r == RUNE_ERROR);
  }
  {
    // The motivating case: "\n hello!" is not just "\n".
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\n hello!", 0, &r) == false);
    test_assert(r == RUNE_ERROR);
  }

  ///////////////////////////////////////////////////////////////////////
  // len > 0: the escape must consume exactly len bytes - same rule,
  // just against an explicit length instead of str's own terminator.

  {
    // The exact length of the escape itself still succeeds.
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\n", 2, &r) == true);
    test_assert(r == '\n');
  }
  {
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\u0041", 6, &r) == true);
    test_assert(r == 0x0041);
  }
  {
    // "\n hello!" as a whole (9 bytes) is not exactly one escape.
    rune_t r = 0;
    const char *s = "\\n hello!";
    test_assert(sys_string_parse_escape(s, sys_string_bytes(s), &r) == false);
    test_assert(r == RUNE_ERROR);
  }
  {
    // Too short: the \u form needs all 6 bytes.
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\u0041", 5, &r) == false);
    test_assert(r == RUNE_ERROR);
  }
  {
    // Too short: a single-character escape needs both bytes.
    rune_t r = 0;
    test_assert(sys_string_parse_escape("\\n", 1, &r) == false);
    test_assert(r == RUNE_ERROR);
  }
  {
    // A len that doesn't reach the 4th hex digit isn't read past -
    // rejected on length before any of the digits are even inspected,
    // so this can't read past a short, non-NUL-terminated buffer either.
    char buf[3] = {'\\', 'u', '1'}; // deliberately not NUL-terminated
    rune_t r = 0;
    test_assert(sys_string_parse_escape(buf, 3, &r) == false);
    test_assert(r == RUNE_ERROR);
  }

}
