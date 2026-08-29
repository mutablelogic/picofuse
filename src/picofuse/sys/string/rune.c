#include <picofuse/sys.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static const uint8_t LOCB = 0x80, HICB = 0xBF;

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/** @brief Decode the next UTF-8 rune from a string. */
const char *sys_rune_next(const char *str, rune_t *rune) {
  if (str == NULL) {
    if (rune)
      *rune = 0;
    return NULL;
  }

  uint8_t s0 = (uint8_t)str[0];

  // End of string
  if (s0 == 0x00) {
    if (rune)
      *rune = 0;
    return NULL;
  }

  // ASCII fast path
  if (s0 < 0x80) {
    if (rune)
      *rune = (rune_t)s0;
    return str + 1;
  }

  int size;
  uint8_t lo = LOCB, hi = HICB;
  rune_t r;

  if (s0 < 0xC2) {
    // stray continuation byte, or overlong 2-byte lead (0xC0/0xC1)
    if (rune)
      *rune = RUNE_ERROR;
    return str + 1;
  } else if (s0 < 0xE0) {
    size = 2;
    r = s0 & 0x1F;
  } else if (s0 < 0xF0) {
    size = 3;
    r = s0 & 0x0F;
    if (s0 == 0xE0)
      lo = 0xA0; // exclude overlong
    else if (s0 == 0xED)
      hi = 0x9F; // exclude surrogates D800-DFFF
  } else if (s0 < 0xF5) {
    size = 4;
    r = s0 & 0x07;
    if (s0 == 0xF0)
      lo = 0x90; // exclude overlong
    else if (s0 == 0xF4)
      hi = 0x8F; // exclude > U+10FFFF
  } else {
    if (rune)
      *rune = RUNE_ERROR;
    return str + 1;
  }

  uint8_t b1 = (uint8_t)str[1];
  if (b1 < lo || b1 > hi) {
    if (rune)
      *rune = RUNE_ERROR;
    return str + 1;
  }
  r = (r << 6) | (b1 & 0x3F);
  if (size == 2) {
    if (rune)
      *rune = r;
    return str + 2;
  }

  uint8_t b2 = (uint8_t)str[2];
  if (b2 < LOCB || b2 > HICB) {
    if (rune)
      *rune = RUNE_ERROR;
    return str + 1;
  }
  r = (r << 6) | (b2 & 0x3F);
  if (size == 3) {
    if (rune)
      *rune = r;
    return str + 3;
  }

  uint8_t b3 = (uint8_t)str[3];
  if (b3 < LOCB || b3 > HICB) {
    if (rune)
      *rune = RUNE_ERROR;
    return str + 1;
  }
  r = (r << 6) | (b3 & 0x3F);
  if (rune)
    *rune = r;
  return str + 4;
}