#include <picofuse/hw/memory.h>
#include <stdint.h>
#include <string.h>

#if defined(SYSTEM_NAME_PICO)
extern char __StackBottom;
extern char end;
extern char __StackLimit;
extern char __StackOneBottom;
extern char __StackOneTop;
extern char __StackTop;
extern void *_sbrk(int increment);
extern void _hw_flash_memory_get_usage(size_t *total_bytes, size_t *used_bytes,
                                       size_t *reserved_bytes,
                                       size_t *free_bytes);
#include <hardware/address_mapped.h>
#include <pico/multicore.h>
#endif

bool hw_memory_get_usage(hw_memory_usage_t *usage) {
  if (usage == NULL) {
    return false;
  }

  memset(usage, 0, sizeof(*usage));
#if defined(SYSTEM_NAME_PICO)
  uintptr_t heap_end = (uintptr_t)_sbrk(0);
  uintptr_t heap_limit = (uintptr_t)&__StackLimit;
  usage->ram_total_bytes = heap_limit - SRAM_BASE;
  usage->ram_free_bytes = heap_limit - heap_end;
  usage->heap_used_bytes = heap_end - (uintptr_t)&end;

  uintptr_t stack_bottom = get_core_num() == 0u ? (uintptr_t)&__StackBottom
                                                : (uintptr_t)&__StackOneBottom;
  uintptr_t stack_top =
      get_core_num() == 0u ? (uintptr_t)&__StackTop : (uintptr_t)&__StackOneTop;
  uintptr_t stack_pointer = (uintptr_t)&stack_pointer;
  usage->stack_total_bytes = stack_top - stack_bottom;
  if (stack_pointer >= stack_bottom && stack_pointer <= stack_top) {
    usage->stack_used_bytes = stack_top - stack_pointer;
    usage->stack_free_bytes = stack_pointer - stack_bottom;
  }

  _hw_flash_memory_get_usage(
      &usage->flash_total_bytes, &usage->flash_used_bytes,
      &usage->flash_reserved_bytes, &usage->flash_free_bytes);
#endif
  return true;
}