# NOTICE

<!-- @brief: Third-party notices and licenses for components picofuse builds against or links. -->

picofuse
Copyright the picofuse contributors

This product is licensed under the Apache License, Version 2.0 (see
[LICENSE](LICENSE)). It builds against, and for Pico targets statically
links, third-party components pulled in via the `third_party/pico-sdk`
submodule. Their licenses are reproduced in full alongside their source,
and summarized here:

## Raspberry Pi Pico SDK

- Path: `third_party/pico-sdk`
- Copyright: Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd.
- License: BSD-3-Clause (see `third_party/pico-sdk/LICENSE.TXT`)

## lwIP

- Path: `third_party/pico-sdk/lib/lwip`
- Copyright: Copyright (c) 2001, 2002 Swedish Institute of Computer Science.
- License: Modified BSD License (see `third_party/pico-sdk/lib/lwip/COPYING`)

## cyw43-driver

- Path: `third_party/pico-sdk/lib/cyw43-driver`
- Copyright: Copyright (C) 2019-2022 George Robotics Pty Ltd
- License: BSD-3-Clause, personal/non-commercial use only (see
  `third_party/pico-sdk/lib/cyw43-driver/LICENSE`); when built for
  RP2040/RP2350 targets (as this project does), the separate `LICENSE.RP`
  grant applies instead, permitting use with Raspberry Pi Ltd semiconductor
  devices (see `third_party/pico-sdk/lib/cyw43-driver/LICENSE.RP`)

## mbedTLS

- Path: `third_party/pico-sdk/lib/mbedtls`
- Copyright: Copyright The Mbed TLS Contributors
- License: Dual Apache-2.0 OR GPL-2.0-or-later (see
  `third_party/pico-sdk/lib/mbedtls/LICENSE`); picofuse uses it under
  Apache-2.0

## TinyUSB

- Path: `third_party/pico-sdk/lib/tinyusb`
- Copyright: Copyright (c) 2018, hathach (tinyusb.org)
- License: MIT (see `third_party/pico-sdk/lib/tinyusb/LICENSE`)

## BTstack

- Path: `third_party/pico-sdk/lib/btstack`
- Copyright: Copyright (C) 2009 BlueKitchen GmbH
- License: BSD-3-Clause, personal/non-commercial use only (see
  `third_party/pico-sdk/lib/btstack/LICENSE`); commercial licensing is
  available directly from BlueKitchen GmbH

---

Not all of the above are linked into every build - cyw43-driver and lwIP
are only pulled in for Wi-Fi-capable boards (`pico_w`, `pico2_w`), and
mbedTLS, TinyUSB and BTstack are vendored as submodules for planned future
use but are not yet linked by this project's build.
