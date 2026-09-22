/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The custom-settings namespace this module publishes under.
 *
 * A setting is dropped with -ENOENT unless its subsystem is registered, and a
 * client groups the list it renders by subsystem, so this is also the heading
 * a person reads. It is short because it is spent, not read: the stored
 * settings name is "custom_settings/<subsystem>/<key>" against Zephyr's
 * 64-byte SETTINGS_MAX_NAME_LEN, so every character here is taken from every
 * node name in every board that uses this module. "tlt" is the module's
 * initials, temp-layer-touch, as "a2r" is abs2rel's.
 */

#pragma once

#include <zephyr/devicetree.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

/*
 * The identifier is spelled once, as a token.
 *
 * custom-settings takes the subsystem as a string in every setting it
 * registers, while ZMK's ZMK_RPC_CUSTOM_SUBSYSTEM takes it as a token and
 * stringifies it. Spelling it twice is a silent failure if the two ever
 * disagree: every setting is then dropped with -ENOENT because no subsystem of
 * its name is registered. So the token is the definition and the string is
 * derived from it.
 */
#define ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM_TOKEN amgskobo__tlt
#define ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM STRINGIFY(ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM_TOKEN)

/*
 * A setting key is the owning node's devicetree name, then the field:
 *
 *     edge_scroll.enabled
 *     edge_scroll.width
 *
 * The node name is DEVICE_DT_NAME(), the same string a view drawing a
 * listener's chain gets from each stage's device, so a stage's settings are
 * the ones whose key starts with its device name.
 */
#define ZMK_INPUT_TEMP_LAYER_TOUCH_SETTING_KEY(n, field) DT_NODE_FULL_NAME(DT_DRV_INST(n)) "." field

/*
 * The name a setting is stored under, which is longer than its key:
 * custom-settings prefixes its own subtree and the subsystem before saving.
 */
#define ZMK_INPUT_TEMP_LAYER_TOUCH_STORAGE_NAME(n, field)                                          \
    "custom_settings/" ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM                                        \
    "/" ZMK_INPUT_TEMP_LAYER_TOUCH_SETTING_KEY(n, field)

/*
 * Fail by name, at build time, when a node cannot fit a settings key.
 *
 * custom-settings refuses a key over CONFIG_ZMK_CUSTOM_SETTINGS_KEY_MAX_LEN
 * (48) at build time, but Zephyr refuses a stored name over
 * SETTINGS_MAX_NAME_LEN (64) only at runtime, inside the save path: the value
 * works until the next reboot and is then gone. So both are checked here, where
 * the node that caused it can be named.
 *
 * The longest field is "enabled" at 7, which with "amgskobo__tlt" leaves a
 * node 25 characters.
 */
#define ZMK_INPUT_TEMP_LAYER_TOUCH_ASSERT_NAME_FITS(n, longest_field)                              \
    BUILD_ASSERT(sizeof(ZMK_INPUT_TEMP_LAYER_TOUCH_SETTING_KEY(n, longest_field)) <=               \
                     CONFIG_ZMK_CUSTOM_SETTINGS_KEY_MAX_LEN,                                       \
                 "devicetree node \"" DT_NODE_FULL_NAME(DT_DRV_INST(                               \
                     n)) "\" has a name too long to key its settings; shorten the node name");     \
    BUILD_ASSERT(                                                                                  \
        sizeof(ZMK_INPUT_TEMP_LAYER_TOUCH_STORAGE_NAME(n, longest_field)) <=                       \
            SETTINGS_MAX_NAME_LEN,                                                                 \
        "devicetree node \"" DT_NODE_FULL_NAME(DT_DRV_INST(                                        \
            n)) "\" has a name too long to store its settings under; shorten the node name");
