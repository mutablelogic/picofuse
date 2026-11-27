#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

#ifndef SYSTEM_NAME_PICO
#include <unistd.h>

// Points the standard output at a pipe, runs fn, and puts it back, then
// reads what came through. Nothing here flushes C's stdout: what printing
// leaves in C's buffer doesn't reach the pipe (#36)
static size_t capture(void (*fn)(void), char *buf, size_t cap) {
  int fds[2];
  test_assert(pipe(fds) == 0);
  int saved = dup(STDOUT_FILENO);
  test_assert(saved >= 0);
  test_assert(dup2(fds[1], STDOUT_FILENO) == STDOUT_FILENO);
  close(fds[1]);

  fn();

  test_assert(dup2(saved, STDOUT_FILENO) == STDOUT_FILENO);
  close(saved);

  // The write end is closed now, so this reads everything, then 0
  size_t got = 0;
  ssize_t n;
  while (got < cap && (n = read(fds[0], buf + got, cap - got)) > 0) {
    got += (size_t)n;
  }
  close(fds[0]);
  return got;
}

static void mixed(void) {
  sys_printf("a");
  test_assert(sys_iostream_write(sys_stdout, "b", 1) == 1);
  sys_printf("c%d", 1);
  sys_puts("d");
  sys_printf("e\n");
  test_assert(sys_iostream_write(sys_stdout, "f\n", 2) == 2);
}

#ifndef NDEBUG
static void debug(void) {
  sys_debugf(NULL, "x");
  test_assert(sys_iostream_write(sys_stdout, "y\n", 2) == 2);
}
#endif
#endif

test_main_sys(0) {
#ifndef SYSTEM_NAME_PICO
  char buf[64];

  ///////////////////////////////////////////////////////////////////////
  // sys_printf() and sys_stdout come out in the order they were written

  size_t n = capture(mixed, buf, sizeof(buf));
  test_assert(n == 9 && memcmp(buf, "abc1de\nf\n", 9) == 0);

#ifndef NDEBUG
  ///////////////////////////////////////////////////////////////////////
  // And so do sys_debugf() and sys_stdout

  n = capture(debug, buf, sizeof(buf));
  test_assert(n == 12 && memcmp(buf, "[DEBUG] x\ny\n", 12) == 0);
#endif
#endif
}
