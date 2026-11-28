#include <errno.h>
#include <fcntl.h>
#include <picofuse/sys.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS
//
// The signal handler only writes the signal's number to a pipe, which is safe
// in a handler. A thread reads it and calls the callback, which can then take
// locks, such as HID's or an event queue's, without deadlocking against the
// thread the signal interrupted.

static int _sys_env_signal_pipe[2] = {-1, -1};
static pthread_once_t _sys_env_signal_once = PTHREAD_ONCE_INIT;
static bool _sys_env_signal_ready = false;

// Held while the callback runs, and while it's replaced, so once
// sys_env_signalhandler() returns, the previous callback isn't running and won't
// be called again. Recursive, so a callback can replace itself.
static pthread_mutex_t _sys_env_signal_lock;
static sys_env_signal_callback_t _sys_env_signal_callback_ex = NULL;
static sys_env_signal_t _sys_env_signal_mask = sys_env_signal_none;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static void _sys_env_signal_handler(int signo) {
  int saved = errno;
  unsigned char byte = (unsigned char)signo;
  // Non-blocking: if the pipe's full, the signal is dropped rather than
  // blocking the thread it interrupted
  ssize_t written = write(_sys_env_signal_pipe[1], &byte, 1);
  (void)written;
  errno = saved;
}

static sys_env_signal_t _sys_env_signal_from(int signo) {
  switch (signo) {
  case SIGTERM:
    return sys_env_signal_term;
  case SIGINT:
    return sys_env_signal_int;
  case SIGQUIT:
    return sys_env_signal_quit;
  default:
    return sys_env_signal_none;
  }
}

static void *_sys_env_signal_thread(void *arg) {
  (void)arg;

  // The handler doesn't run on this thread, so it never interrupts a callback
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGTERM);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGQUIT);
  pthread_sigmask(SIG_BLOCK, &set, NULL);

  for (;;) {
    unsigned char byte;
    ssize_t n = read(_sys_env_signal_pipe[0], &byte, 1);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n != 1) {
      return NULL;
    }
    sys_env_signal_t signal = _sys_env_signal_from(byte);
    pthread_mutex_lock(&_sys_env_signal_lock);
    sys_env_signal_callback_t callback = _sys_env_signal_callback_ex;
    sys_env_signal_t mask = _sys_env_signal_mask;
    if (callback != NULL && signal != sys_env_signal_none &&
        (mask == sys_env_signal_none || (mask & signal) != 0)) {
      callback(signal);
    }
    pthread_mutex_unlock(&_sys_env_signal_lock);
  }
}

static void _sys_env_signal_init(void) {
  pthread_mutexattr_t attr;
  if (pthread_mutexattr_init(&attr) != 0) {
    return;
  }
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  bool locked = pthread_mutex_init(&_sys_env_signal_lock, &attr) == 0;
  pthread_mutexattr_destroy(&attr);
  if (!locked || pipe(_sys_env_signal_pipe) != 0) {
    return;
  }
  fcntl(_sys_env_signal_pipe[0], F_SETFD, FD_CLOEXEC);
  fcntl(_sys_env_signal_pipe[1], F_SETFD, FD_CLOEXEC);
  fcntl(_sys_env_signal_pipe[1], F_SETFL,
        fcntl(_sys_env_signal_pipe[1], F_GETFL) | O_NONBLOCK);

  pthread_t thread;
  if (pthread_create(&thread, NULL, _sys_env_signal_thread, NULL) != 0) {
    return;
  }
  pthread_detach(thread);
  _sys_env_signal_ready = true;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS

bool sys_env_signalhandler(sys_env_signal_t mask,
                           sys_env_signal_callback_t callback) {
  pthread_once(&_sys_env_signal_once, _sys_env_signal_init);
  if (!_sys_env_signal_ready) {
    return false;
  }

  bool success = true;
  pthread_mutex_lock(&_sys_env_signal_lock);

  // A new registration always fully replaces the previous one
  if (signal(SIGTERM, SIG_DFL) == SIG_ERR) {
    success = false;
  }
  if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
    success = false;
  }
  if (signal(SIGQUIT, SIG_DFL) == SIG_ERR) {
    success = false;
  }
  _sys_env_signal_callback_ex = NULL;
  _sys_env_signal_mask = sys_env_signal_none;

  // Set up new signal handlers.
  if (callback != NULL) {
    if (mask & sys_env_signal_term || mask == 0) {
      if (signal(SIGTERM, _sys_env_signal_handler) == SIG_ERR) {
        success = false;
      }
    }
    if (mask & sys_env_signal_int || mask == 0) {
      if (signal(SIGINT, _sys_env_signal_handler) == SIG_ERR) {
        success = false;
      }
    }
    if (mask & sys_env_signal_quit || mask == 0) {
      if (signal(SIGQUIT, _sys_env_signal_handler) == SIG_ERR) {
        success = false;
      }
    }

    if (success) {
      _sys_env_signal_callback_ex = callback;
      _sys_env_signal_mask = mask;
    }
  }

  pthread_mutex_unlock(&_sys_env_signal_lock);
  return success;
}
