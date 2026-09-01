#pragma once
#include <stddef.h>
#include <picofuse/sys/debugf.h>

#define CYW43_PRINTF(...) sys_debugf("wifi", __VA_ARGS__)
