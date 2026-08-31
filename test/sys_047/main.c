#include <picofuse/sys.h>
#include <string.h>
#include <test/test.h>

test_main_sys(0) {

  ///////////////////////////////////////////////////////////////////////
  // sys_string_read - construction

  test_assert(sys_string_read(NULL) == NULL);

  {
    sys_iostream_t *s = sys_string_read("");
    test_assert(s != NULL);
    char buf[4];
    test_assert(sys_iostream_read(s, buf, sizeof(buf)) == 0); // EOF immediately
    sys_iostream_close(s);
  }

  ///////////////////////////////////////////////////////////////////////
  // sys_iostream_read - basic, exhaustion, and repeatability at EOF

  {
    sys_iostream_t *s = sys_string_read("hello");
    char buf[16] = {0};
    test_assert(sys_iostream_read(s, buf, sizeof(buf)) == 5);
    test_assert(memcmp(buf, "hello", 5) == 0);
    // Exhausted: reading again reports EOF, repeatably.
    test_assert(sys_iostream_read(s, buf, sizeof(buf)) == 0);
    test_assert(sys_iostream_read(s, buf, sizeof(buf)) == 0);
    sys_iostream_close(s);
  }

  // Reading fewer bytes than the string holds, then continuing, must
  // reconstruct the original content across the two reads.
  {
    sys_iostream_t *s = sys_string_read("abcdef");
    char buf[16] = {0};
    test_assert(sys_iostream_read(s, buf, 3) == 3);
    test_assert(memcmp(buf, "abc", 3) == 0);
    test_assert(sys_iostream_read(s, buf, 16) == 3); // only 3 bytes remain
    test_assert(memcmp(buf, "def", 3) == 0);
    test_assert(sys_iostream_read(s, buf, 16) == 0);
    sys_iostream_close(s);
  }

  // n == 0 is a no-op, not a crash.
  {
    sys_iostream_t *s = sys_string_read("x");
    char buf[1];
    test_assert(sys_iostream_read(s, buf, 0) == 0);
    test_assert(sys_iostream_read(s, buf, 1) == 1); // stream unaffected
    test_assert(buf[0] == 'x');
    sys_iostream_close(s);
  }

  // NULL stream is a no-op, not a crash.
  {
    char buf[1];
    test_assert(sys_iostream_read(NULL, buf, 1) == 0);
  }

  ///////////////////////////////////////////////////////////////////////
  // sys_iostream_peek - non-consuming, repeatable, and EOF

  {
    sys_iostream_t *s = sys_string_read("ab");
    test_assert(sys_iostream_peek(s) == 'a');
    test_assert(sys_iostream_peek(s) == 'a'); // repeatable, doesn't consume
    test_assert(sys_iostream_peek(s) == 'a');

    char buf[2] = {0};
    test_assert(sys_iostream_read(s, buf, 1) == 1);
    test_assert(buf[0] == 'a'); // read returns the same byte peek saw

    test_assert(sys_iostream_peek(s) == 'b');
    test_assert(sys_iostream_read(s, buf, 1) == 1);
    test_assert(buf[0] == 'b');

    test_assert(sys_iostream_peek(s) == SYS_IOSTREAM_EOF);
    test_assert(sys_iostream_peek(s) == SYS_IOSTREAM_EOF); // repeatable at EOF
    sys_iostream_close(s);
  }

  // Peek on an empty stream is EOF immediately.
  {
    sys_iostream_t *s = sys_string_read("");
    test_assert(sys_iostream_peek(s) == SYS_IOSTREAM_EOF);
    sys_iostream_close(s);
  }

  // A bulk read right after a peek must see the peeked byte as part of
  // the read, not skip past it.
  {
    sys_iostream_t *s = sys_string_read("xyz");
    test_assert(sys_iostream_peek(s) == 'x');
    char buf[3] = {0};
    test_assert(sys_iostream_read(s, buf, 3) == 3);
    test_assert(memcmp(buf, "xyz", 3) == 0);
    sys_iostream_close(s);
  }

  // Peeking right at the last byte, then bulk-reading past it, correctly
  // reports only that one byte, not a short-read miscount.
  {
    sys_iostream_t *s = sys_string_read("z");
    test_assert(sys_iostream_peek(s) == 'z');
    char buf[8] = {0};
    test_assert(sys_iostream_read(s, buf, 8) == 1);
    test_assert(buf[0] == 'z');
    sys_iostream_close(s);
  }

  // NULL stream is EOF, not a crash.
  test_assert(sys_iostream_peek(NULL) == SYS_IOSTREAM_EOF);

  ///////////////////////////////////////////////////////////////////////
  // sys_iostream_seek - relative and absolute, both directions

  {
    sys_iostream_t *s = sys_string_read("hello world");
    char buf[16] = {0};

    // Relative seek forward skips without reading.
    test_assert(sys_iostream_seek(s, 6, false) == 6);
    test_assert(sys_iostream_read(s, buf, 5) == 5);
    test_assert(memcmp(buf, "world", 5) == 0);

    // Relative seek backward, then re-read the same bytes.
    test_assert(sys_iostream_seek(s, -5, false) == 6);
    test_assert(sys_iostream_read(s, buf, 5) == 5);
    test_assert(memcmp(buf, "world", 5) == 0);

    // Absolute seek back to the start.
    test_assert(sys_iostream_seek(s, 0, true) == 0);
    test_assert(sys_iostream_read(s, buf, 5) == 5);
    test_assert(memcmp(buf, "hello", 5) == 0);

    sys_iostream_close(s);
  }

  // Seeking exactly to the end is valid (== length) and reads as EOF.
  {
    sys_iostream_t *s = sys_string_read("hi"); // length 2
    test_assert(sys_iostream_seek(s, 2, true) == 2);
    char buf[1];
    test_assert(sys_iostream_read(s, buf, 1) == 0);
    test_assert(sys_iostream_peek(s) == SYS_IOSTREAM_EOF);
    sys_iostream_close(s);
  }

  // Out-of-bounds seeks fail and leave the position unchanged.
  {
    sys_iostream_t *s = sys_string_read("hi"); // length 2
    test_assert(sys_iostream_seek(s, -1, true) == -1);  // negative absolute
    test_assert(sys_iostream_seek(s, 3, true) == -1);   // past the end
    test_assert(sys_iostream_seek(s, -1, false) == -1); // before the start

    // Position is still 0 - unaffected by every failed seek above.
    char buf[2] = {0};
    test_assert(sys_iostream_read(s, buf, 2) == 2);
    test_assert(memcmp(buf, "hi", 2) == 0);
    sys_iostream_close(s);
  }

  // A relative seek past the end also fails and leaves position
  // unchanged.
  {
    sys_iostream_t *s = sys_string_read("hi");
    test_assert(sys_iostream_seek(s, 1, false) == 1); // now at position 1
    test_assert(sys_iostream_seek(s, 5, false) == -1); // would go to 6, past length 2
    char buf[1];
    test_assert(sys_iostream_read(s, buf, 1) == 1 && buf[0] == 'i');
    sys_iostream_close(s);
  }

  // NULL stream fails, not a crash.
  test_assert(sys_iostream_seek(NULL, 0, true) == -1);

  ///////////////////////////////////////////////////////////////////////
  // sys_iostream_write - a string-backed stream is read-only

  {
    sys_iostream_t *s = sys_string_read("z");
    test_assert(sys_iostream_write(s, "a", 1) == 0);
    test_assert(sys_iostream_write(s, "", 0) == 0);
    // Confirms the stream itself is unaffected by the rejected write.
    char buf[1];
    test_assert(sys_iostream_read(s, buf, 1) == 1);
    test_assert(buf[0] == 'z');
    sys_iostream_close(s);
  }

  // NULL stream is a no-op, not a crash.
  test_assert(sys_iostream_write(NULL, "a", 1) == 0);

  ///////////////////////////////////////////////////////////////////////
  // sys_iostream_close - NULL is a no-op

  sys_iostream_close(NULL);

  ///////////////////////////////////////////////////////////////////////
  // Independent streams don't interfere with each other

  {
    sys_iostream_t *a = sys_string_read("AAA");
    sys_iostream_t *b = sys_string_read("BBB");
    char ca, cb;
    test_assert(sys_iostream_read(a, &ca, 1) == 1 && ca == 'A');
    test_assert(sys_iostream_read(b, &cb, 1) == 1 && cb == 'B');
    test_assert(sys_iostream_peek(a) == 'A');
    test_assert(sys_iostream_peek(b) == 'B');
    sys_iostream_close(a);
    sys_iostream_close(b);
  }

  ///////////////////////////////////////////////////////////////////////
  // Pool exhaustion and reuse after close()

  {
    sys_iostream_t *streams[SYS_IOSTREAM_CAPACITY];
    size_t count = 0;
    while (count < SYS_IOSTREAM_CAPACITY) {
      sys_iostream_t *s = sys_string_read("x");
      if (s == NULL) {
        break;
      }
      streams[count++] = s;
    }
    test_assert(count == SYS_IOSTREAM_CAPACITY);

    // The pool is now exhausted: one more allocation must fail.
    test_assert(sys_string_read("x") == NULL);

    // Closing one frees a slot for reuse.
    sys_iostream_close(streams[0]);
    sys_iostream_t *reused = sys_string_read("y");
    test_assert(reused != NULL);
    char buf[1];
    test_assert(sys_iostream_read(reused, buf, 1) == 1 && buf[0] == 'y');
    sys_iostream_close(reused);

    for (size_t i = 1; i < count; i++) {
      sys_iostream_close(streams[i]);
    }
  }

}
