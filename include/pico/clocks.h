#pragma once

#include "pico/types.h"

// Switch clk_sys (and clk_peri) to the crystal oscillator.
// xosc_hz: crystal frequency in Hz (12000000 for all standard Pico boards).
// After this call PICOFUSE_SYS_CLK_HZ reflects the crystal frequency and
// UART / timing calculations become accurate.
void clocks_init_xosc(uint xosc_hz);
