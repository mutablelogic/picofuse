/**
 * @file sys.h
 * @brief System abstraction headers and process lifecycle
 * hooks.
 * @defgroup System System Abstractions
 * @ingroup Picofuse
 *
 * The System module provides the runtime foundation used by higher-level
 * Picofuse components. It includes cross-platform abstractions for memory,
 * threading, synchronization, timing, event queues, runloops, diagnostics,
 * environment metadata, and process control.
 */
#pragma once
#include "sys/arena.h"
#include "sys/assert.h"
#include "sys/atomic.h"
#include "sys/cond.h"
#include "sys/date.h"
#include "sys/debugf.h"
#include "sys/env.h"
#include "sys/event.h"
#include "sys/halt.h"
#include "sys/hash.h"
#include "sys/init.h"
#include "sys/io.h"
#include "sys/mem.h"
#include "sys/mutex.h"
#include "sys/panicf.h"
#include "sys/printf.h"
#include "sys/random.h"
#include "sys/runloop.h"
#include "sys/rune.h"
#include "sys/scanner.h"
#include "sys/sleep.h"
#include "sys/string.h"
#include "sys/thread.h"
#include "sys/timer.h"
#include "sys/timestamp.h"
#include "sys/waitgroup.h"
