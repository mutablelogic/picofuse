/**
 * @file adc.h
 * @brief ADC (Analog-to-Digital Converter) interface
 * @defgroup ADC ADC
 * @ingroup Hardware
 *
 * Analog-to-Digital Converter (ADC) interface for hardware platforms.
 * This module provides functions to initialize and read from ADC peripherals.
 */
#pragma once
#include "gpio.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Opaque ADC handle.
 * @ingroup ADC
 * @headerfile adc.h hw/hw.h
 */
typedef struct hw_adc_t hw_adc_t;

/**
 * @brief DMA read completion callback.
 * @ingroup ADC
 *
 * @param adc ADC handle the transfer was started on.
 * @param buf Pointer to the partition of hw_adc_read_dma()'s buffer that
 * was just filled with raw samples - not necessarily the start of the
 * buffer passed to hw_adc_read_dma(), see `partitions` there.
 * @param samples Number of samples written to `buf`.
 * @param userdata User-defined data pointer passed to hw_adc_read_dma().
 * @retval true Continue: start capturing into the next partition.
 * @retval false Stop; no further partitions are captured.
 *
 * Called synchronously from the DMA-complete interrupt on systems with a
 * real DMA controller (e.g. Pico) - the system does not start capturing
 * the next partition until this returns, so it must return immediately.
 * Don't process `buf` here: instead hand it off asynchronously (e.g. by
 * posting an event naming this partition) to be read out elsewhere, then
 * return. The system cycles through `partitions` round-robin, so once
 * handed off, that consumer has until the other `partitions - 1` have each
 * been filled once more before this memory is overwritten again.
 */
typedef bool (*hw_adc_dma_callback_t)(hw_adc_t *adc, uint16_t *buf,
                                      size_t samples, void *userdata);

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize an ADC handle for a specific GPIO pin.
 * @ingroup ADC
 * @param gpio GPIO handle configured for ADC-capable pin access.
 * @return ADC handle or NULL on failure.
 */
hw_adc_t *hw_adc_init_pin(hw_gpio_t *gpio);

/**
 * @brief Initialize an ADC handle for the internal temperature sensor channel.
 * @ingroup ADC
 * @return ADC handle or NULL on failure.
 *
 * The internal temperature sensor channel is not associated with a GPIO pin.
 */
hw_adc_t *hw_adc_init_temperature(void);

/**
 * @brief Finalize and release an ADC handle.
 * @ingroup ADC
 * @param adc ADC handle.
 */
void hw_adc_deinit(hw_adc_t *adc);

/**
 * @brief Get the number of external, GPIO-mappable ADC channels.
 * @ingroup ADC
 * @return Number of external ADC channels available on the current
 * platform - use hw_adc_gpio_pin()/hw_adc_gpio_channel() to map between
 * these and GPIO pin numbers.
 *
 * Internal-only channels, such as the temperature sensor initialized by
 * hw_adc_init_temperature(), aren't counted here - they have no GPIO
 * mapping and aren't reachable by iterating 0..hw_adc_count()-1.
 */
uint8_t hw_adc_count(void);

/**
 * @brief Get the ADC channel number for a GPIO pin on bank 0.
 * @ingroup ADC
 * @param pin GPIO pin number.
 * @return Channel number, or 0xFF if the pin is not ADC-capable.
 */
uint8_t hw_adc_gpio_channel(uint8_t pin);

/**
 * @brief Get the GPIO pin number for an ADC channel.
 * @ingroup ADC
 * @param channel ADC channel number.
 * @return GPIO pin number, or 0xFF if the channel has no GPIO mapping.
 *
 * Channels that are valid ADC inputs but are not backed by a GPIO pin, such
 * as internal temperature-sensor channels, return 0xFF here.
 */
uint8_t hw_adc_gpio_pin(uint8_t channel);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Read the current value from an ADC channel as a 12-bit value.
 * @ingroup ADC
 * @param adc ADC handle.
 * @param num_samples Number of conversions to average. 0 or 1 takes a
 * single, immediate reading; higher values sample the ADC FIFO that many
 * times and return the mean, which reduces noise at the cost of latency.
 * Systems may clamp this to an implementation-defined maximum to bound
 * how long the call can block.
 * @return Raw value in the 0-4095 range.
 */
uint16_t hw_adc_read_12(hw_adc_t *adc, uint16_t num_samples);

/**
 * @brief Read the current value from an ADC channel as a 16-bit value.
 * @ingroup ADC
 * @param adc ADC handle.
 * @param num_samples Number of conversions to average. See hw_adc_read_12().
 * @return Raw value in the 0-65535 range.
 */
uint16_t hw_adc_read_16(hw_adc_t *adc, uint16_t num_samples);

/**
 * @brief Read the current value from an ADC channel as a voltage.
 * @ingroup ADC
 * @param adc ADC handle.
 * @param num_samples Number of conversions to average. See hw_adc_read_12().
 * @return Voltage value in volts.
 */
float hw_adc_read_voltage(hw_adc_t *adc, uint16_t num_samples);

/**
 * @brief Read the current value from an ADC channel as a temperature.
 * @ingroup ADC
 * @param adc ADC handle configured for temperature sensing.
 * @param num_samples Number of conversions to average. See hw_adc_read_12().
 * @return Temperature value in degrees Celsius.
 */
float hw_adc_read_temperature(hw_adc_t *adc, uint16_t num_samples);

/**
 * @brief Start an asynchronous, DMA-driven read of raw samples.
 * @ingroup ADC
 * @param adc ADC handle.
 * @param buf Buffer to fill with raw samples, sized for `samples *
 * partitions` samples. Must remain valid for as long as `callback` keeps
 * returning `true`.
 * @param samples Number of samples per partition.
 * @param partitions Number of equal-sized partitions to split `buf` into,
 * filled round-robin. `callback` (see hw_adc_dma_callback_t) must return
 * immediately - it should hand a completed partition off asynchronously
 * rather than process it inline - so `partitions` of 2 or more gives
 * whatever actually reads out the data time to do so before that memory
 * is reused, which matters given the ADC's own FIFO is only a handful of
 * samples deep and drops conversions once full.
 * @param freq Desired free-running sample rate in Hz, or 0 for the ADC's
 * maximum rate (back-to-back conversions, roughly 500 kHz on RP2040 and
 * RP2350 at their standard 48 MHz ADC clock).
 * @param callback Called each time a partition finishes filling; its
 * return value decides whether capturing continues. See
 * hw_adc_dma_callback_t.
 * @param userdata User-defined data pointer passed to `callback`.
 * @retval true The transfer was started; `callback` will be invoked after
 * each partition, until it returns `false`.
 * @retval false DMA-driven reads aren't set up/supported on this platform,
 * `partitions` is less than 2, `freq` is outside the ADC's supported rate
 * range (too high, or too low to fit the clock divider), or a transfer is
 * already in progress - on this handle, or on any other, since the ADC's
 * mux/FIFO/clock divider are shared hardware and only one handle can be
 * capturing at a time - `callback` is not invoked.
 *
 * Not available on every system: DMA-driven ADC reads require a real DMA
 * controller wired to the ADC's FIFO (e.g. Pico), so host systems with no
 * ADC hardware at all always return false here.
 */
bool hw_adc_read_dma(hw_adc_t *adc, uint16_t *buf, size_t samples,
                     size_t partitions, uint32_t freq,
                     hw_adc_dma_callback_t callback, void *userdata);

/** @} */
