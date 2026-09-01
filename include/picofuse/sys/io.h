/**
 * @file io.h
 * @brief Opaque byte streams for reading, writing, seeking, and readiness
 * notification.
 * @defgroup SystemDataStream Stream I/O
 * @ingroup SystemData
 *
 * A sys_iostream_t is a small, fixed set of primitives - peek, read,
 * write, seek, close - implemented differently per backend. Instances
 * come from a static pool (SYS_IOSTREAM_CAPACITY), never the heap, and
 * are constructed by a source-specific function rather than directly.
 * String-backed streams come from sys_string_read() and sys_string_open()
 * (sys/string.h). Platform standard streams are exposed through sys_stdin
 * and sys_stdout (sys/stdio.h). A backend can optionally support readiness
 * callbacks through sys_iostream_set_callback(). sys/rune.h's tokenizer and
 * sys/scanner.h's scanner are built entirely on this interface, so other
 * backends can support them without changing their APIs.
 *
 * Example - read a stream in two passes by seeking back to the start:
 * @code
 * sys_iostream_t *s = sys_string_read("hello");
 *
 * char buf[6] = {0};
 * sys_iostream_read(s, buf, 5); // buf == "hello"
 *
 * sys_iostream_seek(s, 0, true); // back to the start
 * sys_iostream_read(s, buf, 5); // buf == "hello" again
 *
 * sys_iostream_close(s);
 * @endcode
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def SYS_IOSTREAM_CAPACITY
 * @ingroup SystemDataStream
 * @brief Maximum number of open streams.
 */
#ifndef SYS_IOSTREAM_CAPACITY
#define SYS_IOSTREAM_CAPACITY 8
#endif

/**
 * @def SYS_IOSTREAM_EOF
 * @ingroup SystemDataStream
 * @brief Sentinel returned by sys_iostream_peek() at the end of a stream.
 */
#define SYS_IOSTREAM_EOF (-1)

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief An opaque byte stream.
 * @ingroup SystemDataStream
 * @headerfile io.h picofuse/sys.h
 *
 * Instances come from a static pool (SYS_IOSTREAM_CAPACITY), with no heap
 * allocation. Streams are constructed by source-specific functions such as
 * sys_string_read() and sys_string_open(), or provided as platform standard
 * streams through sys_stdin and sys_stdout. Release caller-owned streams
 * with sys_iostream_close(); standard streams are released by sys_exit().
 */
typedef struct sys_iostream_t sys_iostream_t;

/**
 * @brief Stream readiness event flags.
 * @ingroup SystemDataStream
 *
 * Values may be combined with bitwise OR when registering interest in
 * multiple events.
 */
typedef enum sys_iostream_event_t {
  /** @brief No event. */
  sys_iostream_event_none = 0,
  /** @brief Data is available to read. */
  sys_iostream_event_read = 1u,
  /** @brief The stream can accept output. */
  sys_iostream_event_write = 2u,
} sys_iostream_event_t;

/**
 * @brief Callback invoked when a stream becomes ready for I/O.
 * @ingroup SystemDataStream
 * @param stream Stream whose readiness changed.
 * @param events Readiness events that occurred.
 * @param userdata User-defined data pointer provided to
 * sys_iostream_set_callback().
 */
typedef void (*sys_iostream_callback_t)(sys_iostream_t *stream,
                                        sys_iostream_event_t events,
                                        void *userdata);

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/**
 * @brief Release a stream back to the pool.
 * @ingroup SystemDataStream
 * @param s The stream to close, or NULL (a no-op).
 *
 * Releases any resources the stream's source holds (nothing, for a
 * string-backed stream) and frees its pool slot for reuse.
 */
extern void sys_iostream_close(sys_iostream_t *s);

///////////////////////////////////////////////////////////////////////////////
// METHODS

/**
 * @brief Look at the next byte without consuming it.
 * @ingroup SystemDataStream
 * @param s The stream to peek at.
 * @return The next unread byte, as an unsigned value 0-255, or
 * SYS_IOSTREAM_EOF if the stream is at its end. Calling this repeatedly
 * without an intervening sys_iostream_read() returns the same byte every
 * time.
 */
extern int sys_iostream_peek(sys_iostream_t *s);

/**
 * @brief Read bytes from a stream.
 * @ingroup SystemDataStream
 * @param s The stream to read from.
 * @param buf Destination buffer.
 * @param n Maximum number of bytes to read.
 * @return The number of bytes actually read into buf, which may be less
 * than n. 0 means the stream is at its end - it is not an error.
 */
extern size_t sys_iostream_read(sys_iostream_t *s, char *buf, size_t n);

/**
 * @brief Write bytes to a stream.
 * @ingroup SystemDataStream
 * @param s The stream to write to.
 * @param buf Source buffer.
 * @param n Number of bytes to write.
 * @return The number of bytes actually written, which may be less than
 * n (for example if the destination is full). 0 means nothing could be
 * written - for a read-only stream (such as one from sys_string_read()),
 * this is always the case.
 */
extern size_t sys_iostream_write(sys_iostream_t *s, const char *buf, size_t n);

/**
 * @brief Move a stream's read/write position.
 * @ingroup SystemDataStream
 * @param s The stream to seek.
 * @param offset Byte offset - absolute from the start when abs is true
 * (must be >= 0), otherwise relative to the current position (negative
 * moves backward).
 * @param abs true for an absolute seek, false for relative.
 * @return The resulting absolute position (>= 0) on success, or -1 if
 * the seek would go out of bounds - the position is left unchanged in
 * that case.
 */
extern ptrdiff_t sys_iostream_seek(sys_iostream_t *s, ptrdiff_t offset,
                                   bool abs);

/**
 * @brief Set a callback for stream readiness events.
 * @ingroup SystemDataStream
 * @param stream Stream to observe.
 * @param callback Callback to invoke, or NULL to remove the current callback.
 * @param userdata User-defined data pointer passed to callback.
 * @return true if the callback was registered, false if stream is NULL or its
 * backend does not support readiness notifications.
 */
extern bool sys_iostream_set_callback(sys_iostream_t *stream,
                                      sys_iostream_callback_t callback,
                                      void *userdata);

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
