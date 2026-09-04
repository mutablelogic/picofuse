#include "../gpio/private.h"
#include <fcntl.h>
#include <linux/gpio.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

/** @brief Backend-private context for one requested line - the opaque
 * hw_gpio_t only ever sees this via _hw_gpio_context(). One of these exists
 * per (bank, pin) for the process's entire lifetime (see _hw_gpio_lines
 * below), so a pointer to it is safe to hand to epoll directly:
 * hw_gpio_deinit() only ever resets fd to -1, it never frees the slot.
 *
 * claimed tracks whether a live hw_gpio_t currently wraps this (bank,
 * pin) - separate from fd, since fd stays -1 for a handle constructed
 * with mode hw_gpio_none (no line actually requested from the kernel
 * yet). Without it, hw_gpio_init() had no way to reject a second call on
 * a pin some other still-open handle already owns - the second call
 * would just re-request the kernel line, or with mode none, construct a
 * second wrapper around the same ctx with no line request at all - either
 * way silently invalidating the first handle instead of being rejected. */
typedef struct _hw_gpio_ctx_t {
  int fd;
  uint8_t bank;
  uint8_t pin;
  bool claimed;
} _hw_gpio_ctx_t;

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

#define GPIO_MAX_BANKS 4
#define GPIO_MAX_LINES GPIO_V2_LINES_MAX

// Chip fds, and a static pool of one context per (bank, pin) - see the
// note on _hw_gpio_ctx_t above for why this is a static pool rather than
// heap-allocated per hw_gpio_init() call.
static int _hw_gpio_chip_fds[GPIO_MAX_BANKS];
static _hw_gpio_ctx_t _hw_gpio_lines[GPIO_MAX_BANKS][GPIO_MAX_LINES];

// Synchronization and event-monitoring thread state, set up by
// _hw_gpio_module_init() (called from hw_init()) and torn down by
// _hw_gpio_module_exit() (called from hw_exit()).
static sys_mutex_t *_hw_gpio_mutex;
static sys_waitgroup_t *_hw_gpio_event_waitgroup;
static volatile bool _hw_gpio_stop = false;
static volatile int _hw_gpio_epoll_fd = -1; // volatile for thread-safe checking
static bool _hw_gpio_started = false;

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static int _hw_gpio_open_chip(uint8_t bank);
static void _hw_gpio_close_chip(uint8_t bank);
static int _hw_gpio_request_line(uint8_t bank, uint8_t pin, uint64_t flags);
static void _hw_gpio_release_line(uint8_t bank, uint8_t pin);
static void _hw_gpio_remove_from_epoll(int fd);
static void _hw_gpio_start_event_thread(void);
static void _hw_gpio_event_thread(void *arg);
static bool _hw_gpio_request(uint8_t bank, uint8_t pin, hw_gpio_mode_t mode);

///////////////////////////////////////////////////////////////////////////////
// OPS

static uint8_t _hw_gpio_pin(hw_gpio_t *gpio) {
  _hw_gpio_ctx_t *ctx = _hw_gpio_context(gpio);
  return ctx != NULL ? ctx->pin : 0;
}

static uint8_t _hw_gpio_bank(hw_gpio_t *gpio) {
  _hw_gpio_ctx_t *ctx = _hw_gpio_context(gpio);
  return ctx != NULL ? ctx->bank : 0;
}

