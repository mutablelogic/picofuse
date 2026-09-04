#include "dma.h"

#include "../../sys/pico/sync.h"
#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/sync.h>
#include <picofuse/sys.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

// The control channel's source: laid out to match dma_channel_hw_t's
// al1_write_addr/al1_transfer_count_trig pair exactly (write_addr
// immediately followed by transfer_count), since the control channel
// copies both fields in one two-word DMA burst. transfer_count is always
// `samples` - only write_addr ever changes - so one reusable instance is
// enough; the control channel's own read_addr is set to point at it once,
// at setup, and never touched again. Each cycle just overwrites
// write_addr in place (a plain memory write, not a DMA register poke)
// with the next partition's address, before the control channel's next
// firing reads it.
typedef struct hw_dma_fifo_entry_t {
  uint32_t write_addr;
  uint32_t transfer_count;
} hw_dma_fifo_entry_t;

struct hw_dma_fifo_t {
  uint8_t data_chan;
  uint8_t ctrl_chan;
  uint8_t *buf; // byte-addressed, sample_bytes gives the real stride; NULL
                // means this pool slot is free - set by _hw_dma_fifo_alloc(),
                // cleared by hw_dma_fifo_deinit()
  size_t sample_bytes; // 1, 2, or 4 - from data_size (1 << data_size)
  size_t samples;
  size_t partitions;
  size_t report_next; // partition index the next IRQ will report as filled
  hw_dma_fifo_entry_t staging;
  hw_dma_fifo_callback_t callback;
  void *userdata;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static hw_dma_fifo_t _hw_dma_fifo_pool[HW_DMA_FIFO_CAPACITY] = {0};

// Guards the one-time DMA_IRQ_0 handler install below against both cores
// racing through hw_dma_fifo_init() for the first time at once: whichever
// core's sys_atomic_inc() returns 1 does the installing, the other sees a
// higher value and skips it - unlike a plain bool, this can't have both
// cores observe "not installed yet" and both install.
static sys_atomic_t _hw_dma_fifo_irq_installed = {0};

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static void _hw_dma_fifo_irq_handler(void);

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Maps picofuse's own hw_dma_fifo_size_t to the SDK's
 * dma_channel_transfer_size_t and the matching byte count, in one place -
 * false for a value outside the enum. */
static inline bool
_hw_dma_fifo_transfer_size(hw_dma_fifo_size_t size,
                           dma_channel_transfer_size_t *sdk_size,
                           size_t *bytes) {
  switch (size) {
  case hw_dma_fifo_uint8:
    *sdk_size = DMA_SIZE_8;
    *bytes = 1;
    return true;
  case hw_dma_fifo_uint16:
    *sdk_size = DMA_SIZE_16;
    *bytes = 2;
    return true;
  case hw_dma_fifo_uint32:
    *sdk_size = DMA_SIZE_32;
    *bytes = 4;
    return true;
  }
  return false;
}

/** @brief Claims a free slot from the static instance pool, marking it
 * used by setting its buf field to the caller's (already validated
 * non-NULL) `buf`, or NULL if every slot is already in use. `buf` must be
 * set here, still under the lock, rather than left for hw_dma_fifo_init()
 * to fill in later once unlocked - otherwise the slot would still read as
 * free (buf == NULL) to the other core for however long hw_dma_fifo_init()
 * takes to get there, and both cores could be handed the same slot. */
static hw_dma_fifo_t *_hw_dma_fifo_alloc(void *buf) {
  _sys_sync_pool_lock();
  for (size_t i = 0; i < HW_DMA_FIFO_CAPACITY; i++) {
    if (_hw_dma_fifo_pool[i].buf == NULL) {
      memset(&_hw_dma_fifo_pool[i], 0, sizeof(_hw_dma_fifo_pool[i]));
      _hw_dma_fifo_pool[i].buf = buf;
      _sys_sync_pool_unlock();
      return &_hw_dma_fifo_pool[i];
    }
  }
  _sys_sync_pool_unlock();
  return NULL;
}

static void _hw_dma_fifo_irq_handler(void) {
  for (size_t i = 0; i < HW_DMA_FIFO_CAPACITY; i++) {
    hw_dma_fifo_t *dma = &_hw_dma_fifo_pool[i];
    if (dma->buf == NULL ||
        !dma_channel_get_irq0_status((uint)dma->data_chan)) {
      continue;
    }
    dma_channel_acknowledge_irq0((uint)dma->data_chan);

    size_t filled = dma->report_next;
    uint8_t *filled_buf = dma->buf + filled * dma->samples * dma->sample_bytes;

    bool keep_going = dma->callback(filled_buf, dma->samples, dma->userdata);
    if (!keep_going) {
      hw_dma_fifo_deinit(dma);
      continue;
    }

    // The control channel has already reprogrammed the data channel to
    // fill "filled + 1" (staged one cycle ago, triggered by the data
    // channel's own completion that caused this IRQ) - prepare the entry
    // one further ahead, "filled + 2", for the control channel's next
    // firing, by overwriting the one staging entry it always reads from.
    //
    // The control channel's own read_addr, write_addr and transfer_count
    // registers also aren't where they started, independent of the
    // above: the 2-word copy it just did auto-incremented read_addr past
    // the end of the staging entry and write_addr past the data
    // channel's al1_transfer_count_trig register (onto the unrelated,
    // non-triggering al2_ctrl/al2_transfer_count that happen to follow
    // it), and its transfer_count decremented to 0 - all three need
    // resetting here, or its next firing would silently read/write the
    // wrong locations without ever retriggering the data channel.
    dma->report_next = (filled + 1) % dma->partitions;
    size_t arm = (filled + 2) % dma->partitions;
    dma->staging.write_addr =
        (uint32_t)(uintptr_t)(dma->buf +
                              arm * dma->samples * dma->sample_bytes);
    dma_channel_set_read_addr((uint)dma->ctrl_chan, &dma->staging, false);
    dma_channel_set_write_addr(
        (uint)dma->ctrl_chan,
        &dma_channel_hw_addr((uint)dma->data_chan)->al1_write_addr, false);
    dma_channel_set_trans_count((uint)dma->ctrl_chan, 2, false);
  }
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS

hw_dma_fifo_t *hw_dma_fifo_init(const volatile void *fifo_addr, uint dreq,
                                hw_dma_fifo_size_t data_size, void *buf,
                                size_t samples, size_t partitions,
                                hw_dma_fifo_callback_t callback,
                                void *userdata) {
  dma_channel_transfer_size_t sdk_data_size;
  size_t sample_bytes;
  if (buf == NULL || samples == 0 || partitions < 2 || callback == NULL ||
      !_hw_dma_fifo_transfer_size(data_size, &sdk_data_size, &sample_bytes)) {
    return NULL;
  }

  hw_dma_fifo_t *dma = _hw_dma_fifo_alloc(buf);
  if (dma == NULL) {
    return NULL;
  }

  // _hw_dma_fifo_alloc() already marked the slot used (dma->buf == buf) so
  // the other core can't also claim it - release it back on every
  // failure path from here on, the same way hw_dma_fifo_deinit() does.
  int data_chan = dma_claim_unused_channel(false);
  if (data_chan < 0) {
    _sys_sync_pool_lock();
    dma->buf = NULL;
    _sys_sync_pool_unlock();
    return NULL;
  }
  int ctrl_chan = dma_claim_unused_channel(false);
  if (ctrl_chan < 0) {
    dma_channel_unclaim((uint)data_chan);
    _sys_sync_pool_lock();
    dma->buf = NULL;
    _sys_sync_pool_unlock();
    return NULL;
  }

  dma->data_chan = (uint8_t)data_chan;
  dma->ctrl_chan = (uint8_t)ctrl_chan;
  dma->sample_bytes = sample_bytes;
  dma->samples = samples;
  dma->partitions = partitions;
  dma->report_next = 0;
  dma->callback = callback;
  dma->userdata = userdata;

  // Seeded for partition 1 (partition 0 is data_chan's own initial
  // config below); transfer_count never changes after this.
  dma->staging.write_addr =
      (uint32_t)(uintptr_t)(dma->buf + samples * sample_bytes);
  dma->staging.transfer_count = (uint32_t)samples;

  if (sys_atomic_inc(&_hw_dma_fifo_irq_installed) == 1) {
    irq_set_exclusive_handler(DMA_IRQ_0, _hw_dma_fifo_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
  }

  // Control channel: on each trigger, copies the {write_addr,
  // transfer_count} staging entry straight into the data channel's
  // al1_write_addr/al1_transfer_count_trig register pair - the second
  // (transfer_count) write is what actually re-arms and starts the data
  // channel, using the address the first write just staged. chain_to is
  // left at its default (itself), which the hardware treats as "no
  // chaining" - the data channel is retriggered by this direct register
  // write, not by the control channel's own completion.
  dma_channel_config ctrl_cfg = dma_channel_get_default_config((uint)ctrl_chan);
  channel_config_set_transfer_data_size(&ctrl_cfg, DMA_SIZE_32);
  channel_config_set_read_increment(&ctrl_cfg, true);
  channel_config_set_write_increment(&ctrl_cfg, true);
  channel_config_set_dreq(&ctrl_cfg, DREQ_FORCE);
  dma_channel_configure(
      (uint)ctrl_chan, &ctrl_cfg,
      &dma_channel_hw_addr((uint)data_chan)->al1_write_addr, // dst
      &dma->staging,                                         // src
      2,                                                     // 2 words
      false);

  // Data channel: transfers `samples` values of `data_size` width from the
  // fixed FIFO address into the current partition, paced by `dreq`.
  // chain_to the control channel, so completing a partition automatically
  // arms the next one with no CPU/interrupt involvement in between - the
  // IRQ handler only needs to run once per partition to report it and
  // stage the entry after next. Configured but not triggered here -
  // hw_dma_fifo_start() does that.
  dma_channel_config data_cfg = dma_channel_get_default_config((uint)data_chan);
  channel_config_set_transfer_data_size(&data_cfg, sdk_data_size);
  channel_config_set_read_increment(&data_cfg, false);
  channel_config_set_write_increment(&data_cfg, true);
  channel_config_set_dreq(&data_cfg, dreq);
  channel_config_set_chain_to(&data_cfg, (uint)ctrl_chan);
  dma_channel_configure((uint)data_chan, &data_cfg,
                        buf,                       // dst: partition 0
                        fifo_addr,                 // src
                        (uint32_t)samples, false); // configure only

  dma_channel_set_irq0_enabled((uint)data_chan, true);

  return dma;
}

void hw_dma_fifo_start(hw_dma_fifo_t *dma) {
  if (dma == NULL) {
    return;
  }
  dma_channel_start((uint)dma->data_chan);
}

void hw_dma_fifo_deinit(hw_dma_fifo_t *dma) {
  if (dma == NULL) {
    return;
  }

  // hw_dma_fifo_callback_t returning false calls back in here from
  // _hw_dma_fifo_irq_handler itself, but a caller can just as well call
  // this from ordinary foreground code (e.g. _hw_adc_read_raw() stopping
  // an in-progress capture) with interrupts still enabled - if a
  // partition-complete IRQ then landed mid-teardown, the handler would
  // re-enter this same function on the same handle while the outer call
  // is still partway through it, aborting/unclaiming channels already
  // aborted/unclaimed. Disabling interrupts for the whole hardware
  // teardown makes the two calls mutually exclusive on this core (the
  // only core that ever runs the handler - see hw_dma_fifo_init()).
  uint32_t irq_state = save_and_disable_interrupts();
  if (dma->buf == NULL) {
    restore_interrupts(irq_state);
    return;
  }

  dma_channel_set_irq0_enabled(dma->data_chan, false);

  // Aborting data_chan alone isn't enough to stop it for good: as long as
  // its chain_to link to ctrl_chan is intact, a completion that lands
  // between the abort's busy-wait checks (or ctrl_chan re-firing from
  // whatever it was already mid-cycle doing) retriggers it right back,
  // and dma_channel_abort()'s busy-wait can spin forever chasing a
  // channel that keeps getting reborn. Breaking data_chan's own chain_to
  // (self-reference is the hardware's documented "no chaining" value)
  // first removes that possibility entirely, regardless of either
  // channel's timing.
  hw_write_masked(&dma_channel_hw_addr(dma->data_chan)->al1_ctrl,
                  (uint32_t)dma->data_chan << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB,
                  DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS);

  dma_channel_abort(dma->ctrl_chan);
  dma_channel_abort(dma->data_chan);
  dma_channel_acknowledge_irq0(dma->data_chan);
  dma_channel_unclaim((uint)dma->data_chan);
  dma_channel_unclaim((uint)dma->ctrl_chan);
  restore_interrupts(irq_state);

  // Locked against the other core concurrently allocating/releasing a
  // pool slot - see _hw_dma_fifo_alloc().
  _sys_sync_pool_lock();
  dma->buf = NULL;
  _sys_sync_pool_unlock();
}
