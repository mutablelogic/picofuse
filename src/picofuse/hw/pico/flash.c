#include <hardware/address_mapped.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/critical_section.h>
#include <picofuse/hw.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../sys/pico/flash_pause.h"
#include "../block/private.h"

#define HW_FLASH_PAUSE_TIMEOUT_MS 1000u

extern char __flash_binary_end;

typedef struct hw_flash_block_state_t {
  uintptr_t offset_bytes;
  size_t size_bytes;
  size_t erase_size_bytes;
  size_t write_size_bytes;
} hw_flash_block_state_t;

typedef struct hw_flash_reservation_t {
  hw_block_t *block;
  uintptr_t offset_bytes;
  size_t size_bytes;
} hw_flash_reservation_t;

static bool __no_inline_not_in_flash_func(_hw_flash_block_read_cb)(
    const hw_block_t *block, void *userdata, size_t index, void *dst);
static bool __no_inline_not_in_flash_func(_hw_flash_block_erase_cb)(
    hw_block_t *block, void *userdata, size_t index);
static bool __no_inline_not_in_flash_func(_hw_flash_block_write_cb)(
    hw_block_t *block, void *userdata, size_t index, const void *src);
static void _hw_flash_block_deinit_cb(hw_block_t *block, void *userdata);

static const hw_block_callbacks_t _hw_flash_block_callbacks = {
    .read = _hw_flash_block_read_cb,
    .erase = _hw_flash_block_erase_cb,
    .write = _hw_flash_block_write_cb,
    .deinit = _hw_flash_block_deinit_cb,
};

static critical_section_t _hw_flash_lock;
static hw_flash_reservation_t _hw_flash_reservations[BLOCK_MAX_CAPACITY] = {0};

static bool _hw_flash_block_allocated(const hw_flash_block_state_t *state) {
  return state != NULL && state->size_bytes > 0u &&
         state->erase_size_bytes > 0u && state->write_size_bytes > 0u;
}

static bool _hw_flash_block_valid(const hw_block_t *block,
                                  const hw_flash_block_state_t *state) {
  return _hw_flash_block_allocated(state) && block != NULL &&
         block->userdata == state;
}

static bool _hw_flash_block_index_valid(const hw_block_t *block,
                                        const hw_flash_block_state_t *state,
                                        size_t index) {
  return _hw_flash_block_valid(block, state) &&
         index < state->size_bytes / state->erase_size_bytes;
}

static bool _hw_flash_block_bounds(uintptr_t *free_start,
                                   uintptr_t *flash_end) {
  uintptr_t image_end = (uintptr_t)&__flash_binary_end;
  if (image_end < XIP_BASE) {
    return false;
  }

  uintptr_t used_bytes = image_end - XIP_BASE;
  uintptr_t free_offset =
      (used_bytes + FLASH_SECTOR_SIZE - 1u) & ~(FLASH_SECTOR_SIZE - 1u);
  if (free_offset >= PICO_FLASH_SIZE_BYTES) {
    return false;
  }

  *free_start = free_offset;
  *flash_end = PICO_FLASH_SIZE_BYTES;
  return true;
}

void _hw_flash_memory_get_usage(size_t *total_bytes, size_t *used_bytes,
                                size_t *reserved_bytes, size_t *free_bytes) {
  uintptr_t image_end = (uintptr_t)&__flash_binary_end;
  size_t used = image_end >= XIP_BASE ? (size_t)(image_end - XIP_BASE) : 0u;
  size_t aligned_used =
      (used + FLASH_SECTOR_SIZE - 1u) & ~(size_t)(FLASH_SECTOR_SIZE - 1u);
  size_t reserved = 0u;

  critical_section_enter_blocking(&_hw_flash_lock);
  for (size_t i = 0; i < BLOCK_MAX_CAPACITY; i++) {
    reserved += _hw_flash_reservations[i].size_bytes;
  }
  critical_section_exit(&_hw_flash_lock);

  if (total_bytes != NULL) {
    *total_bytes = PICO_FLASH_SIZE_BYTES;
  }
  if (used_bytes != NULL) {
    *used_bytes = used;
  }
  if (reserved_bytes != NULL) {
    *reserved_bytes = reserved;
  }
  if (free_bytes != NULL) {
    *free_bytes = aligned_used <= PICO_FLASH_SIZE_BYTES &&
                          reserved <= PICO_FLASH_SIZE_BYTES - aligned_used
                      ? PICO_FLASH_SIZE_BYTES - aligned_used - reserved
                      : 0u;
  }
}

