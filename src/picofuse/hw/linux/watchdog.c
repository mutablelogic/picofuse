#include <picofuse/hw.h>
#include <picofuse/sys.h>

#include <fcntl.h>
#include <linux/watchdog.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// CONSTANTS

#define HW_WATCHDOG_FALLBACK_TIMEOUT_MS 10000u

// Larger than any real hardware watchdog's actual ceiling (typically tens
// of seconds to a few minutes), just large enough to reliably trigger a
// compliant driver's own WDIOC_SETTIMEOUT clamping (see
// _hw_watchdog_probe_timeout_ms()) - deliberately not INT_MAX, to avoid
// relying on an unusual driver clamping cleanly against an extreme value.
#define HW_WATCHDOG_PROBE_TIMEOUT_S 86400

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct hw_watchdog_t {
  char device[64];
  uint32_t timeout_ms;
  uint32_t reset_timeout_ms;
  int fd;
  bool init;
  bool disable;
  bool reset_armed;
  bool did_reset;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static struct hw_watchdog_t _hw_watchdog = {.fd = -1};

///////////////////////////////////////////////////////////////////////////////
// PRIVATE

static bool _hw_watchdog_is_valid(const hw_watchdog_t *watchdog) {
  return watchdog != NULL && watchdog->init;
}

static void _hw_watchdog_close_fd(int *fd) {
  if (fd == NULL || *fd < 0) {
    return;
  }

  int options = WDIOS_DISABLECARD;
  (void)ioctl(*fd, WDIOC_SETOPTIONS, &options);
  (void)write(*fd, "V", 1);
  (void)close(*fd);
  *fd = -1;
}

static bool _hw_watchdog_read_sysfs_u32(const char *device, const char *attr,
                                        uint32_t *out) {
  if (device == NULL || device[0] == '\0' || attr == NULL || out == NULL) {
    return false;
  }

  const char *base = strrchr(device, '/');
  base = (base != NULL) ? base + 1 : device;

  char path[160] = {0};
  (void)snprintf(path, sizeof(path), "/sys/class/watchdog/%s/%s", base, attr);

  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return false;
  }

  char buffer[32] = {0};
  ssize_t nread = read(fd, buffer, sizeof(buffer) - 1);
  (void)close(fd);
  if (nread <= 0) {
    return false;
  }

  // sysfs attribute reads come back with a trailing newline -
  // sys_string_parse_uint32() requires the buffer to contain exactly one
  // number and nothing else, so that has to go first.
  sys_string_trimspace(buffer);
  return sys_string_parse_uint32(buffer, 0, out);
}

