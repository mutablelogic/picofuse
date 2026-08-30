#include <picofuse/sys.h>
#include <test/test.h>

int main(void) {
  char *argv[] = {"prog", "--name=bob", "--big=5000000000", "--pi=3.14"};
  sys_init(4, argv);
  sys_env_arg_flag_t flags[] = {
      {.long_name = "name", .short_name = NULL, .type = SYS_ENV_ARG_STRING,
       .value = "default"},
      {.long_name = "big", .short_name = NULL, .type = SYS_ENV_ARG_INT,
       .value = "0"}, // 5e9 overflows int32/uint32, fits int64
      {.long_name = "pi", .short_name = NULL, .type = SYS_ENV_ARG_FLOAT,
       .value = "0"},
      {.long_name = "flag", .short_name = NULL, .type = SYS_ENV_ARG_BOOL,
       .value = NULL}, // no default, never present on the command line
      {0},
  };
  sys_env_arg_t *args = sys_env_arg_parse(flags);
  test_assert(args != NULL);

  ///////////////////////////////////////////////////////////////////////
  // Every accessor rejects a name that wasn't declared/matched.

  {
    bool b;
    test_assert(!sys_env_arg_parse_bool(args, "bogus", &b));
    int32_t i32;
    test_assert(!sys_env_arg_parse_int32(args, "bogus", &i32));
    uint32_t u32;
    test_assert(!sys_env_arg_parse_uint32(args, "bogus", &u32));
    int64_t i64;
    test_assert(!sys_env_arg_parse_int64(args, "bogus", &i64));
    uint64_t u64;
    test_assert(!sys_env_arg_parse_uint64(args, "bogus", &u64));
    float f32;
    test_assert(!sys_env_arg_parse_float32(args, "bogus", &f32));
    double f64;
    test_assert(!sys_env_arg_parse_float64(args, "bogus", &f64));
    char buf[8];
    test_assert(sys_env_arg_parse_string(args, "bogus", buf, sizeof(buf)) ==
                0);
  }

  ///////////////////////////////////////////////////////////////////////
  // Every accessor rejects args == NULL, without crashing.

  {
    bool b;
    test_assert(!sys_env_arg_parse_bool(NULL, "name", &b));
    int32_t i32;
    test_assert(!sys_env_arg_parse_int32(NULL, "big", &i32));
    test_assert(sys_env_arg_parse_string(NULL, "name", NULL, 0) == 0);
    test_assert(sys_env_arg_count(NULL) == 0);
    test_assert(sys_env_arg_string(NULL, 0) == NULL);
  }

  ///////////////////////////////////////////////////////////////////////
  // A declared BOOL flag with no default and never present on the
  // command line reports false, not "not found" - matching the header's
  // documented "false if no default is specified".

  {
    bool flag = true;
    test_assert(sys_env_arg_parse_bool(args, "flag", &flag));
    test_assert(flag == false);
  }

  ///////////////////////////////////////////////////////////////////////
  // A value too large for a 32-bit accessor correctly fails there, while
  // the matching 64-bit accessor succeeds on the exact same stored value
  // - this is what having both widths is for.

  {
    int32_t i32 = 0;
    test_assert(!sys_env_arg_parse_int32(args, "big", &i32));
    uint32_t u32 = 0;
    test_assert(!sys_env_arg_parse_uint32(args, "big", &u32));
    int64_t i64 = 0;
    test_assert(sys_env_arg_parse_int64(args, "big", &i64));
    test_assert(i64 == 5000000000LL);
    uint64_t u64 = 0;
    test_assert(sys_env_arg_parse_uint64(args, "big", &u64));
    test_assert(u64 == 5000000000ULL);
  }

  ///////////////////////////////////////////////////////////////////////
  // float32 and float64 accessors both read the same declared value.

  {
    float f32 = 0;
    test_assert(sys_env_arg_parse_float32(args, "pi", &f32));
    test_assert(f32 > 3.13f && f32 < 3.15f);
    double f64 = 0;
    test_assert(sys_env_arg_parse_float64(args, "pi", &f64));
    test_assert(f64 > 3.139 && f64 < 3.141);
  }

  ///////////////////////////////////////////////////////////////////////
  // String accessor: exact fit, truncation (still reports the true
  // length, matching sys_scanner_token()'s truncating convention), and a
  // NULL buffer that only asks for the required size.

  {
    char exact[4] = {0};
    test_assert(sys_env_arg_parse_string(args, "name", exact,
                                          sizeof(exact)) == 3);
    test_assert(sys_string_compare(exact, "bob") == 0);

    char small[2] = {0};
    test_assert(sys_env_arg_parse_string(args, "name", small,
                                          sizeof(small)) == 3);
    test_assert(small[0] == 'b'); // truncated, but not overflowed

    test_assert(sys_env_arg_parse_string(args, "name", NULL, 0) == 3);
  }

  sys_exit();
  return 0;
}
