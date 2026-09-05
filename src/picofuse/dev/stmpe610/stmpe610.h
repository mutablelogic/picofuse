#pragma once
#include <picofuse/hw.h>
#include <stdbool.h>

///////////////////////////////////////////////////////////////////////////////
// REGISTERS
//

#define STMPE610_REG_CHIP_ID 0x00u // 16-bit, MSB first: 0x08 0x11
#define STMPE610_CHIP_ID_VALUE 0x0811u

#define STMPE610_REG_SYS_CTRL1 0x03u
#define STMPE610_SYS_CTRL1_RESET 0x02u

#define STMPE610_REG_SYS_CTRL2 0x04u

#define STMPE610_REG_INT_CTRL 0x09u
#define STMPE610_INT_CTRL_POL_HIGH 0x04u
#define STMPE610_INT_CTRL_ENABLE 0x01u

#define STMPE610_REG_INT_EN 0x0Au
#define STMPE610_INT_EN_TOUCHDET 0x01u

#define STMPE610_REG_INT_STA 0x0Bu

#define STMPE610_REG_ADC_CTRL1 0x20u
#define STMPE610_ADC_CTRL1_10BIT 0x00u

#define STMPE610_REG_ADC_CTRL2 0x21u
#define STMPE610_ADC_CTRL2_6_5MHZ 0x02u

#define STMPE610_REG_TSC_CTRL 0x40u
#define STMPE610_TSC_CTRL_EN 0x01u
#define STMPE610_TSC_CTRL_XYZ 0x00u
#define STMPE610_TSC_CTRL_TOUCHING 0x80u // Live "currently touched" status bit

#define STMPE610_REG_TSC_CFG 0x41u
#define STMPE610_TSC_CFG_4SAMPLE 0xC0u
#define STMPE610_TSC_CFG_DELAY_1MS 0x20u
#define STMPE610_TSC_CFG_SETTLE_5MS 0x04u

#define STMPE610_REG_FIFO_TH 0x4Au

#define STMPE610_REG_FIFO_STA 0x4Bu
#define STMPE610_FIFO_STA_RESET 0x01u
#define STMPE610_FIFO_STA_EMPTY 0x20u

#define STMPE610_REG_TSC_DATA_NONINC 0x57u // 4 bytes: X/Y/Z, FIFO pop on read
#define STMPE610_TSC_DATA_LEN 4u

#define STMPE610_REG_TSC_FRACTION_Z 0x56u

#define STMPE610_REG_TSC_I_DRIVE 0x58u
#define STMPE610_TSC_I_DRIVE_50MA 0x01u

// SPI framing: bit7 of the register byte selects read (set) vs write
// (clear) - the generic hw_spi backend sends the register byte to
// hw_deviceio_read_reg()/write_reg() exactly as given, with no read/write
// bit of its own, so this driver must set it itself.
#define STMPE610_READ_BIT 0x80u

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct dev_stmpe610_t {
  hw_deviceio_t *device;
  hw_gpio_t *int_pin;
  bool irq_active_low;
  bool had_touch;
  uint16_t last_x;
  uint16_t last_y;
  uint8_t last_z;
};
