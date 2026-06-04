# Board: Pimoroni PicoLipo 4MB (RP2040, W25Q080 4MB flash, LED on GPIO25)
set(PICOFUSE_BOOT2    "boot2_w25q080")
set(PICOFUSE_LDSCRIPT "${CMAKE_CURRENT_LIST_DIR}/../src/rp2040/flash_4m.ld")
set(PICOFUSE_STARTUP  "${CMAKE_CURRENT_LIST_DIR}/../src/rp2040/start.s")

if(NOT DEFINED PICOFUSE_LED_PIN)
    set(PICOFUSE_LED_PIN 25)
endif()
if(NOT DEFINED PICOFUSE_USER_SW_PIN)
    set(PICOFUSE_USER_SW_PIN 23)
endif()
if(NOT DEFINED PICOFUSE_USER_SW_PULL)
    set(PICOFUSE_USER_SW_PULL none)  # board has external pull-up
endif()

include(${CMAKE_CURRENT_LIST_DIR}/rp2040.cmake)
