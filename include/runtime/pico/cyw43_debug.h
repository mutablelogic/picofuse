#pragma once
#include <picofuse/sys/debugf.h>
#include <picofuse/sys/mem.h>
#include <stddef.h>

#define CYW43_PRINTF(...) sys_debugf("wifi", __VA_ARGS__)
#define cyw43_malloc sys_malloc
#define cyw43_free sys_free

// lwIP's own debug/assert output. LWIP_PLATFORM_DIAG is called as
// LWIP_PLATFORM_DIAG(("format", args...)) - the extra parens make x a
// single, already-parenthesized argument list here. lwIP's heap use isn't
// hooked here: MEM_LIBC_MALLOC is off (see lwipopts.h), so it allocates
// from its own static pools and never calls malloc()/free() at all.
#define _lwip_diag(format, ...) sys_debugf("lwip", format, ##__VA_ARGS__)
#define LWIP_PLATFORM_DIAG(x) do { _lwip_diag x; } while (0)
