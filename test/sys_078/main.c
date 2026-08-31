#include <picofuse/sys.h>
#include <test/test.h>

int main(int argc, char *argv[]) {
  sys_init(argc, argv);

  ///////////////////////////////////////////////////////////////////////
  // NULL buffer and zero capacity are both rejected - cap == 0 would
  // leave no room even for a lone '\0'.

  {
    test_assert(sys_string_open(NULL, 16) == NULL);
    char zero_cap[4];
    test_assert(sys_string_open(zero_cap, 0) == NULL);
  }

  ///////////////////////////////////////////////////////////////////////
  // Always starts empty, regardless of whatever the buffer previously
  // held - including a buffer that already looks like a valid,
  // NUL-terminated C string. There's no reliable way to distinguish
  // genuine prior content from uninitialized memory that happens to
  // contain a stray '\0' (an uninitialized char buf[N] on the stack, the
  // overwhelmingly common case, is exactly that), so sys_string_open()
  // doesn't try.

  {
    char buf[8] = {'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'};
    sys_iostream_t *s = sys_string_open(buf, sizeof(buf));
    test_assert(s != NULL);
    test_assert(buf[0] == '\0');
    char out[8] = {0};
    test_assert(sys_iostream_read(s, out, sizeof(out)) == 0);
    sys_iostream_close(s);
  }
  {
    char buf[16] = "hello"; // looks like a valid string already
    sys_iostream_t *s = sys_string_open(buf, sizeof(buf));
    test_assert(s != NULL);
    test_assert(buf[0] == '\0'); // wiped, not preserved
    char out[8] = {0};
    test_assert(sys_iostream_read(s, out, sizeof(out)) == 0);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Seeking never goes past the tracked content length, even though
  // there's physically more capacity available in the buffer - content
  // has to be established with a real write first, since opening no
  // longer infers a starting length from the buffer's prior bytes.

  {
    char buf[16];
    sys_iostream_t *s = sys_string_open(buf, sizeof(buf));
    test_assert(sys_iostream_write(s, "hi", 2) == 2); // length now 2
    test_assert(sys_iostream_seek(s, 2, true) == 2);
    test_assert(sys_iostream_seek(s, 3, true) == -1);
    test_assert(sys_iostream_seek(s, 15, true) == -1); // well within cap,
                                                        // still rejected
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Writing at the current end of content grows the tracked length, and
  // the buffer stays correctly terminated exactly at the new length -
  // not just once writing is "done".

  {
    char buf[16];
    sys_iostream_t *s = sys_string_open(buf, sizeof(buf));
    test_assert(sys_iostream_write(s, "hi", 2) == 2);
    test_assert(sys_iostream_write(s, " there", 6) == 6);
    test_assert(sys_string_compare(buf, "hi there") == 0);
    test_assert(sys_iostream_seek(s, 8, true) == 8); // the new, grown length
    test_assert(sys_iostream_seek(s, 9, true) == -1);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Overwriting WITHIN existing content (not extending past it) must not
  // shrink or otherwise disturb the tracked length.

  {
    char buf[16];
    sys_iostream_t *s = sys_string_open(buf, sizeof(buf));
    test_assert(sys_iostream_write(s, "hello world", 11) == 11);
    test_assert(sys_iostream_seek(s, 0, true) == 0);
    test_assert(sys_iostream_write(s, "HELLO", 5) == 5); // same length
    test_assert(sys_string_compare(buf, "HELLO world") == 0); // tail intact
    test_assert(sys_iostream_seek(s, 11, true) == 11); // original length
    test_assert(sys_iostream_seek(s, 12, true) == -1);
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // Writing exactly up to the usable (cap - 1) capacity truncates
  // correctly, reports the actual bytes written, and never touches the
  // reserved terminator byte - a further write reports 0, genuinely
  // full, not silently dropped.

  {
    char buf[5]; // usable capacity 4
    sys_iostream_t *s = sys_string_open(buf, sizeof(buf));
    test_assert(sys_iostream_write(s, "abcdefgh", 8) == 4);
    test_assert(sys_string_compare(buf, "abcd") == 0);
    test_assert(buf[4] == '\0');
    test_assert(sys_iostream_write(s, "x", 1) == 0);
    test_assert(sys_string_compare(buf, "abcd") == 0); // unchanged
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // size == 1: no usable content bytes at all, just the terminator.

  {
    char buf[1];
    sys_iostream_t *s = sys_string_open(buf, sizeof(buf));
    test_assert(s != NULL);
    test_assert(buf[0] == '\0');
    test_assert(sys_iostream_write(s, "x", 1) == 0);
    test_assert(buf[0] == '\0');
    sys_iostream_close(s);
  }

  sys_exit();
  return 0;
}
