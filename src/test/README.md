# testrunner

<!-- @brief: Runs picofuse tests on real Pico hardware through a debug probe. -->

## Introduction

`testrunner` runs a picofuse system test compiled for a Pico board through
CTest, on real hardware, via a debug probe. It's a small host-only tool
(never cross-compiled for `PICO_BOARD`) that:

1. Flashes a given `.elf` onto the target via `openocd`.
2. Resets the target and lets it run.
3. Watches the target's own output for one of two markers -
   `"[TEST] [EXIT] "` (printed by every test on a clean pass) or `"[PANIC] "`
   (printed by `sys_panicf()` - a failed `test_assert()`, or a genuine crash) -
   and reports pass/fail back to CTest accordingly.
4. Fails the test if neither marker appears before a timeout.

Output is read one of two ways:

- **RTT (default)** - `openocd` opens an RTT server against the target's
  `SEGGER RTT` control block and `testrunner` connects to it over TCP. No
  extra wiring required beyond the debug probe's SWD connection.
- **Serial** (`--serial <device>`) - `openocd` programs and resets the
  target, then exits immediately (no SWD session stays open), and
  `testrunner` reads the target's UART output directly over a serial device
  instead - e.g. a debug probe's separate UART-bridge interface. Useful when
  you want the target running with no debugger attached at all (RTT
  requires an active SWD session to poll memory; serial doesn't).

On a host (non-`PICO_BOARD`) build, `picofuse_test()` skips `testrunner`
entirely and runs the compiled test executable directly.

## Dependencies

`testrunner` shells out to `openocd` - it isn't a library dependency, but it
must be on `$PATH` (or passed via `--openocd`) for any `PICO_BOARD` test to
run. Use the Raspberry Pi fork's build, not a distro/mainline OpenOCD
package: download it from
[github.com/raspberrypi/pico-sdk-tools/releases](https://github.com/raspberrypi/pico-sdk-tools/releases).
It carries the RP2040/RP2350 target support and RTT server this tool
depends on, which mainline OpenOCD may lack or only partially support.

## Usage

### Flags

```
Usage: testrunner [options] <elf-file>

  --help, -h <bool>       (default: true, negate: --no-help)
  --openocd <string>      (default: openocd)
  --interface <string>    (default: interface/cmsis-dap.cfg)
  --target <string>       (required)
  --serial <string>       (default: none -- read via OpenOCD's RTT server)
  --baud <uint>           (default: 115200)
  --timeout <uint>        (default: 10)
  --verbose, -v <bool>    (default: false)
```

- `--openocd` - path or bare name of the `openocd` binary (resolved against
  `$PATH` the same way `execvp()` would if it doesn't contain a `/`).
- `--interface` - OpenOCD interface config, e.g. `interface/cmsis-dap.cfg`
  for a CMSIS-DAP debug probe.
- `--target` - OpenOCD target config, e.g. `target/rp2040.cfg` or
  `target/rp2350.cfg`. Required.
- `--serial` - when set, switches from RTT to reading a UART device
  directly (see above). Omit to use RTT.
- `--baud` - baud rate for `--serial` mode.
- `--timeout` - seconds allowed for flashing plus waiting for a pass/fail
  marker, combined.
- `--verbose` - passes `openocd`'s own stdout/stderr through instead of
  discarding it; useful when a test is misbehaving and you need to see what
  OpenOCD itself is doing (flash errors, RTT control block not found, etc).

Example, run directly against real hardware:

```sh
build/src/test/testrunner --target target/rp2350.cfg \
    build-pico/test/sys_019.elf
```

### How to write a `picofuse_test` in CMakeLists.txt

Tests live under `test/<name>/`, one `main.c` per test, and are registered
in `test/CMakeLists.txt` with the `picofuse_test()` macro:

```cmake
# One-line comment describing what this test actually exercises.
picofuse_test(sys_019
    sys_019/main.c
)
```

`picofuse_test(<target> <source>...)`:

- `<target>` becomes both the CMake target name and the CTest test name
  (the convention is `sys_NNN` for `picofuse/sys` coverage, `hw_NNN` for
  `picofuse/hw`), and must be unique across the whole suite.
- `<source>...` are paths relative to `test/` (the calling
  `CMakeLists.txt`'s directory), same as any other CMake `SOURCES` list -
  normally just the one `main.c`.
- It links `picofuse-hw` (which itself pulls in `picofuse-sys`), so both
  APIs are always available regardless of what the test actually uses.
- On a `PICO_BOARD` build it produces a `.elf` and registers a CTest test
  that runs it through `testrunner` (see above) against
  `target/<chip>.cfg` (`rp2040`/`rp2350` chosen automatically from
  `PICO_RP2040`/`PICO_RP2350`). On a host build it registers a CTest test
  that just runs the compiled executable directly.

Guard a test with `if(NOT DEFINED PICO_BOARD)` / `if(NOT APPLE)` /
etc. when it only makes sense on some platforms - see the existing
`test/CMakeLists.txt` for examples (host-only argument-parsing tests,
Darwin having no GPIO backend, and so on). Prefer leaving a test registered
and failing over silently gating it out, unless there's a specific reason
documented in a comment above the guard.

### How to structure a unit test in `.c`

Each test's `main.c` uses one of two macros from `include/test/test.h` in
place of a raw `main()`:

- `test_main_sys(arena_size) { ... }` - wraps `sys_init()`/`sys_exit()`
  around the body, and prints the `"[TEST] [INIT] <env>"` /
  `"[TEST] [EXIT] <env>"` markers `testrunner` looks for.
  `arena_size` is forwarded to `sys_init()`: `0` leaves `sys_malloc()` and
  friends routed to the system allocator, a nonzero value configures a
  fixed-size default arena instead.
- `test_main_hw(arena_size) { ... }` - the same, but also wraps
  `hw_init()`/`hw_exit()` (inside the `sys_init()`/`sys_exit()` pair, since
  `hw` depends on `sys`) - use this instead when the test needs
  `picofuse/hw`.

Inside the body, use:

- `test_assert(condition)` - panics via `sys_panicf()` (printing
  `"[PANIC] "` plus the failed condition, file, and line) if `condition` is
  false. Always checked, regardless of `NDEBUG`.
- `test_assert_strequal(actual, expected)` - like `test_assert`, but for
  comparing two null-terminated strings, with both values included in the
  panic message on mismatch.

A minimal test looks like:

```c
#include <picofuse/sys.h>
#include <test/test.h>

test_main_sys(0) {
  sys_mutex_t *mutex = sys_mutex_init();
  test_assert(mutex != NULL);
  test_assert(sys_mutex_lock(mutex));
  test_assert(sys_mutex_unlock(mutex));
  sys_mutex_deinit(mutex);
}
```
