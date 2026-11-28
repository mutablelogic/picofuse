#include <picofuse/sys.h>
#include <test/test.h>

// Without a command line, as on a Pico: nothing parses, and the error says why only
// once a parse has been tried
test_main_sys(0) {
  test_assert(sys_env_arg_error() == NULL);

  // A command line which is set is still not parsed
  char *line[] = {"prog", "--when", "2026-09-06", NULL};
  sys_env_set_args(3, line);

  sys_env_arg_flag_t flags[] = {
      {.long_name = "when", .type = sys_env_arg_type_date},
      {.long_name = "every", .type = sys_env_arg_type_duration},
      {0},
  };
  sys_env_arg_t *args = sys_env_arg_parse(flags);
  test_assert(args == NULL);
  test_assert(sys_env_arg_error() != NULL);

  sys_date_t date = {0};
  sys_date_t unchanged = date;
  test_assert(!sys_env_arg_parse_date(args, "when", &date));
  test_assert(date.seconds == unchanged.seconds && date.nanoseconds == unchanged.nanoseconds);
  int64_t ns = 42;
  test_assert(!sys_env_arg_parse_duration(args, "every", &ns));
  test_assert(ns == 42);
}
