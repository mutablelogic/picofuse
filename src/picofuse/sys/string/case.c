#include <picofuse/sys.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Convert a string to uppercase in-place.
 * @param str The string to convert.
 * @return The same string pointer, or NULL if the input was NULL.
 */
char *sys_string_to_upper(char *str) {
  if (str == NULL) {
    return NULL;
  }

  char *pos = str;
  rune_t r;
  const char *next;
  while ((next = sys_rune_next(pos, &r)) != NULL) {
    rune_t upper = sys_rune_to_upper(r);
    if (upper != r) {
      // sys_rune_to_upper() only converts within its ASCII/Latin-1 range,
      // and every case pair there keeps the same UTF-8 width (1 byte for
      // ASCII, 2 for Latin-1 Supplement), so the new rune always fits
      // back over the bytes it replaces.
      size_t width = (size_t)(next - pos);
      if (width == 1) {
        pos[0] = (char)(uint8_t)upper;
      } else {
        pos[0] = (char)(uint8_t)(0xC0 | (upper >> 6));
        pos[1] = (char)(uint8_t)(0x80 | (upper & 0x3F));
      }
    }
    pos = (char *)next;
  }
  return str;
}

/**
 * @brief Convert a string to lowercase in-place.
 * @param str The string to convert.
 * @return The same string pointer, or NULL if the input was NULL.
 */
char *sys_string_to_lower(char *str) {
  if (str == NULL) {
    return NULL;
  }

  char *pos = str;
  rune_t r;
  const char *next;
  while ((next = sys_rune_next(pos, &r)) != NULL) {
    rune_t lower = sys_rune_to_lower(r);
    if (lower != r) {
      // sys_rune_to_lower() only converts within its ASCII/Latin-1 range,
      // and every case pair there keeps the same UTF-8 width (1 byte for
      // ASCII, 2 for Latin-1 Supplement), so the new rune always fits
      // back over the bytes it replaces.
      size_t width = (size_t)(next - pos);
      if (width == 1) {
        pos[0] = (char)(uint8_t)lower;
      } else {
        pos[0] = (char)(uint8_t)(0xC0 | (lower >> 6));
        pos[1] = (char)(uint8_t)(0x80 | (lower & 0x3F));
      }
    }
    pos = (char *)next;
  }
  return str;
}
