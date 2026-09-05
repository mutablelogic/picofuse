/**
 * @file dev.h
 * @brief Driver headers for external peripherals.
 * @defgroup Device Device
 * @ingroup Picofuse
 *
 * The Device module provides drivers for external peripherals connected
 * over the buses exposed by the Hardware module (see hw.h). Drivers are
 * built against the bus-agnostic hw_deviceio_t interface (see
 * hw/deviceio.h), so the same driver works unchanged whether the peripheral
 * is wired over I2C or SPI.
 */
#pragma once
#include "dev/bme680.h"
#include "dev/ft6236.h"
#include "dev/ili9341.h"
#include "dev/stmpe610.h"
