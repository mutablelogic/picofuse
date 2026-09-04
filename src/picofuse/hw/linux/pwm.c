#include <picofuse/hw.h>
#include <picofuse/sys.h>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// CONSTANTS

// Longest chip path hw_pwm_init_device() accepts from a caller.
#define PWM_PATH_MAX 128

// Storage for the derived "<chip_dir>/pwm<channel>" leaf path - longer
// than PWM_PATH_MAX alone (chip_dir, up to "/pwm" plus a channel number up
// to 10 digits), with margin to spare.
#define PWM_DEVICE_PATH_MAX (PWM_PATH_MAX + 32)

// Scratch buffer for the transient "<dir>/<attribute file>" paths built in
// _hw_pwm_write_str()/_hw_pwm_read_u64() - never stored, but `dir` there
// can be either a PWM_PATH_MAX chip_dir or a PWM_DEVICE_PATH_MAX device
// leaf path, plus the longest attribute filename ("duty_cycle").
#define PWM_ATTR_PATH_MAX (PWM_DEVICE_PATH_MAX + 16)

// Used when hw_pwm_init_device() is given a NULL config - matches
// pico/pwm.c's own default (max wrap 0xFFFF, divider 1.0) at the RP2040's
// standard 125MHz system clock: 65536 counts / 125000000 Hz = 524288ns.
// There's no clk_sys to query here, so this is the fixed equivalent rather
// than a computed one.
#define PWM_DEFAULT_PERIOD_NS 524288u

///////////////////////////////////////////////////////////////////////////////
// TYPES

// PWM channels are identified by sysfs path alone (no GPIO-driven slice/
// channel derivation the way Pico has), so unlike that backend's static,
// hardware-sized pool, each handle here is heap-allocated - a plain,
// single-purpose struct rather than a shared context format.
struct hw_pwm_t {
  char device[PWM_DEVICE_PATH_MAX]; // "<chip_dir>/pwm<channel>"
  char chip_dir[PWM_PATH_MAX];      // e.g. "/sys/class/pwm/pwmchip0"
  uint8_t channel;
  bool exported_by_us; // only unexport what we ourselves exported
};

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS - SYSFS I/O

static bool _hw_pwm_path_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

