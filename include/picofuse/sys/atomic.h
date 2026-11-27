/**
 * @file sys/atomic.h
 * @brief Defines atomic uint32_t operations for shared state.
 * @defgroup SystemAtomic Atomic Operations
 * @ingroup SystemSync
 *
 * Atomic values that can be safely updated across threads.
 *
 * A uint32_t value maintained safely across threads, with get, set,
 * increment, decrement and compare-and-swap operations. The increment
 * and decrement operations return the new value.
 *
 * sys_atomic_get() acquires and sys_atomic_set() releases, so a thread
 * which sets a value after writing other data, and a thread which gets
 * that value and then reads the data, see the data as it was written.
 * sys_atomic_compare_swap() does both. Increment, decrement and the bit
 * operations are relaxed, which is enough for counts and flags but orders
 * nothing else.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Some embedded targets (e.g., ARMv6-M / RP2040) have no lock-free atomics.
// Clang then warns that builtins may not be lock-free (-Watomic-alignment).
// We accept the fallback and suppress the warning locally.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Watomic-alignment"
#endif

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////

/**
 * Underlying atomic uint32_t value
 * @ingroup SystemAtomic
 * @headerfile atomic.h picofuse/sys.h
 *
 * Do not access the value directly; use the sys_atomic_* APIs.
 * in order to maintain thread safety.
 */
typedef struct sys_atomic_t {
  uint32_t value; ///< Raw atomic storage; use sys_atomic_* APIs instead.
} sys_atomic_t __attribute__((aligned(4)));

/**
 * @brief Initialize an atomic with an initial value.
 * @ingroup SystemAtomic
 *
 * @param a Pointer to the atomic value to initialize (must be non-NULL).
 * @param initial The initial 32-bit value to store.
 *
 * This performs a relaxed store; it does not impose ordering with
 * other memory operations. Use only as an initialization step before
 * the atomic is shared with other threads.
 */
static inline void sys_atomic_init(sys_atomic_t *a, uint32_t initial) {
  __atomic_store_n(&a->value, initial, __ATOMIC_RELAXED);
}

/**
 * @brief Load the current value atomically.
 * @ingroup SystemAtomic
 *
 * @param a Pointer to the atomic value (must be non-NULL).
 * @return The current 32-bit value.
 *
 * This is an acquire load: what the thread which set the value wrote
 * before it did is seen by reads after this.
 */
static inline uint32_t sys_atomic_get(const sys_atomic_t *a) {
  return __atomic_load_n(&a->value, __ATOMIC_ACQUIRE);
}

/**
 * @brief Store a new value atomically.
 * @ingroup SystemAtomic
 *
 * @param a Pointer to the atomic value (must be non-NULL).
 * @param v The value to store.
 *
 * This is a release store: what this thread wrote before it is seen by a
 * thread which gets the value afterwards.
 */
static inline void sys_atomic_set(sys_atomic_t *a, uint32_t v) {
  __atomic_store_n(&a->value, v, __ATOMIC_RELEASE);
}

/**
 * @brief Atomically replace the value, if it is what's expected.
 * @ingroup SystemAtomic
 *
 * @param a Pointer to the atomic value (must be non-NULL).
 * @param expected Pointer to the value expected (must be non-NULL). If the
 * value is something else, that's stored here.
 * @param desired The value to store if the value is *expected.
 * @return true if the value was *expected and is now desired, false if it
 * wasn't and nothing was stored.
 *
 * Acquires and releases when it succeeds, and acquires when it doesn't, so
 * it can guard other data, as a lock does. On targets without atomic
 * instructions (RP2040), it is implemented with a spin lock.
 */
static inline bool sys_atomic_compare_swap(sys_atomic_t *a, uint32_t *expected,
                                           uint32_t desired) {
  return __atomic_compare_exchange_n(&a->value, expected, desired, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

/**
 * @brief Atomically increment and return the new value.
 * @ingroup SystemAtomic
 *
 * @param a Pointer to the atomic value (must be non-NULL).
 * @return The incremented value after the operation.
 *
 * This uses relaxed ordering and wraps modulo 2^32.
 */
static inline uint32_t sys_atomic_inc(sys_atomic_t *a) {
  return __atomic_add_fetch(&a->value, 1, __ATOMIC_RELAXED);
}

/**
 * @brief Atomically decrement and return the new value.
 * @ingroup SystemAtomic
 *
 * @param a Pointer to the atomic value (must be non-NULL).
 * @return The decremented value after the operation.
 *
 * This uses relaxed ordering and wraps modulo 2^32.
 */
static inline uint32_t sys_atomic_dec(sys_atomic_t *a) {
  return __atomic_sub_fetch(&a->value, 1, __ATOMIC_RELAXED);
}

/**
 * @brief Atomically set bits (OR with mask).
 * @ingroup SystemAtomic
 *
 * @param a Pointer to the atomic value (must be non-NULL).
 * @param mask Bit mask of bits to set.
 *
 * Performs a relaxed fetch-or; no ordering is implied.
 */
static inline void sys_atomic_set_bits(sys_atomic_t *a, uint32_t mask) {
  (void)__atomic_fetch_or(&a->value, mask, __ATOMIC_RELAXED);
}

/**
 * @brief Atomically clear bits (AND with ~mask).
 * @ingroup SystemAtomic
 *
 * @param a Pointer to the atomic value (must be non-NULL).
 * @param mask Bit mask of bits to clear.
 *
 * Performs a relaxed fetch-and with the complement of mask; no ordering is
 * implied.
 */
static inline void sys_atomic_clear_bits(sys_atomic_t *a, uint32_t mask) {
  (void)__atomic_fetch_and(&a->value, ~mask, __ATOMIC_RELAXED);
}

///////////////////////////////////////////////////////////////////////////////

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#ifdef __cplusplus
}
#endif
