/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Publishes each strip's switch, layer and width through
 * zmk-feature-custom-settings.
 *
 * The width is the value that wants trying: too narrow and a scroll that
 * starts a little inboard turns into a pointer move, too wide and the pad
 * loses that much of its surface to pointing. The layer and the switch come
 * with it so a strip can be moved or turned off without a rebuild.
 *
 * This file owns persistence. The driver deliberately stores nothing, so
 * there is one owner for the value and no way for the two to disagree after a
 * reboot.
 */

#define DT_DRV_COMPAT zmk_input_processor_temp_layer_touch

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include <zmk/studio/custom.h>

#include <zmk-input-temp-layer-touch/custom_settings.h>
#include <zmk-input-temp-layer-touch/temp_layer_touch.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * Registers the namespace these parameters are published under.
 *
 * custom-settings resolves a setting's subsystem identifier to an index before
 * it can put the setting on the wire, and a setting whose subsystem was never
 * registered is dropped with -ENOENT however correctly it was defined.
 *
 * The subsystem answers no calls of its own: the values are read and written
 * through custom-settings' own RPC, which is what lets this processor appear in
 * a client that has no page for it. The advertised URL is this module's own
 * documentation, which is the only thing that explains what these settings do.
 */
static bool temp_layer_touch_namespace_handler(const zmk_custom_CallRequest *request,
                                               pb_callback_t *encode_response);

static struct zmk_rpc_custom_subsystem_meta temp_layer_touch_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("https://github.com/amgskobo/zmk-input-temp-layer-touch"),
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

/*
 * Through a wrapper so the token expands before it is stringified:
 * ZMK_RPC_CUSTOM_SUBSYSTEM registers `#_identifier`, and `#` suppresses
 * expansion of its own argument.
 */
#define REGISTER_SUBSYSTEM(identifier, meta, handler)                                              \
    ZMK_RPC_CUSTOM_SUBSYSTEM(identifier, meta, handler)

REGISTER_SUBSYSTEM(ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM_TOKEN, &temp_layer_touch_meta,
                   temp_layer_touch_namespace_handler);

static bool temp_layer_touch_namespace_handler(const zmk_custom_CallRequest *request,
                                               pb_callback_t *encode_response) {
    ARG_UNUSED(request);
    ARG_UNUSED(encode_response);

    return false;
}

#define TEMP_LAYER_TOUCH_AXIS_MAX(n)                                                               \
    (DT_INST_ENUM_IDX(n, edge) <= 1 ? DT_INST_PROP(n, x_max) : DT_INST_PROP(n, y_max))

/*
 * The layer is declared as a layer, not as a number in a range, so a client
 * that understands the constraint draws the keymap's own layer list; the range
 * alongside keeps a client that does not from accepting an impossible number.
 * Neither is trusted: temp_layer_touch_set_params() rejects a layer outside the
 * keymap and a strip wider than the pad regardless.
 */
#define TEMP_LAYER_TOUCH_SETTINGS(n)                                                               \
    ZMK_INPUT_TEMP_LAYER_TOUCH_ASSERT_NAME_FITS(n, "enabled")                                      \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        temp_layer_touch_cs_enabled_##n, ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM,                     \
        ZMK_INPUT_TEMP_LAYER_TOUCH_SETTING_KEY(n, "enabled"), ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL,  \
        ZMK_CUSTOM_SETTING_VALUE_BOOL(!DT_INST_PROP(n, start_disabled)),                           \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_NO_CONSTRAINT);                   \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        temp_layer_touch_cs_layer_##n, ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM,                       \
        ZMK_INPUT_TEMP_LAYER_TOUCH_SETTING_KEY(n, "layer"), ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,   \
        ZMK_CUSTOM_SETTING_VALUE_INT32(DT_INST_PROP(n, layer)),                                    \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_LAYER_ID,                         \
        ZMK_CUSTOM_SETTING_RANGE_INT32(0, ZMK_KEYMAP_LAYERS_LEN - 1));                             \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        temp_layer_touch_cs_width_##n, ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM,                       \
        ZMK_INPUT_TEMP_LAYER_TOUCH_SETTING_KEY(n, "width"), ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,   \
        ZMK_CUSTOM_SETTING_VALUE_INT32(DT_INST_PROP(n, width)),                                    \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE,                                                      \
        ZMK_CUSTOM_SETTING_RANGE_INT32(0, TEMP_LAYER_TOUCH_AXIS_MAX(n)));

