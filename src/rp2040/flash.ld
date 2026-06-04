MEMORY {
    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 2048K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 264K
}

SECTIONS {
    /* Boot stage 2: 256 bytes at the very start of flash.
       The ROM verifies the CRC32 in the last 4 bytes before executing. */
    .boot2 : {
        *(.boot2)
    } > FLASH

    /* Vector table and code follow immediately after boot stage 2 */
    .vectors : {
        *(.vectors)
    } > FLASH

    .text : {
        *(.text*)
        *(.rodata*)
    } > FLASH

    /* .data lives in SRAM at runtime but is stored in flash after .text.
       _reset copies it to SRAM before calling main(). */
    .data : {
        _data_start = .;
        *(.data*)
        _data_end = .;
    } > SRAM AT> FLASH

    _data_load = LOADADDR(.data);

    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    } > SRAM

    _stack_top = ORIGIN(SRAM) + LENGTH(SRAM);
}
