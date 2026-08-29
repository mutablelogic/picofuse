#pragma once
#include <picofuse/sys/io.h>
#include <stdbool.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

// A stream's behavior is dispatched through this vtable - each backend
// implements plain sequential read/write, and seek(s, offset, abs) to
// move its position. For a backend with a stable addressable source (a
// string), seeking is just moving a cursor - nothing to buffer. A
// backend without one (a real file stream, say) would need to buffer
// recently-read bytes itself to support seeking backward; that's
// entirely its own concern, invisible here.
// sys_iostream_peek() (iostream.c) is generic - built once on top of
// read+seek - so backends never implement peeking directly.
typedef struct sys_iostream_ops_t {
  size_t (*read)(sys_iostream_t *s, char *buf, size_t n);
  size_t (*write)(sys_iostream_t *s, const char *buf, size_t n);
  ptrdiff_t (*seek)(sys_iostream_t *s, ptrdiff_t offset, bool abs);
  void (*close)(sys_iostream_t *s); // optional, NULL if nothing to release
} sys_iostream_ops_t;

struct sys_iostream_t {
  const sys_iostream_ops_t *ops;
  bool in_use;
  union {
    struct {
      const char *start; // original string, for absolute seeks
      size_t length;      // sys_string_bytes(start), computed once
      size_t pos;          // current offset from start, 0..length
    } string;
  } backend;
};

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS
//
// Backend implementations (string.c, ...) live in their own translation
// units and need this to build a stream; defined in iostream.c.

/** @brief Claims a free pool slot for a backend, or returns NULL if
 * SYS_IOSTREAM_CAPACITY streams are already open. */
sys_iostream_t *_sys_iostream_alloc(const sys_iostream_ops_t *ops);
