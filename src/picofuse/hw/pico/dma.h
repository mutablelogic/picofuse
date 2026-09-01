#pragma once
#include <pico/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Private helper shared by hw/pico backends that need continuous,
// round-robin DMA capture from a hardware FIFO (currently just adc.c, but
// generic over sample width so it also suits e.g. a UART/SPI/PIO RX FIFO)
// - not part of the public picofuse/hw API.

#ifndef HW_DMA_FIFO_CAPACITY
#define HW_DMA_FIFO_CAPACITY 4
#endif

typedef struct hw_dma_fifo_t hw_dma_fifo_t;

/**
 * @brief Width of one FIFO sample - picofuse's own equivalent of the SDK's
 * dma_channel_transfer_size_t, kept separate so this header doesn't need
 * to pull in <hardware/dma.h> just for that one enum (dma.c converts).
 */
typedef enum {
  hw_dma_fifo_uint8,
  hw_dma_fifo_uint16,
  hw_dma_fifo_uint32,
} hw_dma_fifo_size_t;

/**
 * @brief Called from the DMA IRQ handler each time one partition of the
 * buffer passed to hw_dma_fifo_init() finishes filling.
 * @param buf Pointer to the partition that just filled - sized `samples *
 * (1 << data_size)` bytes (data_size the hw_dma_fifo_size_t passed to
 * hw_dma_fifo_init()), i.e. `samples` values of whatever width that was.
 * Cast to the matching fixed-width integer type before indexing.
 * @param samples Number of samples in that partition (matches the
 * `samples` passed to hw_dma_fifo_init()).
 * @param userdata User-defined data pointer passed to hw_dma_fifo_init().
 * @retval true Continue: keep capturing into the next partition.
 * @retval false Stop; hw_dma_fifo_deinit() is called for you and no
 * further callbacks fire.
 *
 * Called synchronously from the DMA-complete interrupt, so it must return
 * immediately - hand `buf` off asynchronously to be read out elsewhere
 * rather than processing it here. The round-robin cycle only reaches this
 * same partition again after `(partitions - 1) * samples` more samples.
 */
typedef bool (*hw_dma_fifo_callback_t)(void *buf, size_t samples,
                                       void *userdata);

/**
 * @brief Configure (but don't yet start) continuous, round-robin DMA
 * capture from a hardware FIFO register into a buffer, notifying by
 * callback after each partition fills.
 *
 * Claims two DMA channels and fully configures both - one paced by `dreq`
 * that will repeatedly transfer `samples` values from `fifo_addr` into the
 * current partition, and a second one that reprograms the first channel's
 * destination address and transfer count for the next partition each time
 * the current one completes (chained via hardware, not an interrupt) -
 * this is what makes the capture continuous and round-robin with no
 * CPU intervention between partitions, and works the same way on RP2040
 * and RP2350 (it doesn't rely on RP2350-only self-triggering DMA).
 *
 * Nothing actually moves until hw_dma_fifo_start() is called - this
 * split lets a caller finish any other setup (or synchronize the start of
 * several captures) before committing to it.
 *
 * @param fifo_addr Fixed hardware FIFO register to read samples from.
 * @param dreq Data request line identifying the peripheral that owns
 * `fifo_addr` (e.g. DREQ_ADC). The peripheral pulses this whenever it has
 * a fresh sample sitting in the FIFO; the data channel only reads
 * `fifo_addr` in response to a pulse, which is what paces the whole
 * capture to the peripheral's own sample rate rather than the bus's.
 * Only the data channel is paced this way - the control channel that
 * reprograms it for the next partition runs on DREQ_FORCE (always ready)
 * instead, since it isn't reading from `fifo_addr` at all.
 * @param data_size Width of one sample, matching `fifo_addr`'s peripheral
 * (e.g. hw_dma_fifo_uint16 for the ADC's 12-bit-in-16 FIFO). Determines
 * both the transfer's bus width and how `buf`/`samples` are interpreted:
 * `buf` is byte-addressed, but always treated as an array of `samples *
 * partitions` samples of this width.
 * @param buf Buffer to fill, sized for `samples * partitions * (1 <<
 * data_size)` bytes.
 * @param samples Number of samples per partition.
 * @param partitions Number of equal-sized partitions to cycle through.
 * Must be at least 2 (so one can be handed off while another fills).
 * @param callback Called after each partition fills - required, since it's
 * also the only way to ever stop the capture (by returning false).
 * @param userdata User-defined data pointer passed to `callback`.
 * @return Handle to the configured (not yet running) capture, or NULL if
 * `partitions < 2`, `callback` is NULL, the static instance pool
 * (HW_DMA_FIFO_CAPACITY) is full, or no DMA channels are available.
 */
hw_dma_fifo_t *hw_dma_fifo_init(const volatile void *fifo_addr, uint dreq,
                                hw_dma_fifo_size_t data_size,
                                void *buf, size_t samples, size_t partitions,
                                hw_dma_fifo_callback_t callback,
                                void *userdata);

/**
 * @brief Start a capture configured by hw_dma_fifo_init().
 * @param dma Handle returned by hw_dma_fifo_init().
 *
 * The caller is responsible for actually starting the peripheral that
 * feeds `fifo_addr` (e.g. adc_run(true)) after this returns - the DMA
 * channel is armed and waiting on `dreq`, but won't move anything until
 * the peripheral starts asserting it.
 */
void hw_dma_fifo_start(hw_dma_fifo_t *dma);

/**
 * @brief Stop a capture and release its DMA channels and pool slot.
 * @param dma Handle returned by hw_dma_fifo_init(), or NULL (a safe
 * no-op). Safe to call whether or not hw_dma_fifo_start() was ever
 * called on it.
 *
 * Safe to call from within the hw_dma_fifo_callback_t itself (e.g.
 * instead of just returning false) or from any other context.
 */
void hw_dma_fifo_deinit(hw_dma_fifo_t *dma);
