#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(peripheral_lbs_1)

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.c
)

# Add PID_v1.h and pid.cpp
#target_sources(app PRIVATE
#  src/*.cpp
#)
#啟用cpp
#target_compile_features(app PRIVATE cxx_std_11)
#set_property(TARGET app PROPERTY CXX_STANDARD 11)
#set_property(TARGET app PROPERTY CXX_STANDARD_REQUIRED ON)
#target_include_directories(app PRIVATE include)

#target_include_directories(app PRIVATE
#  ${CMAKE_CURRENT_SOURCE_DIR}/src

# Preinitialization related to Thingy:53 DFU
target_sources_ifdef(CONFIG_BOARD_THINGY53_NRF5340_CPUAPP app PRIVATE
  boards/thingy53.c
)
target_sources_ifdef(CONFIG_BOARD_THINGY53_NRF5340_CPUAPP_NS app PRIVATE
  boards/thingy53.c
)


# NORDIC SDK APP END
zephyr_library_include_directories(.)
