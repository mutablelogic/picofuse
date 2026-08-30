#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC API

uintptr_t sys_hash_djb2(const char *str) {
  if (str == NULL) {
    return 0;
  }

  uintptr_t hash = 5381;
  while (*str != '\0') {
    hash = ((hash << 5) + hash) + (unsigned char)*str;
    str++;
  }

  return hash;
}
