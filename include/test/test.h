/**
 * @file test/test.h
 * @brief Assertion macros for picofuse system tests.
 */
#pragma once
#include <picofuse/app.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <string.h>

/**
 * @def TEST_STDIO
 * @brief Standard I/O backend test_main_sys()/test_main_hw() initialize.
 * Defaults to sys_stdio_rtt - override by defining it before including this
 * header (or via a compile definition, e.g. picofuse_test()'s TESTRUNNER_STDIO
 * option) to route a test's output elsewhere instead, e.g. sys_stdio_uart
 * when diagnosing whether a given test's own RTT traffic is itself the
 * problem, as opposed to whatever it's actually testing.
 */
#ifndef TEST_STDIO
#define TEST_STDIO sys_stdio_rtt
#endif

/**
 * @def test_assert(condition)
 * @brief Asserts that a condition is true, panicking with the failed
 * condition and file/line context if it is not. Always checked, regardless
 * of NDEBUG.
 */
#define test_assert(condition)                                                 \
  do {                                                                         \
    if (!(condition)) {                                                        \
      sys_panicf("[TEST] FAIL: %s, file %s, line %d", #condition, __FILE__,    \
                 __LINE__);                                                    \
    }                                                                          \
  } while (0)

/**
 * @def test_assert_strequal(actual, expected)
 * @brief Asserts that two null-terminated strings are equal, panicking with
 * both values and file/line context if they are not.
 */
#define test_assert_strequal(actual, expected)                                 \
  do {                                                                         \
    const char *_test_actual = (actual);                                       \
    const char *_test_expected = (expected);                                   \
    if (strcmp(_test_actual, _test_expected) != 0) {                           \
      sys_panicf("[TEST] FAIL: expected \"%s\" but got \"%s\", file %s, "      \
                 "line %d",                                                    \
                 _test_expected, _test_actual, __FILE__, __LINE__);            \
    }                                                                          \
  } while (0)

/**
 * @def test_main_sys(arena_size)
 * @brief Declares a test's entry point in place of a raw main(). Wraps
 * sys_init()/sys_exit() around the test body and brackets it with
 * "[TEST] [INIT] \<env\>" / "[TEST] [EXIT] \<env\>" markers, where `\<env\>`
 * is sys_env_name(). The test body receives the process's own (argc, argv) -
 * unused by most tests, so they're marked maybe-unused to stay warning-free.
 * @param arena_size Forwarded to sys_init() - the default arena's capacity
 * in bytes, or `0` to leave sys_malloc() and friends routed to the system
 * allocator.
 *
 * Usage:
 *   test_main_sys(0) {
 *     ...test body, optionally using argc/argv...
 *   }
 */
#define test_main_sys(arena_size)                                              \
  static void _test_main(int argc, char *argv[]);                              \
  int main(int argc, char *argv[]) {                                           \
    sys_init(argc, argv, (arena_size), TEST_STDIO);                         \
    sys_printf("[TEST] [INIT] %s\n", sys_env_name());                          \
    _test_main(argc, argv);                                                    \
    sys_printf("[TEST] [EXIT] %s\n", sys_env_name());                          \
    sys_exit();                                                                \
    return 0;                                                                  \
  }                                                                            \
  static void _test_main(int argc __attribute__((unused)),                     \
                         char *argv[] __attribute__((unused)))

/**
 * @def test_main_hw(arena_size)
 * @brief Like test_main_sys(), but also wraps hw_init()/hw_exit() around the
 * test body, inside the sys_init()/sys_exit() pair (hw depends on sys, so it
 * must be initialized after and torn down before it).
 * @param arena_size Forwarded to sys_init() - the default arena's capacity
 * in bytes, or `0` to leave sys_malloc() and friends routed to the system
 * allocator.
 *
 * Usage:
 *   test_main_hw(0) {
 *     ...test body, optionally using argc/argv...
 *   }
 */
