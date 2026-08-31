#include "env.h"
#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

// Full definition of the opaque sys_env_arg_t declared in env.h.
struct sys_env_arg_t {
  sys_env_arg_flag_t *flags;              // The array passed to
                                           // sys_env_arg_parse(), with each
                                           // matched entry's value updated.
  const char *args[SYS_ENV_ARG_CAPACITY]; // Positional arguments - argv
                                           // entries that matched no
                                           // declared flag, in order.
  size_t args_count;                      // Number of entries in args.
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static int _sys_env_argc = 0;
static char **_sys_env_argv = NULL;

// The single, process-wide parse result - see sys_env_arg_t's own doc
// comment for why this is never pool-allocated.
static sys_env_arg_t _sys_env_arg_result;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

void _sys_env_set_args(int argc, char *argv[]) {
  _sys_env_argc = argc;
  _sys_env_argv = argv;
}

// Returns true if token (the text after the leading dash(es), e.g.
// "verbose" or "verbose=true") matches declared name - i.e. token equals
// name exactly, or starts with name immediately followed by '='. On a
// match, *value_sep points at the '=' (so the caller can find the
// attached value), or is NULL if the match ended the token exactly.
//
// sys_string_compare() alone can't do this: it needs both sides fully
// NUL-terminated at the same point, but token is only bounded by an
// embedded '=' or the end of the whole argv string - a plain
// hasprefix-plus-boundary-check is the direct fit instead.
static bool _sys_env_name_matches(const char *token, const char *name,
                                   const char **value_sep) {
  if (name == NULL || !sys_string_hasprefix(token, name)) {
    return false;
  }
  const char *after = token + sys_string_bytes(name);
  if (*after != '\0' && *after != '=') {
    return false; // e.g. token "verboseness" must not match name "verbose"
  }
  *value_sep = (*after == '=') ? after : NULL;
  return true;
}

// Finds the declared flag matching a "--name"/"-name" token, where token
// is the text after the leading dash(es) (may include a trailing
// "=value"). For a BOOL flag, also recognizes "--no-<long_name>" and
// reports it via *negated. On any match, *attached_value points just past
// a "=" if one was present, or is NULL otherwise.
static sys_env_arg_flag_t *_sys_env_find_flag(sys_env_arg_flag_t *flags,
                                               bool is_long, const char *token,
                                               bool *negated,
                                               const char **attached_value) {
  *negated = false;
  *attached_value = NULL;

  for (size_t i = 0; flags[i].long_name != NULL; i++) {
    const char *decl = is_long ? flags[i].long_name : flags[i].short_name;
    const char *value_sep = NULL;
    if (_sys_env_name_matches(token, decl, &value_sep)) {
      *attached_value = (value_sep != NULL) ? value_sep + 1 : NULL;
      return &flags[i];
    }
    if (is_long && flags[i].type == SYS_ENV_ARG_BOOL &&
        sys_string_hasprefix(token, "no-") &&
        _sys_env_name_matches(token + 3, flags[i].long_name, &value_sep)) {
      *attached_value = (value_sep != NULL) ? value_sep + 1 : NULL;
      *negated = true;
      return &flags[i];
    }
  }
  return NULL;
}

// Finds a declared flag by its full (NUL-terminated) long or short name -
// used by the sys_env_arg_parse_*() accessors, not the argv scan above.
static sys_env_arg_flag_t *_sys_env_find_by_name(sys_env_arg_flag_t *flags,
                                                  const char *name) {
  if (flags == NULL || name == NULL) {
    return NULL;
  }

  for (size_t i = 0; flags[i].long_name != NULL; i++) {
    sys_env_arg_flag_t *flag = &flags[i];
    if ((flag->long_name != NULL &&
         sys_string_compare(name, flag->long_name) == 0) ||
        (flag->short_name != NULL &&
         sys_string_compare(name, flag->short_name) == 0)) {
      return flag;
    }
  }
  return NULL;
}

static bool _sys_env_validate(sys_env_arg_type_t type, const char *value) {
  switch (type) {
  case SYS_ENV_ARG_STRING:
    return true;
  case SYS_ENV_ARG_BOOL: {
    bool out;
    return sys_string_parse_bool(value, &out);
  }
  case SYS_ENV_ARG_INT: {
    int64_t out;
    return sys_string_parse_int64(value, 0, &out);
  }
  case SYS_ENV_ARG_UINT: {
    uint64_t out;
    return sys_string_parse_uint64(value, 0, &out);
  }
  case SYS_ENV_ARG_FLOAT: {
    double out;
    return sys_string_parse_float64(value, 0, &out);
  }
  default:
    return false;
  }
}

static const char *_sys_env_type_name(sys_env_arg_type_t type) {
  switch (type) {
  case SYS_ENV_ARG_BOOL:
    return "bool";
  case SYS_ENV_ARG_STRING:
    return "string";
  case SYS_ENV_ARG_INT:
    return "int";
  case SYS_ENV_ARG_UINT:
    return "uint";
  case SYS_ENV_ARG_FLOAT:
    return "float";
  default:
    return "?";
  }
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS

sys_env_arg_t *sys_env_arg_parse(sys_env_arg_flag_t *flags) {
  _sys_env_arg_result.flags = NULL;
  _sys_env_arg_result.args_count = 0;

  if (flags == NULL) {
    return NULL;
  }

  bool positional_only = false;

  for (int i = 1; i < _sys_env_argc; i++) {
    const char *arg = _sys_env_argv[i];
    if (arg == NULL) {
      continue;
    }

    if (!positional_only && arg[0] == '-' && arg[1] == '\0') {
      // "-" by itself: skip it, and treat everything after it as
      // positional, regardless of a leading '-'.
      positional_only = true;
      continue;
    }

    if (positional_only || arg[0] != '-') {
      if (_sys_env_arg_result.args_count >= SYS_ENV_ARG_CAPACITY) {
        return NULL;
      }
      _sys_env_arg_result.args[_sys_env_arg_result.args_count++] = arg;
      continue;
    }

    bool is_long = (arg[1] == '-');
    const char *token = arg + (is_long ? 2 : 1);

    bool negated = false;
    const char *attached_value = NULL;
    sys_env_arg_flag_t *flag =
        _sys_env_find_flag(flags, is_long, token, &negated, &attached_value);
    if (flag == NULL) {
      return NULL; // looked like a flag, but nothing declared matches
    }

    const char *value;
    if (attached_value != NULL) {
      value = attached_value;
    } else if (negated) {
      value = "false";
    } else if (flag->type == SYS_ENV_ARG_BOOL) {
      value = "true"; // presence alone means "on"
    } else {
      if (i + 1 >= _sys_env_argc) {
        return NULL; // flag needs a value, but there's nothing left
      }
      value = _sys_env_argv[++i];
    }

    if (!_sys_env_validate(flag->type, value)) {
      return NULL;
    }
    flag->value = value;
  }

  _sys_env_arg_result.flags = flags;
  return &_sys_env_arg_result;
}

size_t sys_env_arg_count(sys_env_arg_t *args) {
  return (args != NULL) ? args->args_count : 0;
}

const char *sys_env_arg_string(sys_env_arg_t *args, size_t index) {
  if (args == NULL || index >= args->args_count) {
    return NULL;
  }
  return args->args[index];
}

bool sys_env_arg_usage(sys_env_arg_flag_t *flags, sys_iostream_t *stream) {
  if (flags == NULL || stream == NULL) {
    return false;
  }

  for (size_t i = 0; flags[i].long_name != NULL; i++) {
    // Build the "(default: X, negate: --no-Y)" suffix, if there's
    // anything to say for this flag - a default is shown whenever one
    // was supplied, and a negate note whenever the flag is BOOL
    // (regardless of whether a default was supplied), so either, both,
    // or neither may end up present.
    char note[96] = "";
    bool has_default = flags[i].value != NULL;
    bool is_bool = flags[i].type == SYS_ENV_ARG_BOOL;
    if (has_default && is_bool) {
      sys_sprintf(note, sizeof(note), " (default: %s, negate: --no-%s)",
                  flags[i].value, flags[i].long_name);
    } else if (is_bool) {
      sys_sprintf(note, sizeof(note), " (negate: --no-%s)",
                  flags[i].long_name);
    } else if (has_default) {
      sys_sprintf(note, sizeof(note), " (default: %s)", flags[i].value);
    }

    char line[128];
    size_t len;
    if (flags[i].short_name != NULL) {
      len = sys_sprintf(line, sizeof(line), "  --%s, -%s <%s>%s\n",
                         flags[i].long_name, flags[i].short_name,
                         _sys_env_type_name(flags[i].type), note);
    } else {
      len = sys_sprintf(line, sizeof(line), "  --%s <%s>%s\n",
                         flags[i].long_name, _sys_env_type_name(flags[i].type),
                         note);
    }
    size_t to_write = len < sizeof(line) ? len : sizeof(line) - 1;
    if (sys_iostream_write(stream, line, to_write) != to_write) {
      return false;
    }
  }
  return true;
}

bool sys_env_arg_parse_bool(sys_env_arg_t *args, const char *name,
                             bool *value) {
  if (args == NULL || value == NULL) {
    return false;
  }
  sys_env_arg_flag_t *flag = _sys_env_find_by_name(args->flags, name);
  if (flag == NULL) {
    return false;
  }
  if (flag->value == NULL) {
    *value = false; // declared but no default and never matched
    return true;
  }
  return sys_string_parse_bool(flag->value, value);
}

bool sys_env_arg_parse_int32(sys_env_arg_t *args, const char *name,
                              int32_t *value) {
  if (args == NULL || value == NULL) {
    return false;
  }
  sys_env_arg_flag_t *flag = _sys_env_find_by_name(args->flags, name);
  if (flag == NULL || flag->value == NULL) {
    return false;
  }
  return sys_string_parse_int32(flag->value, 0, value);
}

bool sys_env_arg_parse_uint32(sys_env_arg_t *args, const char *name,
                               uint32_t *value) {
  if (args == NULL || value == NULL) {
    return false;
  }
  sys_env_arg_flag_t *flag = _sys_env_find_by_name(args->flags, name);
  if (flag == NULL || flag->value == NULL) {
    return false;
  }
  return sys_string_parse_uint32(flag->value, 0, value);
}

bool sys_env_arg_parse_int64(sys_env_arg_t *args, const char *name,
                              int64_t *value) {
  if (args == NULL || value == NULL) {
    return false;
  }
  sys_env_arg_flag_t *flag = _sys_env_find_by_name(args->flags, name);
  if (flag == NULL || flag->value == NULL) {
    return false;
  }
  return sys_string_parse_int64(flag->value, 0, value);
}

bool sys_env_arg_parse_uint64(sys_env_arg_t *args, const char *name,
                               uint64_t *value) {
  if (args == NULL || value == NULL) {
    return false;
  }
  sys_env_arg_flag_t *flag = _sys_env_find_by_name(args->flags, name);
  if (flag == NULL || flag->value == NULL) {
    return false;
  }
  return sys_string_parse_uint64(flag->value, 0, value);
}

bool sys_env_arg_parse_float32(sys_env_arg_t *args, const char *name,
                                float *value) {
  if (args == NULL || value == NULL) {
    return false;
  }
  sys_env_arg_flag_t *flag = _sys_env_find_by_name(args->flags, name);
  if (flag == NULL || flag->value == NULL) {
    return false;
  }
  return sys_string_parse_float32(flag->value, 0, value);
}

bool sys_env_arg_parse_float64(sys_env_arg_t *args, const char *name,
                                double *value) {
  if (args == NULL || value == NULL) {
    return false;
  }
  sys_env_arg_flag_t *flag = _sys_env_find_by_name(args->flags, name);
  if (flag == NULL || flag->value == NULL) {
    return false;
  }
  return sys_string_parse_float64(flag->value, 0, value);
}

size_t sys_env_arg_parse_string(sys_env_arg_t *args, const char *name,
                                 char *value, size_t cap) {
  if (args == NULL) {
    return 0;
  }
  sys_env_arg_flag_t *flag = _sys_env_find_by_name(args->flags, name);
  if (flag == NULL || flag->value == NULL) {
    return 0;
  }

  size_t len = 0;
  while (flag->value[len] != '\0') {
    len++;
  }

  if (value != NULL) {
    size_t to_copy = len < cap ? len : cap;
    for (size_t i = 0; i < to_copy; i++) {
      value[i] = flag->value[i];
    }
  }
  return len;
}
