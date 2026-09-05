
# sys is the foundational layer for the system-specific implementations.
add_subdirectory(sys/${SYSTEM_NAME_LOWER})

# hw provides hardware peripheral abstractions (GPIO, etc.)
add_subdirectory(hw/${SYSTEM_NAME_LOWER})

# dev provides drivers for external peripherals
add_subdirectory(dev)

# hid provides Human Interface Device (HID) support
add_subdirectory(hid)
