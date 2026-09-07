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

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

/**
 * @brief Initializes the hardware system on startup.
 */
void hw_init(void) {
  adc_init();

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