#define test_main_hw(arena_size)                                               \
  static void _test_main(int argc, char *argv[]);                              \
  int main(int argc, char *argv[]) {                                           \
    sys_init(argc, argv, (arena_size), TEST_STDIO);                         \
    sys_printf("[TEST] [INIT] %s\n", sys_env_name());                          \
    hw_init();                                                                 \
    _test_main(argc, argv);                                                    \
    hw_exit();                                                                 \
    sys_printf("[TEST] [EXIT] %s\n", sys_env_name());                          \
    sys_exit();                                                                \
    return 0;                                                                  \
  }                                                                            \
  static void _test_main(int argc __attribute__((unused)),                     \
                         char *argv[] __attribute__((unused)))

/**
 * @def test_main_app(flags, on_event)
 * @brief Like test_main_sys()/test_main_hw(), but wraps app_main() around
 * the test body instead of calling sys_init()/hw_init() directly -
 * app_main() already wraps those itself (see picofuse/app.h), so calling
 * either again here would double-initialize. The test body runs inside
 * app_main()'s on_start() callback instead, with the app_t* it receives
 * available the same way argc/argv are. Prints the "[TEST] [INIT]" marker
 * testrunner waits for right before the body runs, mirroring
 * test_main_sys()/test_main_hw()'s own print-then-teardown ordering (they
 * also print "[TEST] [EXIT]" before tearing down, not after) - see @p
 * on_event for who prints "[TEST] [EXIT]" and calls app_shutdown().
 * @param flags Forwarded to app_main() - see app_flag_t. Passing
 * app_flag_multicore here needs real hardware to exercise meaningfully
 * (see hid_008's own gating for the same reason); most tests won't need
 * it. app_flag_stdio_rtt is OR'd in automatically - unlike
 * test_main_sys()/test_main_hw() (which pick it up via TEST_STDIO's own
 * default), app_main() otherwise defaults to sys_stdio_none, which
 * testrunner's RTT-based output capture can't read anything from on a
 * PICO_BOARD build; harmless on a host build, which ignores the stdio
 * type entirely.
 * @param on_event An app_callback_event_t, forwarded to app_main() as its
 * own on_event, or NULL for a synchronous test with no events of its own
 * to react to - in that case, "[TEST] [EXIT]" is printed and
 * app_shutdown(0) is called for you immediately after the test body
 * returns, same as before this parameter existed. A non-NULL @p on_event
 * needs to already be declared (it's passed by name), and owns both of
 * those itself, once it decides the test is done (app_shutdown() is safe
 * to call from any callback - see its own doc) - events posted by a
 * device app_main() itself registers (a HID source enabled via @p flags,
 * for example) only start arriving once the run loop is actually
 * ticking, which is after on_start() - and so after the test body -
 * returns; there's no way to observe them from there.
 *
 * Usage:
 *   test_main_app(app_flag_signal, NULL) {
 *     ...synchronous test body, optionally using app/argc/argv...
 *   }
 *
 *   static void _on_event(app_t *app, sys_event_t event, void *userdata) {
 *     ...
 *     sys_printf("[TEST] [EXIT] %s\n", sys_env_name());
 *     app_shutdown(0);
 *   }
 *
 *   test_main_app(app_flag_usb, _on_event) {
 *     ...one-time setup, runs in on_start before the run loop starts...
 *   }
 */
#define test_main_app(flags, on_event)                                        \
  static void _test_main(app_t *app, int argc, char *argv[]);                 \
  typedef struct {                                                            \
    int argc;                                                                 \
    char **argv;                                                              \
  } _test_app_ctx_t;                                                          \
  static void _test_on_start(app_t *app, void *userdata) {                    \
    _test_app_ctx_t *ctx = (_test_app_ctx_t *)userdata;                       \
    sys_printf("[TEST] [INIT] %s\n", sys_env_name());                         \
    _test_main(app, ctx->argc, ctx->argv);                                    \
    if ((on_event) == NULL) {                                                 \
      sys_printf("[TEST] [EXIT] %s\n", sys_env_name());                       \
      app_shutdown(0);                                                        \
    }                                                                         \
  }                                                                           \
  int main(int argc, char *argv[]) {                                         \
    _test_app_ctx_t ctx = {argc, argv};                                       \
    return app_main(argc, argv, (flags) | app_flag_stdio_rtt, _test_on_start, \
                    (on_event), &ctx);                                       \
  }                                                                           \
  static void _test_main(app_t *app __attribute__((unused)),                  \
                         int argc __attribute__((unused)),                    \
                         char *argv[] __attribute__((unused)))
