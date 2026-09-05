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

// Verified against the real STMPE610 datasheet (ST Doc ID 15432 Rev 4,
// section 9) and Adafruit's own production device-tree overlay
// (pitft28-resistive-overlay.dts: st,sample-time=4, st,mod-12b=1,
// st,ref-sel=0) - SAMPLE_TIME=100 (80 ADC clocks), MOD_12B=1 (12-bit),
// REF_SEL=0 (internal reference).
#define STMPE610_REG_ADC_CTRL1 0x20u
#define STMPE610_ADC_CTRL1_VALUE 0x48u

#define STMPE610_REG_ADC_CTRL2 0x21u
#define STMPE610_ADC_CTRL2_6_5MHZ 0x02u

#define STMPE610_REG_TSC_CTRL 0x40u
#define STMPE610_TSC_CTRL_EN 0x01u
#define STMPE610_TSC_CTRL_XYZ 0x00u
#define STMPE610_TSC_CTRL_TOUCHING 0x80u // Live "currently touched" status bit

// Also verified against Adafruit's overlay: st,ave-ctrl=3 (8 samples),
// st,touch-det-delay=4 (1 ms), st,settling=2 (500 us).
#define STMPE610_REG_TSC_CFG 0x41u
#define STMPE610_TSC_CFG_8SAMPLE 0xC0u
#define STMPE610_TSC_CFG_DELAY_1MS 0x20u
#define STMPE610_TSC_CFG_SETTLE_500US 0x02u

#define STMPE610_REG_FIFO_TH 0x4Au

#define STMPE610_REG_FIFO_STA 0x4Bu
#define STMPE610_FIFO_STA_RESET 0x01u
#define STMPE610_FIFO_STA_EMPTY 0x20u

#define STMPE610_REG_TSC_DATA_NONINC 0x57u // 4 bytes: X/Y/Z, FIFO pop on read
#define STMPE610_TSC_DATA_LEN 4u

#define STMPE610_REG_TSC_FRACTION_Z 0x56u

// Matches Adafruit's overlay (st,i-drive=0): 20 mA, not the 50 mA this
// project's init previously used.
#define STMPE610_REG_TSC_I_DRIVE 0x58u
#define STMPE610_TSC_I_DRIVE_20MA 0x00u

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
