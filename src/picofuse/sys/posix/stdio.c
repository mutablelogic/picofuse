#include <picofuse/sys.h>

#include "../iostream/iostream.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

extern bool _sys_stdio_platform_init(void);
extern void _sys_stdio_platform_exit(void);
extern bool _sys_stdio_platform_set_callback(sys_iostream_t *stream,
                                             sys_iostream_callback_t callback,
                                             void *userdata);

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

sys_iostream_t *sys_stdout = NULL;
sys_iostream_t *sys_stdin = NULL;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static size_t _sys_stdio_read(sys_iostream_t *stream, char *buf, size_t n) {
  ssize_t got;
  do {
    got = read(stream->backend.fd.value, buf, n);
  } while (got < 0 && errno == EINTR);
  return got > 0 ? (size_t)got : 0;
}

static size_t _sys_stdio_write(sys_iostream_t *stream, const char *buf,
                               size_t n) {
  ssize_t put;
  do {
    put = write(stream->backend.fd.value, buf, n);
  } while (put < 0 && errno == EINTR);
  return put > 0 ? (size_t)put : 0;
}

static ptrdiff_t _sys_stdio_seek(sys_iostream_t *stream, ptrdiff_t offset,
                                 bool abs) {
  off_t position =
      lseek(stream->backend.fd.value, (off_t)offset, abs ? SEEK_SET : SEEK_CUR);
  return position < 0 ? -1 : (ptrdiff_t)position;
}

static bool _sys_stdio_set_callback(sys_iostream_t *stream,
                                    sys_iostream_callback_t callback,
                                    void *userdata) {
  return _sys_stdio_platform_set_callback(stream, callback, userdata);
}

static const sys_iostream_ops_t _sys_iostream_fd_ops = {
    .read = _sys_stdio_read,
    .write = _sys_stdio_write,
    .seek = _sys_stdio_seek,
    .set_callback = _sys_stdio_set_callback,
    .close = NULL,
};

static sys_iostream_t *_sys_iostream_fd_open(int fd) {
  sys_iostream_t *stream = _sys_iostream_alloc(&_sys_iostream_fd_ops);
  if (stream != NULL) {
    stream->backend.fd.value = fd;
  }
  return stream;
}

///////////////////////////////////////////////////////////////////////////////
// MODULE METHODS

void _sys_stdio_module_init(sys_stdio_t type) {
  (void)type; // sys_stdio_none selects the POSIX file descriptor default.
  sys_stdout = _sys_iostream_fd_open(STDOUT_FILENO);
  sys_stdin = _sys_iostream_fd_open(STDIN_FILENO);
  _sys_stdio_platform_init();
}

void _sys_stdio_module_exit(void) {
  _sys_stdio_platform_exit();
  sys_iostream_close(sys_stdout);
  sys_iostream_close(sys_stdin);
  sys_stdout = NULL;
  sys_stdin = NULL;
}