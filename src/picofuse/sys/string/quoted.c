#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

// Writes one byte to out[*written] if it still fits within cap, then
// advances *written regardless - matching sys_scanner_token()'s silent-
// truncation convention (the returned/final *written can exceed cap).
static inline void _sys_string_append(char *out, size_t cap, size_t *written, char c) {
  if (*written < cap) {
    out[*written] = c;
  }
  (*written)++;
}

// Encodes r (0x0000-0xFFFF, never a UTF-16 surrogate - see
// sys_string_parse_escape()) as UTF-8 into buf, returning the byte count
// (1-3).
static inline size_t _sys_string_utf8_encode(rune_t r, char *buf) {
  if (r < 0x80) {
    buf[0] = (char)r;
    return 1;
  }
  if (r < 0x800) {
    buf[0] = (char)(0xC0 | (r >> 6));
    buf[1] = (char)(0x80 | (r & 0x3F));
    return 2;
  }
  buf[0] = (char)(0xE0 | (r >> 12));
  buf[1] = (char)(0x80 | ((r >> 6) & 0x3F));
  buf[2] = (char)(0x80 | (r & 0x3F));
  return 3;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Parse a quoted string, unescaping its content.
 * @param str String to parse, or NULL.
 * @param len Length of the string, or 0 if unknown (NULL-terminated).
 * @param out Buffer to write the unescaped content to.
 * @param cap Capacity of the output buffer.
 * @return The number of bytes written to out, or -1 on error.
 */
ptrdiff_t sys_string_parse_quoted(const char *str, size_t len, char *out,
                                  size_t cap) {
  if (str == NULL) {
    return -1;
  }
  char quote = str[0];
  if (quote != '\'' && quote != '"') {
    return -1;
  }

  // A NULL out is only safe to write through when cap is also 0 - clamp
  // it here rather than trusting the caller to keep the two consistent,
  // so _sys_string_append() below never dereferences NULL, and the
  // final min(written, cap) below correctly reports 0 bytes copied (not
  // the true decoded length) when nothing was actually written anywhere.
  if (out == NULL) {
    cap = 0;
  }

  const char *pos = str + 1;
  size_t written = 0;

  for (;;) {
    char c = *pos;
    if (c == '\0') {
      return -1; // unterminated - ran out of string before a closing quote
    }
    if (c == quote) {
      pos++;
      break;
    }
    if (c == '\\') {
      char next = pos[1];
      if (next == '\0') {
        return -1; // trailing backslash, nothing to escape
      }
      if (next == '\'') {
        // Not in sys_string_parse_escape()'s JSON-derived table, but
        // quoted strings need it to escape a literal single quote.
        _sys_string_append(out, cap, &written, '\'');
        pos += 2;
        continue;
      }
      // Passing exactly the escape's own length (2, or 6 for \uXXXX) -
      // not 0 - means sys_string_parse_escape() only ever looks at
      // these bytes, regardless of what follows them here.
      size_t want = (next == 'u') ? 6 : 2;
      rune_t r;
      if (!sys_string_parse_escape(pos, want, &r)) {
        return -1;
      }
      char buf[3];
      size_t n = _sys_string_utf8_encode(r, buf);
      for (size_t i = 0; i < n; i++) {
        _sys_string_append(out, cap, &written, buf[i]);
      }
      pos += want;
      continue;
    }
    _sys_string_append(out, cap, &written, c);
    pos++;
  }

  // str must contain exactly this one quoted string and nothing else -
  // same "nothing may follow" rule as sys_string_parse_escape().
  if (len != 0) {
    if ((size_t)(pos - str) != len) {
      return -1;
    }
  } else if (*pos != '\0') {
    return -1;
  }

  return (ptrdiff_t)(written < cap ? written : cap);
}
