#include <picofuse/sys.h>

#include "../iostream/iostream.h"
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/sync.h>
#include <hardware/uart.h>

#ifndef SYS_STDIO_UART_BUFFER_SIZE
#define SYS_STDIO_UART_BUFFER_SIZE 256
#endif

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static uart_inst_t *_sys_stdio_uart = NULL;
static char _sys_stdio_uart_buffer[SYS_STDIO_UART_BUFFER_SIZE];
static size_t _sys_stdio_uart_buffer_read = 0;
static size_t _sys_stdio_uart_buffer_write = 0;
static size_t _sys_stdio_uart_buffer_count = 0;
static bool _sys_stdio_uart_write_blocked = false;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static void _sys_stdio_uart_update_irqs(void) {
  bool read_enabled =
      sys_stdin != NULL && sys_stdin->backend.uart.callback != NULL;
  uart_set_irqs_enabled(_sys_stdio_uart, read_enabled,
                        _sys_stdio_uart_buffer_count > 0);
}

static void _sys_stdio_uart_drain(void) {
  while (_sys_stdio_uart_buffer_count > 0 &&
         uart_is_writable(_sys_stdio_uart)) {
    uart_putc(_sys_stdio_uart,
              _sys_stdio_uart_buffer[_sys_stdio_uart_buffer_read]);
    _sys_stdio_uart_buffer_read =
        (_sys_stdio_uart_buffer_read + 1) % SYS_STDIO_UART_BUFFER_SIZE;
    _sys_stdio_uart_buffer_count--;
  }
}

static size_t _sys_stdio_uart_read(sys_iostream_t *stream, char *buf,
                                   size_t n) {
  uart_inst_t *uart = stream->backend.uart.instance;
  size_t got = 0;
  while (got < n && uart_is_readable(uart)) {
    buf[got++] = (char)uart_getc(uart);
  }
  return got;
}

static size_t _sys_stdio_uart_write(sys_iostream_t *stream, const char *buf,
                                    size_t n) {
  if (stream != sys_stdout || _sys_stdio_uart == NULL) {
    return 0;
  }

  uint32_t irq_state = save_and_disable_interrupts();
  size_t put = 0;
  while (put < n && _sys_stdio_uart_buffer_count < SYS_STDIO_UART_BUFFER_SIZE) {
    _sys_stdio_uart_buffer[_sys_stdio_uart_buffer_write] = buf[put++];
    _sys_stdio_uart_buffer_write =
        (_sys_stdio_uart_buffer_write + 1) % SYS_STDIO_UART_BUFFER_SIZE;
    _sys_stdio_uart_buffer_count++;
  }
  if (put < n) {
    _sys_stdio_uart_write_blocked = true;
  }
  _sys_stdio_uart_drain();
  _sys_stdio_uart_update_irqs();
  restore_interrupts(irq_state);
  return put;
}

static ptrdiff_t _sys_stdio_uart_seek(sys_iostream_t *stream, ptrdiff_t offset,
                                      bool abs) {
  (void)stream;
  (void)offset;
  (void)abs;
  return -1;
}

static void _sys_stdio_uart_irq(void) {
  if (sys_stdin != NULL && uart_is_readable(_sys_stdio_uart)) {
    sys_iostream_callback_t callback = sys_stdin->backend.uart.callback;
    if (callback != NULL) {
      callback(sys_stdin, sys_iostream_event_read,
               sys_stdin->backend.uart.userdata);
    }
  }

  _sys_stdio_uart_drain();
  if (_sys_stdio_uart_write_blocked && sys_stdout != NULL &&
      _sys_stdio_uart_buffer_count < SYS_STDIO_UART_BUFFER_SIZE) {
    _sys_stdio_uart_write_blocked = false;
    sys_iostream_callback_t callback = sys_stdout->backend.uart.callback;
    if (callback != NULL) {
      callback(sys_stdout, sys_iostream_event_write,
               sys_stdout->backend.uart.userdata);
    }
  }
  _sys_stdio_uart_update_irqs();
}

static bool _sys_stdio_uart_set_callback(sys_iostream_t *stream,
                                         sys_iostream_callback_t callback,
                                         void *userdata) {
  if ((stream != sys_stdin && stream != sys_stdout) ||
      _sys_stdio_uart == NULL) {
    return false;
  }

  uint32_t irq_state = save_and_disable_interrupts();
  stream->backend.uart.userdata = userdata;
  stream->backend.uart.callback = callback;
  _sys_stdio_uart_update_irqs();
  restore_interrupts(irq_state);
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static const sys_iostream_ops_t _sys_stdio_uart_ops = {
    .read = _sys_stdio_uart_read,
    .write = _sys_stdio_uart_write,
    .seek = _sys_stdio_uart_seek,
    .set_callback = _sys_stdio_uart_set_callback,
    .close = NULL,
};

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

static sys_iostream_t *_sys_stdio_uart_open(uart_inst_t *uart) {
  sys_iostream_t *stream = _sys_iostream_alloc(&_sys_stdio_uart_ops);
  if (stream != NULL) {
    stream->backend.uart.instance = uart;
  }
  return stream;
}

///////////////////////////////////////////////////////////////////////////////
// MODULE LIFECYCLE

void _sys_stdio_uart_init(void) {
  _sys_stdio_uart = uart_get_instance(PICO_DEFAULT_UART);
  sys_assert(_sys_stdio_uart != NULL);
  gpio_pull_up(PICO_DEFAULT_UART_TX_PIN);
  gpio_set_function(
      PICO_DEFAULT_UART_TX_PIN,
      UART_FUNCSEL_NUM(_sys_stdio_uart, PICO_DEFAULT_UART_TX_PIN));
  gpio_set_function(
      PICO_DEFAULT_UART_RX_PIN,
      UART_FUNCSEL_NUM(_sys_stdio_uart, PICO_DEFAULT_UART_RX_PIN));
  uart_init(_sys_stdio_uart, PICO_DEFAULT_UART_BAUD_RATE);
  uart_set_translate_crlf(_sys_stdio_uart, true);
  irq_set_exclusive_handler(UART_IRQ_NUM(_sys_stdio_uart), _sys_stdio_uart_irq);
  irq_set_enabled(UART_IRQ_NUM(_sys_stdio_uart), true);

  sys_stdout = _sys_stdio_uart_open(_sys_stdio_uart);
  sys_stdin = sys_stdout;
  sys_assert(sys_stdout != NULL);
  _sys_stdio_uart_buffer_read = 0;
  _sys_stdio_uart_buffer_write = 0;
  _sys_stdio_uart_buffer_count = 0;
  _sys_stdio_uart_write_blocked = false;
}

void _sys_stdio_uart_exit(void) {
  if (_sys_stdio_uart != NULL) {
    uart_set_irqs_enabled(_sys_stdio_uart, false, false);
    irq_set_enabled(UART_IRQ_NUM(_sys_stdio_uart), false);
    while (_sys_stdio_uart_buffer_count > 0) {
      _sys_stdio_uart_drain();
    }
    uart_tx_wait_blocking(_sys_stdio_uart);
  }
  sys_iostream_close(sys_stdout);
  sys_stdout = NULL;
  sys_stdin = NULL;
  if (_sys_stdio_uart != NULL) {
    uart_deinit(_sys_stdio_uart);
    _sys_stdio_uart = NULL;
  }
}