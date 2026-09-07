# TODO

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

- `hw_wifi_init_device()` - no real Linux backend yet (`hw/wifi.h`).
- `hid_event_queue_touch()` - no `.c` definition or touch-controller
  registration helper yet (`hid/event.h`).
- `hw_usb_register_hid_device()` (working name) - no module reads HID
  *input* yet; needs three separate backends (Pico TinyUSB HID class
  driver, Linux evdev, Darwin IOHIDManager) - see `hw/usb.h`'s own
  top-level doc.
- `_sys_pico_flash_pause_worker_enter()` - doesn't cover core 1 while
  parked in `thread.c`'s own `multicore_fifo_pop_blocking()`, before a
  runloop worker has started or between runloop sessions. Low severity
  (microsecond-scale window, transient-stall failure mode, storage
  blocks live outside the code region core 1 would be executing) - see
  `sys/pico/flash_pause.h`'s own doc.