static bool _hw_gpio_get(hw_gpio_t *gpio) {
  _hw_gpio_ctx_t *ctx = _hw_gpio_context(gpio);
  if (ctx == NULL) {
    return false;
  }

  sys_mutex_lock(_hw_gpio_mutex);
  int fd = ctx->fd;
  if (fd < 0) {
    sys_mutex_unlock(_hw_gpio_mutex);
    return false;
  }

  struct gpio_v2_line_values values = {0};
  values.mask = 1; // Read first line

  if (ioctl(fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0) {
    sys_mutex_unlock(_hw_gpio_mutex);
    return false;
  }
  sys_mutex_unlock(_hw_gpio_mutex);

  return (values.bits & 1) != 0;
}

static void _hw_gpio_set(hw_gpio_t *gpio, bool value) {
  _hw_gpio_ctx_t *ctx = _hw_gpio_context(gpio);
  if (ctx == NULL) {
    return;
  }

  sys_mutex_lock(_hw_gpio_mutex);
  int fd = ctx->fd;
  if (fd < 0) {
    sys_mutex_unlock(_hw_gpio_mutex);
    return;
  }

  struct gpio_v2_line_values values = {0};
  values.mask = 1; // Set first line
  values.bits = value ? 1 : 0;

  ioctl(fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values);
  sys_mutex_unlock(_hw_gpio_mutex);
}

static hw_gpio_mode_t _hw_gpio_get_mode(hw_gpio_t *gpio) {
  _hw_gpio_ctx_t *ctx = _hw_gpio_context(gpio);
  if (ctx == NULL) {
    return hw_gpio_unknown;
  }

  sys_mutex_lock(_hw_gpio_mutex);
  int chip_fd = _hw_gpio_chip_fds[ctx->bank];
  if (chip_fd < 0) {
    sys_mutex_unlock(_hw_gpio_mutex);
    return hw_gpio_unknown;
  }

  struct gpio_v2_line_info info = {0};
  info.offset = ctx->pin;

  if (ioctl(chip_fd, GPIO_V2_GET_LINEINFO_IOCTL, &info) < 0) {
    sys_mutex_unlock(_hw_gpio_mutex);
    return hw_gpio_unknown;
  }
  sys_mutex_unlock(_hw_gpio_mutex);

  // Determine mode from flags
  if (info.flags & GPIO_V2_LINE_FLAG_OUTPUT) {
    return hw_gpio_output;
  } else if (info.flags & GPIO_V2_LINE_FLAG_INPUT) {
    if (info.flags & GPIO_V2_LINE_FLAG_BIAS_PULL_UP) {
      return hw_gpio_pullup;
    } else if (info.flags & GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN) {
      return hw_gpio_pulldown;
    } else {
      return hw_gpio_input;
    }
  }

  return hw_gpio_unknown;
}

static void _hw_gpio_set_mode(hw_gpio_t *gpio, hw_gpio_mode_t mode) {
  _hw_gpio_ctx_t *ctx = _hw_gpio_context(gpio);
  if (ctx == NULL) {
    return;
  }
  _hw_gpio_request(ctx->bank, ctx->pin, mode);
}

static void _hw_gpio_deinit(hw_gpio_t *gpio) {
  _hw_gpio_ctx_t *ctx = _hw_gpio_context(gpio);
  if (ctx == NULL) {
    return;
  }
  _hw_gpio_release_line(ctx->bank, ctx->pin);

  sys_mutex_lock(_hw_gpio_mutex);
  ctx->claimed = false;
  sys_mutex_unlock(_hw_gpio_mutex);
}

static const hw_gpio_ops_t _hw_gpio_ops = {
    .pin = _hw_gpio_pin,
    .bank = _hw_gpio_bank,
    .get = _hw_gpio_get,
    .set = _hw_gpio_set,
    .get_mode = _hw_gpio_get_mode,
    .set_mode = _hw_gpio_set_mode,
    .deinit = _hw_gpio_deinit,
};

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hw_gpio_t *hw_gpio_init(uint8_t bank, uint8_t pin, hw_gpio_mode_t mode) {
  if (!_hw_gpio_started || bank >= GPIO_MAX_BANKS || pin >= GPIO_MAX_LINES) {
    sys_debugf("hw", "gpio_init: invalid bank=%u or pin=%u", bank, pin);
    return NULL;
  }

  // Fail here rather than only once a real line is requested - with mode
  // hw_gpio_none, _hw_gpio_request() never touches the kernel at all, so
  // without this check a chip-less environment (e.g. a container with no
  // /dev/gpiochipN passthrough) would still hand back a "successful"
  // handle whose get()/set() then silently do nothing.
  if (_hw_gpio_open_chip(bank) < 0) {
    sys_debugf("hw", "gpio_init: no gpiochip for bank=%u", bank);
    return NULL;
  }
  sys_debugf("hw", "gpio_init: bank=%u pin=%u mode=%u", bank, pin, mode);

  _hw_gpio_ctx_t *ctx = &_hw_gpio_lines[bank][pin];

  // A second hw_gpio_init() on a pin some other still-open handle already
  // owns is rejected rather than silently stealing it - see the
  // ctx->claimed field comment for why this can't just check fd.
  sys_mutex_lock(_hw_gpio_mutex);
  if (ctx->claimed) {
    sys_mutex_unlock(_hw_gpio_mutex);
    return NULL;
  }
  ctx->claimed = true;
  sys_mutex_unlock(_hw_gpio_mutex);

  if (mode != hw_gpio_none && !_hw_gpio_request(bank, pin, mode)) {
    sys_mutex_lock(_hw_gpio_mutex);
    ctx->claimed = false;
    sys_mutex_unlock(_hw_gpio_mutex);
    return NULL;
  }

  hw_gpio_t *gpio = _hw_gpio_construct(&_hw_gpio_ops, ctx);
  if (gpio == NULL) {
    _hw_gpio_release_line(bank, pin);
    sys_mutex_lock(_hw_gpio_mutex);
    ctx->claimed = false;
    sys_mutex_unlock(_hw_gpio_mutex);
    return NULL;
  }
  return gpio;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @brief Query how many lines this backend's chip for `bank` exposes. */
uint8_t hw_gpio_count(uint8_t bank) {
  int fd = _hw_gpio_open_chip(bank);
  if (fd < 0) {
    return 0;
  }

  struct gpiochip_info info;
  if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) < 0) {
    return 0;
  }

  if (info.lines > UINT8_MAX) {
    return 0; // Exceeds uint8_t range
  }
  return (uint8_t)info.lines;
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Request (or re-request, for a mode change) a line with the flags
 * matching `mode`. Shared by hw_gpio_init() and _hw_gpio_set_mode(). */
static bool _hw_gpio_request(uint8_t bank, uint8_t pin, hw_gpio_mode_t mode) {
  // Release any existing request first - a line can't be re-requested with
  // different flags while already held open.
  _hw_gpio_release_line(bank, pin);

  // Edge detection flags for all input modes
  const uint64_t edge_flags =
      GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING;

  int fd = -1;
  bool is_input_mode = false;

  switch (mode) {
  case hw_gpio_input:
    fd = _hw_gpio_request_line(bank, pin, GPIO_V2_LINE_FLAG_INPUT | edge_flags);
    is_input_mode = true;
    break;
  case hw_gpio_pullup:
    fd = _hw_gpio_request_line(bank, pin,
                               GPIO_V2_LINE_FLAG_INPUT |
                                   GPIO_V2_LINE_FLAG_BIAS_PULL_UP | edge_flags);
    is_input_mode = true;
    break;
  case hw_gpio_pulldown:
    fd = _hw_gpio_request_line(bank, pin,
                               GPIO_V2_LINE_FLAG_INPUT |
                                   GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN |
                                   edge_flags);
    is_input_mode = true;
    break;
  case hw_gpio_output:
    fd = _hw_gpio_request_line(bank, pin, GPIO_V2_LINE_FLAG_OUTPUT);
    break;
  case hw_gpio_none:
    // Do not request line
    return true;
  default:
    // Unsupported mode (spi/i2c/uart/pwm/adc aren't native chardev lines)
    sys_debugf("hw", "gpio_request: unsupported mode=%u for bank=%u pin=%u",
               mode, bank, pin);
    return false;
  }

  if (fd < 0) {
    return false;
  }

  // Add to epoll for input modes (with edge detection)
  if (is_input_mode) {
    int epoll_fd = _hw_gpio_epoll_fd; // Read volatile once for consistency
    if (epoll_fd >= 0) {
      struct epoll_event ev = {0};
      ev.events = EPOLLIN;
      ev.data.ptr = &_hw_gpio_lines[bank][pin];

      if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        sys_debugf("hw", "gpio_request: epoll_ctl failed bank=%u pin=%u", bank,
                   pin);
        _hw_gpio_release_line(bank, pin);
        return false;
      }
    }
  }

  return true;
}

