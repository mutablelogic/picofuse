#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS

bool sys_env_signalhandler(sys_env_signal_t mask,
                            sys_env_signal_callback_t callback) {
  (void)mask;
  (void)callback;
  return false;
}
