# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

add_subdirectory(drivers)
zephyr_include_directories(include)
zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
