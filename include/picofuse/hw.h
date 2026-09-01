/**
 * @file hw.h
 * @brief Hardware abstraction headers for on-board peripherals.
 * @defgroup Hardware Hardware
 * @ingroup Picofuse
 *
 * The Hardware module provides cross-platform abstractions for on-board
 * peripherals such as GPIO and ADC.
 */
#pragma once
#include "hw/adc.h"
#include "hw/deviceio.h"
#include "hw/gpio.h"
#include "hw/i2c.h"
#include "hw/init.h"
#include "hw/spi.h"
