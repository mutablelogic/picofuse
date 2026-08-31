#include "../printf/mutex.h"
#include "sync.h"
#include <hardware/gpio.h>
#include <hardware/uart.h>
#include <picofuse/sys.h>
#include <runtime/stdout.h>
#include <stdlib.h>

// Defined in timer.c.
extern void _sys_timer_module_init(void);
extern void _sys_timer_module_exit(void);

// Defined in mem/mem.c.
extern bool _sys_mem_module_init(size_t capacity, void *(*malloc_fn)(size_t),
                          void (*free_fn)(void *));
extern void _sys_mem_module_exit(void);

void sys_init(int argc, char *argv[], size_t arena_size) {
  // Pico has no command line arguments
  (void)argc;
  (void)argv;

  // Initalize stdout
  sys_stdout = uart_get_instance(PICO_DEFAULT_UART);
  sys_assert(sys_stdout != NULL);
  gpio_pull_up(PICO_DEFAULT_UART_TX_PIN);
  gpio_set_function(
      PICO_DEFAULT_UART_TX_PIN,
      UART_FUNCSEL_NUM((uart_inst_t *)sys_stdout, PICO_DEFAULT_UART_TX_PIN));
  gpio_set_function(
      PICO_DEFAULT_UART_RX_PIN,
      UART_FUNCSEL_NUM((uart_inst_t *)sys_stdout, PICO_DEFAULT_UART_RX_PIN));
  uart_init((uart_inst_t *)sys_stdout, PICO_DEFAULT_UART_BAUD_RATE);
  uart_set_translate_crlf((uart_inst_t *)sys_stdout, true);

  // Initialize the shared critical section used by sync primitives
  _sys_sync_module_init();

  // Initialize the critical section used by the timer pool
  _sys_timer_module_init();

  // Initialize the printf mutex for thread-safe operations
  _sys_printf_init();

  // Set the timestamp base for relative timing
  sys_timestamp_ms();
}

void sys_exit(void) {
  // Stop and release any still-active timers
  _sys_timer_module_exit();

  /* TODO: Shutdown or reset any non-main cores */

  // Deinitialize the printf mutex for thread-safe operations
  _sys_printf_exit();

  // Deinitialize the shared critical section used by sync primitives
  _sys_sync_module_deinit();

  // Print a message indicating that the system is halting
  sys_puts("\n[HALT]\n");

  // Deinit the UART used for stdout
  uart_tx_wait_blocking((uart_inst_t *)sys_stdout);
  uart_deinit((uart_inst_t *)sys_stdout);
  sys_stdout = NULL;

  // Halt the system and never return
  sys_halt();
}
