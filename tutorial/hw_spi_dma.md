# SPI DMA and Async Transfers

Design notes for two changes to `hw_deviceio_t`'s SPI backends (see `deviceio.h`
and `src/picofuse/hw/{pico,linux}/spi.c`):

* Make the existing, synchronous `hw_deviceio_xfr()` faster on the Pico by
  driving it with DMA instead of `spi_write_blocking()`/`spi_read_blocking()`.
* Add a genuinely asynchronous transfer method, so a caller (a display
  driver pumping a full frame over SPI, say) can carry on with other work
  while the bytes move in the background.

Both stay behind `hw_deviceio_ops_t`, same as every other backend
operation, so a device driver written against `hw_deviceio_t` doesn't need
to know or care whether the bus underneath is DMA-driven.

## Improving `hw_deviceio_xfr()`

### Pico

`_hw_spi_ops_xfr()` (`src/picofuse/hw/pico/spi.c`) currently calls
`spi_write_blocking()`/`spi_read_blocking()` directly, which parks the CPU
core one byte at a time for the whole transfer. For anything beyond a
handful of bytes, DMA gets the same bytes onto the wire without the CPU
babysitting every one of them.

The RP2040 has no single "SPI DMA" mode - it needs a channel pair: one
feeding the TX FIFO from `data`, one draining the RX FIFO. That second
channel is required even on a pure write, since the SPI hardware still
shifts a byte in for every byte it shifts out, and an undrained RX FIFO
eventually stalls the bus.

```c
size_t _hw_spi_ops_xfr(hw_deviceio_t *device, void *data, size_t tx,
                       size_t rx, uint32_t timeout_ms) {
  (void)timeout_ms; // no timeout path through the DMA engine either
  hw_spi_ctx_t *ctx = _hw_deviceio_context(device);
  size_t total = tx + rx;

  // Below some threshold the claim/configure/wait overhead costs more
  // than spi_*_blocking() would - and if both DMA channels are already
  // claimed elsewhere, blocking is the only option anyway.
  int tx_chan = total >= HW_SPI_DMA_MIN_BYTES ? dma_claim_unused_channel(false) : -1;
  int rx_chan = tx_chan >= 0 ? dma_claim_unused_channel(false) : -1;
  if (rx_chan < 0) {
    if (tx_chan >= 0) {
      dma_channel_unclaim(tx_chan);
    }
    return _hw_spi_xfr_blocking(ctx, data, tx, rx); // existing code path
  }

  return _hw_spi_xfr_dma(ctx, data, tx, rx, tx_chan, rx_chan);
}
```

