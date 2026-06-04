# Board: Pimoroni Pico LiPo 2 XL W (RP2350B, CYW43 wireless, 16MB W25Q080 flash)
#
# Note: There is no directly accessible LED pin. The LED is on the CYW43
# wireless chip and requires SPI. For blink examples, wire an external LED
# (with resistor) to a free GPIO and set PICOFUSE_LED_PIN accordingly.
# GPIO 0 is a safe default (not connected to any on-board peripheral).

set(PICOFUSE_BOOT2    "boot2_w25q080")
set(PICOFUSE_LDSCRIPT "${CMAKE_CURRENT_LIST_DIR}/../src/rp2350/flash_16m.ld")
set(PICOFUSE_STARTUP  "${CMAKE_CURRENT_LIST_DIR}/../src/rp2350/start.s")

if(NOT DEFINED PICOFUSE_LED_PIN)
    set(PICOFUSE_LED_PIN 0)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/rp2350.cmake)
