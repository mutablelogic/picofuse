#include <picofuse/sys.h>
#include <test/test.h>

#define NS INT64_C(1)
#define US (1000 * NS)
#define MS (1000 * US)
#define S (1000 * MS)
#define M (60 * S)
#define H (60 * M)
#define D (24 * H)

static int64_t parse(const char *str) {
  int64_t ns = 12345;
  test_assert(sys_string_parse_duration(str, 0, &ns));
  return ns;
}

static void invalid(const char *str) {
  int64_t ns = 12345;
  test_assert(!sys_string_parse_duration(str, 0, &ns));
  test_assert(ns == 12345);
}

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // Every unit

  test_assert(parse("1ns") == NS);
  test_assert(parse("1us") == US);
  test_assert(parse("1\xC2\xB5s") == US);
  test_assert(parse("1ms") == MS);
  test_assert(parse("1s") == S);
  test_assert(parse("1m") == M);
  test_assert(parse("1h") == H);
  test_assert(parse("1d") == D);
  test_assert(parse("1day") == D);
  test_assert(parse("2days") == 2 * D);
  test_assert(parse("0s") == 0);

  ///////////////////////////////////////////////////////////////////////
  // More than one, with and without spaces, as NXDurationDescription
  // writes them

  test_assert(parse("1h30m") == H + 30 * M);
  test_assert(parse("1 day 2h 3m 4s 5ms 6us 7ns") == INT64_C(93784005006007));
  test_assert(parse("2 days 1h") == 2 * D + H);
  test_assert(parse("1h 1h") == 2 * H);
  test_assert(parse("90 s") == 90 * S);

  ///////////////////////////////////////////////////////////////////////
  // Fractions, and signs

  test_assert(parse("1.5s") == 1500 * MS);
  test_assert(parse(".5s") == 500 * MS);
  test_assert(parse("1.s") == S);
  test_assert(parse("0.000000001s") == NS);
  test_assert(parse("0.0000000019s") == NS);
  test_assert(parse("1.5ns") == NS);
  test_assert(parse("2.25h") == 2 * H + 15 * M);
  test_assert(parse("-250ms") == -250 * MS);
  test_assert(parse("+1m") == M);
  test_assert(parse("-1h30m") == -(H + 30 * M));
  test_assert(parse("-0s") == 0);

  ///////////////////////////////////////////////////////////////////////
  // The limits

  test_assert(parse("9223372036854775807ns") == INT64_MAX);
  test_assert(parse("-9223372036854775808ns") == INT64_MIN);
  test_assert(parse("106751d") == 106751 * D);
  invalid("9223372036854775808ns");
  invalid("-9223372036854775809ns");
  invalid("106752d");
  invalid("106751d 106751d");
  invalid("99999999999999999999ns");

  ///////////////////////////////////////////////////////////////////////
  // With a length

  int64_t ns = 0;
  test_assert(sys_string_parse_duration("1h30m", 2, &ns) && ns == H);
  test_assert(!sys_string_parse_duration("1h30m", 4, &ns) && ns == H);

  ///////////////////////////////////////////////////////////////////////
  // Those which aren't durations

  const char *bad[] = {"",     " ",    "1",     "s",    "1x",    "1 s ",
                       " 1s",  "1min", "1sec",  "1hours", "1H",  "1S",
                       "-",    "+",    "--1s",  "1s-1s", ".s",   "1..5s",
                       "1.5",  "1,5s", "1h 30", "1h,30m", "1 2s", "1 d ay",
                       "0x1s", "1e3s", "\xC2\xB5s"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    invalid(bad[i]);
  }
  test_assert(!sys_string_parse_duration(NULL, 0, &ns));
  test_assert(!sys_string_parse_duration("1s", 0, NULL));
}
