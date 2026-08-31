#include <picofuse/sys.h>
#include <test/test.h>

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  ///////////////////////////////////////////////////////////////////////
  // NULL flags/stream are both rejected.

  {
    char buf[64];
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    test_assert(stream != NULL);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose", .short_name = NULL,
         .type = SYS_ENV_ARG_BOOL, .value = "false"},
        {0},
    };
    test_assert(!sys_env_arg_usage(NULL, stream));
    test_assert(!sys_env_arg_usage(flags, NULL));
    sys_iostream_close(stream);
  }

  ///////////////////////////////////////////////////////////////////////
  // A single flag with a short name.

  {
    char buf[64];
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    sys_env_arg_flag_t flags[] = {
        {.long_name = "name", .short_name = "n",
         .type = SYS_ENV_ARG_STRING, .value = "bob"},
        {0},
    };
    test_assert(sys_env_arg_usage(flags, stream));
    test_assert(
        sys_string_compare(buf, "  --name, -n <string> (default: bob)\n") ==
        0);
    sys_iostream_close(stream);
  }

  ///////////////////////////////////////////////////////////////////////
  // A single flag with no short name - the ", -x" part is omitted
  // entirely, not left as an empty placeholder.

  {
    char buf[64];
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    sys_env_arg_flag_t flags[] = {
        {.long_name = "name", .short_name = NULL,
         .type = SYS_ENV_ARG_STRING, .value = "default"},
        {0},
    };
    test_assert(sys_env_arg_usage(flags, stream));
    test_assert(sys_string_compare(
                    buf, "  --name <string> (default: default)\n") == 0);
    sys_iostream_close(stream);
  }

  ///////////////////////////////////////////////////////////////////////
  // A non-BOOL flag with no default value omits the whole
  // "(default: ...)" clause entirely, rather than printing a placeholder
  // for it - checked both with and without a short name, since that's a
  // separate branch.

  {
    char buf[64];
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    sys_env_arg_flag_t flags[] = {
        {.long_name = "count", .short_name = NULL, .type = SYS_ENV_ARG_INT,
         .value = NULL},
        {0},
    };
    test_assert(sys_env_arg_usage(flags, stream));
    test_assert(sys_string_compare(buf, "  --count <int>\n") == 0);
    sys_iostream_close(stream);
  }
  {
    char buf[64];
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    sys_env_arg_flag_t flags[] = {
        {.long_name = "count", .short_name = "c", .type = SYS_ENV_ARG_INT,
         .value = NULL},
        {0},
    };
    test_assert(sys_env_arg_usage(flags, stream));
    test_assert(sys_string_compare(buf, "  --count, -c <int>\n") == 0);
    sys_iostream_close(stream);
  }

  ///////////////////////////////////////////////////////////////////////
  // BOOL flags additionally always get a "negate: --no-<flag>" note,
  // since --no-<flag> is a real, documented way to invoke them
  // (sys_env_arg_parse()) that "<bool>" alone doesn't convey - present
  // whether or not a default was also supplied, and combined with it
  // (comma-separated in one set of parens) when both apply.

  {
    char buf[96];
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose", .short_name = "v",
         .type = SYS_ENV_ARG_BOOL, .value = "false"},
        {0},
    };
    test_assert(sys_env_arg_usage(flags, stream));
    test_assert(sys_string_compare(buf,
                                    "  --verbose, -v <bool> (default: "
                                    "false, negate: --no-verbose)\n") == 0);
    sys_iostream_close(stream);
  }
  {
    char buf[64];
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose", .short_name = NULL,
         .type = SYS_ENV_ARG_BOOL, .value = NULL}, // no default
        {0},
    };
    test_assert(sys_env_arg_usage(flags, stream));
    test_assert(sys_string_compare(
                    buf, "  --verbose <bool> (negate: --no-verbose)\n") == 0);
    sys_iostream_close(stream);
  }

  ///////////////////////////////////////////////////////////////////////
  // Multiple flags of every type print one line each, in order,
  // concatenated - the type name shown matches the declared type, and
  // only the BOOL entry gets the negate note.

  {
    char buf[256];
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose", .short_name = "v",
         .type = SYS_ENV_ARG_BOOL, .value = "false"},
        {.long_name = "name", .short_name = NULL,
         .type = SYS_ENV_ARG_STRING, .value = "default"},
        {.long_name = "count", .short_name = "c", .type = SYS_ENV_ARG_INT,
         .value = "0"},
        {.long_name = "limit", .short_name = NULL, .type = SYS_ENV_ARG_UINT,
         .value = "10"},
        {.long_name = "ratio", .short_name = NULL, .type = SYS_ENV_ARG_FLOAT,
         .value = "1.0"},
        {0},
    };
    test_assert(sys_env_arg_usage(flags, stream));
    test_assert(
        sys_string_compare(
            buf, "  --verbose, -v <bool> (default: false, negate: "
                 "--no-verbose)\n"
                 "  --name <string> (default: default)\n"
                 "  --count, -c <int> (default: 0)\n"
                 "  --limit <uint> (default: 10)\n"
                 "  --ratio <float> (default: 1.0)\n") == 0);
    sys_iostream_close(stream);
  }

  ///////////////////////////////////////////////////////////////////////
  // An empty flags array (just the sentinel) writes nothing and still
  // succeeds.

  {
    char buf[16] = "unchanged";
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    sys_env_arg_flag_t flags[] = {{0}};
    test_assert(sys_env_arg_usage(flags, stream));
    test_assert(sys_string_compare(buf, "unchanged") == 0); // nothing written
    sys_iostream_close(stream);
  }

  ///////////////////////////////////////////////////////////////////////
  // If the output stream can't hold a full line, the short write is
  // detected and reported as failure rather than silently truncated.

  {
    char buf[8]; // far too small for a real usage line
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose", .short_name = "v",
         .type = SYS_ENV_ARG_BOOL, .value = "false"},
        {0},
    };
    test_assert(!sys_env_arg_usage(flags, stream));
    sys_iostream_close(stream);
  }

  sys_exit();
  return 0;
}
