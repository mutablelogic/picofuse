#include <picofuse/sys.h>

#include "../iostream/iostream.h"

// Standard input and output over USB, as a CDC serial port, on TinyUSB's device stack
// from the Pico SDK, with the SDK's descriptors. The SDK's own pico_stdio_usb driver
// isn't used, as it needs the default alarm pool, which picofuse disables, but this
// works as it does: TinyUSB's tud_task() runs in a low-priority interrupt, raised from
// the USB controller's shared interrupt handler, and reads and writes are in a
// critical section. It isn't built with PICOFUSE_USB, which has the one USB
// controller for a host, when sys_stdio_usb is the UART instead.
#if defined(PICOFUSE_STDIO_USB)
#include <hardware/irq.h>
#include <pico/critical_section.h>
#include <pico/time.h>
#include <tusb.h>

// How long a write waits for a full buffer to be sent before it gives up, as the
// SDK's driver does, so it doesn't block when nothing reads it
#ifndef SYS_STDIO_USB_TIMEOUT_US
#define SYS_STDIO_USB_TIMEOUT_US 500000
#endif

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static critical_section_t _sys_stdio_usb_lock;
static uint8_t _sys_stdio_usb_irq = 0;
static bool _sys_stdio_usb_started = false;

// When output last got into TinyUSB's buffer, across writes, as printf writes a
// character at a time: once it's been longer than the timeout, writes are dropped
// until there's room again. It's under the lock.
static uint64_t _sys_stdio_usb_progress_us = 0;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

// TinyUSB's work, in the low-priority interrupt
static void _sys_stdio_usb_task(void) {
  critical_section_enter_blocking(&_sys_stdio_usb_lock);
  tud_task();
  critical_section_exit(&_sys_stdio_usb_lock);
}

// The USB controller's interrupt, after TinyUSB's own handler, raises the
// low-priority one
static void _sys_stdio_usb_irq_handler(void) { irq_set_pending(_sys_stdio_usb_irq); }

static size_t _sys_stdio_usb_read(sys_iostream_t *stream, char *buf, size_t n) {
  (void)stream;
  critical_section_enter_blocking(&_sys_stdio_usb_lock);
  size_t got = tud_cdc_connected() && tud_cdc_available() ? tud_cdc_read(buf, n) : 0;
  critical_section_exit(&_sys_stdio_usb_lock);
  return got;
}

// Written as the buffer has room, or dropped if nothing is connected, or nothing has
// been taken from the buffer for longer than the timeout, across writes
static size_t _sys_stdio_usb_write(sys_iostream_t *stream, const char *buf,
                                   size_t n) {
  (void)stream;
  size_t put = 0;
  while (put < n) {
    critical_section_enter_blocking(&_sys_stdio_usb_lock);
    bool connected = tud_cdc_connected();
    bool timed_out = false;
    if (connected) {
      uint32_t room = tud_cdc_write_available();
      uint64_t now = time_us_64();
      if (room > 0) {
        put += tud_cdc_write(buf + put, n - put < room ? n - put : room);
        _sys_stdio_usb_progress_us = now;
      } else {
        timed_out = now - _sys_stdio_usb_progress_us > SYS_STDIO_USB_TIMEOUT_US;
      }
      tud_cdc_write_flush();
    }
    critical_section_exit(&_sys_stdio_usb_lock);
    if (!connected || timed_out) {
      break;
    }
    if (put < n) {
      irq_set_pending(_sys_stdio_usb_irq); // to send what's there
      busy_wait_us(100);
    }
  }
  return n;
}

static ptrdiff_t _sys_stdio_usb_seek(sys_iostream_t *stream, ptrdiff_t offset,
                                     bool abs) {
  (void)stream;
  (void)offset;
  (void)abs;
  return -1;
}

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static const sys_iostream_ops_t _sys_stdio_usb_ops = {
    .read = _sys_stdio_usb_read,
    .write = _sys_stdio_usb_write,
    .seek = _sys_stdio_usb_seek,
    .set_callback = NULL,
    .close = NULL,
};

///////////////////////////////////////////////////////////////////////////////
// MODULE LIFECYCLE

bool _sys_stdio_usb_init(void) {
  if (!_sys_stdio_usb_started) {
    critical_section_init(&_sys_stdio_usb_lock);
    if (!tusb_init()) {
      return false;
    }
    _sys_stdio_usb_irq = (uint8_t)user_irq_claim_unused(true);
    irq_set_exclusive_handler(_sys_stdio_usb_irq, _sys_stdio_usb_task);
    irq_set_enabled(_sys_stdio_usb_irq, true);
    irq_add_shared_handler(USBCTRL_IRQ, _sys_stdio_usb_irq_handler,
                           PICO_SHARED_IRQ_HANDLER_LOWEST_ORDER_PRIORITY);
    _sys_stdio_usb_started = true;
  }
  sys_stdout = _sys_iostream_alloc(&_sys_stdio_usb_ops);
  sys_stdin = sys_stdout;
  sys_assert(sys_stdout != NULL);
  return true;
}

// TinyUSB keeps running, so the port stays, and stdio can be set up again
void _sys_stdio_usb_exit(void) {
  sys_iostream_close(sys_stdout);
  sys_stdout = NULL;
  sys_stdin = NULL;
}

#else

bool _sys_stdio_usb_init(void) { return false; }

void _sys_stdio_usb_exit(void) {}

#endif
