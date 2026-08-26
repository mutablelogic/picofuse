#include "picofuse/sys/debugf.h"

#ifndef NDEBUG
void _sys_debugf_impl(const char *context, const char *format, ...) {
  (void)context;
  (void)format;
}
#endif
