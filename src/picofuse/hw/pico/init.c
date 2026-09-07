#include <hardware/adc.h>
#include <picofuse/hw/init.h>
#include <picofuse/sys.h>

#if PICO_CYW43_SUPPORTED
#include <pico/cyw43_arch.h>
#endif

#ifdef PICOFUSE_WIFI
// Defined in wifi.c.
extern void _hw_wifi_poll(void);
#endif

#ifdef PICOFUSE_USB
// Defined in usb.c.
extern void _hw_usb_poll(void);
#endif

// Defined in flash.c.
extern void _hw_flash_module_init(void);
extern void _hw_flash_module_exit(void);

// Defined in ../led/blink.c.
extern void _hw_led_poll(void);

// Defined in watchdog.c.
extern void _hw_watchdog_module_exit(void);
extern void _hw_watchdog_poll(void);

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Initializes the hardware system on startup.
 */
void hw_init(void) {
  adc_init();
  _hw_flash_module_init();

#if PICO_CYW43_SUPPORTED
  if (cyw43_arch_init()) {
    sys_panicf("cyw43_arch_init failed");
  }
#endif
}

/**
 * @brief Cleans up the hardware system on shutdown.
 */
void hw_exit(void) {
  _hw_watchdog_module_exit();
  _hw_flash_module_exit();
#if PICO_CYW43_SUPPORTED
  cyw43_arch_deinit();
#endif
}

/**
 * @brief Occasional polling function for the hardware system.
 */
void hw_poll(void) {
#if PICO_CYW43_SUPPORTED && PICO_CYW43_ARCH_POLL
  // Only meaningful under the poll architecture - under
  // threadsafe_background, driver/lwIP work already happens on its own via
  // interrupt, and cyw43_arch_poll() isn't there to call.
  cyw43_arch_poll();
#endif
#ifdef PICOFUSE_WIFI
  _hw_wifi_poll();
#endif
#ifdef PICOFUSE_USB
  _hw_usb_poll();
#endif
  _hw_led_poll();
  _hw_watchdog_poll();
}

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/**
 * @brief picofuse-app's own app_main() uses this to decide whether
 * app_flag_multicore should be allowed.
 */
bool _hw_requires_single_core(void) {
#if PICO_CYW43_SUPPORTED && PICO_CYW43_ARCH_POLL
  return true;
#else
  return false;
#endif
}
