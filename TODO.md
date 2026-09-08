# TODO

## Roadmap

Larger modules/features not yet started - no design work done on any of
these yet, just the list of what's next:

- **Power management** - battery status, power supply enumeration,
  setting power state, and wakeups by source.
- **Rename `hw/memory.h` to `hw_stats`** - broaden its scope beyond just
  memory to also cover network and process load.
- **Filesystem module** - LittleFS, wrapping the existing code (already
  written outside picofuse, needs porting in). Comes after SD card
  support below - LittleFS needs a working `hw/block.h` backend to sit
  on, and on-chip flash alone (already done) isn't the target for it.
  See the flash core-1/core-0 marshaling section below, which exists
  specifically because of this dependency.
- **Networking protocols** - HTTP client/server, MQTT client, DNS
  client/server, DHCP server.
- **Displays** - e-ink, TFT, Linux framebuffer, SDL (host builds). Design
  intent so far: a `pix_t` registry, same shape as `hid_t` (`hid_init()`/
  `hid_register_*()`/`hid_deregister()`/`hid_poll()`) - `pix_register_
  display()`/`pix_deregister_display()` attach/detach a `pix_display_t`
  (not yet designed beyond the earlier lock/unlock/framebuffer-pixmap
  sketch), and `pix_poll()` gets called regularly the same way
  `hid_poll()`/`hw_poll()` already are (from the app run loop). Each
  registered display only actually pushes pixels out to the real
  hardware when its framebuffer has changed since the last poll, not
  unconditionally every call, and that push is itself rate-limited to a
  regular framerate rather than firing on every single change - avoids
  hammering a slow SPI/e-ink bus with redundant or excessive updates.

  Two things to watch, found while tracing `sys_runloop_run()`'s actual
  loop (`sys/event/runloop.c`): `poll_fn` (which `pix_poll()` would be
  folded into, alongside `hw_poll()`/`hid_poll()`) runs on *every* loop
  iteration, not on some throttled schedule - so being "one more poll in
  the chain" is cheap as long as `pix_poll()` does its own due-check
  first and bails immediately when not yet time to push a frame. But the
  loop's idle re-check is fixed at `_SYS_RUNLOOP_POLL_INTERVAL_MS`
  (50ms), so with no other event traffic keeping it awake more often,
  `pix_poll()` can only ever be invoked that often - a ~20fps ceiling
  regardless of a display's actual target framerate. Fix: `pix_t` should
  register its own repeating `hid_register_timer()` at the target frame
  interval the moment a display is registered, the same way
  `examples/presto`'s own animation drives its cadence, rather than
  leaning on ambient traffic. Separately, `poll_fn` only ever runs on
  worker 0, so a display's actual hardware push (a big SPI transfer to a
  TFT, say) is serialized with `hw_poll()`/`hid_poll()` and any
  `event_fn()` that happens to land on worker 0 too - fine most of the
  time, but worth remembering if a push is ever slow.
- **Pixel/graphics library, on top of the display work above** - roughly
  in this order:
  1. `pix_font_t` - both vector and pixel (bitmap) fonts.
  2. Pixel-based line and text drawing primitives, onto a
     `pix_bitmap_t`/display's own framebuffer.
  3. JPEG decoding, into a `pix_bitmap_t`.
  4. Vector graphics (paths/shapes generally, not just font glyphs).
  5. UI widgets and layout, on top of all of the above - see the earlier
     LVGL discussion; this is roughly aiming at LVGL's own scope, as a
     picofuse-native alternative to actually integrating LVGL itself.
- **Bluetooth** - BLE and classic.
- **CI/CD** - build and publish picofuse distributions for Linux and
  Darwin.
- **SD card and other mass storage** - another `hw/block.h` backend,
  alongside on-chip flash. Comes before the filesystem item above, not
  after - LittleFS is going on top of this, not on-chip flash.
- **USB mass storage (Pico as host)** - a `hw/block.h` backend over a USB
  flash drive, cheaper than SD despite the similar shape: TinyUSB
  (already vendored) ships a complete host-side Mass Storage Class driver
  (`third_party/pico-sdk/lib/tinyusb/src/class/msc/msc_host.c` - Bulk-Only
  Transport + SCSI READ10/WRITE10/etc, with a working example at
  `examples/host/msc_file_explorer`), so there's no protocol to write
  from scratch the way SD-over-SPI needs. Work is: enable `CFG_TUH_MSC`
  in `tusb_config.h` (currently disabled along with every other class
  driver, deliberately, per its own doc) and make sure `msc_host.c` is
  compiled into the `tinyusb_host` target; then a thin `hw_block_usb_
  init()` adapter wrapping `tuh_msc_read10()`/`_write10()`/
  `_get_maxlun()` in the same `hw_block_ops_t` shape as `flash.c`.
  Attach detection needs nothing new - `hw/usb.h`'s existing interface-
  enumeration callback already fires with mass-storage class/subclass/
  protocol (0x08/0x06/0x50) when a drive is plugged in. TinyUSB's host
  API is callback-driven, so the adapter needs a "block the caller until
  the async callback fires" wrapper - worth building once and sharing
  with the SD backend, which will need the same thing.
