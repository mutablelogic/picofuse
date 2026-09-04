#include "../led/led.h"
#include <picofuse/sys.h>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// CONSTANTS

// Longest "/sys/class/leds/<name>" path this backend stores - generous for
// real LED names (e.g. "led0", "ACT", "input3::scrolllock").
#define HW_LED_DEVICE_DIR_MAX 48

// Scratch buffer for the transient "<dir>/<attribute file>" paths built in
// _hw_led_device_write_str()/_hw_led_device_read_u64() - never stored,
// just dir plus the longest attribute filename ("max_brightness").
#define HW_LED_DEVICE_ATTR_MAX (HW_LED_DEVICE_DIR_MAX + 16)

// "trigger" lists every trigger the kernel driver supports, space-
// separated, with the currently active one in [brackets] - generous for
// a long list (kbd-*, rfkill*, cpu0-N, ... can add up).
#define HW_LED_DEVICE_TRIGGER_LIST_MAX 512

///////////////////////////////////////////////////////////////////////////////
// TYPES

// dir and trigger are both heap-allocated
typedef struct {
  char *dir;     // "/sys/class/leds/<name>"
  char *trigger; // original trigger, restored on deinit; NULL if unknown
} _hw_led_device_ctx_t;

_Static_assert(sizeof(_hw_led_device_ctx_t) <= HW_LED_CONTEXT_SIZE,
               "_hw_led_device_ctx_t exceeds HW_LED_CONTEXT_SIZE");

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS - SYSFS I/O

static bool _hw_led_device_write_str(const char *dir, const char *file,
                                     const char *value) {
  char path[HW_LED_DEVICE_ATTR_MAX];
  sys_sprintf(path, sizeof(path), "%s/%s", dir, file);

  int fd = open(path, O_WRONLY);
  if (fd < 0) {
    return false;
  }

  size_t len = strlen(value);
  ssize_t written = write(fd, value, len);
  close(fd);
  return written == (ssize_t)len;
}

static bool _hw_led_device_write_u64(const char *dir, const char *file,
                                     uint64_t value) {
  char buf[32];
  sys_sprintf(buf, sizeof(buf), "%lu", value);
  return _hw_led_device_write_str(dir, file, buf);
}

static bool _hw_led_device_read_u64(const char *dir, const char *file,
                                    uint64_t *out) {
  char path[HW_LED_DEVICE_ATTR_MAX];
  sys_sprintf(path, sizeof(path), "%s/%s", dir, file);

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return false;
  }

  char buf[32] = {0};
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) {
    return false;
  }

  // sys_string_parse_uint64() requires the string to contain exactly one
  // number and nothing else, but a sysfs read always ends in a trailing
  // '\n' - trim it (and a stray '\r', just in case) before parsing.
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
    n--;
  }
  return sys_string_parse_uint64(buf, (size_t)n, out);
}

// Reads "trigger" and pulls out the currently active entry
static char *_hw_led_device_read_trigger(const char *dir) {
  char path[HW_LED_DEVICE_ATTR_MAX];
  sys_sprintf(path, sizeof(path), "%s/trigger", dir);

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return NULL;
  }

  char buf[HW_LED_DEVICE_TRIGGER_LIST_MAX] = {0};
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) {
    return NULL;
  }

  const char *start = strchr(buf, '[');
  const char *end = start != NULL ? strchr(start + 1, ']') : NULL;
  if (start == NULL || end == NULL || end <= start + 1) {
    return NULL;
  }
  start++;

  size_t len = (size_t)(end - start);
  char *trigger = sys_calloc(1, len + 1);
  if (trigger == NULL) {
    return NULL;
  }
  memcpy(trigger, start, len);
  return trigger;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static bool _hw_led_device_set(hw_led_t *led, uint8_t index, bool enabled) {
  (void)index; // a single sysfs LED has no addressable sub-index
  _hw_led_device_ctx_t *ctx = _hw_led_context(led);

  if (!enabled) {
    return _hw_led_device_write_u64(ctx->dir, "brightness", 0);
  }

  uint64_t max_brightness = 1;
  _hw_led_device_read_u64(ctx->dir, "max_brightness", &max_brightness);
  return _hw_led_device_write_u64(ctx->dir, "brightness", max_brightness);
}

static bool _hw_led_device_set_brightness(hw_led_t *led, uint8_t index,
                                          float percent) {
  (void)index; // a single sysfs LED has no addressable sub-index
  _hw_led_device_ctx_t *ctx = _hw_led_context(led);

  if (percent < 0.0f) {
    percent = 0.0f;
  } else if (percent > 100.0f) {
    percent = 100.0f;
  }

  uint64_t max_brightness = 1;
  _hw_led_device_read_u64(ctx->dir, "max_brightness", &max_brightness);

  uint64_t level = (uint64_t)((float)max_brightness * percent / 100.0f);
  return _hw_led_device_write_u64(ctx->dir, "brightness", level);
}

static bool _hw_led_device_clear(hw_led_t *led) {
  return _hw_led_device_set(led, 0, false);
}

// Restores "trigger" to whatever it was before hw_led_init_device()
// overwrote it with "none"
static void _hw_led_device_deinit(hw_led_t *led) {
  _hw_led_device_ctx_t *ctx = _hw_led_context(led);
  if (ctx->trigger != NULL) {
    _hw_led_device_write_str(ctx->dir, "trigger", ctx->trigger);
    sys_free(ctx->trigger);
  }
  sys_free(ctx->dir);
}

static const hw_led_ops_t _hw_led_device_ops = {
    .set = _hw_led_device_set,
    .set_brightness = _hw_led_device_set_brightness,
    .clear = _hw_led_device_clear,
    .deinit = _hw_led_device_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hw_led_t *hw_led_init_device(const char *name) {
  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  char dir[HW_LED_DEVICE_DIR_MAX];
  size_t n = sys_sprintf(dir, sizeof(dir), "/sys/class/leds/%s", name);
  if (n >= sizeof(dir)) {
    return NULL; // name too long for HW_LED_DEVICE_DIR_MAX
  }

  struct stat st;
  if (stat(dir, &st) != 0) {
    return NULL;
  }

  // Take exclusive manual control
  char *saved_trigger = _hw_led_device_read_trigger(dir);
  if (!_hw_led_device_write_str(dir, "trigger", "none")) {
    sys_free(saved_trigger);
    return NULL;
  }

  char *owned_dir = sys_calloc(1, n + 1);
  if (owned_dir == NULL) {
    if (saved_trigger != NULL) {
      _hw_led_device_write_str(dir, "trigger", saved_trigger);
    }
    sys_free(saved_trigger);
    return NULL;
  }
  memcpy(owned_dir, dir, n + 1);

  hw_led_t *led = _hw_led_alloc(&_hw_led_device_ops);
  if (led == NULL) {
    if (saved_trigger != NULL) {
      _hw_led_device_write_str(dir, "trigger", saved_trigger);
    }
    sys_free(owned_dir);
    sys_free(saved_trigger);
    return NULL;
  }

  _hw_led_device_ctx_t *ctx = _hw_led_context(led);
  ctx->dir = owned_dir;
  ctx->trigger = saved_trigger;
  return led;
}
