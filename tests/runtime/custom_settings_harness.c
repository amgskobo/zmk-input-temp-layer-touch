/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM "amgskobo__tlt"

enum zmk_custom_setting_value_type {
    ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32 = 1,
    ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL,
};
struct zmk_custom_setting_value {
    enum zmk_custom_setting_value_type type;
    union {
        int32_t int32_value;
        bool bool_value;
    };
};
struct zmk_custom_setting {
    const char *custom_subsystem_id;
    const char *key;
    int read_result;
    struct zmk_custom_setting_value value;
};
typedef struct { int unused; } zmk_custom_CallRequest;
typedef struct { int unused; } pb_callback_t;

#define ARG_UNUSED(x) ((void)(x))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static int errors;
#define LOG_ERR(fmt, ...) ((void)errors++, (void)sizeof(printf(fmt, ##__VA_ARGS__)))

static struct zmk_custom_setting *settings;
static size_t settings_len;
#define ZMK_CUSTOM_SETTING_FOREACH(_var)                                                           \
    for (struct zmk_custom_setting *_var = settings; _var < settings + settings_len; _var++)

static int zmk_custom_setting_read(const struct zmk_custom_setting *setting,
                                   struct zmk_custom_setting_value *value) {
    *value = setting->value;
    return setting->read_result;
}

/* The apply step: one devicetree instance and its three values. */
#include <zmk-input-temp-layer-touch/temp_layer_touch.h>
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_KEYMAP_LAYERS_LEN 6
#define DT_INST_FOREACH_STATUS_OKAY(fn) fn(0)
#define DEVICE_DT_INST_GET(n) (&instance)
typedef struct { int kind; } zmk_event_t;
struct device { int unused; };
static struct device instance;
static struct zmk_custom_setting temp_layer_touch_cs_enabled_0 = {
    .value = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, .bool_value = true}};
static struct zmk_custom_setting temp_layer_touch_cs_layer_0 = {
    .value = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, .int32_value = 2}};
static struct zmk_custom_setting temp_layer_touch_cs_width_0 = {
    .value = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, .int32_value = 80}};
static int applies;
static struct temp_layer_touch_params applied;

int temp_layer_touch_set_params(const struct device *dev,
                                const struct temp_layer_touch_params *params) {
    assert(dev == &instance);
    applies++;
    applied = *params;
    return 0;
}

/* DRIVER_FUNCTIONS */

static struct zmk_custom_setting int32_setting(int32_t value) {
    return (struct zmk_custom_setting){
        .value = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, .int32_value = value}};
}

static struct zmk_custom_setting bool_setting(bool value) {
    return (struct zmk_custom_setting){
        .value = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, .bool_value = value}};
}

static void test_readers(void) {
    bool flag = false;
    int32_t number = 7;

    struct zmk_custom_setting setting = bool_setting(true);
    assert(read_bool(&setting, &flag) && flag);
    setting.read_result = -2;
    flag = false;
    assert(!read_bool(&setting, &flag) && !flag);
    setting = int32_setting(1);
    assert(!read_bool(&setting, &flag) && !flag);

    /* Signed on purpose: the apply step range-checks the value itself. */
    setting = int32_setting(-5);
    assert(read_int32(&setting, &number) && number == -5);
    number = 7;
    setting.read_result = -2;
    assert(!read_int32(&setting, &number) && number == 7);
    setting = bool_setting(true);
    assert(!read_int32(&setting, &number) && number == 7);
}

static void test_unique_keys(void) {
    struct zmk_custom_setting list[] = {
        {.custom_subsystem_id = "other", .key = "edge.width"},
        {.custom_subsystem_id = ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM, .key = "edge.width"},
        {.custom_subsystem_id = "other", .key = "edge.layer"},
        {.custom_subsystem_id = ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM, .key = "edge.layer"},
        /* Another subsystem reusing a key after ours is not a duplicate. */
        {.custom_subsystem_id = "other", .key = "edge.width"},
        {.custom_subsystem_id = ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM, .key = "edge.width"},
    };
    settings = list;

    settings_len = ARRAY_SIZE(list);
    assert(temp_layer_touch_check_unique_keys() == 0);
    assert(errors == 1);

    errors = 0;
    settings_len = ARRAY_SIZE(list) - 1;
    assert(temp_layer_touch_check_unique_keys() == 0);
    assert(errors == 0);
}

/* The three values are applied as one set, only when all read and fit. */
static void test_apply(void) {
    const zmk_event_t changed = {1};
    assert(temp_layer_touch_settings_event_cb(&changed) == ZMK_EV_EVENT_BUBBLE);
    assert(applies == 1 && applied.enabled && applied.layer == 2 && applied.width == 80);

    struct zmk_custom_setting *values[] = {&temp_layer_touch_cs_enabled_0,
                                           &temp_layer_touch_cs_layer_0,
                                           &temp_layer_touch_cs_width_0};
    for (size_t i = 0; i < ARRAY_SIZE(values); i++) {
        values[i]->read_result = -2;
        temp_layer_touch_apply_settings();
        values[i]->read_result = 0;
    }

    /* A layer below 0 or past the keymap, a width below 0 or past 16 bits. */
    static const int32_t layers[] = {-1, ZMK_KEYMAP_LAYERS_LEN};
    for (size_t i = 0; i < ARRAY_SIZE(layers); i++) {
        temp_layer_touch_cs_layer_0.value.int32_value = layers[i];
        temp_layer_touch_apply_settings();
    }
    temp_layer_touch_cs_layer_0.value.int32_value = ZMK_KEYMAP_LAYERS_LEN - 1;
    static const int32_t widths[] = {-1, UINT16_MAX + 1};
    for (size_t i = 0; i < ARRAY_SIZE(widths); i++) {
        temp_layer_touch_cs_width_0.value.int32_value = widths[i];
        temp_layer_touch_apply_settings();
    }
    assert(applies == 1);

    /* The edges of the range are accepted. */
    temp_layer_touch_cs_width_0.value.int32_value = UINT16_MAX;
    temp_layer_touch_apply_settings();
    assert(applies == 2 && applied.layer == ZMK_KEYMAP_LAYERS_LEN - 1);
    assert(applied.width == UINT16_MAX);
    temp_layer_touch_cs_width_0.value.int32_value = 0;
    temp_layer_touch_cs_layer_0.value.int32_value = 0;
    temp_layer_touch_apply_settings();
    assert(applies == 3 && applied.width == 0 && applied.layer == 0);
}

int main(void) {
    assert(!temp_layer_touch_namespace_handler(NULL, NULL));
    test_readers();
    test_unique_keys();
    test_apply();
    puts("temp-layer-touch custom settings helpers: PASS");
    return 0;
}
