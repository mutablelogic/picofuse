#include <pico/binary_info.h>
#include <pico/unique_id.h>
#include <picofuse/sys.h>

// External symbols provided by the linker
extern binary_info_t *__binary_info_start;
extern binary_info_t *__binary_info_end;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS

static const char *get_binary_info_string(uint32_t id) {
  binary_info_t **start = (binary_info_t **)&__binary_info_start;
  binary_info_t **end = (binary_info_t **)&__binary_info_end;

  if (start == NULL || end == NULL || start >= end) {
    return NULL;
  }

  for (binary_info_t **info_ptr = start; info_ptr < end; info_ptr++) {
    binary_info_t *info = *info_ptr;
    if (!info) {
      continue;
    }

    if (info->type == BINARY_INFO_TYPE_ID_AND_STRING) {
      binary_info_id_and_string_t *string_info =
          (binary_info_id_and_string_t *)info;
      if (string_info->core.tag == BINARY_INFO_TAG_RASPBERRY_PI &&
          string_info->id == id) {
        return (const char *)string_info->value;
      }
    }
  }
  return NULL;
}

static const char *get_program_name(void) {
  const char *name = get_binary_info_string(BINARY_INFO_ID_RP_PROGRAM_NAME);
  if (name) {
    return name;
  }
#ifdef PICO_PROGRAM_NAME
  return PICO_PROGRAM_NAME;
#elif defined(PICO_TARGET_NAME)
  return PICO_TARGET_NAME;
#else
  return "unknown";
#endif
}

static const char *get_program_version(void) {
  const char *version =
      get_binary_info_string(BINARY_INFO_ID_RP_PROGRAM_VERSION_STRING);
  if (version) {
    return version;
  }
#ifdef PICO_PROGRAM_VERSION_STRING
  return PICO_PROGRAM_VERSION_STRING;
#elif defined(PICOFUSE_VERSION)
  return PICOFUSE_VERSION;
#else
  return "unknown";
#endif
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS

const char *sys_env_serial(void) {
  static char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
  pico_get_unique_board_id_string(serial, sizeof(serial));
  return serial;
}

const char *sys_env_name(void) { return get_program_name(); }

const char *sys_env_system(void) {
#ifdef PICO_BOARD
  return PICO_BOARD;
#else
  return "pico";
#endif
}

const char *sys_env_version(void) { return get_program_version(); }
