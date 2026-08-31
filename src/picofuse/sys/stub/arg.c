#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS
//
// Pico has no real command line - sys_init() has nothing to hand this
// module (see pico/init.c), so there's nothing for these to parse or
// report. Matches sys_env_signalhandler()'s stub: always fail/return
// empty, never crash.

sys_env_arg_t *sys_env_arg_parse(sys_env_arg_flag_t *flags) {
  (void)flags;
  return NULL;
}

size_t sys_env_arg_count(sys_env_arg_t *args) {
  (void)args;
  return 0;
}

const char *sys_env_arg_string(sys_env_arg_t *args, size_t index) {
  (void)args;
  (void)index;
  return NULL;
}

bool sys_env_arg_usage(sys_env_arg_flag_t *flags, sys_iostream_t *stream) {
  (void)flags;
  (void)stream;
  return false;
}

bool sys_env_arg_parse_bool(sys_env_arg_t *args, const char *name,
                             bool *value) {
  (void)args;
  (void)name;
  (void)value;
  return false;
}

bool sys_env_arg_parse_int32(sys_env_arg_t *args, const char *name,
                              int32_t *value) {
  (void)args;
  (void)name;
  (void)value;
  return false;
}

bool sys_env_arg_parse_uint32(sys_env_arg_t *args, const char *name,
                               uint32_t *value) {
  (void)args;
  (void)name;
  (void)value;
  return false;
}

bool sys_env_arg_parse_int64(sys_env_arg_t *args, const char *name,
                              int64_t *value) {
  (void)args;
  (void)name;
  (void)value;
  return false;
}

bool sys_env_arg_parse_uint64(sys_env_arg_t *args, const char *name,
                               uint64_t *value) {
  (void)args;
  (void)name;
  (void)value;
  return false;
}

bool sys_env_arg_parse_float32(sys_env_arg_t *args, const char *name,
                                float *value) {
  (void)args;
  (void)name;
  (void)value;
  return false;
}

bool sys_env_arg_parse_float64(sys_env_arg_t *args, const char *name,
                                double *value) {
  (void)args;
  (void)name;
  (void)value;
  return false;
}

size_t sys_env_arg_parse_string(sys_env_arg_t *args, const char *name,
                                 char *value, size_t cap) {
  (void)args;
  (void)name;
  (void)value;
  (void)cap;
  return 0;
}
