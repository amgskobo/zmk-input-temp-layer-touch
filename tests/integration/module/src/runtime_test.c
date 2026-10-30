/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Self-tests for the temp-layer-touch processor, run inside a native_sim build.
 *
 * Most hand events to the processor directly. The last one reports through a
 * real listener, because the fault it guards against is in what the listener
 * does with an event stopped on a layer route.
 */

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>

#include <drivers/input_processor.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk-input-temp-layer-touch/temp_layer_touch.h>
#if IS_ENABLED(CONFIG_ZMK_INPUT_TEMP_LAYER_TOUCH_CUSTOM_SETTINGS)
#include <cormoran/zmk/custom_settings.h>
#include <zmk-input-temp-layer-touch/custom_settings.h>
#endif

static const struct device *const right_strip = DEVICE_DT_GET(DT_NODELABEL(right_strip));
static const struct device *const bottom_strip = DEVICE_DT_GET(DT_NODELABEL(bottom_strip));
static const struct device *const pad = DEVICE_DT_GET(DT_NODELABEL(pad));

#define STREAM_A 0U
#define STREAM_B 1U
#define NO_STREAM UINT8_MAX
#define ROUTE_LAYER 1
#define TARGET_LAYER 2
/* Inside the 50-count strips of a 1024-count pad, and well away from them. */
#define EDGE 1000
#define MIDDLE 512

static int process(const struct device *strip, uint8_t stream, struct input_event *event) {
    struct zmk_input_processor_state state = {
        .input_device_index = stream,
        .remainder = NULL,
    };

    return zmk_input_processor_handle_event(strip, event, 0, 0, &state);
}

static struct input_event input(uint8_t type, uint16_t code, int32_t value, bool sync) {
    return (struct input_event){
        .type = type,
        .code = code,
        .value = value,
        .sync = sync,
    };
}

/* What ZMK's listener acts on: the touch, the first five buttons and motion. */
static bool listener_acts_on(const struct input_event *event) {
    switch (event->type) {
    case INPUT_EV_KEY:
        return event->code == INPUT_BTN_TOUCH ||
               (event->code >= INPUT_BTN_0 && event->code <= INPUT_BTN_4);
    case INPUT_EV_REL:
        return event->code == INPUT_REL_X || event->code == INPUT_REL_Y ||
               event->code == INPUT_REL_WHEEL || event->code == INPUT_REL_HWHEEL;
    default:
        return false;
    }
}

/* Passed on exactly as it came. */
static void passes(const struct device *strip, uint8_t stream, uint8_t type, uint16_t code,
                   int32_t value, bool sync) {
    struct input_event event = input(type, code, value, sync);

    __ASSERT_NO_MSG(process(strip, stream, &event) == ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(event.type == type && event.code == code);
    __ASSERT_NO_MSG(event.value == value && event.sync == sync);
}

/*
 * Stopped, and left as nothing the listener acts on or reports: on a layer
 * route the listener still handles an event a processor stopped.
 */
static void consumed(const struct device *strip, uint8_t stream, uint8_t type, uint16_t code,
                     int32_t value, bool sync) {
    struct input_event event = input(type, code, value, sync);

    __ASSERT_NO_MSG(process(strip, stream, &event) == ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(!listener_acts_on(&event));
    __ASSERT_NO_MSG(event.sync == (type == INPUT_EV_ABS && sync));
}

static void test_invalid_stream_passes_through(void) {
    passes(right_strip, NO_STREAM, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    passes(right_strip, NO_STREAM, INPUT_EV_ABS, INPUT_ABS_X, EDGE, true);
    passes(right_strip, NO_STREAM, INPUT_EV_KEY, INPUT_BTN_0, 1, true);
    passes(right_strip, NO_STREAM, INPUT_EV_KEY, INPUT_BTN_0, 0, true);
    passes(right_strip, NO_STREAM, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, true);
    __ASSERT_NO_MSG(!zmk_keymap_layer_active(TARGET_LAYER));

    printk("PASS: an index past the listeners passes through untouched\n");
}

static void test_layer_raising_coordinate_is_dropped(void) {
    /* On X the coordinate that raises the layer carries no sync. */
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    consumed(right_strip, STREAM_A, INPUT_EV_ABS, INPUT_ABS_X, EDGE, false);
    __ASSERT_NO_MSG(zmk_keymap_layer_active(TARGET_LAYER));
    passes(right_strip, STREAM_A, INPUT_EV_ABS, INPUT_ABS_Y, MIDDLE, true);
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false);
    __ASSERT_NO_MSG(!zmk_keymap_layer_active(TARGET_LAYER));

    /* On Y the neutral coordinate keeps the report's original sync. */
    passes(bottom_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    passes(bottom_strip, STREAM_A, INPUT_EV_ABS, INPUT_ABS_X, MIDDLE, false);
    consumed(bottom_strip, STREAM_A, INPUT_EV_ABS, INPUT_ABS_Y, EDGE, true);
    __ASSERT_NO_MSG(zmk_keymap_layer_active(TARGET_LAYER));
    passes(bottom_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, true);
    __ASSERT_NO_MSG(!zmk_keymap_layer_active(TARGET_LAYER));

    printk("PASS: the coordinate that raises the layer is dropped\n");
}

static void test_source_buttons_are_transparent(void) {
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    consumed(right_strip, STREAM_A, INPUT_EV_ABS, INPUT_ABS_X, EDGE, false);
    passes(right_strip, STREAM_A, INPUT_EV_ABS, INPUT_ABS_Y, MIDDLE, true);
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false);

    /* A pad reports its tap only after the finger has lifted. */
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_0, 1, true);
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_0, 0, true);

    /* Edge producer remains transparent on the next contact too. */
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_0, 1, true);
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_0, 0, true);
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false);

    printk("PASS: source buttons remain transparent across edge contact and lift\n");
}

