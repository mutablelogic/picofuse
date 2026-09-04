#include "../../sys/iostream/iostream.h"
#include <errno.h>
#include <fcntl.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <poll.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#ifndef HW_UART_BUFFER_SIZE
#define HW_UART_BUFFER_SIZE 256
#endif

// How often the background RX thread re-checks whether it should keep
// running, between otherwise-blocking poll() calls.
#define HW_UART_POLL_TIMEOUT_MS 100

///////////////////////////////////////////////////////////////////////////////
// TYPES

// One heap-allocated context per open device - unlike Pico's fixed
// NUM_UARTS instances, POSIX device paths are unbounded, so there's no
// fixed-size pool to embed this in.
typedef struct {
  int fd;
  volatile bool running; // clear to ask the background thread to exit
  sys_waitgroup_t *wg;    // signaled by the thread just before it exits
  sys_mutex_t *lock;      // guards everything below, shared with the thread
  sys_iostream_t *stream;
  // sys_iostream_peek() reads one byte then calls seek(s, -1, false) to
  // undo it (iostream.h's own contract) - last_byte is whatever read()
  // most recently handed back, and seek(-1) moves it into pushback for
  // the next read() to return again before touching the ring buffer.
  int last_byte;
  int pushback;
  char rx_buf[HW_UART_BUFFER_SIZE];
  size_t rx_read, rx_write, rx_count;
} _hw_uart_ctx_t;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static bool _hw_uart_termios_baud(uint32_t baud_rate, speed_t *out_speed) {
  switch (baud_rate) {
  case 50:
    *out_speed = B50;
    return true;
  case 75:
    *out_speed = B75;
    return true;
  case 110:
    *out_speed = B110;
    return true;
  case 134:
    *out_speed = B134;
    return true;
  case 150:
    *out_speed = B150;
    return true;
  case 200:
    *out_speed = B200;
    return true;
  case 300:
    *out_speed = B300;
    return true;
  case 600:
    *out_speed = B600;
    return true;
  case 1200:
    *out_speed = B1200;
    return true;
  case 1800:
    *out_speed = B1800;
    return true;
  case 2400:
    *out_speed = B2400;
    return true;
  case 4800:
    *out_speed = B4800;
    return true;
  case 9600:
    *out_speed = B9600;
    return true;
  case 19200:
    *out_speed = B19200;
    return true;
  case 38400:
    *out_speed = B38400;
    return true;
  case 57600:
    *out_speed = B57600;
    return true;
  case 115200:
    *out_speed = B115200;
    return true;
  case 230400:
    *out_speed = B230400;
    return true;
  default:
    return false;
  }
}

static hw_uart_config_t _hw_uart_default_config(void) {
  return (hw_uart_config_t){
      .cts_pin = NULL,
      .rts_pin = NULL,
      .data_bits = hw_uart_data_bits_8,
      .stop_bits = hw_uart_stop_bits_1,
      .parity = hw_uart_parity_none,
      .flow_control = hw_uart_flow_control_none,
      .unbuffered = false,
  };
}

/** @brief Applies data bits/stop bits/parity/flow control to `tio`. False
 * if `settings` asked for something termios can't express (an
 * unrecognized enum value, or one-sided CTS/RTS-only flow control). */
static bool _hw_uart_apply_format(struct termios *tio,
                                  const hw_uart_config_t *settings) {
  tio->c_cflag &= (tcflag_t)~CSIZE;
  switch (settings->data_bits) {
  case hw_uart_data_bits_5:
    tio->c_cflag |= CS5;
    break;
  case hw_uart_data_bits_6:
    tio->c_cflag |= CS6;
    break;
  case hw_uart_data_bits_7:
    tio->c_cflag |= CS7;
    break;
  case hw_uart_data_bits_8:
    tio->c_cflag |= CS8;
    break;
  default:
    return false;
  }

  if (settings->stop_bits == hw_uart_stop_bits_2) {
    tio->c_cflag |= CSTOPB;
  } else if (settings->stop_bits == hw_uart_stop_bits_1) {
    tio->c_cflag &= (tcflag_t)~CSTOPB;
  } else {
    return false;
  }

  switch (settings->parity) {
  case hw_uart_parity_none:
    tio->c_cflag &= (tcflag_t)~(PARENB | PARODD);
    break;
  case hw_uart_parity_even:
    tio->c_cflag |= PARENB;
    tio->c_cflag &= (tcflag_t)~PARODD;
    break;
  case hw_uart_parity_odd:
    tio->c_cflag |= PARENB | PARODD;
    break;
  default:
    return false;
  }

  switch (settings->flow_control) {
  case hw_uart_flow_control_none:
    tio->c_cflag &= (tcflag_t)~CRTSCTS;
    break;
  case hw_uart_flow_control_cts_rts:
    tio->c_cflag |= CRTSCTS;
    break;
  case hw_uart_flow_control_cts:
  case hw_uart_flow_control_rts:
    // termios only exposes combined RTS/CTS flow control, not one side
    // alone - reject rather than silently applying both or neither.
    return false;
  default:
    return false;
  }

  tio->c_cflag |= (tcflag_t)(CLOCAL | CREAD);
  return true;
}

