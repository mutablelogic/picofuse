#pragma once

#include <picofuse/hw/block.h>
#include <stdbool.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

typedef struct hw_block_callbacks_t {
  bool (*read)(const hw_block_t *block, void *userdata, size_t index,
               void *dst);
  bool (*erase)(hw_block_t *block, void *userdata, size_t index);
  bool (*write)(hw_block_t *block, void *userdata, size_t index,
                const void *src);
  void (*deinit)(hw_block_t *block, void *userdata);
} hw_block_callbacks_t;

struct hw_block_t {
  const hw_block_callbacks_t *callbacks;
  void *userdata;
  size_t count;
  size_t size;
  _Alignas(max_align_t) uint8_t context[BLOCK_CONTEXT_SIZE];
};

/**
 * @brief Claim a block handle from the fixed pool.
 *
 * The returned handle has a zeroed context buffer. @p callbacks must stay
 * valid until hw_block_deinit() releases the handle.
 */
hw_block_t *_hw_block_alloc_handle(const hw_block_callbacks_t *callbacks,
                                   void *userdata, size_t count, size_t size);

/** @brief Return a block handle's backend-private context, or NULL when the
 * handle is invalid. */
void *_hw_block_context(const hw_block_t *block);