static void test_two_contacts_share_the_layer(void) {
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    consumed(right_strip, STREAM_A, INPUT_EV_ABS, INPUT_ABS_X, EDGE, false);

    /* The layer is already up, so this contact joins it and changes no route. */
    passes(right_strip, STREAM_B, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    passes(right_strip, STREAM_B, INPUT_EV_ABS, INPUT_ABS_X, EDGE, false);

    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false);
    __ASSERT_NO_MSG(zmk_keymap_layer_active(TARGET_LAYER));
    passes(right_strip, STREAM_B, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false);
    __ASSERT_NO_MSG(!zmk_keymap_layer_active(TARGET_LAYER));

    printk("PASS: two contacts hold the layer until both end\n");
}

static void test_disable_preserves_another_nodes_claim(void) {
    struct temp_layer_touch_params params;
    __ASSERT_NO_MSG(temp_layer_touch_get_params(right_strip, &params) == 0);
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    consumed(right_strip, STREAM_A, INPUT_EV_ABS, INPUT_ABS_X, EDGE, false);
    passes(bottom_strip, STREAM_B, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    passes(bottom_strip, STREAM_B, INPUT_EV_ABS, INPUT_ABS_Y, EDGE, true);

    params.enabled = false;
    __ASSERT_NO_MSG(temp_layer_touch_set_params(right_strip, &params) == 0);
    __ASSERT_NO_MSG(zmk_keymap_layer_active(TARGET_LAYER));
    passes(bottom_strip, STREAM_B, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, true);
    __ASSERT_NO_MSG(!zmk_keymap_layer_active(TARGET_LAYER));
    params.enabled = true;
    __ASSERT_NO_MSG(temp_layer_touch_set_params(right_strip, &params) == 0);
    /* Disable also closes the old contact; a coordinate alone cannot claim. */
    passes(right_strip, STREAM_A, INPUT_EV_ABS, INPUT_ABS_X, EDGE, true);
    __ASSERT_NO_MSG(!zmk_keymap_layer_active(TARGET_LAYER));
    printk("PASS: disabling one node releases only its claims and old contacts\n");
}

static void test_disable_clears_all_streams(void) {
    struct temp_layer_touch_params params;
    __ASSERT_NO_MSG(temp_layer_touch_get_params(right_strip, &params) == 0);
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    consumed(right_strip, STREAM_A, INPUT_EV_ABS, INPUT_ABS_X, EDGE, false);
    passes(right_strip, STREAM_B, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    passes(right_strip, STREAM_B, INPUT_EV_ABS, INPUT_ABS_X, EDGE, true);
    params.enabled = false;
    __ASSERT_NO_MSG(temp_layer_touch_set_params(right_strip, &params) == 0);
    __ASSERT_NO_MSG(!zmk_keymap_layer_active(TARGET_LAYER));
    params.enabled = true;
    __ASSERT_NO_MSG(temp_layer_touch_set_params(right_strip, &params) == 0);
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_0, 0, true);
    passes(right_strip, STREAM_B, INPUT_EV_KEY, INPUT_BTN_0, 0, true);
    printk("PASS: disable completes shared-layer/contact cleanup before return\n");
}

static void test_external_layer_is_not_lowered(void) {
    __ASSERT_NO_MSG(zmk_keymap_layer_activate(TARGET_LAYER, false) >= 0);
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    passes(right_strip, STREAM_A, INPUT_EV_ABS, INPUT_ABS_X, EDGE, true);
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, true);
    __ASSERT_NO_MSG(zmk_keymap_layer_active(TARGET_LAYER));
    __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(TARGET_LAYER, false) >= 0);
    __ASSERT_NO_MSG(!zmk_keymap_layer_active(TARGET_LAYER));
    printk("PASS: an externally raised layer retains its owner after touch release\n");
}

