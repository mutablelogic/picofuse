#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

#define DMA_COUNT 64
#define DMA_PARTITIONS 4
#define DMA_STOP_AFTER (DMA_PARTITIONS * 3) // a couple of full round trips

static uint16_t dma_buf[DMA_COUNT * DMA_PARTITIONS];
static volatile size_t dma_calls = 0;
static volatile size_t dma_expected_partition = 0;
static volatile bool dma_order_ok = true;
static volatile bool dma_range_ok = true;

static bool dma_callback(hw_adc_t *adc, uint16_t *buf, size_t samples,
                         void *userdata) {
  (void)adc;
  (void)userdata;

  size_t index = (size_t)(buf - dma_buf) / DMA_COUNT;
  if (index != dma_expected_partition || samples != DMA_COUNT) {
    dma_order_ok = false;
  }
  for (size_t i = 0; i < samples; i++) {
    if (buf[i] > 4095) {
      dma_range_ok = false;
    }
  }

  dma_calls++;
  dma_expected_partition = (dma_expected_partition + 1) % DMA_PARTITIONS;
  return dma_calls < DMA_STOP_AFTER;
}

static volatile size_t dma_calls2 = 0;

static bool dma_callback_never_stop(hw_adc_t *adc, uint16_t *buf,
                                    size_t samples, void *userdata) {
  (void)adc;
  (void)buf;
  (void)samples;
  (void)userdata;
  dma_calls2++;
  return true;
}

// hw_adc_read_dma(): continuous, round-robin DMA capture. Verifies the
// callback fires for each partition in the correct cyclic order with the
// right buffer pointer/count (the part most likely to have a subtle bug -
// see src/picofuse/hw/pico/dma.c), every sample lands in the valid 12-bit
// range, returning false from the callback stops further callbacks, and a
// single-shot read (hw_adc_read_12() and friends) auto-stops a capture
// already running on the same handle instead of fighting it for the ADC
// (see _hw_adc_read_raw() in src/picofuse/hw/pico/adc.c). If the platform
// has no ADC backend at all (host stubs), hw_adc_count() reports 0 and
// there's nothing to exercise.
test_main_hw(0) {
  uint8_t count = hw_adc_count();
  sys_debugf("hw_004", "hw_adc_count() = %u", count);
  if (count == 0) {
    return;
  }

  uint8_t pin = hw_adc_gpio_pin(0);
  test_assert(pin != UINT8_MAX);
  hw_gpio_t *gpio = hw_gpio_init(0, pin, hw_gpio_none);
  test_assert(gpio != NULL);
  hw_adc_t *adc = hw_adc_init_pin(gpio);
  test_assert(adc != NULL);

  // A single-shot read must auto-stop an in-progress capture, checked
  // first (before anything else has ever used DMA on this handle) so it
  // isn't muddied by whatever hw_adc_read_dma()'s own self-stop path
  // leaves behind.
  test_assert(hw_adc_read_dma(adc, dma_buf, DMA_COUNT, DMA_PARTITIONS, 0,
                              dma_callback_never_stop, NULL));
  for (int i = 0; i < 100 && dma_calls2 == 0; i++) {
    sys_sleep_ms(10);
  }
  test_assert(dma_calls2 > 0); // confirm it actually started

  (void)hw_adc_read_12(adc, 1);

  size_t dma2_calls_after_read = dma_calls2;
  sys_sleep_ms(200);
  test_assert(dma_calls2 == dma2_calls_after_read);

  // Round-robin order/range, and self-stop via a false return.
  test_assert(hw_adc_read_dma(adc, dma_buf, DMA_COUNT, DMA_PARTITIONS, 0,
                              dma_callback, NULL));

  // Wait for at least DMA_STOP_AFTER callbacks, or time out.
  for (int i = 0; i < 200 && dma_calls < DMA_STOP_AFTER; i++) {
    sys_sleep_ms(10);
  }
  sys_debugf("hw_004", "dma_calls=%zu order_ok=%d range_ok=%d", dma_calls,
             dma_order_ok, dma_range_ok);
  test_assert(dma_calls == DMA_STOP_AFTER);
  test_assert(dma_order_ok);
  test_assert(dma_range_ok);

  // Confirm it actually stopped: no more callbacks after a further wait.
  size_t calls_at_stop = dma_calls;
  sys_sleep_ms(200);
  test_assert(dma_calls == calls_at_stop);

  hw_adc_deinit(adc);
  hw_gpio_deinit(gpio);
}
