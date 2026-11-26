#include <picofuse/sys.h>
#include <test/test.h>

#ifndef SYSTEM_NAME_PICO
#include <unistd.h>
#endif

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // A stream which can seek puts a peeked byte back, as before

  {
    sys_iostream_t *s = sys_string_read("abc");
    test_assert(s != NULL);
    test_assert(sys_iostream_peek(s) == 'a');
    test_assert(sys_iostream_peek(s) == 'a');
    test_assert(sys_iostream_seek(s, 0, false) == 0);
    test_assert(sys_iostream_seek(s, 1, false) == 1);
    test_assert(sys_iostream_peek(s) == 'b');
    test_assert(sys_iostream_seek(s, 0, true) == 0);
    char buf[4] = {0};
    test_assert(sys_iostream_read(s, buf, sizeof(buf)) == 3);
    test_assert_strequal(buf, "abc");
    test_assert(sys_iostream_peek(s) == SYS_IOSTREAM_EOF);
    sys_iostream_close(s);
  }

#ifndef SYSTEM_NAME_PICO
  ///////////////////////////////////////////////////////////////////////
  // Standard input from a pipe, which can't seek, keeps the peeked byte
  // (issue #26)

  {
    int fds[2];
    test_assert(pipe(fds) == 0 && dup2(fds[0], 0) == 0);
    test_assert(write(fds[1], "abc", 3) == 3);
    test_assert(sys_iostream_seek(sys_stdin, 0, false) < 0);

    // Peeking again gives the same byte, and so does reading
    test_assert(sys_iostream_peek(sys_stdin) == 'a');
    test_assert(sys_iostream_peek(sys_stdin) == 'a');
    test_assert(!sys_iostream_eof(sys_stdin));
    test_assert(sys_iostream_seek(sys_stdin, 0, false) < 0);
    test_assert(sys_iostream_seek(sys_stdin, 1, false) < 0);
    test_assert(sys_iostream_seek(sys_stdin, 0, true) < 0);
    test_assert(sys_iostream_peek(sys_stdin) == 'a');
    char buf[8] = {0};
    test_assert(sys_iostream_read(sys_stdin, buf, sizeof(buf)) == 1);
    test_assert(buf[0] == 'a');
    test_assert(sys_iostream_read(sys_stdin, buf, sizeof(buf)) == 2);
    test_assert(buf[0] == 'b' && buf[1] == 'c');

    // A byte of every value, including those which are negative as a char
    test_assert(write(fds[1], "\xFF\x00", 2) == 2);
    test_assert(sys_iostream_peek(sys_stdin) == 0xFF);
    test_assert(sys_iostream_read(sys_stdin, buf, 1) == 1);
    test_assert((uint8_t)buf[0] == 0xFF);
    test_assert(sys_iostream_peek(sys_stdin) == 0x00);
    test_assert(sys_iostream_peek(sys_stdin) == 0x00);
    test_assert(sys_iostream_read(sys_stdin, buf, 1) == 1 && buf[0] == 0);

    // At the end
    test_assert(write(fds[1], "d", 1) == 1 && close(fds[1]) == 0);
    test_assert(sys_iostream_peek(sys_stdin) == 'd');
    test_assert(sys_iostream_read(sys_stdin, buf, sizeof(buf)) == 1);
    test_assert(buf[0] == 'd');
    test_assert(sys_iostream_peek(sys_stdin) == SYS_IOSTREAM_EOF);
    test_assert(sys_iostream_read(sys_stdin, buf, sizeof(buf)) == 0);
  }
#endif
}
