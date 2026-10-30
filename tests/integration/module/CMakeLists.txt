# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_INPUT_TEMP_LAYER_TOUCH_TEST)
    target_sources(app PRIVATE src/runtime_test.c)
endif()

if(CONFIG_ZMK_INPUT_TEMP_LAYER_TOUCH_CUSTOM_SETTINGS)
    zephyr_link_libraries(-Wl,--wrap=zmk_custom_setting_read)
endif()