static bool _hw_pwm_write_str(const char *dir, const char *file,
                              const char *value) {
  char path[PWM_ATTR_PATH_MAX];
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

static bool _hw_pwm_write_u64(const char *dir, const char *file,
                              uint64_t value) {
  char buf[32];
  sys_sprintf(buf, sizeof(buf), "%lu", value);
  return _hw_pwm_write_str(dir, file, buf);
}

static bool _hw_pwm_read_u64(const char *dir, const char *file,
                             uint64_t *out) {
  char path[PWM_ATTR_PATH_MAX];
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

static inline bool _hw_pwm_write_enabled(const char *dir, bool enabled) {
  return _hw_pwm_write_str(dir, "enable", enabled ? "1" : "0");
}

static bool _hw_pwm_read_enabled(const char *dir, bool *out) {
  uint64_t value = 0;
  if (!_hw_pwm_read_u64(dir, "enable", &value)) {
    return false;
  }
  *out = value != 0;
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS - EXPORT / CONFIGURATION

// Writing a channel number to "<chip_dir>/export" makes the kernel create
// "<device>/" (period, duty_cycle, enable, ...) - asynchronously, so this
// polls briefly for it to appear rather than assuming it's there the
// instant the write returns.
static bool _hw_pwm_export(const char *chip_dir, uint8_t channel,
                           const char *device) {
  if (_hw_pwm_path_exists(device)) {
    return true; // already exported, by us in a prior run or someone else
  }
  if (!_hw_pwm_write_u64(chip_dir, "export", channel)) {
    return false;
  }
  for (int attempt = 0; attempt < 100 && !_hw_pwm_path_exists(device);
      attempt++) {
    sys_sleep_ms(1);
  }
  return _hw_pwm_path_exists(device);
}

static inline void _hw_pwm_unexport(const char *chip_dir,
                                    uint8_t channel) {
  _hw_pwm_write_u64(chip_dir, "unexport", channel);
}

static inline uint64_t _hw_pwm_duty_ns(float duty_percent,
                                       uint64_t period_ns) {
  if (duty_percent <= 0.0f) {
    return 0;
  }
  if (duty_percent >= 100.0f) {
    return period_ns;
  }
  return (uint64_t)((double)period_ns * (double)duty_percent / 100.0);
}

// Applies period/duty/enabled together in the order sysfs's PWM class
// actually accepts: disable, zero duty_cycle, set the new period, set the
// new duty_cycle, then enable - since a duty_cycle left greater than a
// shrinking period is rejected by most drivers, and changing period/duty
// while running can glitch the output.
static bool _hw_pwm_apply(hw_pwm_t *pwm, uint64_t period_ns,
                          float duty_percent, bool enabled) {
  uint64_t duty_ns = _hw_pwm_duty_ns(duty_percent, period_ns);

  if (!_hw_pwm_write_enabled(pwm->device, false) ||
      !_hw_pwm_write_u64(pwm->device, "duty_cycle", 0) ||
      !_hw_pwm_write_u64(pwm->device, "period", period_ns) ||
      !_hw_pwm_write_u64(pwm->device, "duty_cycle", duty_ns)) {
    return false;
  }
  return !enabled || _hw_pwm_write_enabled(pwm->device, true);
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hw_pwm_t *hw_pwm_init(hw_gpio_t *gpio, hw_pwm_callback_t callback,
                      void *userdata, const hw_pwm_config_t *config) {
  sys_debugf("hw",
      "pwm_init: unsupported on this target (gpio=%p callback=%p config=%p)",
      gpio, callback, config);
  (void)gpio;
  (void)callback;
  (void)userdata;
  (void)config;
  return NULL;
}

hw_pwm_t *hw_pwm_init_device(const char *device, uint8_t channel,
                             const hw_pwm_config_t *config) {
  sys_debugf("hw", "pwm_init_device: device=%s channel=%u config=%p",
             device != NULL ? device : "(null)", channel, config);
  if (device == NULL || device[0] == '\0' || strlen(device) >= PWM_PATH_MAX) {
    return NULL;
  }

  char channel_path[PWM_DEVICE_PATH_MAX];
  sys_sprintf(channel_path, sizeof(channel_path), "%s/pwm%u", device, channel);

  bool already_exported = _hw_pwm_path_exists(channel_path);
  if (!_hw_pwm_export(device, channel, channel_path)) {
    return NULL;
  }

  hw_pwm_config_t defaults = {
      .period_ns = PWM_DEFAULT_PERIOD_NS,
      .duty_percent = 0.0f,
      .enabled = false,
  };
  if (config == NULL) {
    config = &defaults;
  }

  hw_pwm_t *pwm = sys_calloc(1, sizeof(*pwm));
  if (pwm == NULL) {
    if (!already_exported) {
      _hw_pwm_unexport(device, channel);
    }
    return NULL;
  }

  sys_sprintf(pwm->device, sizeof(pwm->device), "%s", channel_path);
  sys_sprintf(pwm->chip_dir, sizeof(pwm->chip_dir), "%s", device);
  pwm->channel = channel;
  pwm->exported_by_us = !already_exported;

  if (!_hw_pwm_apply(pwm, config->period_ns, config->duty_percent,
                     config->enabled)) {
    if (pwm->exported_by_us) {
      _hw_pwm_unexport(device, channel);
    }
    sys_free(pwm);
    return NULL;
  }

  return pwm;
}

void hw_pwm_deinit(hw_pwm_t *pwm) {
  sys_debugf("hw", "pwm_deinit: pwm=%p", pwm);
  if (pwm == NULL) {
    return;
  }

  _hw_pwm_write_enabled(pwm->device, false);
  if (pwm->exported_by_us) {
    _hw_pwm_unexport(pwm->chip_dir, pwm->channel);
  }
  sys_free(pwm);
}

///////////////////////////////////////////////////////////////////////////////
// CONFIGURATION

bool hw_pwm_set_period_ns(hw_pwm_t *pwm, uint64_t period_ns) {
  if (pwm == NULL) {
    return false;
  }

  uint64_t current_duty_ns = 0;
  _hw_pwm_read_u64(pwm->device, "duty_cycle", &current_duty_ns);
  uint64_t new_duty_ns =
      current_duty_ns > period_ns ? period_ns : current_duty_ns;

  if (current_duty_ns > period_ns &&
      !_hw_pwm_write_u64(pwm->device, "duty_cycle", 0)) {
    return false;
  }
  if (!_hw_pwm_write_u64(pwm->device, "period", period_ns)) {
    return false;
  }
  return _hw_pwm_write_u64(pwm->device, "duty_cycle", new_duty_ns);
}

uint64_t hw_pwm_get_period_ns(const hw_pwm_t *pwm) {
  if (pwm == NULL) {
    return 0;
  }
  uint64_t value = 0;
  _hw_pwm_read_u64(pwm->device, "period", &value);
  return value;
}

bool hw_pwm_set_duty_percent(hw_pwm_t *pwm, float duty_percent) {
  if (pwm == NULL) {
    return false;
  }
  uint64_t period_ns = 0;
  if (!_hw_pwm_read_u64(pwm->device, "period", &period_ns)) {
    return false;
  }
  return _hw_pwm_write_u64(pwm->device, "duty_cycle",
                           _hw_pwm_duty_ns(duty_percent, period_ns));
}

float hw_pwm_get_duty_percent(const hw_pwm_t *pwm) {
  if (pwm == NULL) {
    return 0.0f;
  }
  uint64_t period_ns = 0;
  uint64_t duty_ns = 0;
  if (!_hw_pwm_read_u64(pwm->device, "period", &period_ns) || period_ns == 0 ||
      !_hw_pwm_read_u64(pwm->device, "duty_cycle", &duty_ns)) {
    return 0.0f;
  }
  return (float)((double)duty_ns / (double)period_ns * 100.0);
}

bool hw_pwm_set_config(hw_pwm_t *pwm, const hw_pwm_config_t *config) {
  if (pwm == NULL || config == NULL) {
    return false;
  }
  return _hw_pwm_apply(pwm, config->period_ns, config->duty_percent,
                       config->enabled);
}

bool hw_pwm_get_config(const hw_pwm_t *pwm, hw_pwm_config_t *out_config) {
  if (pwm == NULL || out_config == NULL) {
    return false;
  }

  out_config->period_ns = hw_pwm_get_period_ns(pwm);
  out_config->duty_percent = hw_pwm_get_duty_percent(pwm);
  bool enabled = false;
  _hw_pwm_read_enabled(pwm->device, &enabled);
  out_config->enabled = enabled;
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// CONTROL

void hw_pwm_set_enabled(hw_pwm_t *pwm, bool enabled) {
  if (pwm == NULL) {
    return;
  }
  _hw_pwm_write_enabled(pwm->device, enabled);
}

bool hw_pwm_get_enabled(const hw_pwm_t *pwm) {
  if (pwm == NULL) {
    return false;
  }
  bool enabled = false;
  _hw_pwm_read_enabled(pwm->device, &enabled);
  return enabled;
}

///////////////////////////////////////////////////////////////////////////////
// INTERRUPTS

/** sysfs's PWM class has no wrap-interrupt mechanism exposed to userspace. */
bool hw_pwm_irq_supported(void) { return false; }