static uint32_t _hw_watchdog_probe_timeout_ms(const char *device) {
  if (device == NULL || device[0] == '\0' || access(device, F_OK) != 0) {
    // No such device - see this function's own callers
    // (hw_watchdog_maxtimeout_ms(), hw_watchdog_init_device()) for why 0
    // unambiguously means that here.
    return 0u;
  }

  uint32_t sysfs_max_s = 0u;
  if (_hw_watchdog_read_sysfs_u32(device, "max_timeout", &sysfs_max_s) &&
      sysfs_max_s > 0u) {
    return sysfs_max_s * 1000u;
  }

  uint32_t sysfs_timeout_s = 0u;
  bool have_sysfs_timeout =
      _hw_watchdog_read_sysfs_u32(device, "timeout", &sysfs_timeout_s) &&
      sysfs_timeout_s > 0u;
  uint32_t sysfs_fallback_ms = have_sysfs_timeout
                                   ? sysfs_timeout_s * 1000u
                                   : HW_WATCHDOG_FALLBACK_TIMEOUT_MS;

  // Only even attempt to open the character device - for the
  // WDIOC_SETTIMEOUT clamp-and-readback technique below, a more accurate
  // ceiling than a bare "currently configured" value - when sysfs itself
  // confirms it's safe to: "nowayout" == 0 means a magic close (see
  // below) actually disarms it again afterward. If nowayout is
  // unreadable or true, opening it either can't be safely undone or we
  // can't tell either way - stay on sysfs/the fallback constant instead.
  // Mirrors wdctl's own should_read_from_device() gate, for the same
  // reason.
  uint32_t nowayout = 1u;
  bool safe_to_open =
      _hw_watchdog_read_sysfs_u32(device, "nowayout", &nowayout) &&
      nowayout == 0u;
  if (!safe_to_open) {
    return sysfs_fallback_ms;
  }

  // Opening the device is not side-effect-free even for a read-only
  // query - the kernel's watchdog framework treats a mere open() as
  // claiming/arming it, and a bare close() afterward leaves it running,
  // unfed, ticking toward a real reset if nothing else was already
  // feeding it (confirmed on real hardware: this exact function, with a
  // bare close(), caused a second, unintended reboot during testing).
  // Always end with the documented magic close character below, never a
  // bare close() - matching util-linux's own wdctl, which carries the
  // exact same warning for the exact same reason: "successfully opened
  // watchdog has to be properly closed with magic close character
  // otherwise the machine will be rebooted!"
  int fd = open(device, O_WRONLY | O_CLOEXEC);
  if (fd < 0) {
    return sysfs_fallback_ms;
  }

  int original_s = 0;
  bool have_original =
      ioctl(fd, WDIOC_GETTIMEOUT, &original_s) == 0 && original_s > 0;

  // WDIOC_SETTIMEOUT is documented (Documentation/watchdog/watchdog-api.rst)
  // to clamp an out-of-range request to the real supported range and
  // write back whatever it actually applied, so requesting a
  // deliberately oversized value and reading that back is the only
  // portable way to discover a ceiling sysfs itself didn't report.
  int probe_s = HW_WATCHDOG_PROBE_TIMEOUT_S;
  uint32_t timeout_ms = 0u;
  if (ioctl(fd, WDIOC_SETTIMEOUT, &probe_s) == 0 && probe_s > 0) {
    timeout_ms = (uint32_t)probe_s * 1000u;
  } else if (have_original) {
    timeout_ms = (uint32_t)original_s * 1000u;
  }

  // Restore whatever was configured before this probe touched it, so a
  // watchdog someone else already armed isn't left reconfigured to this
  // probe's oversized value.
  if (have_original) {
    (void)ioctl(fd, WDIOC_SETTIMEOUT, &original_s);
  }

  static const char magic_close = 'V';
  (void)write(fd, &magic_close, 1);
  close(fd);

  return timeout_ms != 0u ? timeout_ms : sysfs_fallback_ms;
}

static bool _hw_watchdog_open(hw_watchdog_t *watchdog) {
  if (!_hw_watchdog_is_valid(watchdog)) {
    return false;
  }

  if (watchdog->fd >= 0) {
    return true;
  }

  watchdog->fd = open(watchdog->device, O_WRONLY | O_CLOEXEC);
  return watchdog->fd >= 0;
}

static bool _hw_watchdog_set_timeout(hw_watchdog_t *watchdog,
                                     uint32_t timeout_ms) {
  if (!_hw_watchdog_is_valid(watchdog) || timeout_ms == 0u) {
    return false;
  }

  if (!_hw_watchdog_open(watchdog)) {
    return false;
  }

  int timeout_s = (int)((timeout_ms + 999u) / 1000u);
  if (timeout_s <= 0) {
    timeout_s = 1;
  }

  return ioctl(watchdog->fd, WDIOC_SETTIMEOUT, &timeout_s) == 0 &&
         timeout_s > 0;
}

static bool _hw_watchdog_read_bootstatus(const char *device) {
  uint32_t status = 0u;
  if (!_hw_watchdog_read_sysfs_u32(device, "bootstatus", &status)) {
    return false;
  }
  return (status & WDIOF_CARDRESET) != 0u;
}

///////////////////////////////////////////////////////////////////////////////
// MODULE HOOKS

// Called from hw_exit() - see hw/linux/init.c.
void _hw_watchdog_module_exit(void) { hw_watchdog_deinit(&_hw_watchdog); }

