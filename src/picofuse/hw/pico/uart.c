#include "../../sys/iostream/iostream.h"
#include "../../sys/pico/sync.h"
#include <hardware/irq.h>
#include <hardware/regs/uart.h>
#include <hardware/sync.h>
#include <hardware/uart.h>
#include <pico/time.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <string.h>

#ifndef HW_UART_BUFFER_SIZE
#define HW_UART_BUFFER_SIZE 256
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

// Per-instance (uart0/uart1) state, one slot each - a UART has exactly one
// owner at a time, unlike I2C's shared-bus model.
typedef struct {
  uart_inst_t *instance;
  irq_handler_t irq_handler;
  sys_iostream_t *stream; // backpointer, for the IRQ handler to reach it
  bool active;
  bool unbuffered;    // hw_uart_config_t.unbuffered, snapshotted at init
  bool irq_installed; // this instance's exclusive vector is currently ours
  // sys_iostream_peek() reads one byte then calls seek(s, -1, false) to
  // undo it (iostream.h's own contract) - a live UART FIFO has no real
  // position to seek back to, so these two fields fake it: last_byte is
  // whatever read() most recently handed back, and seek(-1) moves it into
  // pushback for the next read() to return again before touching the FIFO.
  int last_byte;
  int pushback;
  // Software ring buffers, background-drained by a real interrupt - see
  // hw_uart_init()'s own doc for why: the hardware's own FIFOs can't
  // raise their fill-level interrupts reliably on this chip, so
  // hw_uart_init() runs in character mode (1 byte of hardware buffering)
  // and these make up the difference. Unused entirely when unbuffered.
  char tx_buf[HW_UART_BUFFER_SIZE];
  size_t tx_read, tx_write, tx_count;
  char rx_buf[HW_UART_BUFFER_SIZE];
  size_t rx_read, rx_write, rx_count;
} _hw_uart_ctx_t;

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static _hw_uart_ctx_t _hw_uart_ctx[NUM_UARTS] = {0};

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static void _hw_uart0_irq_handler(void);
#if NUM_UARTS > 1
static void _hw_uart1_irq_handler(void);
#endif

/** @brief RP2040/RP2350: each GPIO's low 2 bits fix which UART instance and
 * signal (TX/RX/CTS/RTS) it can serve, in repeating groups of 4 pins - a TX
 * pin is always immediately followed by its instance's RX pin. */
static inline uint8_t _hw_uart_num_for_group_base(uint8_t group_base) {
  return (uint8_t)((((group_base >> 2) & 1u) ^ ((group_base >> 3) & 1u)) & 1u);
}

static bool _hw_uart_instance_for_pins(const hw_gpio_t *rx_pin,
                                       const hw_gpio_t *tx_pin,
                                       uart_inst_t **instance) {
  *instance = NULL;
  if (rx_pin == NULL || tx_pin == NULL) {
    return false;
  }

#if PICO_RP2040 || PICO_RP2350
  uint8_t rx_num = hw_gpio_pin(rx_pin);
  uint8_t tx_num = hw_gpio_pin(tx_pin);

  if ((tx_num & 0x3u) == 0u && rx_num == (uint8_t)(tx_num + 1u)) {
    *instance = UART_INSTANCE(_hw_uart_num_for_group_base(tx_num));
    return true;
  }
#endif

  return false;
}

/** @brief True if `pin` is the correct CTS (signal_offset 2) or RTS
 * (signal_offset 3) pin for `instance`. */
