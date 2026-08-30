#include <picofuse/sys.h>
#include <test/test.h>

// Decodes one rune from str and checks both the decoded value and how far
// the returned pointer advanced. expect_advance == -1 means "expect NULL
// back" (end of string).
static void check(const char *label, const char *str, rune_t expect_rune,
                   long expect_advance) {
  rune_t r = 0x12345678; // poison, so a forgotten *rune write is caught
  const char *p = sys_rune_next(str, &r);
  long advance = p ? (long)(p - str) : -1;
  if (r != expect_rune || advance != expect_advance) {
    sys_panicf("[TEST] FAIL: %s - rune=0x%lx (want 0x%lx) advance=%ld "
               "(want %ld)",
               label, (long)r, (long)expect_rune, advance, expect_advance);
  }
}

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  ///////////////////////////////////////////////////////////////////////
  // NULL / empty-string handling

  check("NULL str", NULL, 0, -1);
  check("empty string", "", 0, -1);

  {
    // NULL rune pointer must not crash on the NULL-str/end-of-string path.
    const char *p = sys_rune_next(NULL, NULL);
    test_assert(p == NULL);
    p = sys_rune_next("", NULL);
    test_assert(p == NULL);
  }

  ///////////////////////////////////////////////////////////////////////
  // ASCII fast path (0x00 excluded - that's end of string above)

  check("ASCII 0x01", "\x01", 0x01, 1);
  check("ASCII 'A'", "A", 'A', 1);
  check("ASCII max 0x7F", "\x7F", 0x7F, 1);

  {
    // NULL rune pointer on the ASCII fast path.
    const char *p = sys_rune_next("A", NULL);
    test_assert(p != NULL);
  }

  ///////////////////////////////////////////////////////////////////////
  // Stray continuation bytes (0x80-0xBF with no lead byte): each one is
  // its own single-byte error.

  check("stray continuation 0x80", "\x80", RUNE_ERROR, 1);
  check("stray continuation 0xBF", "\xBF", RUNE_ERROR, 1);

  ///////////////////////////////////////////////////////////////////////
  // Overlong 2-byte leads 0xC0/0xC1 (always invalid, regardless of what
  // follows)

  check("overlong 2-byte lead 0xC0", "\xC0\x80", RUNE_ERROR, 1);
  check("overlong 2-byte lead 0xC1", "\xC1\xBF", RUNE_ERROR, 1);

  ///////////////////////////////////////////////////////////////////////
  // Valid 2-byte sequences, including both boundaries of the range

  check("2-byte min U+0080", "\xC2\x80", 0x0080, 2);
  check("2-byte max U+07FF", "\xDF\xBF", 0x07FF, 2);
  check("2-byte mid (pound sign)", "\xC2\xA3", 0x00A3, 2);

  ///////////////////////////////////////////////////////////////////////
  // 2-byte sequences with a bad continuation byte

  check("2-byte cont too low (ASCII)", "\xC2\x7F", RUNE_ERROR, 1);
  check("2-byte cont too high", "\xC2\xC0", RUNE_ERROR, 1);
  check("2-byte cont is another lead", "\xC2\xC2", RUNE_ERROR, 1);

  ///////////////////////////////////////////////////////////////////////
  // Truncated 2-byte sequence (lead byte at end of string)

  check("2-byte truncated", "\xC2", RUNE_ERROR, 1);

  ///////////////////////////////////////////////////////////////////////
  // Valid 3-byte sequences: generic lead, and the E0/ED special cases

  check("3-byte generic (euro sign)", "\xE2\x82\xAC", 0x20AC, 3);
  check("3-byte min U+0800 (E0 boundary)", "\xE0\xA0\x80", 0x0800, 3);
  check("3-byte max before surrogates U+D7FF (ED boundary)", "\xED\x9F\xBF",
        0xD7FF, 3);
  check("3-byte just after surrogates U+E000", "\xEE\x80\x80", 0xE000, 3);
  check("3-byte max U+FFFF", "\xEF\xBF\xBF", 0xFFFF, 3);

  ///////////////////////////////////////////////////////////////////////
  // 3-byte overlong (E0 with b1 < 0xA0) and surrogate (ED with b1 >= 0xA0)
  // rejection

  check("3-byte overlong E0 80 80", "\xE0\x80\x80", RUNE_ERROR, 1);
  check("3-byte overlong E0 9F BF", "\xE0\x9F\xBF", RUNE_ERROR, 1);
  check("3-byte surrogate D800 (ED A0 80)", "\xED\xA0\x80", RUNE_ERROR, 1);
  check("3-byte surrogate DFFF (ED BF BF)", "\xED\xBF\xBF", RUNE_ERROR, 1);

  ///////////////////////////////////////////////////////////////////////
  // 3-byte sequences with a bad first continuation byte (generic lead, so
  // full 0x80-0xBF range applies)

  check("3-byte b1 too low", "\xE1\x7F\x80", RUNE_ERROR, 1);
  check("3-byte b1 too high", "\xE1\xC0\x80", RUNE_ERROR, 1);

  ///////////////////////////////////////////////////////////////////////
  // 3-byte sequences with a bad second continuation byte

  check("3-byte b2 too low", "\xE1\x80\x7F", RUNE_ERROR, 1);
  check("3-byte b2 too high", "\xE1\x80\xC0", RUNE_ERROR, 1);

  ///////////////////////////////////////////////////////////////////////
  // Truncated 3-byte sequences (lead only, and lead + 1 continuation)

  check("3-byte truncated after lead", "\xE1", RUNE_ERROR, 1);
  check("3-byte truncated after 1 continuation", "\xE1\x80", RUNE_ERROR, 1);

  ///////////////////////////////////////////////////////////////////////
  // Valid 4-byte sequences: generic lead, and the F0/F4 special cases

  check("4-byte generic (grinning face U+1F600)", "\xF0\x9F\x98\x80",
        0x1F600, 4);
  check("4-byte min U+10000 (F0 boundary)", "\xF0\x90\x80\x80", 0x10000, 4);
  check("4-byte max U+10FFFF (F4 boundary)", "\xF4\x8F\xBF\xBF", 0x10FFFF,
        4);

  ///////////////////////////////////////////////////////////////////////
  // 4-byte overlong (F0 with b1 < 0x90) and over-max (F4 with b1 > 0x8F)
  // rejection

  check("4-byte overlong F0 80 80 80", "\xF0\x80\x80\x80", RUNE_ERROR, 1);
  check("4-byte overlong F0 8F BF BF", "\xF0\x8F\xBF\xBF", RUNE_ERROR, 1);
  check("4-byte over max F4 90 80 80", "\xF4\x90\x80\x80", RUNE_ERROR, 1);
  check("4-byte over max F4 BF BF BF", "\xF4\xBF\xBF\xBF", RUNE_ERROR, 1);

  ///////////////////////////////////////////////////////////////////////
  // 4-byte sequences with a bad continuation byte at each position

  check("4-byte b1 too low", "\xF1\x7F\x80\x80", RUNE_ERROR, 1);
  check("4-byte b1 too high", "\xF1\xC0\x80\x80", RUNE_ERROR, 1);
  check("4-byte b2 too low", "\xF1\x80\x7F\x80", RUNE_ERROR, 1);
  check("4-byte b2 too high", "\xF1\x80\xC0\x80", RUNE_ERROR, 1);
  check("4-byte b3 too low", "\xF1\x80\x80\x7F", RUNE_ERROR, 1);
  check("4-byte b3 too high", "\xF1\x80\x80\xC0", RUNE_ERROR, 1);

  ///////////////////////////////////////////////////////////////////////
  // Truncated 4-byte sequences (lead only, +1 continuation, +2
  // continuations)

  check("4-byte truncated after lead", "\xF1", RUNE_ERROR, 1);
  check("4-byte truncated after 1 continuation", "\xF1\x80", RUNE_ERROR, 1);
  check("4-byte truncated after 2 continuations", "\xF1\x80\x80", RUNE_ERROR,
        1);

  ///////////////////////////////////////////////////////////////////////
  // Invalid lead bytes 0xF5-0xFF (beyond the U+10FFFF ceiling by
  // construction)

  check("invalid lead 0xF5", "\xF5\x80\x80\x80", RUNE_ERROR, 1);
  check("invalid lead 0xFE", "\xFE\x80\x80\x80", RUNE_ERROR, 1);
  check("invalid lead 0xFF", "\xFF\x80\x80\x80", RUNE_ERROR, 1);

  ///////////////////////////////////////////////////////////////////////
  // Error recovery consumes 1 byte at a time, even mid-sequence: verify a
  // bad 3rd byte re-syncs by reprocessing the earlier continuation bytes
  // as their own stray-continuation errors.

  {
    const char *s = "\xE0\xA0\x41"; // valid lead+cont1, then ASCII 'A'
    rune_t r;
    const char *p = s;

    p = sys_rune_next(p, &r);
    test_assert(p == s + 1 && r == RUNE_ERROR); // E0 alone: bad b2

    p = sys_rune_next(p, &r);
    test_assert(p == s + 2 && r == RUNE_ERROR); // A0: stray continuation

    p = sys_rune_next(p, &r);
    test_assert(p == s + 3 && r == 'A'); // 'A': back to normal ASCII

    p = sys_rune_next(p, &r);
    test_assert(p == NULL && r == 0); // end of string
  }

  ///////////////////////////////////////////////////////////////////////
  // Full mixed string: ASCII, 2/3/4-byte runes and an embedded invalid
  // byte, walked end to end to confirm total consumption reaches the
  // terminator with no over/under-run.

  {
    // "A" + pound + euro + grinning-face + stray-continuation + "z"
    const char *s = "A\xC2\xA3\xE2\x82\xAC\xF0\x9F\x98\x80\x80z";
    rune_t expect[] = {'A', 0x00A3, 0x20AC, 0x1F600, RUNE_ERROR, 'z'};
    const char *p = s;
    for (size_t i = 0; i < sizeof(expect) / sizeof(expect[0]); i++) {
      rune_t r;
      p = sys_rune_next(p, &r);
      test_assert(p != NULL);
      test_assert(r == expect[i]);
    }
    // Exactly at the terminator now.
    rune_t r;
    p = sys_rune_next(p, &r);
    test_assert(p == NULL && r == 0);
  }

  sys_exit();
  return 0;
}
