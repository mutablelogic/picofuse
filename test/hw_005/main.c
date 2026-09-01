#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

#define DMA_COUNT 32
#define DMA_PARTITIONS 4
#define ITERATIONS 20
#define RUN_MS 20

// A slower-than-max sample rate for both channels, not the default 0
// (back-to-back, clkdiv 0). At clkdiv 0 the ADC's READY bit may only pulse
// true for a single cycle between conversions, so adc_fifo_drain()'s
// "wait for READY" busy-wait (in _hw_adc_dma_stop()/_hw_adc_read_raw())
// can plausibly miss it indefinitely. A real gap between conversions
// avoids that without weakening what this test is actually after (see
// worker_core1()).
#define DMA_FREQ_HZ 100000u

static uint16_t dma_buf_a[DMA_COUNT * DMA_PARTITIONS];
static uint16_t dma_buf_b[DMA_COUNT * DMA_PARTITIONS];

static volatile size_t dma_calls_a = 0;
static volatile size_t dma_calls_b = 0;

static bool dma_callback_a(hw_adc_t *adc, uint16_t *buf, size_t samples,
                           void *userdata) {
  (void)adc;
  (void)buf;
  (void)samples;
  (void)userdata;
  dma_calls_a++;
  return true; // never self-stops - the driving loop force-stops it instead
}

static bool dma_callback_b(hw_adc_t *adc, uint16_t *buf, size_t samples,
                           void *userdata) {
  (void)adc;
  (void)buf;
  (void)samples;
  (void)userdata;
  dma_calls_b++;
  return true;
}

static hw_adc_t *_adc_b;
static sys_waitgroup_t *_wg;

// Runs on core1, concurrently with the main loop below on core0. Each side
// repeatedly tries to start a never-self-stopping capture on its own
// channel, lets it run briefly if it got one, then force-stops it with a
// single-shot read on the same handle - _hw_adc_read_raw() always tears
// down an in-progress capture on its own handle (a no-op if there wasn't
// one), so this doesn't depend on hw_adc_read_dma() having succeeded.
//
// hw_adc_read_dma() now enforces that only one handle can be capturing at
// a time across the whole ADC (see _hw_adc_dma_claim() in adc.c), so most
// attempts from whichever side isn't currently holding it are expected to
// return false here - that's the point, not a failure (see the explicit,
// deterministic check of that guard in test_main_hw() below). What this
// loop is after is hammering hw_dma_fifo_t's shared pool allocation,
// DMA_IRQ_0 install-once guard, and teardown (see
// src/picofuse/hw/pico/dma.c) with real cross-core contention on top of
// the claim/release bookkeeping in adc.c - if any of that weren't safe
// across cores, this would be expected to eventually panic, hang, or trip
// an SDK assert (a double dma_channel_unclaim(), a corrupted pool slot,
// DMA_IRQ_0 serviced from neither or both cores).
static void worker_core1(void *arg) {
  (void)arg;
  for (int i = 0; i < ITERATIONS; i++) {
    (void)hw_adc_read_dma(_adc_b, dma_buf_b, DMA_COUNT, DMA_PARTITIONS,
                          DMA_FREQ_HZ, dma_callback_b, NULL);
    sys_sleep_ms(RUN_MS);
    (void)hw_adc_read_12(_adc_b, 1);
  }
  test_assert(sys_waitgroup_done(_wg));
}

// If the platform has no ADC backend at all (host stubs) or no external
// channels, there's nothing to exercise - the internal temperature
// channel used as the second handle below is always present otherwise.
test_main_hw(0) {
  uint8_t count = hw_adc_count();
  sys_debugf("hw_005", "hw_adc_count() = %u", count);
  if (count < 1) {
    return;
  }

  uint8_t pin = hw_adc_gpio_pin(0);
  test_assert(pin != UINT8_MAX);
  hw_gpio_t *gpio = hw_gpio_init(0, pin, hw_gpio_none);
  test_assert(gpio != NULL);
  hw_adc_t *adc_a = hw_adc_init_pin(gpio);
  test_assert(adc_a != NULL);

  _adc_b = hw_adc_init_temperature();
  test_assert(_adc_b != NULL);

  // Deterministic, single-core check of the exclusivity guard itself
  // before introducing any cross-core timing: a second hw_adc_read_dma()
  // on a *different* handle must be rejected outright while the first is
  // still active, rather than being allowed to silently race it for the
  // same ADC mux/FIFO/clkdiv/run bit.
  test_assert(hw_adc_read_dma(adc_a, dma_buf_a, DMA_COUNT, DMA_PARTITIONS,
                              DMA_FREQ_HZ, dma_callback_a, NULL));
  test_assert(!hw_adc_read_dma(_adc_b, dma_buf_b, DMA_COUNT, DMA_PARTITIONS,
                               DMA_FREQ_HZ, dma_callback_b, NULL));
  (void)hw_adc_read_12(adc_a, 1); // stop adc_a's capture, releasing the claim
  test_assert(hw_adc_read_dma(_adc_b, dma_buf_b, DMA_COUNT, DMA_PARTITIONS,
                              DMA_FREQ_HZ, dma_callback_b, NULL));
  (void)hw_adc_read_12(_adc_b, 1); // stop adc_b's capture in turn

  _wg = sys_waitgroup_init();
  test_assert(_wg != NULL);
  test_assert(sys_waitgroup_add(_wg, 1));
  test_assert(sys_thread_create_on_core(worker_core1, NULL, 1));

  for (int i = 0; i < ITERATIONS; i++) {
    (void)hw_adc_read_dma(adc_a, dma_buf_a, DMA_COUNT, DMA_PARTITIONS,
                          DMA_FREQ_HZ, dma_callback_a, NULL);
    sys_sleep_ms(RUN_MS);
    (void)hw_adc_read_12(adc_a, 1);
  }

  sys_waitgroup_wait(_wg);
  sys_waitgroup_deinit(_wg);

  // Weak sanity check only - see the comment on worker_core1() for why
  // neither side's count can be asserted individually.
  sys_debugf("hw_005", "dma_calls_a=%zu dma_calls_b=%zu", dma_calls_a,
             dma_calls_b);
  test_assert(dma_calls_a + dma_calls_b > 0);

  hw_adc_deinit(adc_a);
  hw_gpio_deinit(gpio);
  hw_adc_deinit(_adc_b);
}
