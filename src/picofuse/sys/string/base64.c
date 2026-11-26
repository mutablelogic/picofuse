#include <picofuse/sys.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static const char _sys_base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Returns the value of a base64 character, or -1 if it isn't one
static inline int _sys_base64_value(char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  } else if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  } else if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  } else if (c == '+') {
    return 62;
  } else if (c == '/') {
    return 63;
  }
  return -1;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

size_t sys_string_base64_encode(const void *data, size_t size, char *buf,
                                size_t cap) {
  if (data == NULL) {
    size = 0;
  }
  if (size > (SIZE_MAX - 1) / 4 * 3) {
    if (buf != NULL && cap > 0) {
      buf[0] = '\0';
    }
    return 0;
  }
  const uint8_t *bytes = data;
  size_t length = (size + 2) / 3 * 4;

  // The length is known without reading the bytes, if nothing can be written
  if (buf == NULL || cap == 0) {
    return length;
  }

  // Only as much as fits, with the NUL terminator
  size_t written = 0;
  for (size_t i = 0; i < size && written + 1 < cap; i += 3) {
    uint32_t group = (uint32_t)bytes[i] << 16;
    if (i + 1 < size) {
      group |= (uint32_t)bytes[i + 1] << 8;
    }
    if (i + 2 < size) {
      group |= bytes[i + 2];
    }
    char out[4] = {_sys_base64_alphabet[(group >> 18) & 0x3F],
                   _sys_base64_alphabet[(group >> 12) & 0x3F],
                   i + 1 < size ? _sys_base64_alphabet[(group >> 6) & 0x3F] : '=',
                   i + 2 < size ? _sys_base64_alphabet[group & 0x3F] : '='};
    for (size_t j = 0; j < 4 && written + 1 < cap; j++, written++) {
      buf[written] = out[j];
    }
  }
  buf[written] = '\0';
  return length;
}

ptrdiff_t sys_string_parse_base64(const char *str, size_t len, void *out,
                                  size_t cap) {
  if (str == NULL) {
    return -1;
  }
  if (len == 0) {
    len = sys_string_bytes(str);
  }

  // Padding is optional, but if it's there, the length is a multiple of four
  size_t chars = len;
  size_t padding = 0;
  while (chars > 0 && padding < 2 && str[chars - 1] == '=') {
    chars--;
    padding++;
  }
  if ((padding > 0 && len % 4 != 0) || chars % 4 == 1) {
    return -1;
  }

  uint8_t *bytes = out;
  size_t written = 0;
  uint32_t group = 0;
  size_t count = 0;
  for (size_t i = 0; i < chars; i++) {
    int value = _sys_base64_value(str[i]);
    if (value < 0) {
      return -1;
    }
    group = (group << 6) | (uint32_t)value;
    if (++count == 4) {
      uint8_t decoded[3] = {(uint8_t)(group >> 16), (uint8_t)(group >> 8),
                            (uint8_t)group};
      for (size_t j = 0; j < 3; j++, written++) {
        if (bytes != NULL && written < cap) {
          bytes[written] = decoded[j];
        }
      }
      group = 0;
      count = 0;
    }
  }

  // The last two or three characters are one or two bytes, and the bits left
  // over must be zero
  if (count == 2) {
    if ((group & 0x0F) != 0) {
      return -1;
    }
    if (bytes != NULL && written < cap) {
      bytes[written] = (uint8_t)(group >> 4);
    }
    written++;
  } else if (count == 3) {
    if ((group & 0x03) != 0) {
      return -1;
    }
    uint8_t decoded[2] = {(uint8_t)(group >> 10), (uint8_t)(group >> 2)};
    for (size_t j = 0; j < 2; j++, written++) {
      if (bytes != NULL && written < cap) {
        bytes[written] = decoded[j];
      }
    }
  }
  return (ptrdiff_t)written;
}
