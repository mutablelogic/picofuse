#pragma once
#include <picofuse/sys.h>
#include <pthread.h>

struct sys_mutex_t {
  pthread_mutex_t pmutex;
  bool init;
};

/** @brief Checks if a mutex is valid and initialized. */
static inline bool _sys_mutex_valid(const sys_mutex_t *mutex) {
  return mutex != NULL && mutex->init;
}