`_hw_spi_xfr_dma()` configures and runs the TX phase then the RX phase in
turn (SPI is full-duplex hardware, but `hw_deviceio_xfr()`'s own contract
is write-then-read, not simultaneous), waiting on
`dma_channel_wait_for_finish_blocking()` for each - still synchronous from
the caller's point of view, just off the CPU for the actual byte-shifting.
One per-context detail matters here: the RX-discard sink used during the
TX phase (there's nothing to write incoming dummy bytes into) has to be a
field on `hw_spi_ctx_t`, not a single `static uint8_t` shared by the whole
file - two devices transferring at once would otherwise have their DMA
engines racing to write through the same address.

### Linux

Nothing to add here - `_hw_spi_ops_xfr()` (`src/picofuse/hw/linux/spi.c`)
already goes through `SPI_IOC_MESSAGE`, and the kernel's own spidev driver
already upgrades that to DMA on its end when the controller and transfer
size warrant it. This is a Pico-only change.

## A new asynchronous transfer method

`hw_deviceio_xfr()`, DMA-backed or not, still blocks the caller until the
transfer finishes. The new method starts a transfer and returns
immediately, with the caller finding out about completion either by
polling, by blocking later on its own schedule, or via a callback:

```c
/** Fires once an async transfer completes - see the callback-context
 * rules below before doing anything nontrivial in one. */
typedef void (*hw_deviceio_cb_t)(hw_deviceio_t *device, void *userdata);

/** Starts an async transfer - same tx/rx contract as hw_deviceio_xfr().
 * `callback` may be NULL if the caller would rather poll hw_deviceio_xfr_busy()
 * or just call hw_deviceio_xfr_wait() later. Returns false (transfer not
 * started - caller should fall back to hw_deviceio_xfr()) if the backend
 * has no async support, or a transfer on this handle is already in flight. */
bool hw_deviceio_xfr_async(hw_deviceio_t *device, void *data, size_t tx,
                           size_t rx, hw_deviceio_cb_t callback,
                           void *userdata);

/** Blocks until the in-flight async transfer finishes (or `timeout_ms`
 * elapses - 0 waits forever). Returns bytes transferred, or 0 on
 * timeout/failure/no transfer in flight. Safe to call whether or not a
 * callback was also registered. */
size_t hw_deviceio_xfr_wait(hw_deviceio_t *device, uint32_t timeout_ms);

/** Non-blocking check - true if an async transfer on this handle hasn't
 * completed yet. */
bool hw_deviceio_xfr_busy(hw_deviceio_t *device);
```

One method with an optional callback, rather than a separate `_cb()`
sibling of `_async()`/`_wait()` - a caller that wants a callback passes
one, a caller that'd rather block later just calls `hw_deviceio_xfr_wait()`
on its own schedule, and both are driven by the same underlying
in-flight-transfer state instead of two parallel code paths in every
backend.

`hw_deviceio_ops_t` grows to match, all three optional together - a
backend that leaves `xfr_async` NULL supports only blocking transfers, and
callers fall back to `hw_deviceio_xfr()`:

```c
typedef struct hw_deviceio_ops_t {
  size_t (*xfr)(hw_deviceio_t *device, void *data, size_t tx, size_t rx,
               uint32_t timeout_ms);
  size_t (*read_reg)(hw_deviceio_t *device, uint8_t reg, void *data,
                     size_t len, uint32_t timeout_ms);
  size_t (*write_reg)(hw_deviceio_t *device, uint8_t reg, const void *data,
                      size_t len, uint32_t timeout_ms);
  void (*deinit)(hw_deviceio_t *device);

  // New - async transfer support, optional.
  bool (*xfr_async)(hw_deviceio_t *device, void *data, size_t tx, size_t rx,
                    hw_deviceio_cb_t callback, void *userdata);
  size_t (*xfr_wait)(hw_deviceio_t *device, uint32_t timeout_ms);
  bool (*xfr_busy)(hw_deviceio_t *device);
} hw_deviceio_ops_t;
```

### Pico: DMA IRQ-driven

`xfr_async` claims the same TX/RX channel pair as the DMA-backed
`hw_deviceio_xfr()` above, but starts them with `dma_channel_start()`
instead of waiting on them, and only enables the completion IRQ on the RX
channel - RX always finishes last in a write-then-read pair, so its IRQ
firing means the whole transfer is done.

The channel pair, the registered callback, and a busy flag all need to
live in `hw_spi_ctx_t` (`_hw_deviceio_context()`'s scratch buffer) rather
than as new fields bolted onto `hw_deviceio_t` itself - that struct is
private to `deviceio.c`, and every backend already keeps its own
per-device state the same way.

The interrupt handler needs to get from "channel N finished" back to the
owning `hw_deviceio_t *` - a fixed-size lookup table indexed by DMA
channel number does that without any heap involvement, sized off the SDK's
own `NUM_DMA_CHANNELS` rather than a hardcoded `12` (RP2350 has 16):

```c
static hw_deviceio_t *_hw_spi_dma_owner[NUM_DMA_CHANNELS] = {0};

static void _hw_spi_dma_irq_handler(void) {
  uint32_t status = dma_hw->ints0;
  for (uint chan = 0; chan < NUM_DMA_CHANNELS; chan++) {
    if (!(status & (1u << chan))) {
      continue;
    }
    dma_hw->ints0 = 1u << chan; // Clear first - the callback may take a while.

    hw_deviceio_t *device = _hw_spi_dma_owner[chan];
    if (device == NULL) {
      continue;
    }
    _hw_spi_dma_owner[chan] = NULL;

    hw_spi_ctx_t *ctx = _hw_deviceio_context(device);
    critical_section_enter_blocking(&ctx->async_lock);
    int tx_chan = ctx->tx_chan, rx_chan = ctx->rx_chan;
    hw_deviceio_cb_t callback = ctx->callback;
    void *userdata = ctx->callback_userdata;
    ctx->tx_chan = ctx->rx_chan = -1;
    ctx->callback = NULL;
    critical_section_exit(&ctx->async_lock);

    _hw_spi_set_cs(ctx, false);
    dma_channel_unclaim(tx_chan);
    dma_channel_unclaim(rx_chan);

    if (callback != NULL) {
      callback(device, userdata);
    }
  }
}
```

That `critical_section_t` (not `mutex_t`) matters: the Pico SDK's `mutex_t`,
already used elsewhere in `hw_spi_ctx_t` to serialize blocking transfers,
isn't safe to take from interrupt context, and this state is now shared
between the ISR and whichever core calls `hw_deviceio_xfr_wait()`/
`hw_deviceio_xfr_busy()`. `xfr_wait()` itself spins on `ctx->tx_chan >= 0`
(checked under the same critical section) with a `__wfi()`/short sleep
between checks - `__wfi()` parks the core until the next interrupt rather
than burning cycles, which matters here since the ISR that clears this
flag is exactly the kind of interrupt `__wfi()` wakes on.

For a display redrawing continuously (video, a game loop) rather than
occasionally, claiming and unclaiming the DMA pair every single frame is
avoidable overhead worth coming back to once this design is otherwise
working: claim `tx_chan`/`rx_chan` once, in the backend's own init, and
have `xfr_async` skip straight to `dma_channel_configure()` on the
already-claimed channels, never calling `dma_channel_unclaim()` in the
ISR at all. The trade-off is that those two channels are then permanently
unavailable to anything else for as long as the device handle lives -
fine for a display that owns a dedicated SPI bus, less fine if other
devices on the same board are competing for the pool. Worth making opt-in
per device rather than the default.

### Linux: worker thread

Unlike the Pico, spidev's `ioctl(SPI_IOC_MESSAGE)` has no async completion
path - the call itself blocks until the kernel driver is done. POSIX AIO
(`aio_write()`) exists, but its behavior against character devices is
driver-dependent and not something to rely on without testing against the
actual target hardware, and it has no answer for `hw_deviceio_xfr()`'s
write-then-read shape (`aio_write`/`aio_read` are single-buffer, single-
direction). The safer, if heavier, option is a small worker thread per
device that does the existing blocking `_hw_spi_ops_xfr()` call and then
either clears the busy flag (for `_wait()`/`_busy()` callers) or invokes
the callback itself - which also sidesteps the IRQ-context restrictions
the Pico path has to work around, since a plain thread can safely call
anything. This needs prototyping against real hardware before committing
to either approach.

## Teardown while a transfer is in flight

`hw_deviceio_deinit()` has to account for an async transfer still
running - the same shape of problem `pix_display_deinit()` had with
`pix_poll()` (see `src/picofuse/pix/display.c`): waiting for "not busy" and then acting
on it are two different moments unless they're the same locked/guarded
step, otherwise a transfer could start (or the ISR could still be mid-
callback) in the gap between the check and the teardown. `hw_deviceio_deinit()`
should claim "no new async transfer can start" first, then wait for any
already in flight to finish, before calling `ops->deinit()`.

## Byte-swap via the DMA sniffer

Some displays (the ILI9341 included) expect each RGB565 pixel big-endian
on the wire, while a `pix_bitmap_t` stores it little-endian in memory -
today that means a CPU pass over the whole framebuffer to swap every pixel
before it's handed to `hw_deviceio_xfr()`. The RP2040's DMA sniffer can do
that swap in hardware, during the transfer itself, at the cost of moving
16-bit words instead of bytes:

```c
channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_16);
dma_sniffer_enable(tx_chan, DMA_SNIFF_CTRL_FUNC_VALUE_B_SWAP, true);
dma_channel_configure(tx_chan, &tx_config, &spi_get_hw(spi)->dr, buf,
                      tx / 2, true); // word count, not byte count
```

One hardware constraint makes this narrower than it looks: the RP2040 has
exactly **one** sniffer block for the whole chip, not one per DMA channel.
`dma_sniffer_enable()` binds the single shared block to whichever channel
it's called with. With up to `HW_DEVICEIO_CAPACITY` devices
potentially DMA-transferring at once, at most one of them can have the
sniffer enabled at any given moment; a second device requesting it while
the first is still in flight has to either wait or fall back to the CPU
byte-swap it was trying to avoid. If this is worth having, it needs its
own global claim (a single flag/lock, not per-channel), and the completion
handler must call `dma_sniffer_disable()` before releasing that claim -
it's a good fit for `xfr_async`'s callback path (already handling
per-transfer cleanup), less so for the plain blocking `xfr()` path where
nothing currently tracks "is the sniffer free."

Worth prototyping as an ILI9341-specific opt-in rather than a default for
every transfer - it only helps 16-bit-aligned whole-pixel writes, not the
single-byte command/register writes `hw_deviceio_write_reg()` also uses
this same channel pair for.

## Pixel format conversion (RGB444/RGB888 → RGB565)

A related idea worth being careful about: using the RP2040's SIO
interpolators (`interp0`/`interp1`) to unpack a smaller in-memory pixel
format (RGB444, RGB888) into RGB565 on the way out, so the frame buffer
itself can be smaller than the wire format. The interpolator isn't
actually wired into the DMA data path, though - it's a per-core,
memory-mapped ALU the CPU pokes explicitly (write `accum`/`base`, read
back `pop`/`peek`), not a block DMA can chain a transfer *through*.
`channel_config_set_chain_to()` only restarts one channel once another's
whole transfer count is exhausted; there's no per-word hardware hook that
routes each value through the interpolator automatically.

So this still means a CPU loop over the source buffer, writing converted
RGB565 pixels into a scratch buffer, which is what actually gets handed to
`hw_deviceio_xfr()`/`hw_deviceio_xfr_async()` afterwards - same DMA path
as everything else here. The interpolator's real benefit is making that
per-pixel unpack loop cheap (its shift/mask lane runs in a cycle or two,
versus several plain ALU ops for an RGB444/888 unpack), not eliminating
the CPU pass entirely. Whether that's worth the RAM it saves versus just
keeping frame buffers in RGB565 to begin with is a question for whichever
display actually needs the smaller format.

On Linux there's no interpolator or sniffer equivalent either way, so any
format conversion - byte-swap included - is a plain software loop before
handing the buffer to `_hw_spi_ops_xfr()`/the async worker thread:

```c
static void _swap16_inplace(uint16_t *pixels, size_t count) {
  for (size_t i = 0; i < count; i++) {
    pixels[i] = __builtin_bswap16(pixels[i]);
  }
}
```

That only works in place for a same-size transform like a byte-swap - an
actual format change (RGB888 → RGB565) shrinks the data, so it needs a
separate destination buffer rather than mutating `data` under the caller.

## Usage sketch: double-buffered frame streaming

The case all of the above is really for: a display redrawing continuously
rather than once. The CPU renders into whichever buffer isn't currently
being streamed out, waits only if it gets ahead of the hardware, then
swaps and fires the next transfer:

```c
static uint16_t buffer_a[240 * 320];
static uint16_t buffer_b[240 * 320];
static uint16_t *render_target = buffer_a;
static uint16_t *dma_source = buffer_b;
static volatile bool xfr_busy = false;

static void _on_xfr_complete(hw_deviceio_t *device, void *userdata) {
  (void)device;
  (void)userdata;
  xfr_busy = false;
}

void update_screen_tick(hw_deviceio_t *device, dev_ili9341_t *ili9341) {
  // The CPU is free to draw here regardless of whether the previous
  // frame's transfer has finished yet.
  render_game_scene(render_target);

  // Only wait if we've genuinely caught up with the hardware - __wfi()
  // parks the core rather than spinning until the completion IRQ wakes it.
  while (xfr_busy) {
    __wfi();
  }

  uint16_t *tmp = render_target;
  render_target = dma_source;
  dma_source = tmp;

  _ili9341_set_window(ili9341, 0, 0, 240, 320);
  hw_gpio_set(ili9341->dc_pin, true);

  xfr_busy = true;
  if (!hw_deviceio_xfr_async(device, dma_source, 153600, 0, _on_xfr_complete, NULL)) {
    hw_deviceio_xfr(device, dma_source, 153600, 0, 100); // no async support - block instead
    xfr_busy = false;
  }
}
```

## Callback context rules

A registered callback runs inside a hardware interrupt on the Pico, or
(with the worker-thread design above) a separate thread on Linux -
either way, keep it to setting a flag or similar. No `printf()`,
`malloc()`, or anything that can block - on the Pico especially, a stalled
IRQ handler stalls everything else waiting on interrupts too.
