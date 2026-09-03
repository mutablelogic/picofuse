/**
 * @file bme680.h
 * @brief Bosch BME680 environmental sensor interface.
 * @defgroup BME680 BME680
 * @ingroup Device
 */
#pragma once

#include <picofuse/hw.h>
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// CONSTANTS

/**
 * @def DEV_BME680_I2C_ADDR_PRIMARY
 * @ingroup BME680
 * @brief 7-bit I2C address when the SDO pin is tied low.
 */
#define DEV_BME680_I2C_ADDR_PRIMARY 0x76

/**
 * @def DEV_BME680_I2C_ADDR_SECONDARY
 * @ingroup BME680
 * @brief 7-bit I2C address when the SDO pin is tied high.
 */
#define DEV_BME680_I2C_ADDR_SECONDARY 0x77

/**
 * @def DEV_BME680_CHIP_ID
 * @ingroup BME680
 * @brief Expected value of the chip ID register, used by dev_bme680_init()
 * to confirm a BME680 is actually present before returning a handle.
 */
#define DEV_BME680_CHIP_ID 0x61

/**
 * @def DEV_BME680_REG_CHIP_ID
 * @ingroup BME680
 * @brief Register holding the chip ID (see DEV_BME680_CHIP_ID).
 */
#define DEV_BME680_REG_CHIP_ID 0xD0u

/**
 * @def DEV_BME680_REG_VARIANT_ID
 * @ingroup BME680
 * @brief Register holding the chip variant (see DEV_BME680_VARIANT_GAS_LOW,
 * DEV_BME680_VARIANT_GAS_HIGH).
 */
#define DEV_BME680_REG_VARIANT_ID 0xF0u

/**
 * @def DEV_BME680_REG_SOFT_RESET
 * @ingroup BME680
 * @brief Register that triggers a soft reset when DEV_BME680_SOFT_RESET_CMD
 * is written to it.
 */
#define DEV_BME680_REG_SOFT_RESET 0xE0u

/**
 * @def DEV_BME680_REG_FIELD0
 * @ingroup BME680
 * @brief First register of the sensor data field read by dev_bme680_read().
 */
#define DEV_BME680_REG_FIELD0 0x1Du

/**
 * @def DEV_BME680_REG_RES_HEAT0
 * @ingroup BME680
 * @brief First heater resistance register, for the gas heater profile.
 */
#define DEV_BME680_REG_RES_HEAT0 0x5Au

/**
 * @def DEV_BME680_REG_GAS_WAIT0
 * @ingroup BME680
 * @brief First heater duration register, for the gas heater profile.
 */
#define DEV_BME680_REG_GAS_WAIT0 0x64u

/**
 * @def DEV_BME680_REG_CTRL_GAS_0
 * @ingroup BME680
 * @brief Gas heater control register.
 */
#define DEV_BME680_REG_CTRL_GAS_0 0x70u

/**
 * @def DEV_BME680_REG_CTRL_GAS_1
 * @ingroup BME680
 * @brief Gas measurement/heater-profile-selection control register.
 */
#define DEV_BME680_REG_CTRL_GAS_1 0x71u

/**
 * @def DEV_BME680_REG_CTRL_HUM
 * @ingroup BME680
 * @brief Humidity oversampling control register.
 */
#define DEV_BME680_REG_CTRL_HUM 0x72u

/**
 * @def DEV_BME680_REG_CTRL_MEAS
 * @ingroup BME680
 * @brief Temperature/pressure oversampling and power-mode control register.
 */
#define DEV_BME680_REG_CTRL_MEAS 0x74u

/**
 * @def DEV_BME680_REG_CONFIG
 * @ingroup BME680
 * @brief IIR filter control register.
 */
#define DEV_BME680_REG_CONFIG 0x75u

/**
 * @def DEV_BME680_REG_COEFF1
 * @ingroup BME680
 * @brief First byte of the first calibration coefficient block.
 */
#define DEV_BME680_REG_COEFF1 0x8Au

/**
 * @def DEV_BME680_REG_COEFF2
 * @ingroup BME680
 * @brief First byte of the second calibration coefficient block.
 */
#define DEV_BME680_REG_COEFF2 0xE1u

/**
 * @def DEV_BME680_REG_COEFF3
 * @ingroup BME680
 * @brief First byte of the third calibration coefficient block.
 */
#define DEV_BME680_REG_COEFF3 0x00u

/**
 * @def DEV_BME680_SOFT_RESET_CMD
 * @ingroup BME680
 * @brief Value that triggers a soft reset when written to
 * DEV_BME680_REG_SOFT_RESET.
 */
#define DEV_BME680_SOFT_RESET_CMD 0xB6u

/**
 * @def DEV_BME680_VARIANT_GAS_LOW
 * @ingroup BME680
 * @brief Chip variant value read from DEV_BME680_REG_VARIANT_ID on earlier
 * BME680 units, using the "low" gas resistance compensation formula.
 */
#define DEV_BME680_VARIANT_GAS_LOW 0x00u

/**
 * @def DEV_BME680_VARIANT_GAS_HIGH
 * @ingroup BME680
 * @brief Chip variant value read from DEV_BME680_REG_VARIANT_ID on later
 * BME680 units, using the "high" gas resistance compensation formula.
 */
