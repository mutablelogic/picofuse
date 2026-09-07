#include <picofuse/hw.h>
#include <test/test.h>

#define FLASH_SECTOR_BYTES 4096u

static uint8_t _write_data[FLASH_SECTOR_BYTES];
static uint8_t _read_data[FLASH_SECTOR_BYTES];

test_main_hw(0) {
  size_t capacity = hw_block_flash_get_capacity();
  hw_memory_usage_t usage = {0};
  test_assert(hw_memory_get_usage(&usage));
  test_assert(usage.flash_total_bytes > 0u);
  test_assert(usage.flash_used_bytes > 0u);
  test_assert(usage.flash_reserved_bytes == 0u);
  test_assert(usage.flash_free_bytes == capacity);
  test_assert(capacity >= 2u * FLASH_SECTOR_BYTES);
  test_assert(hw_block_flash_init(0) == NULL);

  hw_block_t *first = hw_block_flash_init(FLASH_SECTOR_BYTES);
  test_assert(first != NULL);
  test_assert(hw_block_count(first) == 1u);
  test_assert(hw_block_size(first) == FLASH_SECTOR_BYTES);
  test_assert(hw_block_flash_get_capacity() == capacity - FLASH_SECTOR_BYTES);
  test_assert(hw_memory_get_usage(&usage));
  test_assert(usage.flash_reserved_bytes == FLASH_SECTOR_BYTES);
  test_assert(usage.flash_free_bytes == capacity - FLASH_SECTOR_BYTES);

  hw_block_t *second = hw_block_flash_init(FLASH_SECTOR_BYTES);
  test_assert(second != NULL);
  test_assert(second != first);
  test_assert(hw_block_flash_get_capacity() ==
              capacity - 2u * FLASH_SECTOR_BYTES);
  test_assert(hw_memory_get_usage(&usage));
  test_assert(usage.flash_reserved_bytes == 2u * FLASH_SECTOR_BYTES);
  test_assert(usage.flash_free_bytes == capacity - 2u * FLASH_SECTOR_BYTES);

  for (size_t i = 0; i < sizeof(_write_data); i++) {
    _write_data[i] = (uint8_t)(i * 37u + 0x5Au);
  }
  test_assert(!hw_block_write(first, 1, _write_data));
  test_assert(hw_block_erase(first, 0));
  test_assert(hw_block_write(first, 0, _write_data));
  test_assert(hw_block_read(first, 0, _read_data));
  test_assert(memcmp(_read_data, _write_data, sizeof(_read_data)) == 0);

  test_assert(hw_block_erase(first, 0));
  test_assert(hw_block_read(first, 0, _read_data));
  for (size_t i = 0; i < sizeof(_read_data); i++) {
    test_assert(_read_data[i] == 0xFFu);
  }

  hw_block_deinit(first);
  hw_block_deinit(second);
  test_assert(hw_block_flash_get_capacity() == capacity);
  test_assert(hw_memory_get_usage(&usage));
  test_assert(usage.flash_reserved_bytes == 0u);
  test_assert(usage.flash_free_bytes == capacity);
}