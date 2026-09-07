#include <picofuse/hw.h>
#include <picofuse/sys.h>
#include <test/test.h>

test_main_hw(0) {
  hw_memory_usage_t usage = {0};
  test_assert(!hw_memory_get_usage(NULL));
  test_assert(hw_memory_get_usage(&usage));
  test_assert(usage.psram_used_bytes == 0);

#if defined(SYSTEM_NAME_PICO)
  test_assert(usage.ram_total_bytes > 0);
  test_assert(usage.ram_free_bytes <= usage.ram_total_bytes);
  test_assert(usage.stack_total_bytes > 0);
  test_assert(usage.stack_used_bytes <= usage.stack_total_bytes);
  test_assert(usage.stack_free_bytes <= usage.stack_total_bytes);
  test_assert(usage.stack_used_bytes + usage.stack_free_bytes ==
              usage.stack_total_bytes);
  test_assert(usage.flash_total_bytes > 0);
  test_assert(usage.flash_used_bytes > 0);
  test_assert(usage.flash_free_bytes <= usage.flash_total_bytes);
  size_t initial_heap = usage.heap_used_bytes;
  void *allocation = sys_malloc(4096);
  test_assert(allocation != NULL);
  test_assert(hw_memory_get_usage(&usage));
  test_assert(usage.heap_used_bytes >= initial_heap);
  sys_free(allocation);
#else
  test_assert(usage.ram_total_bytes == 0);
  test_assert(usage.ram_free_bytes == 0);
  test_assert(usage.heap_used_bytes == 0);
  test_assert(usage.stack_total_bytes == 0);
  test_assert(usage.stack_free_bytes == 0);
  test_assert(usage.flash_total_bytes == 0);
  test_assert(usage.flash_used_bytes == 0);
  test_assert(usage.flash_reserved_bytes == 0);
  test_assert(usage.flash_free_bytes == 0);
#endif
}