#pragma once
#include <picofuse/net.h>
#include <stdbool.h>

///////////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

/** @brief Services the active net_mqtt_t singleton, if any - called from
 * net_poll() (poll.c). Not part of the public API - see net_mqtt_t's own
 * doc on why there's no public net_mqtt_poll().
 * @return true if a handle was active and got a chance to process
 * whatever was pending.
 */
bool _net_mqtt_poll(void);