static bool _hw_uart_signal_pin_matches_instance(const hw_gpio_t *pin,
                                                 uart_inst_t *instance,
                                                 uint8_t signal_offset) {
  if (pin == NULL || instance == NULL) {
    return false;
  }

#if PICO_RP2040 || PICO_RP2350
  uint8_t pin_num = hw_gpio_pin(pin);
  if ((pin_num & 0x3u) != signal_offset) {
    return false;
  }
  return uart_get_index(instance) ==
         _hw_uart_num_for_group_base((uint8_t)(pin_num & ~0x3u));
#else
  (void)pin;
  (void)instance;
  (void)signal_offset;
  return false;
#endif
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

static uart_parity_t _hw_uart_sdk_parity(hw_uart_parity_t parity) {
  switch (parity) {
  case hw_uart_parity_even:
    return UART_PARITY_EVEN;
  case hw_uart_parity_odd:
    return UART_PARITY_ODD;
  case hw_uart_parity_none:
  default:
    return UART_PARITY_NONE;
  }
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS - RING BUFFER / IRQ STATE

/** @brief Pushes as much of the TX ring buffer into hardware as fits right
 * now. Called with interrupts already disabled, or from IRQ context. */
static void _hw_uart_tx_drain(_hw_uart_ctx_t *ctx) {
  while (ctx->tx_count > 0 && uart_is_writable(ctx->instance)) {
    uart_get_hw(ctx->instance)->dr = (uint8_t)ctx->tx_buf[ctx->tx_read];
    ctx->tx_read = (ctx->tx_read + 1) % HW_UART_BUFFER_SIZE;
    ctx->tx_count--;
  }
}

static inline bool _hw_uart_tx_idle(const _hw_uart_ctx_t *ctx) {
  return ctx->tx_count == 0 &&
        (uart_get_hw(ctx->instance)->fr & UART_UARTFR_BUSY_BITS) == 0;
}

/** @brief Recomputes which UART interrupts this instance needs and
 * (de)installs/enables its exclusive vector to match, then applies it.
 * Two independent things can each want the interrupt on: background
 * ring-buffer draining (whenever buffered, regardless of whether the
 * caller has registered a readiness callback) and the caller's own
 * callback (which, once registered, always watches both directions -
 * see _hw_uart_irq_handler()). Safe to call from foreground or from
 * inside the IRQ handler itself. */
static void _hw_uart_update_irqs(_hw_uart_ctx_t *ctx) {
  uint32_t irq_state = save_and_disable_interrupts();

  bool have_callback = ctx->stream->backend.uart.callback != NULL;
  bool rx_enabled = !ctx->unbuffered || have_callback;
  bool tx_enabled = (!ctx->unbuffered && ctx->tx_count > 0) || have_callback;

  if ((rx_enabled || tx_enabled) && !ctx->irq_installed) {
    uint32_t irq_num = UART_IRQ_NUM(ctx->instance);
    irq_set_exclusive_handler(irq_num, ctx->irq_handler);
    irq_set_enabled(irq_num, true);
    ctx->irq_installed = true;
  }
  uart_set_irqs_enabled(ctx->instance, rx_enabled, tx_enabled);

  restore_interrupts(irq_state);
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS - OPS

static size_t _hw_uart_ops_read(sys_iostream_t *s, char *buf, size_t n) {
  _hw_uart_ctx_t *ctx = (_hw_uart_ctx_t *)s->backend.uart.instance;

  if (ctx->unbuffered) {
    size_t read = 0;
    if (ctx->pushback >= 0 && read < n) {
      buf[read++] = (char)(uint8_t)ctx->pushback;
      ctx->last_byte = ctx->pushback;
      ctx->pushback = -1;
    }
    while (read < n && uart_is_readable(ctx->instance)) {
      uint8_t byte = (uint8_t)uart_get_hw(ctx->instance)->dr;
      buf[read++] = (char)byte;
      ctx->last_byte = byte;
    }
    return read;
  }

  uint32_t irq_state = save_and_disable_interrupts();
  size_t read = 0;
  if (ctx->pushback >= 0 && read < n) {
    buf[read++] = (char)(uint8_t)ctx->pushback;
    ctx->last_byte = ctx->pushback;
    ctx->pushback = -1;
  }
  while (read < n && ctx->rx_count > 0) {
    uint8_t byte = (uint8_t)ctx->rx_buf[ctx->rx_read];
    ctx->rx_read = (ctx->rx_read + 1) % HW_UART_BUFFER_SIZE;
    ctx->rx_count--;
    buf[read++] = (char)byte;
    ctx->last_byte = byte;
  }
  restore_interrupts(irq_state);
  return read;
}

static size_t _hw_uart_ops_write(sys_iostream_t *s, const char *buf,
                                 size_t n) {
  _hw_uart_ctx_t *ctx = (_hw_uart_ctx_t *)s->backend.uart.instance;

  if (ctx->unbuffered) {
    size_t written = 0;
    while (written < n && uart_is_writable(ctx->instance)) {
      uart_get_hw(ctx->instance)->dr = (uint8_t)buf[written++];
    }
    return written;
  }

  uint32_t irq_state = save_and_disable_interrupts();
  size_t put = 0;
  while (put < n && ctx->tx_count < HW_UART_BUFFER_SIZE) {
    ctx->tx_buf[ctx->tx_write] = buf[put++];
    ctx->tx_write = (ctx->tx_write + 1) % HW_UART_BUFFER_SIZE;
    ctx->tx_count++;
  }
  _hw_uart_tx_drain(ctx);
  restore_interrupts(irq_state);

  // Arms the TX interrupt if bytes are still queued after that inline
  // drain, so the rest goes out in the background as room frees up.
  _hw_uart_update_irqs(ctx);
  return put;
}

// The only seek this stream supports is sys_iostream_peek()'s own "undo
// the single byte I just read" pattern - a live FIFO has no
// general-purpose position to seek to.
static ptrdiff_t _hw_uart_ops_seek(sys_iostream_t *s, ptrdiff_t offset,
                                   bool abs) {
  _hw_uart_ctx_t *ctx = (_hw_uart_ctx_t *)s->backend.uart.instance;
  if (abs || offset != -1 || ctx->last_byte < 0) {
    return -1;
  }
  ctx->pushback = ctx->last_byte;
  ctx->last_byte = -1;
  return 0;
}

static bool _hw_uart_ops_set_callback(sys_iostream_t *s,
                                      sys_iostream_callback_t callback,
                                      void *userdata) {
  _hw_uart_ctx_t *ctx = (_hw_uart_ctx_t *)s->backend.uart.instance;
  s->backend.uart.callback = callback;
  s->backend.uart.userdata = userdata;
  _hw_uart_update_irqs(ctx);
  return true;
}

static void _hw_uart_ops_close(sys_iostream_t *s) {
  _hw_uart_ctx_t *ctx = (_hw_uart_ctx_t *)s->backend.uart.instance;

  if (ctx->irq_installed) {
    uint32_t irq_num = UART_IRQ_NUM(ctx->instance);
    uart_set_irqs_enabled(ctx->instance, false, false);
    irq_set_enabled(irq_num, false);
    irq_remove_handler(irq_num, ctx->irq_handler);
  }
  uart_deinit(ctx->instance);

  _sys_sync_pool_lock();
  memset(ctx, 0, sizeof(*ctx));
  ctx->last_byte = -1;
  ctx->pushback = -1;
  _sys_sync_pool_unlock();
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

sys_iostream_t *hw_uart_init(const hw_gpio_t *rx_pin, const hw_gpio_t *tx_pin,
                             uint32_t baud_rate,
                             const hw_uart_config_t *config) {
  sys_debugf("hw", "uart_init: rx=%p tx=%p baud=%u config=%p", (void *)rx_pin,
             (void *)tx_pin, baud_rate, (void *)config);

  if (rx_pin == NULL || tx_pin == NULL || baud_rate == 0) {
    return NULL;
  }

  hw_uart_config_t settings =
      config != NULL ? *config : _hw_uart_default_config();

  uart_inst_t *instance = NULL;
  if (!_hw_uart_instance_for_pins(rx_pin, tx_pin, &instance)) {
    return NULL;
  }

  if ((settings.flow_control == hw_uart_flow_control_rts ||
       settings.flow_control == hw_uart_flow_control_cts_rts) &&
      !_hw_uart_signal_pin_matches_instance(settings.rts_pin, instance, 3)) {
    return NULL;
  }
  if ((settings.flow_control == hw_uart_flow_control_cts ||
       settings.flow_control == hw_uart_flow_control_cts_rts) &&
      !_hw_uart_signal_pin_matches_instance(settings.cts_pin, instance, 2)) {
    return NULL;
  }

  uint8_t idx = uart_get_index(instance);
  _hw_uart_ctx_t *ctx = &_hw_uart_ctx[idx];

  _sys_sync_pool_lock();
  if (ctx->active) {
    // Already claimed by a still-open handle - reject rather than silently
    // stealing it out from under whoever holds it (matches hw_gpio_init()'s
    // and hw_spi_init_default()'s own exclusivity contract).
    _sys_sync_pool_unlock();
    return NULL;
  }
  ctx->active = true;
  _sys_sync_pool_unlock();

  sys_iostream_t *stream = _sys_iostream_alloc(&_hw_uart_ops);
  if (stream == NULL) {
    _sys_sync_pool_lock();
    ctx->active = false;
    _sys_sync_pool_unlock();
    return NULL;
  }

  hw_gpio_set_mode((hw_gpio_t *)rx_pin, hw_gpio_uart);
  hw_gpio_set_mode((hw_gpio_t *)tx_pin, hw_gpio_uart);
  if (settings.flow_control == hw_uart_flow_control_rts ||
      settings.flow_control == hw_uart_flow_control_cts_rts) {
    hw_gpio_set_mode((hw_gpio_t *)settings.rts_pin, hw_gpio_uart);
  }
  if (settings.flow_control == hw_uart_flow_control_cts ||
      settings.flow_control == hw_uart_flow_control_cts_rts) {
    hw_gpio_set_mode((hw_gpio_t *)settings.cts_pin, hw_gpio_uart);
  }

  ctx->instance = instance;
  ctx->stream = stream;
  ctx->unbuffered = settings.unbuffered;
  ctx->last_byte = -1;
  ctx->pushback = -1;

  switch (idx) {
  case 0:
    ctx->irq_handler = _hw_uart0_irq_handler;
    break;
#if NUM_UARTS > 1
  case 1:
    ctx->irq_handler = _hw_uart1_irq_handler;
    break;
#endif
  default:
    sys_panicf("hw_uart_init: invalid UART instance %u", idx);
    break;
  }

  uart_init(instance, baud_rate);
  uart_set_format(instance, settings.data_bits, settings.stop_bits,
                  _hw_uart_sdk_parity(settings.parity));
  uart_set_hw_flow(instance,
                   settings.flow_control == hw_uart_flow_control_cts ||
                       settings.flow_control == hw_uart_flow_control_cts_rts,
                   settings.flow_control == hw_uart_flow_control_rts ||
                       settings.flow_control == hw_uart_flow_control_cts_rts);

  // uart_init() enables hardware FIFOs by default, but their fill-level
  // interrupts (TXIM/RXIM) don't reliably assert on this chip - confirmed
  // via live register inspection, and matched by the Pico SDK's own
  // uart_advanced example, which disables FIFOs specifically to get
  // interrupts working ("we want to do this character by character").
  // Character mode drops hardware buffering to 1 byte; the software ring
  // buffers above make up for it unless settings.unbuffered opted out.
  uart_set_fifo_enabled(instance, false);

  stream->backend.uart.instance = ctx;
  stream->backend.uart.callback = NULL;
  stream->backend.uart.userdata = NULL;

  // Arms the background RX-draining interrupt if buffered - independent
  // of whether the caller ever registers a readiness callback.
  _hw_uart_update_irqs(ctx);

  return stream;
}

/** Stub implementation: Pico selects a UART by GPIO pins, not a device
 * path - see hw_uart_init(). */
sys_iostream_t *hw_uart_init_device(const char *device, uint32_t baud_rate,
                                    const hw_uart_config_t *config) {
  sys_debugf("hw", "uart_init_device: unsupported on this target "
                   "(device=%s baud=%u config=%p)",
            device != NULL ? device : "(null)", baud_rate, (void *)config);
  (void)device;
  (void)baud_rate;
  (void)config;
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool hw_uart_flush(sys_iostream_t *uart, uint32_t timeout_ms) {
  if (uart == NULL || uart->ops != &_hw_uart_ops) {
    return false;
  }
  _hw_uart_ctx_t *ctx = (_hw_uart_ctx_t *)uart->backend.uart.instance;

  if (_hw_uart_tx_idle(ctx)) {
    return true;
  }
  if (timeout_ms == 0) {
    return false;
  }

  absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
  while (!_hw_uart_tx_idle(ctx)) {
    if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
      return false;
    }
  }
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// INTERRUPTS

static void _hw_uart_irq_handler(_hw_uart_ctx_t *ctx) {
  uint32_t status = uart_get_hw(ctx->instance)->mis;
  uint32_t clear_mask = 0;
  sys_iostream_event_t events = sys_iostream_event_none;

  if ((status & (UART_UARTMIS_RXMIS_BITS | UART_UARTMIS_RTMIS_BITS)) != 0) {
    clear_mask |= UART_UARTICR_RXIC_BITS | UART_UARTICR_RTIC_BITS;
    if (!ctx->unbuffered) {
      while (uart_is_readable(ctx->instance) &&
            ctx->rx_count < HW_UART_BUFFER_SIZE) {
        ctx->rx_buf[ctx->rx_write] = (char)uart_get_hw(ctx->instance)->dr;
        ctx->rx_write = (ctx->rx_write + 1) % HW_UART_BUFFER_SIZE;
        ctx->rx_count++;
      }
    }
    events |= sys_iostream_event_read;
  }
  if ((status & UART_UARTMIS_TXMIS_BITS) != 0) {
    clear_mask |= UART_UARTICR_TXIC_BITS;
    if (!ctx->unbuffered) {
      _hw_uart_tx_drain(ctx);
    }
    events |= sys_iostream_event_write;
  }

  if (clear_mask != 0) {
    uart_get_hw(ctx->instance)->icr = clear_mask;
  }

  // Re-evaluate now that tx_count/rx state may have changed - e.g. turns
  // the TX interrupt back off once the ring buffer's fully drained,
  // rather than spinning on an interrupt with nothing left to send.
  _hw_uart_update_irqs(ctx);

  sys_iostream_callback_t callback = ctx->stream->backend.uart.callback;
  if (callback != NULL && events != sys_iostream_event_none) {
    callback(ctx->stream, events, ctx->stream->backend.uart.userdata);
  }
}

static void _hw_uart0_irq_handler(void) {
  _hw_uart_irq_handler(&_hw_uart_ctx[0]);
}

#if NUM_UARTS > 1
static void _hw_uart1_irq_handler(void) {
  _hw_uart_irq_handler(&_hw_uart_ctx[1]);
}
#endif
