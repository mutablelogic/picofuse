#pragma once
#include <stdint.h>

// Command bytes - see the ILI9341 datasheet section 8.2/8.3 for each
// register's full description. Names and values match the manufacturer's
// own section numbering (e.g. ILI9341_CASET is "8.2.20 Column Address Set
// (2Ah)").
#define ILI9341_SWRESET 0x01u
#define ILI9341_SLPOUT 0x11u
#define ILI9341_GAMMASET 0x26u
#define ILI9341_DISPON 0x29u
#define ILI9341_CASET 0x2Au
#define ILI9341_PASET 0x2Bu
#define ILI9341_RAMWR 0x2Cu
#define ILI9341_MADCTL 0x36u
#define ILI9341_VSCRSADD 0x37u
#define ILI9341_PIXFMT 0x3Au
#define ILI9341_FRMCTR1 0xB1u
#define ILI9341_DFUNCTR 0xB6u
#define ILI9341_PWCTR1 0xC0u
#define ILI9341_PWCTR2 0xC1u
#define ILI9341_VMCTR1 0xC5u
#define ILI9341_VMCTR2 0xC7u
#define ILI9341_GMCTRP1 0xE0u
#define ILI9341_GMCTRN1 0xE1u

// Memory Access Control (36h) bits - row/column order and color order for
// dev_ili9341_init()'s rotation handling.
#define ILI9341_MADCTL_MY 0x80u  // Row address order
#define ILI9341_MADCTL_MX 0x40u  // Column address order
#define ILI9341_MADCTL_MV 0x20u  // Row/column exchange
#define ILI9341_MADCTL_BGR 0x08u // Blue-Green-Red pixel order

// Reset pulse/settle timings (milliseconds) - generous relative to the
// datasheet's minimums, matching common driver practice rather than
// cutting it close.
#define ILI9341_RESET_PULSE_MS 5u
#define ILI9341_RESET_SETTLE_MS 150u
#define ILI9341_CMD_DELAY_MS 150u

// initcmd[] entries are {command, argc | ILI9341_CMD_DELAY_FLAG, args...}
// - a high bit on the length byte means "wait ILI9341_CMD_DELAY_MS after
// this command", used after SWRESET/SLPOUT/DISPON where the controller
// needs settling time the SPI bus itself gives no other signal for.
#define ILI9341_CMD_DELAY_FLAG 0x80u
#define ILI9341_CMD_ARGC_MASK 0x7Fu

// Startup register sequence, adapted from the manufacturer's own
// application notes - most of these are documented as "for optimum
// performance" tuning registers (power/gamma/VCOM) rather than ones this
// driver has independently derived values for.
static const uint8_t _ili9341_initcmd[] = {
    0xEF, 3, 0x03, 0x80, 0x02,
    0xCF, 3, 0x00, 0xC1, 0x30,
    0xED, 4, 0x64, 0x03, 0x12, 0x81,
    0xE8, 3, 0x85, 0x00, 0x78,
    0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
    0xF7, 1, 0x20,
    0xEA, 2, 0x00, 0x00,
    ILI9341_PWCTR1, 1, 0x23,
    ILI9341_PWCTR2, 1, 0x10,
    ILI9341_VMCTR1, 2, 0x3E, 0x28,
    ILI9341_VMCTR2, 1, 0x86,
    ILI9341_VSCRSADD, 1, 0x00,
    ILI9341_PIXFMT, 1, 0x55,
    ILI9341_FRMCTR1, 2, 0x00, 0x18,
    ILI9341_DFUNCTR, 3, 0x08, 0x82, 0x27,
    0xF2, 1, 0x00,
    ILI9341_GAMMASET, 1, 0x01,
    ILI9341_GMCTRP1, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
    0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
    ILI9341_GMCTRN1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
    0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
    ILI9341_SLPOUT, ILI9341_CMD_DELAY_FLAG,
    ILI9341_DISPON, ILI9341_CMD_DELAY_FLAG,
    0x00, // end of list
};
