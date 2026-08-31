#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // sys_rune_count

  test_assert(sys_rune_count(NULL) == 0);
  test_assert(sys_rune_count("") == 0);
  test_assert(sys_rune_count("A") == 1);
  test_assert(sys_rune_count("hello") == 5);

  // Multi-byte runes each count as one, not one per byte.
  test_assert(sys_rune_count("\xC2\xA3") == 1);             // pound (2 bytes)
  test_assert(sys_rune_count("\xE2\x82\xAC") == 1);          // euro (3 bytes)
  test_assert(sys_rune_count("\xF0\x9F\x98\x80") == 1);      // emoji (4 bytes)
  test_assert(sys_rune_count("caf\xC3\xA9") == 4);            // "café" (é precomposed)

  // Malformed bytes are each counted individually, one rune per error
  // step - matches walking the string with sys_rune_next().
  test_assert(sys_rune_count("\x80\x80\x80") == 3);
  test_assert(sys_rune_count("\xC0\x80") == 2); // overlong lead + its continuation, resynced separately

  // Mixed valid/invalid: 'A', £, €, emoji, one stray byte, 'z' -> 6 runes
  test_assert(sys_rune_count("A\xC2\xA3\xE2\x82\xAC\xF0\x9F\x98\x80\x80z") ==
               6);

  ///////////////////////////////////////////////////////////////////////
  // sys_rune_valid

  test_assert(sys_rune_valid(NULL) == true);
  test_assert(sys_rune_valid("") == true);
  test_assert(sys_rune_valid("hello world") == true);
  test_assert(sys_rune_valid("caf\xC3\xA9") == true);
  test_assert(sys_rune_valid("\xE2\x82\xAC \xF0\x9F\x98\x80") == true);

  // A legitimately-encoded U+FFFD (EF BF BD) must NOT be mistaken for a
  // decode error - this is the specific case sys_rune_valid has to get
  // right, since RUNE_ERROR's value IS U+FFFD.
  test_assert(sys_rune_valid("\xEF\xBF\xBD") == true);
  test_assert(sys_rune_valid("a\xEF\xBF\xBD"
                              "b") == true);

  // Stray continuation byte / overlong / surrogate / truncated / bad lead
  test_assert(sys_rune_valid("\x80") == false);
  test_assert(sys_rune_valid("\xC0\x80") == false);
  test_assert(sys_rune_valid("\xE0\x80\x80") == false);
  test_assert(sys_rune_valid("\xED\xA0\x80") == false);
  test_assert(sys_rune_valid("\xC2") == false);
  test_assert(sys_rune_valid("\xF1\x80\x80") == false);
  test_assert(sys_rune_valid("\xFF") == false);

  // A valid prefix doesn't hide an invalid suffix.
  test_assert(sys_rune_valid("hello\x80") == false);
  test_assert(sys_rune_valid("\xC2\xA3\xE2\x82\xAC\x80") == false);

}