- **Objective-C API** - bindings on top of picofuse, organized as
  separate frameworks the way Apple's own are: Foundation, Application,
  Network, Filesystem.
- **WASM runtime** - not yet scoped further.
- **OTA firmware updates** - write+verify a new firmware image and
  reboot into it, on top of the existing on-chip flash block work.
  Source isn't just network - also from mass storage (SD/USB, once
  those land above), for an update pushed via a card/drive rather than
  downloaded.
- **TLS** - for the HTTP/MQTT clients above, which need HTTPS/TLS in
  practice; mbedtls is already vendored for the SDK build
  (`src/runtime/pico/mbedtls.c`), just not wired up as a general-purpose
  TLS API yet.

## Transparent core-1 -> core-0 flash write/erase marshaling

### Context

`hw_block_flash_init()`'s erase/write callbacks
(`src/picofuse/hw/pico/flash.c`) currently hard-require core 0:
`_sys_pico_flash_pause_request()` (`src/picofuse/sys/pico/flash_pause.c`)
starts with `if (get_core_num() != 0u) return false;`, so calling
`hw_block_erase()`/`hw_block_write()` from an event handler that happens
to run on worker 1 (core 1) fails outright, every time - not a race, a
guaranteed failure. Reads have no such restriction (a plain `memcpy()`
from the XIP-mapped address under the shared `critical_section_t`), so
this is specifically a write/erase problem.

This matters because the upcoming filesystem module is meant to sit on
top of this block backend, and picofuse's own event system deliberately
doesn't let a handler know or control which core it runs on - and that's
staying that way (less mental load for every future caller) rather than
pushing "make sure you write from core 0" onto fs code or its callers.
So the fix belongs entirely inside the flash backend: a write/erase
issued from core 1 should transparently execute on core 0 and block
until done, invisibly to the caller.

### Design

#### 1. Split erase/write into "do it" vs "dispatch it"

Factor the current body of `_hw_flash_block_erase_cb()`/
`_hw_flash_block_write_cb()` (validate index, `_sys_pico_flash_pause_
request()`, `save_and_disable_interrupts()`, `flash_range_erase()`/
`_program()`, `restore_interrupts()`, `_sys_pico_flash_pause_release()`)
into two new core-0-only helpers, e.g. `_hw_flash_do_erase(hw_flash_
block_state_t *state, uintptr_t offset)` / `_hw_flash_do_write(...,
const void *src)` - unchanged logic, just extracted so both the direct
path and the RPC-serviced path (below) call the identical, single
implementation. These still assume core 0 and still do the existing
`_sys_pico_flash_pause_request()` dance (unchanged - that mechanism is
already correct for "core 0 writes while core 1 does something else").

#### 2. New cross-core RPC, core 1 -> core 0

A second, separate mechanism from `flash_pause.c`'s own core-0->core-1
pause protocol (that one stays exactly as-is) - same low-level technique
(atomic flags + `__sev()`/`__wfe()`, matching `flash_pause.c`'s own
style) but the opposite direction and carrying real parameters, not just
a boolean. Lives in `flash.c` itself (tightly coupled to its own
`hw_flash_block_state_t`/reservations, not a reusable primitive the way
`flash_pause.c` is - no new file needed):

```c
typedef struct {
  hw_flash_block_state_t *state;
  uintptr_t offset;
  const void *src;  // NULL for erase
  bool result;
} _hw_flash_rpc_t;

static volatile _hw_flash_rpc_t _hw_flash_rpc;
static volatile uint32_t _hw_flash_rpc_pending = 0;
static volatile uint32_t _hw_flash_rpc_done = 0;
```

- `_hw_flash_rpc_call(state, offset, src)` (core 1 side): populate
  `_hw_flash_rpc`, set `pending`, `__sev()`, then spin-wait (`__wfe()`
  loop, same shape as `_sys_pico_flash_pause_point()`) on `done` with a
  timeout mirroring `HW_FLASH_PAUSE_TIMEOUT_MS` (1000ms) - times out to
  `false` on no response, same fail-safe philosophy as `_sys_pico_flash_
  pause_request()`. Returns `_hw_flash_rpc.result`.
