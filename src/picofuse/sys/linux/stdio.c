#include <picofuse/sys.h>

#include "../iostream/iostream.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static int _sys_stdio_epoll_fd = -1;
static int _sys_stdio_wakeup_fd = -1;
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
  struct epoll_event events[4];

  while (atomic_load(&_sys_stdio_running)) {
    int count = epoll_wait(_sys_stdio_epoll_fd, events, 4, -1);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }

    for (int i = 0; i < count; i++) {
      if (events[i].data.ptr == NULL) {
        uint64_t discarded;
        read(_sys_stdio_wakeup_fd, &discarded, sizeof(discarded));
        continue;
      }
      _sys_stdio_dispatch(events[i].data.ptr);
    }
  }
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// MODULE METHODS

bool _sys_stdio_platform_init(void) {
  _sys_stdio_epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (_sys_stdio_epoll_fd < 0) {
    return false;
  }

  _sys_stdio_wakeup_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (_sys_stdio_wakeup_fd < 0) {
    close(_sys_stdio_epoll_fd);
    _sys_stdio_epoll_fd = -1;
    return false;
  }

  struct epoll_event event = {.events = EPOLLIN, .data.ptr = NULL};
  if (epoll_ctl(_sys_stdio_epoll_fd, EPOLL_CTL_ADD, _sys_stdio_wakeup_fd,
                &event) != 0) {
    close(_sys_stdio_wakeup_fd);
    close(_sys_stdio_epoll_fd);
    _sys_stdio_wakeup_fd = -1;
    _sys_stdio_epoll_fd = -1;
    return false;
  }

  atomic_store(&_sys_stdio_running, true);
  if (pthread_create(&_sys_stdio_thread, NULL, _sys_stdio_thread_main, NULL) !=
      0) {
    atomic_store(&_sys_stdio_running, false);
    close(_sys_stdio_wakeup_fd);
    close(_sys_stdio_epoll_fd);
    _sys_stdio_wakeup_fd = -1;
    _sys_stdio_epoll_fd = -1;
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
  uint64_t wakeup = 1;
  write(_sys_stdio_wakeup_fd, &wakeup, sizeof(wakeup));
  pthread_join(_sys_stdio_thread, NULL);
  close(_sys_stdio_wakeup_fd);
  close(_sys_stdio_epoll_fd);
  _sys_stdio_wakeup_fd = -1;
  _sys_stdio_epoll_fd = -1;
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
  struct epoll_event event = {
      .events = EPOLLIN | EPOLLERR | EPOLLHUP,
      .data.ptr = stream,
  };
  int operation =
      callback == NULL
          ? EPOLL_CTL_DEL
          : (_sys_stdio_stdin_registered ? EPOLL_CTL_MOD : EPOLL_CTL_ADD);
  int result =
      epoll_ctl(_sys_stdio_epoll_fd, operation, stream->backend.fd.value,
                callback == NULL ? NULL : &event);
  if (result == 0) {
    _sys_stdio_callback = callback;
    _sys_stdio_userdata = userdata;
    _sys_stdio_stdin_registered = callback != NULL;
  }
  pthread_mutex_unlock(&_sys_stdio_lock);
  return result == 0;
}