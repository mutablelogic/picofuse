#include <picofuse/hw.h>

size_t hw_block_flash_get_capacity(void) { return 0u; }

void _hw_flash_memory_get_usage(size_t *total_bytes, size_t *used_bytes,
                                size_t *reserved_bytes, size_t *free_bytes) {
  if (total_bytes != NULL) {
    *total_bytes = 0u;
  }
  if (used_bytes != NULL) {
    *used_bytes = 0u;
  }
  if (reserved_bytes != NULL) {
    *reserved_bytes = 0u;
  }
  if (free_bytes != NULL) {
    *free_bytes = 0u;
  }
}

hw_block_t *hw_block_flash_init(size_t size_bytes) {
  (void)size_bytes;
  return NULL;
}