static size_t _hw_flash_block_max_free(uintptr_t free_start,
                                       uintptr_t flash_end) {
  size_t maximum = 0;
  for (size_t i = 0; i <= BLOCK_MAX_CAPACITY; i++) {
    uintptr_t upper = i == BLOCK_MAX_CAPACITY
                          ? flash_end
                          : _hw_flash_reservations[i].offset_bytes;
    if (i < BLOCK_MAX_CAPACITY && _hw_flash_reservations[i].block == NULL) {
      continue;
    }

    uintptr_t lower = free_start;
    for (size_t j = 0; j < BLOCK_MAX_CAPACITY; j++) {
      const hw_flash_reservation_t *reservation = &_hw_flash_reservations[j];
      uintptr_t reservation_end =
          reservation->offset_bytes + reservation->size_bytes;
      if (reservation->block != NULL && reservation_end <= upper &&
          reservation_end > lower) {
        lower = reservation_end;
      }
    }
    if (upper > lower && upper - lower > maximum) {
      maximum = upper - lower;
    }
  }
  return maximum - maximum % FLASH_SECTOR_SIZE;
}

static bool _hw_flash_block_find_region(uintptr_t free_start,
                                        uintptr_t flash_end, size_t size,
                                        uintptr_t *offset) {
  uintptr_t end = flash_end;
  while (end >= free_start && size <= end - free_start) {
    uintptr_t start = end - size;
    uintptr_t next_end = 0;
    for (size_t i = 0; i < BLOCK_MAX_CAPACITY; i++) {
      const hw_flash_reservation_t *reservation = &_hw_flash_reservations[i];
      uintptr_t reservation_end =
          reservation->offset_bytes + reservation->size_bytes;
      if (reservation->block != NULL && start < reservation_end &&
          reservation->offset_bytes < end &&
          reservation->offset_bytes > next_end) {
        next_end = reservation->offset_bytes;
      }
    }
    if (next_end == 0) {
      *offset = start;
      return true;
    }
    end = next_end;
  }
  return false;
}

void _hw_flash_module_init(void) { critical_section_init(&_hw_flash_lock); }

void _hw_flash_module_exit(void) { critical_section_deinit(&_hw_flash_lock); }

hw_block_t *hw_block_flash_init(size_t size_bytes) {
  size_t aligned_size = size_bytes - size_bytes % FLASH_SECTOR_SIZE;
  if (aligned_size == 0u) {
    return NULL;
  }

  critical_section_enter_blocking(&_hw_flash_lock);
  uintptr_t free_start = 0;
  uintptr_t flash_end = 0;
  uintptr_t offset = 0;
  if (!_hw_flash_block_bounds(&free_start, &flash_end) ||
      !_hw_flash_block_find_region(free_start, flash_end, aligned_size,
                                   &offset)) {
    critical_section_exit(&_hw_flash_lock);
    return NULL;
  }

  size_t reservation_index = BLOCK_MAX_CAPACITY;
  for (size_t i = 0; i < BLOCK_MAX_CAPACITY; i++) {
    if (_hw_flash_reservations[i].block == NULL) {
      reservation_index = i;
      break;
    }
  }
  if (reservation_index == BLOCK_MAX_CAPACITY) {
    critical_section_exit(&_hw_flash_lock);
    return NULL;
  }

  hw_block_t *block = _hw_block_alloc_handle(&_hw_flash_block_callbacks, NULL,
                                             aligned_size / FLASH_SECTOR_SIZE,
                                             FLASH_SECTOR_SIZE);
  hw_flash_block_state_t *state = _hw_block_context(block);
  static_assert(sizeof(*state) <= BLOCK_CONTEXT_SIZE,
                "flash block state exceeds BLOCK_CONTEXT_SIZE");
  if (state == NULL) {
    critical_section_exit(&_hw_flash_lock);
    return NULL;
  }

  *state = (hw_flash_block_state_t){
      .offset_bytes = offset,
      .size_bytes = aligned_size,
      .erase_size_bytes = FLASH_SECTOR_SIZE,
      .write_size_bytes = FLASH_PAGE_SIZE,
  };
  block->userdata = state;
  _hw_flash_reservations[reservation_index] = (hw_flash_reservation_t){
      .block = block, .offset_bytes = offset, .size_bytes = aligned_size};
  critical_section_exit(&_hw_flash_lock);
  return block;
}

