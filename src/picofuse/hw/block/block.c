#include <picofuse/hw.h>

#include "private.h"
#include <stddef.h>
#include <string.h>

#ifdef SYSTEM_NAME_PICO
#include "../../sys/pico/sync.h"
#define _HW_BLOCK_LOCK() _sys_sync_pool_lock()
#define _HW_BLOCK_UNLOCK() _sys_sync_pool_unlock()
#else
#define _HW_BLOCK_LOCK()
#define _HW_BLOCK_UNLOCK()
#endif

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static hw_block_t _hw_block_pool[BLOCK_MAX_CAPACITY] = {0};

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

static bool _hw_block_allocated(const hw_block_t *block) {
  return block != NULL && block->callbacks != NULL;
}

static bool _hw_block_valid(const hw_block_t *block) {
  return _hw_block_allocated(block);
}

static bool _hw_block_belongs(const hw_block_t *block) {
  if (block == NULL) {
    return false;
  }
  uintptr_t address = (uintptr_t)block;
  uintptr_t first = (uintptr_t)&_hw_block_pool[0];
  uintptr_t last = (uintptr_t)&_hw_block_pool[BLOCK_MAX_CAPACITY];
  return address >= first && address < last &&
         (address - first) % sizeof(_hw_block_pool[0]) == 0;
}

hw_block_t *_hw_block_alloc_handle(const hw_block_callbacks_t *callbacks,
                                   void *userdata, size_t count, size_t size) {
  if (callbacks == NULL || count == 0u || size == 0u) {
    return NULL;
  }

  _HW_BLOCK_LOCK();
  for (size_t i = 0; i < BLOCK_MAX_CAPACITY; i++) {
    hw_block_t *block = &_hw_block_pool[i];
    if (block->callbacks == NULL) {
      *block = (hw_block_t){0};
      block->userdata = userdata;
      block->count = count;
      block->size = size;
      block->callbacks = callbacks;
      _HW_BLOCK_UNLOCK();
      return block;
    }
  }
  _HW_BLOCK_UNLOCK();
  return NULL;
}

void *_hw_block_context(const hw_block_t *block) {
  return _hw_block_allocated(block) ? (void *)block->context : NULL;
}

size_t hw_block_count(const hw_block_t *block) {
  return _hw_block_valid(block) ? block->count : 0u;
}

size_t hw_block_size(const hw_block_t *block) {
  return _hw_block_valid(block) ? block->size : 0u;
}

bool hw_block_read(const hw_block_t *block, size_t index, void *dst) {
  if (!_hw_block_valid(block) || dst == NULL ||
      block->callbacks->read == NULL) {
    return false;
  }

  return block->callbacks->read(block, block->userdata, index, dst);
}

bool hw_block_erase(hw_block_t *block, size_t index) {
  if (!_hw_block_valid(block) || block->callbacks->erase == NULL) {
    return false;
  }

  return block->callbacks->erase(block, block->userdata, index);
}

bool hw_block_write(hw_block_t *block, size_t index, const void *src) {
  if (!_hw_block_valid(block) || src == NULL ||
      block->callbacks->write == NULL) {
    return false;
  }

  return block->callbacks->write(block, block->userdata, index, src);
}

void hw_block_deinit(hw_block_t *block) {
  if (!_hw_block_allocated(block)) {
    return;
  }

  if (block->callbacks->deinit != NULL) {
    block->callbacks->deinit(block, block->userdata);
  }
  if (_hw_block_belongs(block)) {
    _HW_BLOCK_LOCK();
    *block = (hw_block_t){0};
    _HW_BLOCK_UNLOCK();
  }
}