// Called from hw_poll() - see hw/linux/init.c.
void _hw_watchdog_poll(void) {
  hw_watchdog_t *watchdog = &_hw_watchdog;

  if (!_hw_watchdog_is_valid(watchdog) || watchdog->disable ||
      watchdog->reset_armed) {
    return;
  }

  if (!_hw_watchdog_open(watchdog)) {
    return;
  }

  int keepalive = 0;
  (void)ioctl(watchdog->fd, WDIOC_KEEPALIVE, &keepalive);
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC API

hw_watchdog_t *hw_watchdog_init(void) {
  return hw_watchdog_init_device(HW_WATCHDOG_DEFAULT_DEVICE);
}

hw_watchdog_t *hw_watchdog_init_device(const char *device) {
  const char *path = device;

  hw_watchdog_deinit(&_hw_watchdog);

  if (path == NULL || path[0] == '\0') {
    path = HW_WATCHDOG_DEFAULT_DEVICE;
  }

  // 0 here unambiguously means no such device (see
  // _hw_watchdog_probe_timeout_ms()'s own doc) - don't hand back a handle
  // that looks initialized for hardware that was never actually there.
  uint32_t timeout_ms = _hw_watchdog_probe_timeout_ms(path);
  if (timeout_ms == 0u) {
    return NULL;
  }

  memset(&_hw_watchdog, 0, sizeof(_hw_watchdog));
  _hw_watchdog.fd = -1;

  (void)snprintf(_hw_watchdog.device, sizeof(_hw_watchdog.device), "%s", path);
  _hw_watchdog.timeout_ms = timeout_ms;
  _hw_watchdog.reset_timeout_ms = 0u;

  _hw_watchdog.did_reset = _hw_watchdog_read_bootstatus(path);
  _hw_watchdog.disable = true;
  _hw_watchdog.init = true;
  return &_hw_watchdog;
}

void hw_watchdog_deinit(hw_watchdog_t *watchdog) {
  if (!_hw_watchdog_is_valid(watchdog)) {
    return;
  }

  _hw_watchdog_close_fd(&watchdog->fd);
  memset(watchdog, 0, sizeof(*watchdog));
  watchdog->fd = -1;
}

uint32_t hw_watchdog_maxtimeout_ms(void) {
  if (_hw_watchdog_is_valid(&_hw_watchdog)) {
    return _hw_watchdog.timeout_ms;
  }

  // 0 here already unambiguously means no such device - see
  // _hw_watchdog_probe_timeout_ms()'s own doc.
  return _hw_watchdog_probe_timeout_ms(HW_WATCHDOG_DEFAULT_DEVICE);
}

bool hw_watchdog_did_reset(hw_watchdog_t *watchdog) {
  if (!_hw_watchdog_is_valid(watchdog)) {
    return false;
  }

  return watchdog->did_reset;
}

void hw_watchdog_enable(hw_watchdog_t *watchdog, bool enable) {
  if (!_hw_watchdog_is_valid(watchdog)) {
    return;
  }

  if (enable) {
    // hw_watchdog_init_device() can now succeed via sysfs alone even when
    // the device is exclusively held by something else (e.g. systemd's
    // own RuntimeWatchdogSec) - see _hw_watchdog_probe_timeout_ms()'s own
    // doc. Only claim to be feeding it if actually opening/configuring it
    // just now really worked - otherwise _hw_watchdog_poll() would think
    // it's keeping a real watchdog fed when every open() it tries is
    // silently failing.
    if (_hw_watchdog_set_timeout(watchdog, watchdog->timeout_ms)) {
      watchdog->reset_armed = false;
      watchdog->reset_timeout_ms = 0u;
      watchdog->disable = false;
      int keepalive = 0;
      (void)ioctl(watchdog->fd, WDIOC_KEEPALIVE, &keepalive);
    }
  } else {
    watchdog->disable = true;
    watchdog->reset_armed = false;
    watchdog->reset_timeout_ms = 0u;
    _hw_watchdog_close_fd(&watchdog->fd);
  }
}

void hw_watchdog_reset(hw_watchdog_t *watchdog, uint32_t delay_ms) {
  if (!_hw_watchdog_is_valid(watchdog) || delay_ms == 0u) {
    return;
  }

  uint32_t max_timeout_ms = hw_watchdog_maxtimeout_ms();
  if (max_timeout_ms == 0u) {
    return;
  }

  if (delay_ms > max_timeout_ms) {
    delay_ms = max_timeout_ms;
  }

  watchdog->reset_timeout_ms = delay_ms;
  if (!_hw_watchdog_set_timeout(watchdog, watchdog->reset_timeout_ms)) {
    return;
  }

  int keepalive = 0;
  (void)ioctl(watchdog->fd, WDIOC_KEEPALIVE, &keepalive);

  // Stop periodic feeding so the watchdog naturally expires after delay_ms.
  watchdog->disable = true;
  watchdog->reset_armed = true;
}