#define DEV_BME680_VARIANT_GAS_HIGH 0x01u

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Opaque BME680 handle.
 * @ingroup BME680
 */
typedef struct dev_bme680_t dev_bme680_t;

typedef enum {
  dev_bme680_oversampling_skip = 0,
  dev_bme680_oversampling_1x = 1,
  dev_bme680_oversampling_2x = 2,
  dev_bme680_oversampling_4x = 3,
  dev_bme680_oversampling_8x = 4,
  dev_bme680_oversampling_16x = 5,
} dev_bme680_oversampling_t;

typedef enum {
  dev_bme680_iir_filter_off = 0,
  dev_bme680_iir_filter_1 = 1,
  dev_bme680_iir_filter_3 = 2,
  dev_bme680_iir_filter_7 = 3,
  dev_bme680_iir_filter_15 = 4,
  dev_bme680_iir_filter_31 = 5,
  dev_bme680_iir_filter_63 = 6,
  dev_bme680_iir_filter_127 = 7,
} dev_bme680_iir_filter_t;

typedef enum {
  dev_bme680_heater_temp_200c = 200,
  dev_bme680_heater_temp_250c = 250,
  dev_bme680_heater_temp_300c = 300,
  dev_bme680_heater_temp_320c = 320,
  dev_bme680_heater_temp_350c = 350,
  dev_bme680_heater_temp_400c = 400,
} dev_bme680_heater_temp_t;

typedef enum {
  dev_bme680_heater_dur_50ms = 50,
  dev_bme680_heater_dur_100ms = 100,
  dev_bme680_heater_dur_120ms = 120,
  dev_bme680_heater_dur_150ms = 150,
  dev_bme680_heater_dur_200ms = 200,
} dev_bme680_heater_duration_t;

typedef enum {
  dev_bme680_ambient_temp_0c = 0,
  dev_bme680_ambient_temp_10c = 10,
  dev_bme680_ambient_temp_20c = 20,
  dev_bme680_ambient_temp_25c = 25,
  dev_bme680_ambient_temp_30c = 30,
  dev_bme680_ambient_temp_35c = 35,
  dev_bme680_ambient_temp_40c = 40,
} dev_bme680_ambient_temp_t;

typedef enum {
  dev_bme680_gas_disabled = 0,
  dev_bme680_gas_enabled = 1,
} dev_bme680_gas_mode_t;

/**
 * @brief BME680 measurement values.
 * @ingroup BME680
 */
typedef struct {
  float temperature_c;       ///< Temperature in degrees Celsius.
  float pressure_pa;         ///< Pressure in pascals.
  float humidity_pct;        ///< Relative humidity in percent.
  float gas_resistance_ohms; ///< Gas resistance in ohms.
} dev_bme680_data_t;

/**
 * @brief BME680 runtime configuration.
 * @ingroup BME680
 *
 * Oversampling/filter fields use strongly typed enums.
 */
typedef struct {
  dev_bme680_oversampling_t os_temp;
  dev_bme680_oversampling_t os_press;
  dev_bme680_oversampling_t os_hum;
  dev_bme680_iir_filter_t iir_filter;
  dev_bme680_heater_temp_t heater_temp_c;
  dev_bme680_heater_duration_t heater_duration_ms;
  dev_bme680_ambient_temp_t ambient_temp_c;
  dev_bme680_gas_mode_t gas_mode;
} dev_bme680_config_t;

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Fill a BME680 config struct with safe defaults.
 * @ingroup BME680
 * @param config Config structure to initialize.
 */
void dev_bme680_default_config(dev_bme680_config_t *config);

/**
 * @brief Initialize a BME680 sensor.
 * @ingroup BME680
 * @param device Device I/O handle to communicate over, from hw_i2c_init*()
 * or hw_spi_init*() (see hw/i2c.h, hw/spi.h).
 * @param config Optional configuration. Pass NULL for defaults.
 * @return BME680 handle, or NULL on failure. Release it with
 * dev_bme680_deinit().
 */
dev_bme680_t *dev_bme680_init(hw_deviceio_t *device,
                              const dev_bme680_config_t *config);

/**
 * @brief Deinitialize a BME680 sensor.
 * @ingroup BME680
 * @param bme680 BME680 handle.
 *
 * Releases `bme680` itself only - the hw_deviceio_t it was initialized with
 * is left open, since dev_bme680_init() doesn't take ownership of it.
 */
void dev_bme680_deinit(dev_bme680_t *bme680);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// PROPERTIES

/** @name Properties
 * @{ */

/**
 * @brief Get the detected chip ID.
 * @ingroup BME680
 * @param bme680 BME680 handle.
 * @return Chip ID, or 0 when handle is invalid.
 */
uint8_t dev_bme680_chip_id(const dev_bme680_t *bme680);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Read sensor data from BME680.
 * @ingroup BME680
 * @param bme680 BME680 handle.
 * @param data Structure to receive sensor values.
 * @retval true Read succeeded.
 * @retval false Read failed.
 */
bool dev_bme680_read(dev_bme680_t *bme680, dev_bme680_data_t *data);

/** @} */
