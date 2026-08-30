#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS

const char *sys_env_system(void) { return SYSTEM_NAME; }

const char *sys_env_version(void) {
#ifdef PICOFUSE_VERSION
  return PICOFUSE_VERSION;
#else
  return "unknown";
#endif
}
