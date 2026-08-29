#pragma once
#include <picofuse/sys.h>

// Decodes the next rune from stream. Always attempts to read the
// maximum possible UTF-8 width (4 bytes) up front, then reuses the
// existing, already-tested sys_rune_next() to decode/validate from that
// local buffer (zero-padded if fewer bytes were actually available -
// sys_rune_next() never reads past a 0x00 byte, so this is safe even at
// a truncated end of stream). Any bytes read but not actually part of
// the decoded rune (or error unit) are pushed back with
// sys_iostream_seek(), so this has the exact same behavior - including
// error recovery - as sys_rune_next() over an in-memory string.
//
// Shared by tokenize.c and scanner.c - not part of the public API.
bool _sys_rune_decode(sys_iostream_t *stream, rune_t *r, size_t *width);
