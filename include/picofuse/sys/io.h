/**
 * @file io.h
 * @brief Opaque byte-stream type for reading and writing, backed by
 * different sources (strings today; files and others can be added later
 * without changing this interface).
 * @defgroup SystemDataStream Stream I/O
 * @ingroup SystemData
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
 * Instances come from a static pool (SYS_IOSTREAM_CAPACITY) - no heap
 * allocation. Constructed by a source-specific function (e.g.
 * sys_string_read() in sys/string.h) and released with
 * sys_iostream_close().
 */
typedef struct sys_iostream_t sys_iostream_t;

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

///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