static void _hw_uart_rx_thread(void *arg) {
  _hw_uart_ctx_t *ctx = (_hw_uart_ctx_t *)arg;
  struct pollfd pfd = {.fd = ctx->fd, .events = POLLIN, .revents = 0};

  while (ctx->running) {
    int res = poll(&pfd, 1, HW_UART_POLL_TIMEOUT_MS);
    if (res < 0) {
      if (errno == EINTR) {
        continue;
      }
      sys_debugf("hw", "uart_init_device: poll error on fd=%d, stopping",
                ctx->fd);
      break;
    }
    if (res == 0) {
      continue; // timeout - just recheck ctx->running
    }

    char buf[64];
    ssize_t got = read(ctx->fd, buf, sizeof(buf));
    if (got <= 0) {
      sys_debugf("hw", "uart_init_device: read returned %d on fd=%d, "
                       "stopping (disconnected?)",
                (int)got, ctx->fd);
      break;
    }

    sys_mutex_lock(ctx->lock);
    for (ssize_t i = 0; i < got && ctx->rx_count < HW_UART_BUFFER_SIZE; i++) {
      ctx->rx_buf[ctx->rx_write] = buf[i];
      ctx->rx_write = (ctx->rx_write + 1) % HW_UART_BUFFER_SIZE;
      ctx->rx_count++;
    }
    sys_mutex_unlock(ctx->lock);

    sys_iostream_callback_t callback = ctx->stream->backend.uart.callback;
    if (callback != NULL) {
      callback(ctx->stream, sys_iostream_event_read,
               ctx->stream->backend.uart.userdata);
    }
  }

  ctx->running = false;
  sys_waitgroup_done(ctx->wg);
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS - OPS

static size_t _hw_uart_ops_read(sys_iostream_t *s, char *buf, size_t n) {
  _hw_uart_ctx_t *ctx = (_hw_uart_ctx_t *)s->backend.uart.instance;

  sys_mutex_lock(ctx->lock);
  size_t read_n = 0;
  if (ctx->pushback >= 0 && read_n < n) {
    buf[read_n++] = (char)(uint8_t)ctx->pushback;
    ctx->last_byte = ctx->pushback;
    ctx->pushback = -1;
  }
  while (read_n < n && ctx->rx_count > 0) {
    uint8_t byte = (uint8_t)ctx->rx_buf[ctx->rx_read];
    ctx->rx_read = (ctx->rx_read + 1) % HW_UART_BUFFER_SIZE;
    ctx->rx_count--;
    buf[read_n++] = (char)byte;
    ctx->last_byte = byte;
  }
  sys_mutex_unlock(ctx->lock);
  return read_n;
}

// A direct, blocking write(2) - no software buffering on the TX side,
// since a POSIX serial write essentially never has to wait for "room"
// (the kernel's own output buffer absorbs it) - see hw_uart_init_device()'s
// own doc.
static size_t _hw_uart_ops_write(sys_iostream_t *s, const char *buf,
                                 size_t n) {
  _hw_uart_ctx_t *ctx = (_hw_uart_ctx_t *)s->backend.uart.instance;
  ssize_t written = write(ctx->fd, buf, n);
  return written > 0 ? (size_t)written : 0;
}

// The only seek this stream supports is sys_iostream_peek()'s own "undo
// the single byte I just read" pattern.
static ptrdiff_t _hw_uart_ops_seek(sys_iostream_t *s, ptrdiff_t offset,
                                   bool abs) {
  _hw_uart_ctx_t *ctx = (_hw_uart_ctx_t *)s->backend.uart.instance;
  if (abs || offset != -1) {
    return -1;
  }
  sys_mutex_lock(ctx->lock);
  bool ok = ctx->last_byte >= 0;
  if (ok) {
    ctx->pushback = ctx->last_byte;
    ctx->last_byte = -1;
  }
  sys_mutex_unlock(ctx->lock);
  return ok ? 0 : -1;
}

// The background RX thread runs for this stream's whole lifetime
// regardless of callback registration (see hw_uart_init_device()'s own
// doc) - this just decides whether it has anyone to tell.
static bool _hw_uart_ops_set_callback(sys_iostream_t *s,
                                      sys_iostream_callback_t callback,
                                      void *userdata) {
  s->backend.uart.callback = callback;
  s->backend.uart.userdata = userdata;
  return true;
}

static void _hw_uart_ops_close(sys_iostream_t *s) {
  _hw_uart_ctx_t *ctx = (_hw_uart_ctx_t *)s->backend.uart.instance;

  ctx->running = false;
  sys_waitgroup_wait(ctx->wg);
  sys_waitgroup_deinit(ctx->wg);
  sys_mutex_deinit(ctx->lock);
  close(ctx->fd);
  sys_free(ctx);
}

static const sys_iostream_ops_t _hw_uart_ops = {
    .read = _hw_uart_ops_read,
    .write = _hw_uart_ops_write,
    .seek = _hw_uart_ops_seek,
    .set_callback = _hw_uart_ops_set_callback,
    .close = _hw_uart_ops_close,
};

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** Stub implementation: host platforms have no GPIO pins for a serial
 * port - see hw_uart_init_device(). */
sys_iostream_t *hw_uart_init(const hw_gpio_t *rx_pin, const hw_gpio_t *tx_pin,
                             uint32_t baud_rate,
                             const hw_uart_config_t *config) {
  sys_debugf("hw", "uart_init: unsupported on this target (rx=%p tx=%p "
                   "baud=%u config=%p)",
            (void *)rx_pin, (void *)tx_pin, baud_rate, (void *)config);
  (void)rx_pin;
  (void)tx_pin;
  (void)baud_rate;
  (void)config;
  return NULL;
}

sys_iostream_t *hw_uart_init_device(const char *device, uint32_t baud_rate,
                                    const hw_uart_config_t *config) {
  sys_debugf("hw", "uart_init_device: device=%s baud=%u config=%p",
             device != NULL ? device : "(null)", baud_rate, (void *)config);

  if (device == NULL || device[0] == '\0' || baud_rate == 0) {
    return NULL;
  }

  hw_uart_config_t settings =
      config != NULL ? *config : _hw_uart_default_config();

  speed_t speed;
  if (!_hw_uart_termios_baud(baud_rate, &speed)) {
    return NULL;
  }

  int fd = open(device, O_RDWR | O_NOCTTY);
  if (fd < 0) {
    return NULL;
  }

  struct termios tio;
  if (tcgetattr(fd, &tio) != 0) {
    close(fd);
    return NULL;
  }
  cfmakeraw(&tio); // no line discipline processing - just bytes
  cfsetspeed(&tio, speed);
  if (!_hw_uart_apply_format(&tio, &settings)) {
    close(fd);
    return NULL;
  }
  tcflush(fd, TCIOFLUSH);
  if (tcsetattr(fd, TCSANOW, &tio) != 0) {
    close(fd);
    return NULL;
  }

  _hw_uart_ctx_t *ctx = sys_malloc(sizeof(_hw_uart_ctx_t));
  if (ctx == NULL) {
    close(fd);
    return NULL;
  }
  memset(ctx, 0, sizeof(*ctx));
  ctx->fd = fd;
  ctx->last_byte = -1;
  ctx->pushback = -1;
  ctx->lock = sys_mutex_init();
  ctx->wg = sys_waitgroup_init();
  if (ctx->lock == NULL || ctx->wg == NULL) {
    sys_mutex_deinit(ctx->lock);
    sys_waitgroup_deinit(ctx->wg);
    close(fd);
    sys_free(ctx);
    return NULL;
  }

  sys_iostream_t *stream = _sys_iostream_alloc(&_hw_uart_ops);
  if (stream == NULL) {
    sys_mutex_deinit(ctx->lock);
    sys_waitgroup_deinit(ctx->wg);
    close(fd);
    sys_free(ctx);
    return NULL;
  }

  ctx->stream = stream;
  stream->backend.uart.instance = ctx;
  stream->backend.uart.callback = NULL;
  stream->backend.uart.userdata = NULL;

  ctx->running = true;
  sys_waitgroup_add(ctx->wg, 1);
  if (!sys_thread_create(_hw_uart_rx_thread, ctx)) {
    ctx->running = false;
    sys_waitgroup_add(ctx->wg, -1); // undo - the thread never started
    stream->in_use = false;         // release the pool slot directly -
                                     // ops.close() would double-free ctx
    sys_mutex_deinit(ctx->lock);
    sys_waitgroup_deinit(ctx->wg);
    close(fd);
    sys_free(ctx);
    return NULL;
  }

  return stream;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

// tcdrain(2) has no portable non-blocking or bounded-wait form, so
// timeout_ms is accepted for interface consistency but not enforced -
// see hw_uart_flush()'s own doc.
bool hw_uart_flush(sys_iostream_t *uart, uint32_t timeout_ms) {
  (void)timeout_ms;
  if (uart == NULL || uart->ops != &_hw_uart_ops) {
    return false;
  }
  _hw_uart_ctx_t *ctx = (_hw_uart_ctx_t *)uart->backend.uart.instance;
  return tcdrain(ctx->fd) == 0;
}
