/**
 * @file fs.h
 * @brief File system abstraction layer for lightweight embedded & host storage.
 *
 * @defgroup FileSystem File System Runtime
 * @ingroup Picofuse
 *
 * High–level API wrapping an underlying littlefs-based implementation that can
 * operate either purely in RAM, Flash or backed by a host file (persisted
 * across runs).
 *
 * Thread-safety: Functions are NOT thread-safe; the
 * caller must serialize access to a volume and file. Returned pointers
 * (paths/names) reference caller-managed or internal static memory and must not
 * be freed.
 */
#pragma once
#include "fs/types.h"
#include "fs/file.h"
#include "fs/volume.h"
