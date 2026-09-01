/**
 * @file hw/init.h
 * @ingroup Hardware
 * @brief Hardware initialization, cleanup and polling hooks.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @name Lifecycle
 * @{ */

/**
 * @brief Initializes the hardware system on startup.
 * @ingroup Hardware
 */
void hw_init(void);

/**
 * @brief Cleans up the hardware system on shutdown.
 * @ingroup Hardware
 */
void hw_exit(void);

/**
 * @brief Occasional polling function for the hardware system.
 * @ingroup Hardware
 */
void hw_poll(void);

/** @} */

#ifdef __cplusplus
}
#endif
