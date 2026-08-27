# picofuse

## Requirements

- The `third_party/pico-sdk` submodule, initialized recursively (pico-sdk
  itself pulls in further submodules — tinyusb, btstack, cyw43-driver, lwip,
  mbedtls):

  ```sh
  git submodule update --init --recursive
  ```

- An ARM GNU Toolchain (`arm-none-eabi-gcc` and friends) on `PATH`.
- CMake 3.20+.
- Python 3 (used by pico-sdk's own build for code generation and
  `picotool`).

## Purpose

This repository builds a standalone, per-board "picofuse" installation —
a compiled `libpicosdk.a` (plus the `picofuse/sys` runtime layer) together
with its headers and `pkg-config` metadata, installed under a plain
directory prefix (e.g. `/opt/picofuse/<board>`). A third party who only has
that installed prefix — not this repository, not the pico-sdk source tree —
can build a real `.elf` for that board using nothing but `pkg-config` and
the ARM GNU toolchain:

```sh
export PKG_CONFIG_PATH=/opt/picofuse/<board>/lib/pkgconfig
arm-none-eabi-gcc $(pkg-config --cflags picosdk) -c app.c -o app.o
arm-none-eabi-gcc app.o $(pkg-config --libs picosdk) -o app.elf
```

## Building and installing a library for each board

Define PICO_BOARD for the target board/chip combination.

```sh
# Pico (RP2040)
cmake -S . -B build-pico -DPICO_BOARD=pico
cmake --build build-pico --target picosdk_static picofuse_sys_pico_static
cmake --install build-pico --prefix /opt/picofuse/pico

# Pico W (RP2040 + CYW43 Wi-Fi/Bluetooth)
cmake -S . -B build-pico_w -DPICO_BOARD=pico_w
cmake --build build-pico_w --target picosdk_static picofuse_sys_pico_static
cmake --install build-pico_w --prefix /opt/picofuse/pico_w

# Pico 2 (RP2350)
cmake -S . -B build-pico2 -DPICO_BOARD=pico2
cmake --build build-pico2 --target picosdk_static picofuse_sys_pico_static
cmake --install build-pico2 --prefix /opt/picofuse/pico2

# Pico 2 W (RP2350 + CYW43)
cmake -S . -B build-pico2_w -DPICO_BOARD=pico2_w
cmake --build build-pico2_w --target picosdk_static picofuse_sys_pico_static
cmake --install build-pico2_w --prefix /opt/picofuse/pico2_w
```

Installing produces, under each prefix:

- `lib/libpicosdk.a`, `lib/libpicofuse_sys_pico.a`
- `include/` — every SDK module's headers merged into one consolidated tree,
  plus `picofuse/` and `runtime/`
- `lib/pkgconfig/picosdk.pc`, `lib/pkgconfig/picofuse_sys_pico.pc`
  (the latter `Requires: picosdk`, so pulling it in via `pkg-config` also
  pulls in picosdk's own flags)
- `lib/picosdk/src/` — the linker script fragments the `.pc` file's `Libs`
  point at
