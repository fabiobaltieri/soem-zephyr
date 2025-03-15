cmake_minimum_required(VERSION 3.13.1)

set(BOARD nucleo_h745zi_q/stm32h745xx/m7)
set(ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(soem_zephyr)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