DT_INST_FOREACH_STATUS_OKAY(TEMP_LAYER_TOUCH_SETTINGS)

static bool read_bool(const struct zmk_custom_setting *setting, bool *out) {
    struct zmk_custom_setting_value value;

    if (zmk_custom_setting_read(setting, &value) != 0 ||
        value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL) {
        return false;
    }

    *out = value.bool_value;

    return true;
}

static bool read_int32(const struct zmk_custom_setting *setting, int32_t *out) {
    struct zmk_custom_setting_value value;

    if (zmk_custom_setting_read(setting, &value) != 0 ||
        value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32) {
        return false;
    }

    *out = value.int32_value;

    return true;
}

/*
 * Applied as a set, so the layer and the width never come from different
 * generations of the same edit.
 */
#define TEMP_LAYER_TOUCH_APPLY(n)                                                                  \
    {                                                                                              \
        bool enabled;                                                                              \
        int32_t layer;                                                                             \
        int32_t width;                                                                             \
                                                                                                   \
        if (read_bool(&temp_layer_touch_cs_enabled_##n, &enabled) &&                               \
            read_int32(&temp_layer_touch_cs_layer_##n, &layer) &&                                  \
            read_int32(&temp_layer_touch_cs_width_##n, &width) && layer >= 0 &&                    \
            layer < ZMK_KEYMAP_LAYERS_LEN && width >= 0 && width <= UINT16_MAX) {                  \
            const struct temp_layer_touch_params params = {                                        \
                .enabled = enabled,                                                                \
                .layer = (uint8_t)layer,                                                           \
                .width = (uint16_t)width,                                                          \
            };                                                                                     \
                                                                                                   \
            (void)temp_layer_touch_set_params(DEVICE_DT_INST_GET(n), &params);                     \
        }                                                                                          \
    }

static void temp_layer_touch_apply_settings(void) {
    DT_INST_FOREACH_STATUS_OKAY(TEMP_LAYER_TOUCH_APPLY)
}

static int temp_layer_touch_settings_event_cb(const zmk_event_t *eh) {
    ARG_UNUSED(eh);

    /*
     * Both subscribed events mean the same thing here -- some stored value may
     * now differ from what the processor is running -- and re-reading every
     * instance is cheaper than working out which one moved.
     */
    temp_layer_touch_apply_settings();

    return ZMK_EV_EVENT_BUBBLE;
}

/*
 * Applied on two signals, and deliberately not from a SYS_INIT.
 *
 * zmk_custom_settings_initialized fires from the settings-subtree commit that
 * ends the boot settings_load pass, which is the only point at which a stored
 * value is both present and readable. A SYS_INIT runs before settings_load(),
 * so it would read the devicetree default and leave the processor on it for
 * the rest of the session. The load path stores values without raising
 * zmk_custom_setting_changed, so that event alone would never deliver a stored
 * value either. Together the two cover boot and every later edit.
 */
ZMK_LISTENER(temp_layer_touch_custom_settings, temp_layer_touch_settings_event_cb);
ZMK_SUBSCRIPTION(temp_layer_touch_custom_settings, zmk_custom_setting_changed);
ZMK_SUBSCRIPTION(temp_layer_touch_custom_settings, zmk_custom_settings_initialized);

/*
 * Two nodes can still produce one key, and nothing downstream would say so.
 *
 * A key is the owning node's DT_NODE_FULL_NAME, which is unique only among its
 * siblings, and zmk_custom_setting_find() returns the first match: a client's
 * write would always land on whichever node linked first while the other kept
 * its devicetree values and looked edited. String equality is not something
 * the preprocessor can evaluate, so this runs once at startup and names the
 * duplicated key. It walks descriptors, not values, so it needs nothing from
 * settings_load().
 */
static int temp_layer_touch_check_unique_keys(void) {
    ZMK_CUSTOM_SETTING_FOREACH(setting) {
        if (strcmp(setting->custom_subsystem_id, ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM) != 0) {
            continue;
        }

        ZMK_CUSTOM_SETTING_FOREACH(other) {
            if (other == setting) {
                /* Only report a pair once: stop at the first of the two. */
                break;
            }

            if (strcmp(other->custom_subsystem_id, ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM) == 0 &&
                strcmp(other->key, setting->key) == 0) {
                LOG_ERR("Duplicate setting key \"%s\": two devicetree nodes share a "
                        "name, so only one of them is editable",
                        setting->key);
            }
        }
    }

    return 0;
}

SYS_INIT(temp_layer_touch_check_unique_keys, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
