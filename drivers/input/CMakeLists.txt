# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

if (CONFIG_ZMK_INPUT_PROCESSOR_TEMP_LAYER_TOUCH)
    zephyr_library()
    zephyr_library_sources(input_processor_temp_layer_touch.c)
endif()

if (CONFIG_ZMK_INPUT_TEMP_LAYER_TOUCH_CUSTOM_SETTINGS)
    # On the app target rather than this module's library: it includes
    # zmk/studio/custom.h, which pulls ZMK's nanopb-generated
    # proto/zmk/custom.pb.h, and that header is only on the app include path.
    target_sources(app PRIVATE input_processor_temp_layer_touch_custom_settings.c)
endif()
