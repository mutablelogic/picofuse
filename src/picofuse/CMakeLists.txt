
# sys is the foundational layer for the system-specific implementations.
add_subdirectory(sys/${SYSTEM_NAME_LOWER})

# hw provides hardware peripheral abstractions (GPIO, etc.)
add_subdirectory(hw/${SYSTEM_NAME_LOWER})

# pix provides shared pixel/display types, drawn on by dev's display backends
add_subdirectory(pix)

# dev provides drivers for external peripherals
add_subdirectory(dev)

# hid provides Human Interface Device (HID) support
add_subdirectory(hid)

# app wraps the sys/hw/hid lifecycle behind a single app_main() entry point
add_subdirectory(app)

# net provides TCP/UDP sockets
add_subdirectory(net/${SYSTEM_NAME_LOWER})
