#include <picofuse/hw.h>
#include <stddef.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** Stub implementation: no PWM hardware on this platform. */
hw_pwm_t *hw_pwm_init(hw_gpio_t *gpio, hw_pwm_callback_t callback,
                      void *userdata, const hw_pwm_config_t *config) {
  (void)gpio;
  (void)callback;
  (void)userdata;
  (void)config;
  return NULL;
}

/** Stub implementation: no PWM hardware on this platform. */
hw_pwm_t *hw_pwm_init_device(const char *device, uint8_t channel,
                             const hw_pwm_config_t *config) {
  (void)device;
  (void)channel;
  (void)config;
  return NULL;
}

/** Stub implementation: no PWM hardware on this platform. */
void hw_pwm_deinit(hw_pwm_t *pwm) { (void)pwm; }

///////////////////////////////////////////////////////////////////////////////
// CONFIGURATION

/** Stub implementation: no PWM hardware on this platform. */
bool hw_pwm_set_period_ns(hw_pwm_t *pwm, uint64_t period_ns) {
  (void)pwm;
  (void)period_ns;
  return false;
}

/** Stub implementation: no PWM hardware on this platform. */
uint64_t hw_pwm_get_period_ns(const hw_pwm_t *pwm) {
  (void)pwm;
  return 0;
}

/** Stub implementation: no PWM hardware on this platform. */
bool hw_pwm_set_duty_percent(hw_pwm_t *pwm, float duty_percent) {
  (void)pwm;
  (void)duty_percent;
  return false;
}

/** Stub implementation: no PWM hardware on this platform. */
float hw_pwm_get_duty_percent(const hw_pwm_t *pwm) {
  (void)pwm;
  return 0.0f;
}

/** Stub implementation: no PWM hardware on this platform. */
bool hw_pwm_set_config(hw_pwm_t *pwm, const hw_pwm_config_t *config) {
  (void)pwm;
  (void)config;
  return false;
}

/** Stub implementation: no PWM hardware on this platform. */
bool hw_pwm_get_config(const hw_pwm_t *pwm, hw_pwm_config_t *out_config) {
  (void)pwm;
  (void)out_config;
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// CONTROL

/** Stub implementation: no PWM hardware on this platform. */
void hw_pwm_set_enabled(hw_pwm_t *pwm, bool enabled) {
  (void)pwm;
  (void)enabled;
}

/** Stub implementation: no PWM hardware on this platform. */
bool hw_pwm_get_enabled(const hw_pwm_t *pwm) {
  (void)pwm;
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// INTERRUPTS

/** Stub implementation: no PWM hardware on this platform. */
bool hw_pwm_irq_supported(void) { return false; }
