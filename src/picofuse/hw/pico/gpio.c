#include "../../sys/pico/sync.h"
#include <hardware/adc.h>
#include <hardware/gpio.h>
#include <hardware/uart.h>
#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct hw_gpio_t {
  uint8_t pin;
  uint64_t mask;
  hw_gpio_mode_t mode;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static struct hw_gpio_t pins[NUM_BANK0_GPIOS] = {0};
static hw_gpio_callback_t _hw_gpio_callback_func;
static void *_hw_gpio_userdata;

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static void _hw_gpio_callback(uint pin, uint32_t events);

/** @brief Validate the GPIO pin. */
static inline bool _hw_gpio_valid(const hw_gpio_t *gpio) {
  return gpio && gpio->pin < NUM_BANK0_GPIOS && gpio->mask != 0;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/**
 * @brief Initialize a GPIO pin with the specified mode.
 */
hw_gpio_t *hw_gpio_init(uint8_t bank, uint8_t pin, hw_gpio_mode_t mode) {
  sys_debugf("hw", "gpio_init: bank=%u pin=%u mode=%u", bank, pin, mode);
  if (bank != 0 || pin >= hw_gpio_count(bank)) {
    return NULL;
  }

  // pins[] has no kernel/hardware-level exclusivity of its own (unlike,
  // say, Linux's gpiochip line requests) - without this check, a second
  // hw_gpio_init() on a pin some other still-open handle already owns
  // would silently reconfigure out from under it rather than being
  // rejected, the same class of bug _hw_deviceio_alloc()/_hw_led_alloc()
  // guard against for their own pools. Held for the whole call, not just
  // the mask check, so a concurrent claim on the other core can't
  // interleave with this one's own hardware setup below.
  _sys_sync_pool_lock();
  if (pins[pin].mask != 0) {
    _sys_sync_pool_unlock();
    return NULL;
  }

  // Initialize the GPIO pin using the Pico SDK
  gpio_init(pin);

  // Initialize the GPIO pin
  hw_gpio_t *gpio = &pins[pin];
  gpio->mask = (1ULL << pin);
  gpio->pin = pin;
  gpio->mode = mode;

  // Set the GPIO mode
  if (mode > 0) {
    hw_gpio_set_mode(gpio, mode);
  }
  _sys_sync_pool_unlock();

  // Return the initialized GPIO
  return gpio;
}

/**
 * @brief Deinitialize and release a GPIO pin.
 */
void hw_gpio_deinit(hw_gpio_t *gpio) {
  sys_debugf("hw", "gpio_deinit: gpio=%p", gpio);
  if (!_hw_gpio_valid(gpio)) {
    return;
  }
  gpio_deinit(gpio->pin);
  memset(gpio, 0, sizeof(hw_gpio_t));
}

/**
 * @brief Get the logical pin number for a GPIO handle.
 */
uint8_t hw_gpio_pin(const hw_gpio_t *gpio) {
  sys_assert(_hw_gpio_valid(gpio));
  return gpio->pin;
}

/**
 * @brief Get the GPIO bank number for a GPIO handle.
 */
uint8_t hw_gpio_bank(const hw_gpio_t *gpio) {
  (void)gpio;
  return 0; // Pico only supports bank 0
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Set the global GPIO interrupt callback handler.
 */
void hw_gpio_set_callback(hw_gpio_callback_t callback, void *userdata) {
  _hw_gpio_callback_func = callback;
  _hw_gpio_userdata = userdata;
}

/**
 * @brief Get the total number of available GPIO pins for a given bank.
 */
uint8_t hw_gpio_count(uint8_t bank) {
  // Pico only supports bank 0
  if (bank != 0) {
    return 0;
  }

  if (NUM_BANK0_GPIOS > UINT8_MAX) {
    return UINT8_MAX;
  }

  return (uint8_t)NUM_BANK0_GPIOS;
}

/**
 * @brief Get the current mode configuration of a GPIO pin.
 */
hw_gpio_mode_t hw_gpio_get_mode(const hw_gpio_t *gpio) {
  sys_assert(gpio && gpio->mask != 0);

  gpio_function_t f = gpio_get_function(gpio->pin);
  switch (f) {
  case GPIO_FUNC_NULL:
    return hw_gpio_adc; // Default to ADC if no function set
  case GPIO_FUNC_SPI:
    return hw_gpio_spi;
  case GPIO_FUNC_I2C:
    return hw_gpio_i2c;
  case GPIO_FUNC_UART:
    return hw_gpio_uart;
#ifdef GPIO_FUNC_UART_AUX
  case GPIO_FUNC_UART_AUX:
    return hw_gpio_uart;
#endif
  case GPIO_FUNC_PWM:
    return hw_gpio_pwm;
  case GPIO_FUNC_SIO:
    if (gpio_get_dir(gpio->pin) == GPIO_IN) {
      if (gpio_is_pulled_up(gpio->pin)) {
        return hw_gpio_pullup;
      } else if (gpio_is_pulled_down(gpio->pin)) {
        return hw_gpio_pulldown;
      } else {
        return hw_gpio_input;
      }
    } else {
      return hw_gpio_output;
    }
  default:
    // Unsupported or unknown function
    return hw_gpio_unknown;
  }
}

/**
 * @brief Set the current mode configuration of a GPIO pin.
 */
void hw_gpio_set_mode(hw_gpio_t *gpio, hw_gpio_mode_t mode) {
  if (!_hw_gpio_valid(gpio)) {
    return;
  }

  // Set GPIO pin to SIO
  gpio_init(gpio->pin);

  // Cancel the interrupt
  if (mode != hw_gpio_input && mode != hw_gpio_pullup &&
      mode != hw_gpio_pulldown) {
    gpio_set_irq_enabled(gpio->pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                         false);
  }

  // Set the GPIO mode
  switch (mode) {
  case hw_gpio_none:
    return; // No function set, do nothing
  case hw_gpio_input:
    gpio_set_dir(gpio->pin, GPIO_IN);
    gpio_set_pulls(gpio->pin, false, false);
    gpio_set_irq_enabled_with_callback(gpio->pin,
                                       GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                       true, &_hw_gpio_callback);
    break;
  case hw_gpio_pullup:
    gpio_set_pulls(gpio->pin, true, false);
    gpio_set_irq_enabled_with_callback(gpio->pin,
                                       GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                       true, &_hw_gpio_callback);
    break;
  case hw_gpio_pulldown:
    gpio_set_dir(gpio->pin, GPIO_IN);
    gpio_set_pulls(gpio->pin, false, true);
    gpio_set_irq_enabled_with_callback(gpio->pin,
                                       GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                       true, &_hw_gpio_callback);
    break;
  case hw_gpio_output:
    gpio_set_dir(gpio->pin, GPIO_OUT);
    break;
  case hw_gpio_spi:
    gpio_set_function(gpio->pin, GPIO_FUNC_SPI);
    break;
  case hw_gpio_i2c:
    gpio_set_function(gpio->pin, GPIO_FUNC_I2C);
    gpio_set_pulls(gpio->pin, true, false);
    break;
  case hw_gpio_uart:
    gpio_set_function(gpio->pin, GPIO_FUNC_UART);
    break;
  case hw_gpio_pwm:
    gpio_set_function(gpio->pin, GPIO_FUNC_PWM);
    break;
  case hw_gpio_adc:
    adc_gpio_init(gpio->pin);
    break;
  default:
    sys_panicf("Invalid GPIO mode");
    break;
  }
}

/**
 * @brief Read the current state of a GPIO pin.
 */
bool hw_gpio_get(const hw_gpio_t *gpio) {
  sys_assert(_hw_gpio_valid(gpio));
  return gpio_get(gpio->pin);
}

/**
 * @brief Set the state of a GPIO pin.
 */
void hw_gpio_set(hw_gpio_t *gpio, bool value) {
  sys_assert(_hw_gpio_valid(gpio));
  gpio_put(gpio->pin, value);
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static void _hw_gpio_callback(uint pin, uint32_t events) {
  if (_hw_gpio_callback_func == NULL) {
    return; // No callback set, do nothing
  }

  // Set event state
  hw_gpio_event_t event = 0;
  if (events & GPIO_IRQ_EDGE_RISE) {
    event |= hw_gpio_rising;
  }
  if (events & GPIO_IRQ_EDGE_FALL) {
    event |= hw_gpio_falling;
  }

  // Pico only has bank 0
  _hw_gpio_callback_func(0, pin, event, _hw_gpio_userdata);
}