- `_hw_flash_rpc_poll(void)` (core 0 side): if `pending`, call the
  matching `_hw_flash_do_erase()`/`_hw_flash_do_write()` from step 1
  (acquiring `_hw_flash_lock` itself, fresh - see locking note below),
  store the result, clear `pending`, set `done`, `__sev()`.

`_hw_flash_block_erase_cb()`/`_write_cb()` become the dispatch point:
validate the index and compute `offset` under `_hw_flash_lock` exactly
as today, then branch on `get_core_num()` - `== 0` calls `_hw_flash_do_
*()` directly (today's path, lock still held), `!= 0` releases the lock
*before* calling `_hw_flash_rpc_call()` (see below), re-acquiring nothing
afterward since there's nothing left to do but return the result.

**Locking note (important):** core 1 must not hold `_hw_flash_lock`
while blocked in `_hw_flash_rpc_call()`'s wait loop - `_hw_flash_rpc_
poll()` needs that same lock on core 0 to actually perform the op, and
holding it across the wait would deadlock the two cores against each
other. Core 1 only holds the lock for its own quick, existing input
validation/offset computation, releases it, *then* marshals and blocks
lock-free; core 0 acquires it fresh inside `_hw_flash_rpc_poll()`.

#### 3. Wire `_hw_flash_rpc_poll()` into `hw_poll()`

`src/picofuse/hw/pico/init.c` already declares/calls one `extern void
_hw_*_poll(void)` per subsystem (`_hw_led_poll()`, `_hw_watchdog_poll()`,
`_hw_wifi_poll()`/`_hw_usb_poll()` under their own flags) - add `_hw_
flash_rpc_poll()` the same way, unconditionally (flash.c is already
compiled in unconditionally on Pico, no `PICOFUSE_FLASH`-style gate).
This makes latency bounded by however often `hw_poll()` actually runs -
via `app_main()`, that's `_app_poll()` every run-loop tick, capped at
`_SYS_RUNLOOP_POLL_INTERVAL_MS` (50ms) plus whatever the in-flight
`event_fn` on core 0 takes to return - the same latency/timeout
trade-off `_sys_pico_flash_pause_request()`'s own 1000ms budget already
accepts for the reverse direction, not a new weakness.

#### 4. Reads stay untouched

No core restriction exists today for `_hw_flash_block_read_cb()` and
none is being added - only erase/write get the dispatch-or-marshal
treatment, keeping the common/frequent read path exactly as fast as it
is now.

#### Already fixed: lock acquired before the core-affinity check

While building the verification test below, found and fixed a sharper
version of the core-1-fails problem: `_hw_flash_block_erase_cb()`/
`_write_cb()` used to acquire `_hw_flash_lock` (a genuine cross-core
spinlock) *before* anything checked which core was calling. A core-1
caller would spin trying to acquire that same lock while core 0 held it
waiting on `_sys_pico_flash_pause_request()` for core 1 to reach its own
pause point - which core 1 can never do while stuck spinning on the
lock, so core 0's own otherwise-valid call would time out and fail too.
Fixed by moving the `get_core_num() != 0u` check to the very top of both
callbacks, before `_hw_flash_lock` is ever touched - a core-1 call now
backs out immediately without contending for anything. This step is
still needed for the RPC design above (core 1 must not hold the lock
while marshaling either), so it's not superseded by it.

### Verification

1. Done - `test/hw_028/main.c`, gated `if(DEFINED PICO_BOARD)` like
   `hw_026`/`hw_027`. Uses `app_flag_multicore`-equivalent
   (`sys_runloop_run(2, ...)`), calls `hw_block_erase()`/`_write()`/
   `_read()` from inside `on_event()` (which may land on either worker,
   unlike `hw_027`'s own `on_poll()`-based exercise - see
   `sys_runloop_poll_func_t`'s own doc on why that matters), asserts
   core 0 always succeeds and core 1 always fails erase/write (documents
   *today's* limitation - flip those assertions once the RPC lands).
   Needed a `_worker_ready` startup gate (same pattern as `hw_027`'s
   own) before the first event can be posted - without it, a backlog
   built up during core 1's own launch delay
   (`_sys_thread_ensure_core1_worker()`'s reset-safety `sleep_ms(100)` in
   `sys/pico/thread.c`) let core 0 attempt a flash op before core 1 had
   even reached `_sys_pico_flash_pause_worker_enter()`, producing
   failures unrelated to the actual code under test.
2. Re-run `hw_024`-`hw_027` on real hardware (`build-pico` and/or
   `build-picow`, whichever board is attached) to confirm the direct
   core-0 path (untouched logic, just extracted into `_hw_flash_do_*()`)
   still behaves identically.
3. Full `ctest` run on whichever Pico board is currently connected, plus
   the host builds (`build`, `build-usb`) for a general regression check
   even though this code is Pico-only.

## Other known gaps (see inline `@todo` comments for detail)

- `hw_wifi_init_device(const char *device)` - not implemented on Linux;
  always returns `NULL` there (`src/picofuse/hw/linux/CMakeLists.txt`
  always falls back to `hw/stub/wifi.c`, rather than gating a real
  backend behind `PICOFUSE_WIFI` the way `hw/pico/CMakeLists.txt` does).
  Needs a real `wpa_supplicant` control-socket client under
  `picofuse/hw`.
- ~~`hid_event_queue_touch()` - no `.c` definition~~ - **Done.** Now
  implemented in `src/picofuse/hid/event.c`, and `dev/ft6236.h` has a real
  `dev_ft6236_register_hid()` calling it directly (no generic
  `hid_register_touch()` helper - each touch controller is its own driver
  with its own protocol, so each calls this itself, the same way ADC/
  temperature drivers call `hid_event_queue_metric_float()` directly).
  `dev_stmpe610_register_hid()` exists too, same shape - both drivers
  share `hid_touch_t` (with a `pressure` field added for STMPE610's own
  resistive-panel reading) rather than each having their own bespoke
  touch struct.
- `hw_usb_register_hid_device()` (working name only - needs a better
  name, and a real signature, presumably taking a `hw_usb_device_t`
  identifying which interface to read). No module reads USB HID *input*
  (keystrokes, mouse movement) yet - scoped initially to boot-protocol
  interfaces (`hw_usb_device_subclass_boot_interface`,
  `interface_protocol` == keyboard/mouse) so a fixed, known report shape
  (8 bytes keyboard, 3-4 bytes mouse) can be assumed without a general
  HID report-descriptor parser. This is NOT one cross-platform
  implementation - three genuinely different backends are needed:
  - **Pico**: straightforward, directly through TinyUSB's own HID host
    class driver (`CFG_TUH_HID`, currently left at 0 in
    `tusb_config.h`) plus `tuh_hid_report_received_cb()` - this
    process's USB stack is the only consumer of the bus, nothing else to
    conflict with.
  - **Linux**: NOT through libusb/this module at all - the kernel's own
    `usbhid` driver already owns the interface the instant it's plugged
    in (confirmed via `lsusb -t` showing `Driver=usbhid`), so reading it
    via libusb would need `libusb_detach_kernel_driver()`, which steals
    the device from the rest of the running system (the real keyboard
    stops working for the OS itself). The correct mechanism is evdev
    (`/dev/input/eventN`), read alongside the OS rather than instead of
    it - see the already-reserved but unimplemented `hid_type_evdev`.
  - **Darwin**: same reasoning as Linux, different API -
    `IOHIDManager`/`IOHIDDeviceClient` (IOKit's HID Manager) taps into
    HID collections the kernel's own driver already parsed, without
    taking exclusive ownership. Needs the user to grant Input Monitoring
    permission on modern macOS. Neither the Linux nor Darwin backend
    needs `hw_usb_init()`/`PICOFUSE_USB` engaged at all for this, since
    both observe at the kernel-input layer rather than the raw USB layer
    this module provides.
- `_sys_pico_flash_pause_worker_enter()` - doesn't cover core 1 while
  parked in `thread.c`'s own `multicore_fifo_pop_blocking()`, before a
  runloop worker has started or between runloop sessions. Low severity
  (microsecond-scale window, transient-stall failure mode, storage
  blocks live outside the code region core 1 would be executing) - see
  `sys/pico/flash_pause.h`'s own doc.
- `hid/gpio.c`'s shared GPIO edge dispatcher only produces
  `hid_event_type_keycode` events (rising/falling edge -> on/off, with
  debounce) - there's no way for something that isn't a keycode-shaped
  input to hook a real GPIO interrupt through it. `hw_gpio_set_callback()`
  is a single global slot, already claimed by this dispatcher the moment
  any GPIO-backed HID device is registered (buttons, etc.), so nothing
  else can call it directly without silently stealing/losing that slot -
  see `dev_ft6236_register_hid()`'s own doc for a concrete case this
  blocks: FT6236's own interrupt pin can't get a genuine ISR-driven poll
  today, just a fast-polling approximation (cheap thanks to
  `dev_ft6236_poll()`'s own IRQ-skip check, but still HID-timer-driven,
  not interrupt-driven). Fix would be extending `hid/gpio.c`'s dispatcher
  to also support a generic raw edge-callback registration alongside its
  existing keycode-producing one, so callers like
  `dev_ft6236_register_hid()` can register through the shared mechanism
  instead of needing `hw_gpio_set_callback()` themselves.
