# Board: Pimoroni Presto (RP2350B, CYW43 wireless, 16MB W25Q080 flash)
# No direct LED. Buzzer on GPIO 43 — used as the default output pin so
# blink examples produce an audible click rather than a visible flash.
set(PICOFUSE_BOOT2    "boot2_w25q080")
set(PICOFUSE_LDSCRIPT "${CMAKE_CURRENT_LIST_DIR}/../src/rp2350/flash_16m.ld")
set(PICOFUSE_STARTUP  "${CMAKE_CURRENT_LIST_DIR}/../src/rp2350/start.s")

if(NOT DEFINED PICOFUSE_LED_PIN)
    set(PICOFUSE_LED_PIN 43)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/rp2350.cmake)
