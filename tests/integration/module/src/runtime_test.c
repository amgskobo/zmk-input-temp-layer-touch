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

    __ASSERT_NO_MSG(process(strip, stream, &event) == ZMK_INPUT_PROC_STOP);
    __ASSERT_NO_MSG(!listener_acts_on(&event));
    __ASSERT_NO_MSG(!event.sync);
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

    /* On Y it carries the report's sync, which must not survive either. */
    passes(bottom_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    passes(bottom_strip, STREAM_A, INPUT_EV_ABS, INPUT_ABS_X, MIDDLE, false);
    consumed(bottom_strip, STREAM_A, INPUT_EV_ABS, INPUT_ABS_Y, EDGE, true);
    __ASSERT_NO_MSG(zmk_keymap_layer_active(TARGET_LAYER));
    passes(bottom_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, true);
    __ASSERT_NO_MSG(!zmk_keymap_layer_active(TARGET_LAYER));

    printk("PASS: the coordinate that raises the layer is dropped\n");
}

static void test_edge_contact_buttons_are_dropped(void) {
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    consumed(right_strip, STREAM_A, INPUT_EV_ABS, INPUT_ABS_X, EDGE, false);
    passes(right_strip, STREAM_A, INPUT_EV_ABS, INPUT_ABS_Y, MIDDLE, true);
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false);

    /* A pad reports its tap only after the finger has lifted. */
    consumed(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_0, 1, true);
    consumed(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_0, 0, true);

    /* The next contact ends the window, so its tap goes through. */
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_0, 1, true);
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_0, 0, true);
    passes(right_strip, STREAM_A, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false);

    printk("PASS: buttons after an edge contact are dropped\n");
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
    test_edge_contact_buttons_are_dropped();
    test_two_contacts_share_the_layer();
    test_dropped_tap_never_clicks();

    printk("temp-layer-touch runtime tests: PASS\n");
    exit(0);
}

K_THREAD_DEFINE(temp_layer_touch_runtime_tests, 4096, run_tests, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
