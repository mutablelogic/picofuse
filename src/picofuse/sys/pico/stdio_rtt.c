#include <picofuse/sys.h>

#include "../iostream/iostream.h"
#include <SEGGER_RTT.h>

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static size_t _sys_stdio_rtt_read(sys_iostream_t *stream, char *buf, size_t n) {
  (void)stream;
  return SEGGER_RTT_Read(0, buf, (unsigned)n);
}

static size_t _sys_stdio_rtt_write(sys_iostream_t *stream, const char *buf,
                                   size_t n) {
  (void)stream;
  return SEGGER_RTT_Write(0, buf, (unsigned)n);
}

static ptrdiff_t _sys_stdio_rtt_seek(sys_iostream_t *stream, ptrdiff_t offset,
                                     bool abs) {
  (void)stream;
  (void)offset;
  (void)abs;
  return -1;
}

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static const sys_iostream_ops_t _sys_stdio_rtt_ops = {
    .read = _sys_stdio_rtt_read,
    .write = _sys_stdio_rtt_write,
    .seek = _sys_stdio_rtt_seek,
    .set_callback = NULL,
    .close = NULL,
};

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

static sys_iostream_t *_sys_stdio_rtt_open(void) {
  return _sys_iostream_alloc(&_sys_stdio_rtt_ops);
}

///////////////////////////////////////////////////////////////////////////////
// MODULE LIFECYCLE

void _sys_stdio_rtt_init(void) {
  SEGGER_RTT_Init();
  sys_stdout = _sys_stdio_rtt_open();
  sys_stdin = sys_stdout;
  sys_assert(sys_stdout != NULL);
}

void _sys_stdio_rtt_exit(void) {
  sys_iostream_close(sys_stdout);
  sys_stdout = NULL;
  sys_stdin = NULL;
}