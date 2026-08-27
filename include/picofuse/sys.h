/**
 * @file sys.h
 * @brief Core system abstraction headers and process lifecycle
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
#include "sys/assert.h"
#include "sys/debugf.h"
#include "sys/init.h"
#include "sys/mutex.h"
#include "sys/panicf.h"
#include "sys/printf.h"
#include "sys/sleep.h"
#include "sys/timestamp.h"