/** @brief Sets up the mutex, fd tables and event thread. Called from
 * hw_init(); see linux/init.c. */
bool _hw_gpio_module_init(void) {
  if (_hw_gpio_started) {
    return true;
  }

  _hw_gpio_mutex = sys_mutex_init();
  if (_hw_gpio_mutex == NULL) {
    return false;
  }

  _hw_gpio_event_waitgroup = sys_waitgroup_init();
  if (_hw_gpio_event_waitgroup == NULL) {
    sys_mutex_deinit(_hw_gpio_mutex);
    return false;
  }

  for (uint8_t bank = 0; bank < GPIO_MAX_BANKS; bank++) {
    _hw_gpio_chip_fds[bank] = -1;
    for (uint16_t pin = 0; pin < GPIO_MAX_LINES; pin++) {
      _hw_gpio_lines[bank][pin].fd = -1;
      _hw_gpio_lines[bank][pin].bank = bank;
      _hw_gpio_lines[bank][pin].pin = (uint8_t)pin;
    }
  }

  _hw_gpio_stop = false;
  sys_waitgroup_add(_hw_gpio_event_waitgroup, 1);
  _hw_gpio_start_event_thread();
  _hw_gpio_started = true;
  return true;
}

/** @brief Stops the event thread and releases all chips/lines. Called from
 * hw_exit(); see linux/init.c. */
