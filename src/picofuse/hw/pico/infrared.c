#include "../../sys/pico/sync.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "infrared_tx.pio.h"
#include "pico/time.h"
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <stddef.h>
#include <string.h>

#define HW_INFRARED_DEFAULT_CARRIER_FREQ 38000u
#define HW_INFRARED_DEFAULT_TIMEOUT_US 50000u

// How long to wait for the TX FIFO to fully drain before giving up
#define HW_INFRARED_FIFO_TIMEOUT_US 3000u

///////////////////////////////////////////////////////////////////////////////
// TYPES

// A pool slot with no dedicated hardware instance to index by (unlike
// e.g. NUM_PWM_SLICES/NUM_UARTS) - any GPIO pin reachable by a free PIO
// state machine can serve, so this is just a small fixed-size pool of
// handles instead, matching hw_pwm_t/hw_uart_t's own "define the real
// struct here, hand out pointers into a static array" pattern.
struct hw_infrared_t {
  bool active;
  bool has_tx;
  PIO tx_pio;
  uint tx_sm;
  uint tx_offset;
  uint32_t tx_carrier_freq; // periods-per-second, for hw_infrared_transmit()'s
                            // microseconds -> whole-periods conversion
  bool has_rx; // @todo always false - see hw_infrared_init()'s own comment
  hw_infrared_callback_t callback;
  void *userdata;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static hw_infrared_t _hw_infrared_pool[HW_INFRARED_CAPACITY] = {0};

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static hw_infrared_t *_hw_infrared_alloc(void) {
  _sys_sync_pool_lock();
  for (size_t i = 0; i < HW_INFRARED_CAPACITY; i++) {
    if (!_hw_infrared_pool[i].active) {
      _hw_infrared_pool[i].active = true;
      _sys_sync_pool_unlock();
      return &_hw_infrared_pool[i];
    }
  }
  _sys_sync_pool_unlock();
  return NULL;
}

static void _hw_infrared_free(hw_infrared_t *ir) {
  _sys_sync_pool_lock();
  memset(ir, 0, sizeof(*ir));
  _sys_sync_pool_unlock();
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hw_infrared_t *hw_infrared_init(const hw_gpio_t *rx_pin,
                                const hw_gpio_t *tx_pin,
                                const hw_infrared_config_t *config) {
  sys_debugf("hw", "infrared_init: rx=%p tx=%p config=%p", (void *)rx_pin,
             (void *)tx_pin, (void *)config);

  if (rx_pin == NULL && tx_pin == NULL) {
    return NULL;
  }
  if (rx_pin != NULL) {
    // @todo No real RX backend wired in yet (infrared_rx.pio + an IRQ-
    // driven decode loop) - reject outright rather than accepting a pin
    // that would silently never deliver an event, which
    // hw_infrared_set_callback() returning true for it would imply.
    sys_debugf("hw", "infrared_init: RX not yet supported on this backend");
    return NULL;
  }

  uint32_t carrier_freq = (config != NULL && config->carrier_freq != 0)
                              ? config->carrier_freq
                              : HW_INFRARED_DEFAULT_CARRIER_FREQ;

  hw_infrared_t *ir = _hw_infrared_alloc();
  if (ir == NULL) {
    return NULL;
  }

  uint pin = hw_gpio_pin(tx_pin);
  PIO pio;
  uint sm, offset;
  if (!pio_claim_free_sm_and_add_program_for_gpio_range(
          &infrared_tx_program, &pio, &sm, &offset, pin, 1, true)) {
    _hw_infrared_free(ir);
    return NULL;
  }
  infrared_tx_program_init(pio, sm, offset, pin, (float)carrier_freq);

  ir->has_tx = true;
  ir->tx_pio = pio;
  ir->tx_sm = sm;
  ir->tx_offset = offset;
  ir->tx_carrier_freq = carrier_freq;

  return ir;
}

hw_infrared_t *hw_infrared_init_device(const char *rx_device,
                                       const char *tx_device,
                                       const hw_infrared_config_t *config) {
  sys_debugf("hw",
             "infrared_init_device: unsupported on this target (rx=%s "
             "tx=%s config=%p)",
             rx_device != NULL ? rx_device : "(null)",
             tx_device != NULL ? tx_device : "(null)", (void *)config);
  (void)rx_device;
  (void)tx_device;
  (void)config;
  return NULL;
}

void hw_infrared_deinit(hw_infrared_t *ir) {
  sys_debugf("hw", "infrared_deinit: ir=%p", (void *)ir);
  if (ir == NULL || !ir->active) {
    return;
  }

  if (ir->has_tx) {
    pio_sm_set_enabled(ir->tx_pio, ir->tx_sm, false);
    pio_remove_program_and_unclaim_sm(&infrared_tx_program, ir->tx_pio,
                                      ir->tx_sm, ir->tx_offset);
  }

  _hw_infrared_free(ir);
}

///////////////////////////////////////////////////////////////////////////////
// METHODS

bool hw_infrared_set_callback(hw_infrared_t *ir,
                              hw_infrared_callback_t callback, void *userdata) {
  if (ir == NULL || !ir->has_rx) {
    return false;
  }
  ir->callback = callback;
  ir->userdata = userdata;
  return true;
}

bool hw_infrared_transmit(hw_infrared_t *ir, const uint32_t *durations_us,
                          size_t count) {
  if (ir == NULL || !ir->has_tx || (durations_us == NULL && count > 0)) {
    return false;
  }

  for (size_t i = 0; i < count; i++) {
    // infrared_tx.pio counts whole carrier periods, not microseconds -
    // convert, rounding to the nearest period (IR receivers tolerate
    // small timing error far better than a systematic short-bias
    // compounding over a whole frame would).
    uint64_t periods =
        ((uint64_t)durations_us[i] * ir->tx_carrier_freq + 500000ull) /
        1000000ull;
    if (periods == 0) {
      periods = 1;
    }

    // The PIO program's own loop counter is "periods - 1": with the
    // counter at 0, its jmp x-- instructions don't jump, but they - like
    // every other instruction - still execute their own side-set/delay
    // once before falling through, so a counter of 0 already plays out
    // exactly one period, not zero.
    uint32_t word = (uint32_t)(periods - 1);

    uint64_t start = time_us_64();
    while (pio_sm_is_tx_fifo_full(ir->tx_pio, ir->tx_sm)) {
      if (time_us_64() - start > HW_INFRARED_FIFO_TIMEOUT_US) {
        return false;
      }
    }
    pio_sm_put(ir->tx_pio, ir->tx_sm, word);
  }

  uint64_t start = time_us_64();
  while (!pio_sm_is_tx_fifo_empty(ir->tx_pio, ir->tx_sm)) {
    if (time_us_64() - start > HW_INFRARED_FIFO_TIMEOUT_US) {
      return false;
    }
  }
  return true;
}
