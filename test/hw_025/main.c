#include <picofuse/hw.h>
#include <test/test.h>

#include "../../src/picofuse/hw/block/private.h"

typedef struct test_block_state_t {
  uint8_t bytes[8];
} test_block_state_t;

static bool _test_block_read(const hw_block_t *block, void *userdata,
                             size_t index, void *dst) {
  (void)block;
  test_block_state_t *state = userdata;
  if (index != 0u || dst == NULL) {
    return false;
  }
  memcpy(dst, state->bytes, sizeof(state->bytes));
  return true;
}

static bool _test_block_erase(hw_block_t *block, void *userdata, size_t index) {
  (void)block;
  test_block_state_t *state = userdata;
  if (index != 0u) {
    return false;
  }
  memset(state->bytes, 0xFF, sizeof(state->bytes));
  return true;
}

static bool _test_block_write(hw_block_t *block, void *userdata, size_t index,
                              const void *src) {
  (void)block;
  test_block_state_t *state = userdata;
  if (index != 0u || src == NULL) {
    return false;
  }
  memcpy(state->bytes, src, sizeof(state->bytes));
  return true;
}

static const hw_block_callbacks_t _test_block_callbacks = {
    .read = _test_block_read,
    .erase = _test_block_erase,
    .write = _test_block_write,
};

static hw_block_t *_test_block_alloc(void) {
  hw_block_t *block = _hw_block_alloc_handle(&_test_block_callbacks, NULL, 1,
                                             sizeof(test_block_state_t));
  if (block != NULL) {
    block->userdata = _hw_block_context(block);
  }
  return block;
}

test_main_hw(0) {
  test_assert(hw_block_count(NULL) == 0u);
  test_assert(hw_block_size(NULL) == 0u);
  test_assert(!hw_block_read(NULL, 0, NULL));
  test_assert(!hw_block_erase(NULL, 0));
  test_assert(!hw_block_write(NULL, 0, NULL));
  hw_block_deinit(NULL);

  hw_block_t *blocks[BLOCK_MAX_CAPACITY] = {0};
  for (size_t i = 0; i < BLOCK_MAX_CAPACITY; i++) {
    blocks[i] = _test_block_alloc();
    test_assert(blocks[i] != NULL);
  }
  test_assert(_test_block_alloc() == NULL);

  hw_block_t *block = blocks[0];
  uint8_t write_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  uint8_t read_data[8] = {0};
  test_assert(hw_block_count(block) == 1u);
  test_assert(hw_block_size(block) == sizeof(write_data));
  test_assert(!hw_block_read(block, 1, read_data));
  test_assert(hw_block_write(block, 0, write_data));
  test_assert(hw_block_read(block, 0, read_data));
  test_assert(memcmp(read_data, write_data, sizeof(read_data)) == 0);
  test_assert(hw_block_erase(block, 0));
  test_assert(hw_block_read(block, 0, read_data));
  for (size_t i = 0; i < sizeof(read_data); i++) {
    test_assert(read_data[i] == 0xFFu);
  }

  hw_block_deinit(block);
  blocks[0] = _test_block_alloc();
  test_assert(blocks[0] != NULL);
  for (size_t i = 0; i < BLOCK_MAX_CAPACITY; i++) {
    hw_block_deinit(blocks[i]);
  }
}