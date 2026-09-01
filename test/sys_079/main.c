#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // NULL flags/stream are both rejected.

  {
    char buf[64];
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    test_assert(stream != NULL);
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose", .short_name = NULL,
         .type = sys_env_arg_type_bool, .value = "false"},
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
         .type = sys_env_arg_type_string, .value = "bob"},
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
         .type = sys_env_arg_type_string, .value = "default"},
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
        {.long_name = "count", .short_name = NULL, .type = sys_env_arg_type_int,
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
        {.long_name = "count", .short_name = "c", .type = sys_env_arg_type_int,
         .value = NULL},
        {0},
    };
    test_assert(sys_env_arg_usage(flags, stream));
    test_assert(sys_string_compare(buf, "  --count, -c <int>\n") == 0);
    sys_iostream_close(stream);
  }

  ///////////////////////////////////////////////////////////////////////
  // BOOL flags get a "negate: --no-<flag>" note only when --no-<flag>
  // would actually change something: with no default (either form picks
  // the value) or a "true" default (--no-<flag> is the only way to turn
  // it off). A "false" default makes --no-<flag> a pure no-op - identical
  // to omitting the flag - so the note is left out there, though
  // "(default: false)" is still shown on its own.

  {
    char buf[64];
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose", .short_name = "v",
         .type = sys_env_arg_type_bool, .value = "false"},
        {0},
    };
    test_assert(sys_env_arg_usage(flags, stream));
    test_assert(sys_string_compare(
                    buf, "  --verbose, -v <bool> (default: false)\n") == 0);
    sys_iostream_close(stream);
  }
  {
    char buf[96];
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose", .short_name = "v",
         .type = sys_env_arg_type_bool, .value = "true"},
        {0},
    };
    test_assert(sys_env_arg_usage(flags, stream));
    test_assert(sys_string_compare(buf,
                                    "  --verbose, -v <bool> (default: "
                                    "true, negate: --no-verbose)\n") == 0);
    sys_iostream_close(stream);
  }
  {
    char buf[64];
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose", .short_name = NULL,
         .type = sys_env_arg_type_bool, .value = NULL}, // no default
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
  // only the BOOL entry (with a non-"false" or absent default) gets the
  // negate note.

  {
    char buf[256];
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    sys_env_arg_flag_t flags[] = {
        {.long_name = "verbose", .short_name = "v",
         .type = sys_env_arg_type_bool, .value = "true"},
        {.long_name = "name", .short_name = NULL,
         .type = sys_env_arg_type_string, .value = "default"},
        {.long_name = "count", .short_name = "c", .type = sys_env_arg_type_int,
         .value = "0"},
        {.long_name = "limit", .short_name = NULL, .type = sys_env_arg_type_uint,
         .value = "10"},
        {.long_name = "ratio", .short_name = NULL, .type = sys_env_arg_type_float,
         .value = "1.0"},
        {0},
    };
    test_assert(sys_env_arg_usage(flags, stream));
    test_assert(
        sys_string_compare(
            buf, "  --verbose, -v <bool> (default: true, negate: "
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
    char buf[16];
    sys_iostream_t *stream = sys_string_open(buf, sizeof(buf));
    sys_env_arg_flag_t flags[] = {{0}};
    test_assert(sys_env_arg_usage(flags, stream));
    test_assert(sys_string_compare(buf, "") == 0); // nothing written
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
         .type = sys_env_arg_type_bool, .value = "false"},
        {0},
    };
    test_assert(!sys_env_arg_usage(flags, stream));
    sys_iostream_close(stream);
  }

}
