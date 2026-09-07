#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

/**
 * Manual-only watchdog reset test - never registered with picofuse_test()/
 * ctest (see test/CMakeLists.txt's own comment), since running it
 * deliberately reboots the machine.
 *
 * Run by hand, twice, across the reboot it causes:
 *
 *   1. First run (no marker file yet): prints hw_watchdog_maxtimeout_ms(),
 *      arms a short delayed reset via hw_watchdog_reset(), and returns
 *      immediately WITHOUT calling hw_exit() - hw_exit() calls
 *      hw_watchdog_deinit(), which disables the very reset this just
 *      armed (see its own doc). hw_watchdog_reset() doesn't need
 *      continued polling to take effect either (see its own doc), so
 *      there's nothing left for this process to do - the kernel resets
 *      the board on its own a few seconds later, whether or not this
 *      process is even still running by then.
 *   2. Second run, after the board comes back up: finds the marker this
 *      same binary left behind before resetting, and reports whether
 *      hw_watchdog_did_reset() actually detects that reset.
 *
 * Needs exclusive access to the watchdog device - if something else
 * already owns it (e.g. systemd's own RuntimeWatchdogSec - see
 * picofuse/hw/watchdog.h's own note), hw_watchdog_init() returns NULL and
 * this exits without doing anything.
 */

#define HW_022_MARKER_DIR "/var/tmp"
#define HW_022_MARKER_PATH HW_022_MARKER_DIR "/picofuse_hw_022_marker"
#define HW_022_RESET_DELAY_MS 5000u

int main(void) {
  sys_init(0, NULL, 0, sys_stdio_none);
  hw_init();

  hw_watchdog_t *watchdog = hw_watchdog_init();
  if (watchdog == NULL) {
    sys_printf("[hw_022] no watchdog backend available (or already owned "
               "elsewhere) - nothing to do\n");
    hw_exit();
    sys_exit();
    return 1;
  }

  sys_printf("[hw_022] maxtimeout_ms=%u\n", hw_watchdog_maxtimeout_ms());

  FILE *marker = fopen(HW_022_MARKER_PATH, "r");
  if (marker != NULL) {
    fclose(marker);
    (void)remove(HW_022_MARKER_PATH);

    bool did_reset = hw_watchdog_did_reset(watchdog);
    sys_printf("[hw_022] second run (post-reset): did_reset=%s -> %s\n",
               did_reset ? "yes" : "no", did_reset ? "PASS" : "FAIL");

    hw_watchdog_deinit(watchdog);
    hw_exit();
    sys_exit();
    return did_reset ? 0 : 1;
  }

  marker = fopen(HW_022_MARKER_PATH, "w");
  if (marker == NULL) {
    sys_printf("[hw_022] could not create marker file %s - aborting before "
               "arming a reset\n",
               HW_022_MARKER_PATH);
    hw_watchdog_deinit(watchdog);
    hw_exit();
    sys_exit();
    return 1;
  }
  // A plain fclose() only guarantees the data reaches the OS page cache,
  // not the physical disk - the reset armed below can (and, on real
  // hardware, did) fire before the kernel's own periodic writeback gets
  // around to flushing it, losing the marker entirely. fsync() the file
  // itself for its data, then the containing directory for the new
  // directory entry (some filesystems don't guarantee the latter is
  // durable from the file's own fsync() alone) - both must return before
  // it's safe to arm an unrecoverable reset.
  bool marker_durable = fflush(marker) == 0 && fsync(fileno(marker)) == 0;
  fclose(marker);
  if (marker_durable) {
    int dir_fd = open(HW_022_MARKER_DIR, O_RDONLY);
    if (dir_fd >= 0) {
      marker_durable = fsync(dir_fd) == 0;
      close(dir_fd);
    } else {
      marker_durable = false;
    }
  }
  if (!marker_durable) {
    sys_printf("[hw_022] could not durably write marker file %s - aborting "
               "before arming a reset\n",
               HW_022_MARKER_PATH);
    (void)remove(HW_022_MARKER_PATH);
    hw_watchdog_deinit(watchdog);
    hw_exit();
    sys_exit();
    return 1;
  }

  sys_printf(
      "[hw_022] first run: arming a reset in %ums - the board will reset "
      "itself shortly. Re-run this program after it comes back up to "
      "check hw_watchdog_did_reset().\n",
      HW_022_RESET_DELAY_MS);
  hw_watchdog_reset(watchdog, HW_022_RESET_DELAY_MS);

  // Deliberately not calling hw_watchdog_deinit()/hw_exit() here - see this
  // file's own top comment on why that would cancel the reset just armed.
  return 0;
}
