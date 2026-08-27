#pragma once
#include <picofuse/sys/mutex.h>
#include <stdbool.h>

/** @brief Initializes the printf mutex. Returns false on allocation failure. */
bool _sys_printf_init(void);

/** @brief Releases the printf mutex, if initialized. */
void _sys_printf_exit(void);

/** @brief The printf mutex, guarding sys_vprintf() against concurrent
 * output. NULL until _sys_printf_init() succeeds. */
extern sys_mutex_t *_sys_printf_mutex;
