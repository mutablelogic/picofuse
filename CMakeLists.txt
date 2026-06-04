cmake_minimum_required(VERSION 3.20)
project(picofuse NONE)

set(PICOFUSE_BOARD "pico" CACHE STRING "Target board")
include(cmake/${PICOFUSE_BOARD}.cmake)

add_subdirectory(examples/blink)
