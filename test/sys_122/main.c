#include <picofuse/sys.h>
#include <test/test.h>

// sys_env_arg_error(): why sys_env_arg_parse() failed, and NULL when it didn't
static void expect_error(int argc, char *argv[], const char *error) {
  sys_env_arg_flag_t flags[] = {
      {.long_name = "count", .short_name = "c", .type = sys_env_arg_type_int, .value = "0"},
      {.long_name = "ratio", .short_name = NULL, .type = sys_env_arg_type_float, .value = NULL},
      {.long_name = "on", .short_name = NULL, .type = sys_env_arg_type_bool, .value = NULL},
      {.long_name = "at", .short_name = NULL, .type = sys_env_arg_type_date, .value = NULL},
      {.long_name = "for", .short_name = NULL, .type = sys_env_arg_type_duration, .value = NULL},
      {0},
  };
  sys_env_set_args(argc, argv);
  test_assert(sys_env_arg_parse(flags) == NULL);
  test_assert(sys_env_arg_error() != NULL);
  test_assert_strequal(sys_env_arg_error(), error);
}

test_main_sys(0) {
  // None yet, and none after a parse which succeeds
  test_assert(sys_env_arg_error() == NULL);
  {
    char *argv[] = {"prog", "--count=3", "rest"};
    sys_env_arg_flag_t flags[] = {
        {.long_name = "count", .short_name = "c", .type = sys_env_arg_type_int, .value = "0"},
        {0},
    };
    sys_env_set_args(3, argv);
    test_assert(sys_env_arg_parse(flags) != NULL && sys_env_arg_error() == NULL);
  }

  // Each reason
  {
    char *argv[] = {"prog", "--size=3"};
    expect_error(2, argv, "unknown flag --size=3");
  }
  {
    char *argv[] = {"prog", "-x"};
    expect_error(2, argv, "unknown flag -x");
  }
  {
    char *argv[] = {"prog", "--count"};
    expect_error(2, argv, "--count needs a value");
  }
  {
    char *argv[] = {"prog", "-c", "abc"};
    expect_error(3, argv, "--count: \"abc\" isn't an integer");
  }
  {
    char *argv[] = {"prog", "--ratio=x"};
    expect_error(2, argv, "--ratio: \"x\" isn't a number");
  }
  {
    char *argv[] = {"prog", "--on=maybe"};
    expect_error(2, argv, "--on: \"maybe\" isn't true or false");
  }
  {
    char *argv[] = {"prog", "--at=yesterday"};
    expect_error(2, argv, "--at: \"yesterday\" isn't an ISO 8601 date");
  }
  {
    char *argv[] = {"prog", "--for", "10 minutes"};
    expect_error(3, argv, "--for: \"10 minutes\" isn't a duration");
  }
  test_assert(sys_env_arg_parse(NULL) == NULL);
  test_assert_strequal(sys_env_arg_error(), "there are no flags");

  // Dates and durations, with their defaults
  {
    char *argv[] = {"prog", "--at", "2026-09-06T11:37:05Z", "--for=1h30m"};
    sys_env_arg_flag_t flags[] = {
        {.long_name = "at", .short_name = NULL, .type = sys_env_arg_type_date, .value = NULL},
        {.long_name = "for", .short_name = NULL, .type = sys_env_arg_type_duration, .value = NULL},
        {.long_name = "since", .short_name = NULL, .type = sys_env_arg_type_date,
         .value = "2026-01-01"},
        {.long_name = "wait", .short_name = "w", .type = sys_env_arg_type_duration,
         .value = "500ms"},
        {0},
    };
    sys_env_set_args(4, argv);
    sys_env_arg_t *args = sys_env_arg_parse(flags);
    test_assert(args != NULL);
    sys_date_t at = {0}, since = {0};
    int64_t ns = 0;
    test_assert(sys_env_arg_parse_date(args, "at", &at) && at.seconds == 1788694625);
    test_assert(sys_env_arg_parse_duration(args, "for", &ns) && ns == 5400000000000LL);
    test_assert(sys_env_arg_parse_date(args, "since", &since) && since.seconds == 1767225600);
    test_assert(sys_env_arg_parse_duration(args, "w", &ns) && ns == 500000000LL);
    test_assert(!sys_env_arg_parse_date(args, "nothing", &at));
    test_assert(!sys_env_arg_parse_duration(args, "nothing", &ns));
  }

  // Cleared by the next parse which succeeds
  {
    char *argv[] = {"prog"};
    sys_env_arg_flag_t flags[] = {{0}};
    sys_env_set_args(1, argv);
    test_assert(sys_env_arg_parse(flags) != NULL && sys_env_arg_error() == NULL);
  }
}