void _hw_gpio_module_exit(void) {
  if (!_hw_gpio_started) {
    return;
  }

  _hw_gpio_stop = true;
  sys_waitgroup_wait(_hw_gpio_event_waitgroup);
  sys_waitgroup_deinit(_hw_gpio_event_waitgroup);

  int epoll_fd = _hw_gpio_epoll_fd;
  _hw_gpio_epoll_fd = -1;
  if (epoll_fd >= 0) {
    close(epoll_fd);
  }

  for (uint8_t bank = 0; bank < GPIO_MAX_BANKS; bank++) {
    _hw_gpio_close_chip(bank);
  }

  sys_mutex_deinit(_hw_gpio_mutex);
  _hw_gpio_started = false;
}

static int _hw_gpio_open_chip(uint8_t bank) {
  if (bank >= GPIO_MAX_BANKS || !_hw_gpio_started) {
    return -1;
  }

  sys_mutex_lock(_hw_gpio_mutex);
  if (_hw_gpio_chip_fds[bank] >= 0) {
    int fd = _hw_gpio_chip_fds[bank];
    sys_mutex_unlock(_hw_gpio_mutex);
    return fd; // Already opened
  }

  char dev[32];
  sys_sprintf(dev, sizeof(dev), "/dev/gpiochip%u", bank);
  int fd = open(dev, O_RDONLY);
  if (fd < 0) {
    sys_debugf("hw", "gpio_open_chip: failed to open %s", dev);
    sys_mutex_unlock(_hw_gpio_mutex);
    return -1;
  }

  sys_debugf("hw", "gpio_open_chip: opened %s fd=%d", dev, fd);
  _hw_gpio_chip_fds[bank] = fd;
  sys_mutex_unlock(_hw_gpio_mutex);
  return fd;
}

static void _hw_gpio_close_chip(uint8_t bank) {
  if (bank >= GPIO_MAX_BANKS) {
    return;
  }

  sys_mutex_lock(_hw_gpio_mutex);
  if (_hw_gpio_chip_fds[bank] < 0) {
    sys_mutex_unlock(_hw_gpio_mutex);
    return; // Not opened
  }

  // Release all line requests for this bank
  for (uint16_t pin = 0; pin < GPIO_MAX_LINES; pin++) {
    int fd = _hw_gpio_lines[bank][pin].fd;
    if (fd >= 0) {
      _hw_gpio_lines[bank][pin].fd = -1; // Mark as released
      _hw_gpio_remove_from_epoll(fd);
      close(fd);
    }
  }

  close(_hw_gpio_chip_fds[bank]);
  _hw_gpio_chip_fds[bank] = -1;
  sys_mutex_unlock(_hw_gpio_mutex);
}

static void _hw_gpio_remove_from_epoll(int fd) {
  // Remove from epoll if it was added (safe to call even if not in epoll)
  int epoll_fd = _hw_gpio_epoll_fd; // Read volatile once
  if (epoll_fd >= 0 && fd >= 0) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
  }
}

static int _hw_gpio_request_line(uint8_t bank, uint8_t pin, uint64_t flags) {
  sys_assert(bank < GPIO_MAX_BANKS);
  sys_assert(pin < GPIO_MAX_LINES);
  sys_assert(flags != 0);

  // Open the chip if not already opened
  int chip_fd = _hw_gpio_open_chip(bank);
  if (chip_fd < 0) {
    return -1;
  }

  // Prepare GPIO v2 line request
  struct gpio_v2_line_request req = {0};
  req.offsets[0] = pin;
  req.num_lines = 1;
  req.config.flags = flags; // Set the flags

  // If output mode, set initial value to low
  if (flags & GPIO_V2_LINE_FLAG_OUTPUT) {
    req.config.num_attrs = 1;
    req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    req.config.attrs[0].attr.values = 0; // Start with low
    req.config.attrs[0].mask = 1;
  }

  sys_sprintf(req.consumer, sizeof(req.consumer), "hw_gpio");

  // Request the line
  sys_mutex_lock(_hw_gpio_mutex);
  if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
    sys_debugf("hw", "gpio_request_line: failed bank=%u pin=%u", bank, pin);
    sys_mutex_unlock(_hw_gpio_mutex);
    return -1;
  }
  _hw_gpio_lines[bank][pin].fd = req.fd;
  sys_mutex_unlock(_hw_gpio_mutex);
  return req.fd;
}

