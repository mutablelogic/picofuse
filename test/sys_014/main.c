#include <picofuse/sys.h>
#include <stdint.h>
#include <test/test.h>

// Number of samples drawn per generator. Large enough that the chance of a
// genuinely random bit staying stuck across every sample is astronomically
// small (2 * 0.5^SAMPLES), so a failure here means a broken generator
// (e.g. a stuck, masked, or zero-extended value), not bad luck.
#define SAMPLES 64

test_main_sys(0) {

  // Two consecutive 32-bit draws colliding is a 1-in-2^32 event; a fixed or
  // broken generator would collide every time.
  uint32_t a = sys_random_uint32();
  uint32_t b = sys_random_uint32();
  test_assert(a != b);

  // Across enough samples, every bit position should flip both ways: ORing
  // them all together should saturate to all-ones, and ANDing them together
  // should saturate to all-zeros. Either failing points at a stuck bit.
  uint32_t or32 = 0;
  uint32_t and32 = 0xFFFFFFFFu;
  for (int i = 0; i < SAMPLES; i++) {
    uint32_t v = sys_random_uint32();
    or32 |= v;
    and32 &= v;
  }
  test_assert(or32 == 0xFFFFFFFFu);
  test_assert(and32 == 0);

  // Same coverage check for the 64-bit generator. This also proves the
  // upper 32 bits are actually populated, not a zero-extended 32-bit value:
  // if they were always zero, or64 could never reach UINT64_MAX.
  uint64_t or64 = 0;
  uint64_t and64 = UINT64_MAX;
  for (int i = 0; i < SAMPLES; i++) {
    uint64_t v = sys_random_uint64();
    or64 |= v;
    and64 &= v;
  }
  test_assert(or64 == UINT64_MAX);
  test_assert(and64 == 0);

}
