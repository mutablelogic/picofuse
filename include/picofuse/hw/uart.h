/**
 * @file uart.h
 * @brief UART (Universal Asynchronous Receiver/Transmitter) interface
 * @defgroup UART UART
 * @ingroup Hardware
 *
 * Universal Asynchronous Receiver/Transmitter (UART) interface for hardware
 * platforms.
 *
 * hw_uart_init() hands back a plain sys_iostream_t (picofuse/sys/io.h) -
 * once open, a UART is just another byte stream, so reading, writing,
 * closing, and readiness notification all go through the generic
 * sys_iostream_read()/sys_iostream_write()/sys_iostream_close()/
 * sys_iostream_set_callback() rather than UART-specific equivalents. Only
 * hw_uart_flush() remains here, for the one thing genuinely specific to a
 * hardware serial line: waiting for the transmit FIFO to actually drain.
 */
#pragma once
#include "gpio.h"
#include <picofuse/sys/io.h>
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief UART data bit configuration.
 * @ingroup UART
 */
typedef enum {
  hw_uart_data_bits_5 = 5,
  hw_uart_data_bits_6 = 6,
  hw_uart_data_bits_7 = 7,
  hw_uart_data_bits_8 = 8,
} hw_uart_data_bits_t;

/**
 * @brief UART stop bit configuration.
 * @ingroup UART
 */
typedef enum {
  hw_uart_stop_bits_1 = 1,
  hw_uart_stop_bits_2 = 2,
} hw_uart_stop_bits_t;

/**
 * @brief UART parity configuration.
 * @ingroup UART
 */
typedef enum {
  hw_uart_parity_none,
  hw_uart_parity_even,
  hw_uart_parity_odd,
} hw_uart_parity_t;

/**
 * @brief UART hardware flow control mode.
 * @ingroup UART
 */
typedef enum {
  hw_uart_flow_control_none,
  hw_uart_flow_control_cts,
  hw_uart_flow_control_rts,
  hw_uart_flow_control_cts_rts,
} hw_uart_flow_control_t;

/**
 * @brief UART initialization configuration.
 * @ingroup UART
 *
 * Describes optional UART settings beyond the required RX pin, TX pin,
 * and baud rate passed directly to hw_uart_init().
 *
 * When NULL is passed to hw_uart_init(), implementation defaults are used
 * for every field here.
 */
typedef struct {
  const hw_gpio_t *cts_pin; ///< Optional CTS pin for hardware flow control.
  const hw_gpio_t *rts_pin; ///< Optional RTS pin for hardware flow control.
  hw_uart_data_bits_t data_bits; ///< Number of data bits per frame.
  hw_uart_stop_bits_t stop_bits; ///< Number of stop bits per frame.
  hw_uart_parity_t parity; ///< Parity mode for transmitted and received frames.
  hw_uart_flow_control_t flow_control; ///< Flow-control mode to enable.
} hw_uart_config_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize a UART device.
 * @ingroup UART
 * @param rx_pin The GPIO pin to use for UART receive.
 * @param tx_pin The GPIO pin to use for UART transmit.
 * @param baud_rate The UART baud rate in bits per second.
 * @param config Optional pointer to extended UART configuration. Pass NULL
 * to use default line format and no flow control.
 * @return An open stream ready for sys_iostream_read()/write(), or NULL if
 * initialization fails. Release it with sys_iostream_close() - there is no
 * separate hw_uart_deinit().
 *
 * Readiness notification (data available to read, space available to
 * write) is available through sys_iostream_set_callback() on the returned
 * stream, using sys_iostream_event_read/sys_iostream_event_write, the same
 * as any other stream - not a UART-specific event/callback type. On the
 * Pico backend, this costs real throughput: the hardware's hold-up-to-32-
 * bytes FIFOs don't reliably raise their own fill-level interrupts
 * (confirmed independently of this driver, and matched by the Pico SDK's
 * own uart_advanced example, which disables FIFOs for the same reason),
 * so hw_uart_init() runs the UART in character mode instead - 1 byte of
 * hardware buffering rather than 32 - to make readiness callbacks fire
 * reliably. A consequence: under load, a single sys_iostream_write() call
 * only ever accepts 1 byte rather than filling a deep FIFO in one shot -
 * write the rest in a loop rather than assuming a burst is atomic.
 */
sys_iostream_t *hw_uart_init(const hw_gpio_t *rx_pin, const hw_gpio_t *tx_pin,
                             uint32_t baud_rate,
                             const hw_uart_config_t *config);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Wait for UART transmission to complete.
 * @ingroup UART
 * @param uart A stream from hw_uart_init().
 * @param timeout_ms Timeout for the operation in milliseconds. Set to 0 for
 * a non-blocking status check.
 * @retval true All pending transmit data was sent before the timeout expired.
 * @retval false Transmission was still in progress when the timeout expired,
 * or @p uart is invalid or not a UART stream.
 *
 * Waits until all data already accepted by sys_iostream_write() has left
 * the UART's own transmit FIFO/shift register - something no generic
 * sys_iostream_t operation can express, since it's about the underlying
 * hardware's state rather than the stream's buffer.
 */
bool hw_uart_flush(sys_iostream_t *uart, uint32_t timeout_ms);

/** @} */