size_t hw_block_flash_get_capacity(void) {
  critical_section_enter_blocking(&_hw_flash_lock);
  uintptr_t free_start = 0;
  uintptr_t flash_end = 0;
  size_t capacity = _hw_flash_block_bounds(&free_start, &flash_end)
                        ? _hw_flash_block_max_free(free_start, flash_end)
                        : 0u;
  critical_section_exit(&_hw_flash_lock);
  return capacity;
}

static bool __no_inline_not_in_flash_func(_hw_flash_block_read_cb)(
    const hw_block_t *block, void *userdata, size_t index, void *dst) {
  const hw_flash_block_state_t *state = userdata;
  critical_section_enter_blocking(&_hw_flash_lock);
  if (dst == NULL || !_hw_flash_block_index_valid(block, state, index)) {
    critical_section_exit(&_hw_flash_lock);
    return false;
  }

  uintptr_t offset = state->offset_bytes + index * state->erase_size_bytes;
  memcpy(dst, (const void *)(XIP_BASE + offset), state->erase_size_bytes);
  critical_section_exit(&_hw_flash_lock);
  return true;
}

static bool __no_inline_not_in_flash_func(_hw_flash_block_erase_cb)(
    hw_block_t *block, void *userdata, size_t index) {
  // Checked before _hw_flash_lock, not after: that lock is a genuine
  // cross-core spinlock, and _sys_pico_flash_pause_request() (below) needs
  // core 1 to reach its own pause point promptly once core 0 holds this
  // lock. A core-1 caller spinning to acquire the same lock first - which
  // is exactly what happens if this check runs after it - can never reach
  // that pause point, so core 0's own otherwise-valid erase times out and
  // fails too. Checking core affinity first means a core-1 call backs out
  // immediately without ever contending for the lock at all.
  //
  // @todo Currently just fails - see TODO.md's "Transparent core-1 ->
  // core-0 flash write/erase marshaling" for the planned fix.
  if (get_core_num() != 0u) {
    return false;
  }

  const hw_flash_block_state_t *state = userdata;
  critical_section_enter_blocking(&_hw_flash_lock);
  if (!_hw_flash_block_index_valid(block, state, index)) {
    critical_section_exit(&_hw_flash_lock);
    return false;
  }

  uintptr_t offset = state->offset_bytes + index * state->erase_size_bytes;
  if (!_sys_pico_flash_pause_request(HW_FLASH_PAUSE_TIMEOUT_MS)) {
    critical_section_exit(&_hw_flash_lock);
    return false;
  }
  uint32_t irq_state = save_and_disable_interrupts();
  flash_range_erase((uint32_t)offset, state->erase_size_bytes);
  restore_interrupts(irq_state);
  _sys_pico_flash_pause_release();
  critical_section_exit(&_hw_flash_lock);
  return true;
}

static bool __no_inline_not_in_flash_func(_hw_flash_block_write_cb)(
    hw_block_t *block, void *userdata, size_t index, const void *src) {
  // See _hw_flash_block_erase_cb()'s own doc on why this is checked before
  // _hw_flash_lock.
  if (get_core_num() != 0u) {
    return false;
  }

  const hw_flash_block_state_t *state = userdata;
  critical_section_enter_blocking(&_hw_flash_lock);
  if (src == NULL || !_hw_flash_block_index_valid(block, state, index)) {
    critical_section_exit(&_hw_flash_lock);
    return false;
  }

  uintptr_t offset = state->offset_bytes + index * state->erase_size_bytes;
  if (!_sys_pico_flash_pause_request(HW_FLASH_PAUSE_TIMEOUT_MS)) {
    critical_section_exit(&_hw_flash_lock);
    return false;
  }
  uint32_t irq_state = save_and_disable_interrupts();
  flash_range_program((uint32_t)offset, src, state->erase_size_bytes);
  restore_interrupts(irq_state);
  _sys_pico_flash_pause_release();
  critical_section_exit(&_hw_flash_lock);
  return true;
}

static void _hw_flash_block_deinit_cb(hw_block_t *block, void *userdata) {
  hw_flash_block_state_t *state = userdata;
  critical_section_enter_blocking(&_hw_flash_lock);
  if (_hw_flash_block_valid(block, state)) {
    for (size_t i = 0; i < BLOCK_MAX_CAPACITY; i++) {
      if (_hw_flash_reservations[i].block == block) {
        _hw_flash_reservations[i] = (hw_flash_reservation_t){0};
        break;
      }
    }
    memset(state, 0, sizeof(*state));
  }
  critical_section_exit(&_hw_flash_lock);
}