static void _hw_gpio_release_line(uint8_t bank, uint8_t pin) {
  sys_assert(bank < GPIO_MAX_BANKS);
  sys_assert(pin < GPIO_MAX_LINES);

  sys_mutex_lock(_hw_gpio_mutex);
  int fd = _hw_gpio_lines[bank][pin].fd;
  if (fd >= 0) {
    _hw_gpio_lines[bank][pin].fd = -1; // Mark as released first
    sys_mutex_unlock(_hw_gpio_mutex);

    // Remove from epoll and close outside the mutex
    _hw_gpio_remove_from_epoll(fd);
    close(fd);
  } else {
    sys_mutex_unlock(_hw_gpio_mutex);
  }
}

///////////////////////////////////////////////////////////////////////////////
// EVENT THREAD

static void _hw_gpio_event_thread(void *arg) {
  (void)arg;
  sys_debugf("hw", "gpio_event_thread: started");

  while (!_hw_gpio_stop) {
    // Wait for events with 100ms timeout
    struct epoll_event events[32]; // Process up to 32 events per iteration
    int nfds = epoll_wait(_hw_gpio_epoll_fd, events, 32, 100);

    if (nfds < 0) {
      sys_debugf("hw", "gpio_event_thread: epoll_wait error");
      sys_sleep_ms(10);
      continue;
    }

    if (nfds == 0) {
      continue; // Timeout, no events
    }

    // Process each event
    for (int i = 0; i < nfds; i++) {
      if (!(events[i].events & EPOLLIN)) {
        continue;
      }

      // data.ptr points directly at the (bank, pin)'s static context slot
      // (see _hw_gpio_ctx_t) - always safely dereferenceable, since that
      // slot is never freed, only reset.
      _hw_gpio_ctx_t *ctx = events[i].data.ptr;

      sys_mutex_lock(_hw_gpio_mutex);
      int fd = ctx->fd;
      sys_mutex_unlock(_hw_gpio_mutex);

      if (fd < 0) {
        continue; // FD was released, skip it
      }

      // Read the event (no mutex needed - reading from our own FD)
      struct gpio_v2_line_event event;
      ssize_t rd = read(fd, &event, sizeof(event));
      if (rd != (ssize_t)sizeof(event)) {
        continue;
      }

      hw_gpio_event_t hw_event = 0;
      if (event.id == GPIO_V2_LINE_EVENT_RISING_EDGE) {
        hw_event = hw_gpio_rising;
      } else if (event.id == GPIO_V2_LINE_EVENT_FALLING_EDGE) {
        hw_event = hw_gpio_falling;
      } else {
        continue; // Unknown event type
      }

      sys_debugf("hw", "gpio_event_thread: bank=%u pin=%u edge=%s", ctx->bank,
                 ctx->pin, hw_event == hw_gpio_rising ? "rising" : "falling");

      _hw_gpio_dispatch_callback(ctx->bank, ctx->pin, hw_event);
    }
  }

  sys_debugf("hw", "gpio_event_thread: stopping");
  sys_waitgroup_done(_hw_gpio_event_waitgroup);
}

static void _hw_gpio_start_event_thread(void) {
  sys_debugf("hw", "gpio_event_thread: starting");

  int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (epoll_fd < 0) {
    sys_debugf("hw", "gpio_event_thread: epoll_create1 failed");
    sys_waitgroup_done(_hw_gpio_event_waitgroup);
    return;
  }

  _hw_gpio_epoll_fd = epoll_fd;

  if (!sys_thread_create(_hw_gpio_event_thread, NULL)) {
    sys_debugf("hw", "gpio_event_thread: sys_thread_create failed");
    sys_waitgroup_done(_hw_gpio_event_waitgroup);
    close(epoll_fd);
    _hw_gpio_epoll_fd = -1;
  }
}
