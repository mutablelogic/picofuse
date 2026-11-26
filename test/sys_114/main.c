#include <picofuse/sys.h>
#include <test/test.h>

typedef struct {
  sys_scanner_class_t isa;
  const char *text;
  size_t runes;
} token_t;

// Scans a string and checks every token
static void scan(const char *str, sys_scanner_flags_t flags,
                 const token_t *tokens, size_t count) {
  sys_iostream_t *s = sys_string_read(str);
  test_assert(s != NULL);
  sys_scanner_t it = sys_scanner_init(s, flags);
  char buf[80];
  for (size_t i = 0; i < count; i++) {
    test_assert(sys_scanner_next(&it));
    size_t n = sys_scanner_token(&it, buf, sizeof(buf) - 1);
    buf[n] = '\0';
    test_assert_strequal(buf, tokens[i].text);
    test_assert(it.isa == tokens[i].isa);
    if (tokens[i].runes > 0) {
      test_assert(it.runes == tokens[i].runes);
    }
  }
  test_assert(!sys_scanner_next(&it));
  sys_iostream_close(s);
}

#define SCAN(str, flags, ...)                                                  \
  do {                                                                         \
    static const token_t tokens[] = {__VA_ARGS__};                             \
    scan(str, flags, tokens, sizeof(tokens) / sizeof(tokens[0]));              \
  } while (0)

#define SP {sys_scanner_space, " ", 1}

test_main_sys(0) {
  sys_scanner_flags_t all = sys_scanner_dates | sys_scanner_durations |
                            sys_scanner_numbers_float | sys_scanner_keywords;

  ///////////////////////////////////////////////////////////////////////
  // Dates, before numbers

  SCAN("at 2026-09-06T11:37:05Z or 2026-09-06 or 2026", all,
       {sys_scanner_keyword, "at", 2}, SP,
       {sys_scanner_date, "2026-09-06T11:37:05Z", 20}, SP,
       {sys_scanner_keyword, "or", 2}, SP, {sys_scanner_date, "2026-09-06", 10},
       SP, {sys_scanner_keyword, "or", 2}, SP, {sys_scanner_number, "2026", 4});
  SCAN("2026-09-06T12:37:05.25+01:00.", all,
       {sys_scanner_date, "2026-09-06T12:37:05.25+01:00", 28},
       {sys_scanner_punct, ".", 1});
  SCAN("2026-09-06,2026-09-07", all, {sys_scanner_date, "2026-09-06", 10},
       {sys_scanner_punct, ",", 1}, {sys_scanner_date, "2026-09-07", 10});

  // Not dates: one which doesn't exist, and one which runs into a word
  SCAN("2026-02-30", all, {sys_scanner_number, "2026", 4},
       {sys_scanner_number, "-02", 3}, {sys_scanner_number, "-30", 3});
  SCAN("2026-09-06x", all, {sys_scanner_number, "2026", 4},
       {sys_scanner_number, "-09", 3}, {sys_scanner_number, "-06", 3},
       {sys_scanner_keyword, "x", 1});

  ///////////////////////////////////////////////////////////////////////
  // Durations, before numbers

  SCAN("wait 1h30m then 1.5s or -250ms, 2d and 5us", all,
       {sys_scanner_keyword, "wait", 4}, SP,
       {sys_scanner_duration, "1h30m", 5}, SP,
       {sys_scanner_keyword, "then", 4}, SP,
       {sys_scanner_duration, "1.5s", 4}, SP, {sys_scanner_keyword, "or", 2},
       SP, {sys_scanner_duration, "-250ms", 6}, {sys_scanner_punct, ",", 1},
       SP, {sys_scanner_duration, "2d", 2}, SP,
       {sys_scanner_keyword, "and", 3}, SP, {sys_scanner_duration, "5us", 3});
  SCAN("1d2h3m4s5ms6us7ns", all,
       {sys_scanner_duration, "1d2h3m4s5ms6us7ns", 17});
  SCAN("3\xC2\xB5s", all, {sys_scanner_duration, "3\xC2\xB5s", 3});

  // Not durations: no unit, a unit which runs into a word, and spaces
  SCAN("1.5 10min 1 s", all, {sys_scanner_number, "1.5", 3}, SP,
       {sys_scanner_number, "10", 2}, {sys_scanner_keyword, "min", 3}, SP,
       {sys_scanner_number, "1", 1}, SP, {sys_scanner_keyword, "s", 1});

  ///////////////////////////////////////////////////////////////////////
  // Only when they're asked for

  SCAN("1h 2026-09-06", sys_scanner_numbers | sys_scanner_keywords,
       {sys_scanner_number, "1", 1}, {sys_scanner_keyword, "h", 1}, SP,
       {sys_scanner_number, "2026", 4}, {sys_scanner_number, "-09", 3},
       {sys_scanner_number, "-06", 3});
  SCAN("1h 2026-09-06", sys_scanner_durations | sys_scanner_numbers,
       {sys_scanner_duration, "1h", 2}, SP, {sys_scanner_number, "2026", 4},
       {sys_scanner_number, "-09", 3}, {sys_scanner_number, "-06", 3});
  SCAN("1h 2026-09-06", sys_scanner_dates,
       {sys_scanner_digit, "1", 1}, {sys_scanner_alpha, "h", 1}, SP,
       {sys_scanner_date, "2026-09-06", 10});

  ///////////////////////////////////////////////////////////////////////
  // A sign or dot on its own, and the end of the stream

  SCAN("- . +x", all, {sys_scanner_punct, "-", 1}, SP,
       {sys_scanner_punct, ".", 1}, SP, {sys_scanner_symbol, "+", 1},
       {sys_scanner_keyword, "x", 1});
  SCAN("5s", all, {sys_scanner_duration, "5s", 2});
}
