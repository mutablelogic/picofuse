#include "pico/clocks.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/xosc.h"
#include "hardware/regs/clocks.h"

#define REG(addr) (*(volatile uint32_t *)(addr))

#define XOSC_CTRL     REG(XOSC_BASE   + XOSC_CTRL_OFFSET)
#define XOSC_STATUS   REG(XOSC_BASE   + XOSC_STATUS_OFFSET)
#define XOSC_STARTUP  REG(XOSC_BASE   + XOSC_STARTUP_OFFSET)

#define CLK_REF_CTRL     REG(CLOCKS_BASE + CLOCKS_CLK_REF_CTRL_OFFSET)
#define CLK_REF_SELECTED REG(CLOCKS_BASE + CLOCKS_CLK_REF_SELECTED_OFFSET)
#define CLK_SYS_CTRL     REG(CLOCKS_BASE + CLOCKS_CLK_SYS_CTRL_OFFSET)
#define CLK_SYS_SELECTED REG(CLOCKS_BASE + CLOCKS_CLK_SYS_SELECTED_OFFSET)
#define CLK_PERI_CTRL    REG(CLOCKS_BASE + CLOCKS_CLK_PERI_CTRL_OFFSET)

void clocks_init_xosc(uint xosc_hz) {
    // Set freq range and startup delay (~1ms), then enable
    XOSC_CTRL    = XOSC_CTRL_FREQ_RANGE_VALUE_1_15MHZ;
    XOSC_STARTUP = ((xosc_hz / 1000u) + 128u) / 256u;
    XOSC_CTRL    = XOSC_CTRL_FREQ_RANGE_VALUE_1_15MHZ |
                   (XOSC_CTRL_ENABLE_VALUE_ENABLE << 12u);
    while (!(XOSC_STATUS & XOSC_STATUS_STABLE_BITS));

    // Switch clk_ref to XOSC (glitchless mux — wait for selected bit)
    CLK_REF_CTRL = CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC
                   << CLOCKS_CLK_REF_CTRL_SRC_LSB;
    while (!(CLK_REF_SELECTED & (1u << CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC)));

    // Switch clk_sys to clk_ref
    CLK_SYS_CTRL = CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF
                   << CLOCKS_CLK_SYS_CTRL_SRC_LSB;
    while (!(CLK_SYS_SELECTED & 1u));

    // Enable clk_peri sourced from clk_sys
    CLK_PERI_CTRL = CLOCKS_CLK_PERI_CTRL_ENABLE_BITS |
                    (CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS
                     << CLOCKS_CLK_PERI_CTRL_AUXSRC_LSB);
}
