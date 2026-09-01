#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // Long-form: attached ("--name=value") and space-separated
  // ("--name value") both work, and so does the short-name alias.

  {
    char *argv[] = {"prog", "--name=bob"};
    sys_init(2, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "name",
         .short_name = NULL,
         .type = sys_env_arg_type_string,
         .value = "default"},
        {0},
    };
    sys_env_arg_t *args = sys_env_arg_parse(flags);
    test_assert(args != NULL);
    char buf[16] = {0};
    test_assert(sys_env_arg_parse_string(args, "name", buf, sizeof(buf)) == 3);
    test_assert(sys_string_compare(buf, "bob") == 0);
  }
  {
    char *argv[] = {"prog", "--count", "42"};
    sys_init(3, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "count",
         .short_name = "c",
         .type = sys_env_arg_type_int,
         .value = "0"},
        {0},
    };
    sys_env_arg_t *args = sys_env_arg_parse(flags);
    test_assert(args != NULL);
    int32_t count = 0;
    test_assert(sys_env_arg_parse_int32(args, "count", &count));
    test_assert(count == 42);
  }
  {
    char *argv[] = {"prog", "-c", "7"};
    sys_init(3, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "count",
         .short_name = "c",
         .type = sys_env_arg_type_int,
         .value = "0"},
        {0},
    };
    sys_env_arg_t *args = sys_env_arg_parse(flags);
    test_assert(args != NULL);
    int32_t count = 0;
    test_assert(sys_env_arg_parse_int32(args, "c", &count)); // by short name
    test_assert(count == 7);
  }
  {
    char *argv[] = {"prog", "-c=9"};
    sys_init(2, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "count",
         .short_name = "c",
         .type = sys_env_arg_type_int,
         .value = "0"},
        {0},
    };
    sys_env_arg_t *args = sys_env_arg_parse(flags);
    test_assert(args != NULL);
    int32_t count = 0;
    test_assert(sys_env_arg_parse_int32(args, "count", &count));
    test_assert(count == 9);
  }

  ///////////////////////////////////////////////////////////////////////
  // BOOL flags: presence alone means true, "--no-<flag>" means an
  // explicit false, and an attached value on a negated flag still wins
  // over the implicit false.

  {
    char *argv[] = {"prog", "--verbose"};
    sys_init(2, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose",
         .short_name = NULL,
         .type = sys_env_arg_type_bool,
         .value = "false"},
        {0},
    };
    sys_env_arg_t *args = sys_env_arg_parse(flags);
    test_assert(args != NULL);
    bool verbose = false;
    test_assert(sys_env_arg_parse_bool(args, "verbose", &verbose));
    test_assert(verbose == true);
  }
  {
    char *argv[] = {"prog", "--no-verbose"};
    sys_init(2, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose",
         .short_name = NULL,
         .type = sys_env_arg_type_bool,
         .value = "true"},
        {0},
    };
    sys_env_arg_t *args = sys_env_arg_parse(flags);
    test_assert(args != NULL);
    bool verbose = true;
    test_assert(sys_env_arg_parse_bool(args, "verbose", &verbose));
    test_assert(verbose == false);
  }
  {
    char *argv[] = {"prog", "--no-verbose=true"};
    sys_init(2, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose",
         .short_name = NULL,
         .type = sys_env_arg_type_bool,
         .value = "false"},
        {0},
    };
    sys_env_arg_t *args = sys_env_arg_parse(flags);
    test_assert(args != NULL);
    bool verbose = false;
    test_assert(sys_env_arg_parse_bool(args, "verbose", &verbose));
    test_assert(verbose == true); // attached value overrides the negation
  }

  ///////////////////////////////////////////////////////////////////////
  // A flag name that's a proper prefix of the token, but not the whole
  // name (or name followed by '='), must not match - "--verboseness"
  // is a different, undeclared flag, not "verbose" with extra garbage.

  {
    char *argv[] = {"prog", "--verboseness"};
    sys_init(2, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose",
         .short_name = NULL,
         .type = sys_env_arg_type_bool,
         .value = "false"},
        {0},
    };
    test_assert(sys_env_arg_parse(flags) == NULL);
  }

  ///////////////////////////////////////////////////////////////////////
  // Positional arguments are collected in order, interleaved with flags.

  {
    char *argv[] = {"prog", "in.txt", "--verbose", "out.txt"};
    sys_init(4, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose",
         .short_name = NULL,
         .type = sys_env_arg_type_bool,
         .value = "false"},
        {0},
    };
    sys_env_arg_t *args = sys_env_arg_parse(flags);
    test_assert(args != NULL);
    test_assert(sys_env_arg_count(args) == 2);
    test_assert(sys_string_compare(sys_env_arg_string(args, 0), "in.txt") == 0);
    test_assert(sys_string_compare(sys_env_arg_string(args, 1), "out.txt") ==
                0);
    test_assert(sys_env_arg_string(args, 2) == NULL); // out of bounds
  }

  ///////////////////////////////////////////////////////////////////////
  // "-" by itself is skipped, and marks everything after it as
  // positional even if it looks like a flag.

  {
    char *argv[] = {"prog", "-", "--not-a-flag", "-x"};
    sys_init(4, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose",
         .short_name = NULL,
         .type = sys_env_arg_type_bool,
         .value = "false"},
        {0},
    };
    sys_env_arg_t *args = sys_env_arg_parse(flags);
    test_assert(args != NULL);
    test_assert(sys_env_arg_count(args) == 2);
    test_assert(
        sys_string_compare(sys_env_arg_string(args, 0), "--not-a-flag") == 0);
    test_assert(sys_string_compare(sys_env_arg_string(args, 1), "-x") == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // Multiple distinct flags together, plus an unsigned-integer flag.

  {
    char *argv[] = {"prog", "--name=bob", "-c", "3", "--limit=9", "--verbose"};
    sys_init(6, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "name",
         .short_name = NULL,
         .type = sys_env_arg_type_string,
         .value = "default"},
        {.long_name = "count",
         .short_name = "c",
         .type = sys_env_arg_type_int,
         .value = "0"},
        {.long_name = "limit",
         .short_name = NULL,
         .type = sys_env_arg_type_uint,
         .value = "0"},
        {.long_name = "verbose",
         .short_name = NULL,
         .type = sys_env_arg_type_bool,
         .value = "false"},
        {0},
    };
    sys_env_arg_t *args = sys_env_arg_parse(flags);
    test_assert(args != NULL);

    char namebuf[16] = {0};
    test_assert(
        sys_env_arg_parse_string(args, "name", namebuf, sizeof(namebuf)) == 3);
    int32_t count = 0;
    test_assert(sys_env_arg_parse_int32(args, "count", &count));
    test_assert(count == 3);
    uint32_t limit = 0;
    test_assert(sys_env_arg_parse_uint32(args, "limit", &limit));
    test_assert(limit == 9);
    bool verbose = false;
    test_assert(sys_env_arg_parse_bool(args, "verbose", &verbose));
    test_assert(verbose == true);
  }

  ///////////////////////////////////////////////////////////////////////
  // Failure paths: unrecognized flag, a value that doesn't match the
  // flag's declared type, a flag needing a value with nothing left, and
  // invalid parameters.

  {
    char *argv[] = {"prog", "--bogus"};
    sys_init(2, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose",
         .short_name = NULL,
         .type = sys_env_arg_type_bool,
         .value = "false"},
        {0},
    };
    test_assert(sys_env_arg_parse(flags) == NULL);
  }
  {
    char *argv[] = {"prog", "--count=abc"};
    sys_init(2, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "count",
         .short_name = NULL,
         .type = sys_env_arg_type_int,
         .value = "0"},
        {0},
    };
    test_assert(sys_env_arg_parse(flags) == NULL);
  }
  {
    char *argv[] = {"prog", "--limit=-1"};
    sys_init(2, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "limit",
         .short_name = NULL,
         .type = sys_env_arg_type_uint,
         .value = "0"},
        {0},
    };
    test_assert(sys_env_arg_parse(flags) == NULL); // negative isn't unsigned
  }
  {
    char *argv[] = {"prog", "--count"};
    sys_init(2, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "count",
         .short_name = NULL,
         .type = sys_env_arg_type_int,
         .value = "0"},
        {0},
    };
    test_assert(sys_env_arg_parse(flags) == NULL);
  }
  {
    char *argv[] = {"prog"};
    sys_init(1, argv, 0, sys_stdio_rtt);
    test_assert(sys_env_arg_parse(NULL) == NULL);
  }

  ///////////////////////////////////////////////////////////////////////
  // Too many positional arguments (beyond SYS_ENV_ARG_CAPACITY) fails.

  {
    char *argv[SYS_ENV_ARG_CAPACITY + 2];
    argv[0] = "prog";
    for (int i = 1; i < SYS_ENV_ARG_CAPACITY + 2; i++) {
      argv[i] = "x";
    }
    sys_init(SYS_ENV_ARG_CAPACITY + 2, argv, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {{0}};
    test_assert(sys_env_arg_parse(flags) == NULL);
  }

  ///////////////////////////////////////////////////////////////////////
  // Re-parsing after a failed call works cleanly with a fresh, valid
  // command line (nothing from the failed attempt lingers).

  {
    char *argv1[] = {"prog", "--bogus"};
    sys_init(2, argv1, 0, sys_stdio_rtt);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose",
         .short_name = NULL,
         .type = sys_env_arg_type_bool,
         .value = "false"},
        {0},
    };
    test_assert(sys_env_arg_parse(flags) == NULL);

    char *argv2[] = {"prog", "--verbose"};
    sys_init(2, argv2, 0, sys_stdio_rtt);
    sys_env_arg_t *args = sys_env_arg_parse(flags);
    test_assert(args != NULL);
    bool verbose = false;
    test_assert(sys_env_arg_parse_bool(args, "verbose", &verbose));
    test_assert(verbose == true);
  }
}