static void report(uint16_t type, uint16_t code, int32_t value, bool sync) {
    __ASSERT_NO_MSG(input_report(pad, type, code, value, sync, K_FOREVER) == 0);
    /* The input thread outranks this one; this only makes sure it has run. */
    k_msleep(1);
}

/* An edge contact, then the tap its pad reports after the lift. */
static void edge_tap_through_listener(void) {
    report(INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    report(INPUT_EV_ABS, INPUT_ABS_X, EDGE, false);
    report(INPUT_EV_ABS, INPUT_ABS_Y, MIDDLE, true);
    __ASSERT_NO_MSG(zmk_keymap_layer_active(TARGET_LAYER));
    report(INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, true);
    __ASSERT_NO_MSG(!zmk_keymap_layer_active(TARGET_LAYER));

    report(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
    __ASSERT_NO_MSG(zmk_hid_get_mouse_report()->body.buttons == 0);
    report(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
    __ASSERT_NO_MSG(zmk_hid_get_mouse_report()->body.buttons == 0);

    /* The final owner clears only this source's contact latch on a new touch. */
    report(INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    report(INPUT_EV_ABS, INPUT_ABS_X, MIDDLE, false);
    report(INPUT_EV_ABS, INPUT_ABS_Y, MIDDLE, true);
    report(INPUT_EV_KEY, INPUT_BTN_0, 1, true);
    __ASSERT_NO_MSG(zmk_hid_get_mouse_report()->body.buttons == 1);
    report(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
    report(INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, true);
    __ASSERT_NO_MSG(zmk_hid_get_mouse_report()->body.buttons == 0);
}

static void test_dropped_tap_never_clicks(void) {
    __ASSERT_NO_MSG(zmk_hid_get_mouse_report()->body.buttons == 0);

    /* The listener's default route, where a stop ends everything. */
    edge_tap_through_listener();

    /* A layer route, where the listener still handles a stopped event. */
    (void)zmk_keymap_layer_activate(ROUTE_LAYER, false);
    __ASSERT_NO_MSG(zmk_keymap_layer_active(ROUTE_LAYER));
    edge_tap_through_listener();
    (void)zmk_keymap_layer_deactivate(ROUTE_LAYER, false);
    __ASSERT_NO_MSG(!zmk_keymap_layer_active(ROUTE_LAYER));

    printk("PASS: a dropped tap never reaches the mouse report, on any route\n");
}

#if IS_ENABLED(CONFIG_ZMK_INPUT_TEMP_LAYER_TOUCH_CUSTOM_SETTINGS)
K_SEM_DEFINE(settings_snapshot_ready,0,1);
K_SEM_DEFINE(settings_snapshot_resume,0,1);
K_SEM_DEFINE(settings_writers_done,0,2);
K_THREAD_STACK_DEFINE(settings_stack_a,4096);
K_THREAD_STACK_DEFINE(settings_stack_b,4096);
static struct k_thread settings_thread_a,settings_thread_b;
static const struct zmk_custom_setting *race_enabled,*race_delay;
static atomic_t pause_settings_read;
int __real_zmk_custom_setting_read(const struct zmk_custom_setting *,struct zmk_custom_setting_value *);
int __wrap_zmk_custom_setting_read(const struct zmk_custom_setting *setting,
                                  struct zmk_custom_setting_value *value) {
    int rc=__real_zmk_custom_setting_read(setting,value);
    if(rc==0 && setting==race_delay && k_current_get()==&settings_thread_a &&
       atomic_cas(&pause_settings_read,1,0)) {
        k_sem_give(&settings_snapshot_ready);
        __ASSERT_NO_MSG(k_sem_take(&settings_snapshot_resume,K_SECONDS(1))==0);
    }
    return rc;
}
static void settings_writer(void *which,void *b,void *c) {
    ARG_UNUSED(b); ARG_UNUSED(c);
    struct zmk_custom_setting_value value;
    const struct zmk_custom_setting *setting;
    if(which==NULL) { setting=race_enabled; value=(struct zmk_custom_setting_value)ZMK_CUSTOM_SETTING_VALUE_BOOL(false); }
    else { setting=race_delay; value=(struct zmk_custom_setting_value)ZMK_CUSTOM_SETTING_VALUE_INT32(60); }
    __ASSERT_NO_MSG(zmk_custom_setting_write(setting,&value,ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY)==0);
    k_sem_give(&settings_writers_done);
}
static void test_concurrent_settings(void) {

    race_enabled=zmk_custom_setting_find(ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM,"right_strip.enabled");
    race_delay=zmk_custom_setting_find(ZMK_INPUT_TEMP_LAYER_TOUCH_SUBSYSTEM,"right_strip.width");
    __ASSERT_NO_MSG(race_enabled!=NULL && race_delay!=NULL);
    atomic_set(&pause_settings_read,1);
    k_thread_create(&settings_thread_a,settings_stack_a,K_THREAD_STACK_SIZEOF(settings_stack_a),
        settings_writer,NULL,NULL,NULL,1,0,K_NO_WAIT);
    __ASSERT_NO_MSG(k_sem_take(&settings_snapshot_ready,K_SECONDS(1))==0);
    k_thread_create(&settings_thread_b,settings_stack_b,K_THREAD_STACK_SIZEOF(settings_stack_b),
        settings_writer,(void *)1,NULL,NULL,1,0,K_NO_WAIT);
    /* Both writer threads outrank this test thread. B has now finished or
     * blocked in the adapter, after its registry value was committed. */
    struct zmk_custom_setting_value stored;
    __ASSERT_NO_MSG(zmk_custom_setting_read(race_delay,&stored)==0 && stored.int32_value==60);
    k_sem_give(&settings_snapshot_resume);
    __ASSERT_NO_MSG(k_sem_take(&settings_writers_done,K_SECONDS(1))==0);
    __ASSERT_NO_MSG(k_sem_take(&settings_writers_done,K_SECONDS(1))==0);
    struct temp_layer_touch_params params;
    __ASSERT_NO_MSG(temp_layer_touch_get_params(right_strip,&params)==0 && !params.enabled && params.width==60);
    printk("PASS: concurrent registry edits cannot reapply an older edge-touch snapshot\n");
}

#endif

static void run_tests(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    __ASSERT_NO_MSG(device_is_ready(right_strip));
    __ASSERT_NO_MSG(device_is_ready(bottom_strip));
    __ASSERT_NO_MSG(device_is_ready(pad));
    /* The test board brings listeners of its own; two streams are all these tests need. */
    __ASSERT_NO_MSG(DT_NUM_INST_STATUS_OKAY(zmk_input_listener) >= 2);

    test_invalid_stream_passes_through();
    test_layer_raising_coordinate_is_dropped();
    test_source_buttons_are_transparent();
    test_two_contacts_share_the_layer();
    test_disable_preserves_another_nodes_claim();
    test_disable_clears_all_streams();
    test_external_layer_is_not_lowered();
    test_dropped_tap_never_clicks();

#if IS_ENABLED(CONFIG_ZMK_INPUT_TEMP_LAYER_TOUCH_CUSTOM_SETTINGS)
    test_concurrent_settings();
#endif
    printk("temp-layer-touch runtime tests: PASS\n");
    exit(0);
}

K_THREAD_DEFINE(temp_layer_touch_runtime_tests, 4096, run_tests, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
