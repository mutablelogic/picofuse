#include <picofuse/sys.h>

#include "../iostream/iostream.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/event.h>
#include <sys/types.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static int _sys_stdio_kqueue_fd = -1;
static pthread_t _sys_stdio_thread;
static pthread_mutex_t _sys_stdio_lock = PTHREAD_MUTEX_INITIALIZER;
static sys_iostream_callback_t _sys_stdio_callback = NULL;
static void *_sys_stdio_userdata = NULL;
static atomic_bool _sys_stdio_running = false;
static bool _sys_stdio_thread_started = false;
static bool _sys_stdio_stdin_registered = false;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static void _sys_stdio_dispatch(sys_iostream_t *stream) {
  pthread_mutex_lock(&_sys_stdio_lock);
  sys_iostream_callback_t callback = _sys_stdio_callback;
  void *userdata = _sys_stdio_userdata;
  pthread_mutex_unlock(&_sys_stdio_lock);

  if (callback != NULL) {
    callback(stream, sys_iostream_event_read, userdata);
  }
}

static void *_sys_stdio_thread_main(void *arg) {
  (void)arg;
  struct kevent events[4];

  while (atomic_load(&_sys_stdio_running)) {
    int count = kevent(_sys_stdio_kqueue_fd, NULL, 0, events, 4, NULL);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }

    for (int i = 0; i < count; i++) {
      if (events[i].filter == EVFILT_READ) {
        _sys_stdio_dispatch(events[i].udata);
      }
    }
  }
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// MODULE METHODS

bool _sys_stdio_platform_init(void) {
  _sys_stdio_kqueue_fd = kqueue();
  if (_sys_stdio_kqueue_fd < 0) {
    return false;
  }

  struct kevent event;
  EV_SET(&event, 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
  if (kevent(_sys_stdio_kqueue_fd, &event, 1, NULL, 0, NULL) != 0) {
    close(_sys_stdio_kqueue_fd);
    _sys_stdio_kqueue_fd = -1;
    return false;
  }

  atomic_store(&_sys_stdio_running, true);
  if (pthread_create(&_sys_stdio_thread, NULL, _sys_stdio_thread_main, NULL) !=
      0) {
    atomic_store(&_sys_stdio_running, false);
    close(_sys_stdio_kqueue_fd);
    _sys_stdio_kqueue_fd = -1;
    return false;
  }
  _sys_stdio_thread_started = true;
  return true;
}

void _sys_stdio_platform_exit(void) {
  if (!_sys_stdio_thread_started) {
    return;
  }

  atomic_store(&_sys_stdio_running, false);
  struct kevent event;
  EV_SET(&event, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
  kevent(_sys_stdio_kqueue_fd, &event, 1, NULL, 0, NULL);
  pthread_join(_sys_stdio_thread, NULL);
  close(_sys_stdio_kqueue_fd);
  _sys_stdio_kqueue_fd = -1;
  _sys_stdio_thread_started = false;
  _sys_stdio_stdin_registered = false;
}

bool _sys_stdio_platform_set_callback(sys_iostream_t *stream,
                                      sys_iostream_callback_t callback,
                                      void *userdata) {
  if (stream != sys_stdin || !_sys_stdio_thread_started) {
    return false;
  }

  pthread_mutex_lock(&_sys_stdio_lock);
  if (callback == NULL && !_sys_stdio_stdin_registered) {
    _sys_stdio_callback = NULL;
    _sys_stdio_userdata = NULL;
    pthread_mutex_unlock(&_sys_stdio_lock);
    return true;
  }
  struct kevent event;
  int flags = callback == NULL ? EV_DELETE : EV_ADD | EV_ENABLE | EV_CLEAR;
  EV_SET(&event, (uintptr_t)stream->backend.fd.value, EVFILT_READ, flags, 0, 0,
         stream);
  int result = kevent(_sys_stdio_kqueue_fd, &event, 1, NULL, 0, NULL);
  if (result == 0) {
    _sys_stdio_callback = callback;
    _sys_stdio_userdata = userdata;
    _sys_stdio_stdin_registered = callback != NULL;
  }
  pthread_mutex_unlock(&_sys_stdio_lock);
  return result == 0;
}