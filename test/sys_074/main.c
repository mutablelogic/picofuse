#include <picofuse/sys.h>
#include <test/test.h>

#if !defined(SYSTEM_NAME_PICO)
#include <signal.h>

static sys_env_signal_t last_signal = SYS_ENV_SIGNAL_NONE;
static int call_count = 0;

static void record_callback(sys_env_signal_t signal) {
  last_signal = signal;
  call_count++;
}

static sys_env_signal_t last_signal_b = SYS_ENV_SIGNAL_NONE;
static int call_count_b = 0;

static void record_callback_b(sys_env_signal_t signal) {
  last_signal_b = signal;
  call_count_b++;
}

// Returns true if sig's current disposition is SIG_DFL, without ever
// raising it - signal() returns the previous handler, so installing
// SIG_IGN and inspecting (then restoring) what it displaced tells us
// this safely, even for signals whose default action would terminate
// the process (like SIGTERM).
static bool is_default_disposition(int sig) {
  void (*prev)(int) = signal(sig, SIG_IGN);
  bool is_default = (prev == SIG_DFL);
  signal(sig, prev);
  return is_default;
}
#endif

test_main_sys() {

#if !defined(SYSTEM_NAME_PICO)
  ///////////////////////////////////////////////////////////////////////
  // A registered callback actually fires when the real signal is
  // raised, with the correct sys_env_signal_t mapping for each of
  // SIGTERM/SIGINT/SIGQUIT. "Going beyond picofuse" here since picofuse
  // itself has no API to send a signal - raise() is POSIX, not ours.

  {
    call_count = 0;
    test_assert(sys_env_signalhandler(SYS_ENV_SIGNAL_TERM, record_callback));
    raise(SIGTERM);
    test_assert(call_count == 1 && last_signal == SYS_ENV_SIGNAL_TERM);
  }
  {
    call_count = 0;
    test_assert(sys_env_signalhandler(SYS_ENV_SIGNAL_INT, record_callback));
    raise(SIGINT);
    test_assert(call_count == 1 && last_signal == SYS_ENV_SIGNAL_INT);
  }
  {
    call_count = 0;
    test_assert(sys_env_signalhandler(SYS_ENV_SIGNAL_QUIT, record_callback));
    raise(SIGQUIT);
    test_assert(call_count == 1 && last_signal == SYS_ENV_SIGNAL_QUIT);
  }

  ///////////////////////////////////////////////////////////////////////
  // A single callback can be registered for more than one signal at
  // once via an OR'd mask, and each still reports its own signal value.

  {
    call_count = 0;
    test_assert(sys_env_signalhandler(SYS_ENV_SIGNAL_TERM | SYS_ENV_SIGNAL_INT,
                                       record_callback));
    raise(SIGTERM);
    raise(SIGINT);
    test_assert(call_count == 2);
    test_assert(last_signal == SYS_ENV_SIGNAL_INT); // most recent
  }

  ///////////////////////////////////////////////////////////////////////
  // A new registration fully replaces the previous one, including
  // signals NOT mentioned in the new mask - a signal registered by an
  // earlier call must not stay silently wired to any callback once
  // superseded by a call with a different, narrower mask.

  {
    call_count = 0;
    call_count_b = 0;
    test_assert(sys_env_signalhandler(SYS_ENV_SIGNAL_TERM, record_callback));
    test_assert(sys_env_signalhandler(SYS_ENV_SIGNAL_INT, record_callback_b));

    // SIGTERM must be back to its default disposition - checked without
    // raising it, since SIG_DFL for SIGTERM terminates the process.
    test_assert(is_default_disposition(SIGTERM));

    raise(SIGINT);
    test_assert(call_count_b == 1 && last_signal_b == SYS_ENV_SIGNAL_INT);
    test_assert(call_count == 0); // the old TERM callback never fires again
  }

  ///////////////////////////////////////////////////////////////////////
  // mask == 0 covers every supported signal, and callback == NULL
  // disables handling entirely, restoring default dispositions.

  {
    call_count = 0;
    test_assert(sys_env_signalhandler(0, record_callback));
    raise(SIGTERM);
    raise(SIGINT);
    raise(SIGQUIT);
    test_assert(call_count == 3);

    test_assert(sys_env_signalhandler(0, NULL));
    test_assert(is_default_disposition(SIGTERM));
    test_assert(is_default_disposition(SIGINT));
    test_assert(is_default_disposition(SIGQUIT));
  }
#else
  ///////////////////////////////////////////////////////////////////////
  // Pico has no POSIX-style signal delivery - sys_env_signalhandler is
  // documented to report "not supported" here, for any input.

  test_assert(!sys_env_signalhandler(SYS_ENV_SIGNAL_TERM, NULL));
  test_assert(!sys_env_signalhandler(0, NULL));
#endif

}
