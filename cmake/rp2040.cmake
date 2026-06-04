set(PICOFUSE_ARMGNU      "arm-none-eabi")
set(PICOFUSE_CPU         "cortex-m0plus")
set(PICOFUSE_CHIP_DIR    "rp2040")
set(PICOFUSE_FAMILY      "rp2040")
set(PICOFUSE_SRAM_ENTRY  "0x20040001")
set(PICOFUSE_SRAM_LOAD   "0x20040000")
set(PICOFUSE_FLASH_ENTRY "0x10000101")
set(PICOFUSE_FLASH_LOAD  "0x10000000")

if(NOT DEFINED PICOFUSE_XOSC_HZ)
    set(PICOFUSE_XOSC_HZ 12000000)
endif()
if(NOT DEFINED PICOFUSE_SYS_CLK_HZ)
    set(PICOFUSE_SYS_CLK_HZ ${PICOFUSE_XOSC_HZ})
endif()

set(PICOFUSE_IO_BANK0_IRQ 13)
set(PICOFUSE_NUM_GPIOS    30)

set(PICOFUSE_CHIP_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../src/rp2040/gpio.c"
    "${CMAKE_CURRENT_LIST_DIR}/../src/common/uart.c"
    "${CMAKE_CURRENT_LIST_DIR}/../src/common/clocks.c"
    "${CMAKE_CURRENT_LIST_DIR}/../src/common/irq.c"
    "${CMAKE_CURRENT_LIST_DIR}/../src/common/gpio_irq.c"
)

include(${CMAKE_CURRENT_LIST_DIR}/picofuse.cmake)
