/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>

#include <zmk-input-temp-layer-touch/temp_layer_touch_core.h>

static int failures;

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expr);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void test_strip_on_each_edge(void) {
    /* Far edges: strictly more than max - width. */
    CHECK(!temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_RIGHT, 974, 1024, 50));
    CHECK(temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_RIGHT, 975, 1024, 50));
    CHECK(temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_RIGHT, 1024, 1024, 50));
    CHECK(!temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_BOTTOM, 974, 1024, 50));
    CHECK(temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_BOTTOM, 975, 1024, 50));

    /* Near edges mirror it, so every strip is width counts wide. */
    CHECK(temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_LEFT, 0, 1024, 50));
    CHECK(temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_LEFT, 49, 1024, 50));
    CHECK(!temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_LEFT, 50, 1024, 50));
    CHECK(temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_TOP, 49, 1024, 50));
    CHECK(!temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_TOP, 50, 1024, 50));
}

static void test_zero_width_is_no_strip(void) {
    CHECK(!temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_RIGHT, 1024, 1024, 0));
    CHECK(!temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_LEFT, 0, 1024, 0));
    CHECK(!temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_TOP, 0, 1024, 0));
    CHECK(!temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_BOTTOM, 1024, 1024, 0));
    CHECK(!temp_layer_touch_in_strip((enum temp_layer_touch_edge)99, 0, 1024, 50));
}

static void test_report_guard_and_saturation(void) {
    struct temp_layer_touch_contact contact = {0};

    temp_layer_touch_contact_report(&contact, 3);
    CHECK(contact.reports == 0);

    temp_layer_touch_contact_open(&contact);
    CHECK(temp_layer_touch_contact_sample(&contact, true));
    temp_layer_touch_contact_report(&contact, 3);
    CHECK(contact.reports == 0);

    temp_layer_touch_contact_open(&contact);
    contact.reports = UINT8_MAX;
    temp_layer_touch_contact_report(&contact, UINT8_MAX);
    CHECK(contact.reports == UINT8_MAX);
}

static void test_out_of_range_coordinate_is_no_strip(void) {
    CHECK(!temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_LEFT, -1, 1024, 50));
    CHECK(!temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_TOP, -1, 1024, 50));
    CHECK(!temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_RIGHT, 1025, 1024, 50));
    CHECK(!temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_BOTTOM, 1025, 1024, 50));
    CHECK(!temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_RIGHT, 0, -1, 50));
}

static void test_axis_per_edge(void) {
    CHECK(temp_layer_touch_edge_on_x_axis(TEMP_LAYER_TOUCH_EDGE_RIGHT));
    CHECK(temp_layer_touch_edge_on_x_axis(TEMP_LAYER_TOUCH_EDGE_LEFT));
    CHECK(!temp_layer_touch_edge_on_x_axis(TEMP_LAYER_TOUCH_EDGE_TOP));
    CHECK(!temp_layer_touch_edge_on_x_axis(TEMP_LAYER_TOUCH_EDGE_BOTTOM));
}

static void test_generation_snapshot(void) {
    CHECK(temp_layer_touch_generation_stable(0, 0));
    CHECK(temp_layer_touch_generation_stable(2, 2));
    CHECK(!temp_layer_touch_generation_stable(1, 1));
    CHECK(!temp_layer_touch_generation_stable(2, 4));
    CHECK(!temp_layer_touch_generation_stable(UINT32_MAX, UINT32_MAX));
}

static void test_trigger_layers(void) {
    static const uint8_t allowed[] = {0, 3, 7};

    CHECK(temp_layer_touch_trigger_layer_allowed(NULL, 0, 0));
    CHECK(temp_layer_touch_trigger_layer_allowed(NULL, 0, 31));
    CHECK(!temp_layer_touch_trigger_layer_allowed(NULL, 1, 0));
    CHECK(temp_layer_touch_trigger_layer_allowed(allowed, 3, 0));
    CHECK(temp_layer_touch_trigger_layer_allowed(allowed, 3, 3));
    CHECK(temp_layer_touch_trigger_layer_allowed(allowed, 3, 7));
    CHECK(!temp_layer_touch_trigger_layer_allowed(allowed, 3, 1));

    CHECK(!temp_layer_touch_shared_target_allowed(6, 6, false));
    CHECK(temp_layer_touch_shared_target_allowed(6, 6, true));
    CHECK(!temp_layer_touch_shared_target_allowed(5, 6, true));
}

static void test_recognised_once_per_contact(void) {
    struct temp_layer_touch_contact contact;

    temp_layer_touch_contact_open(&contact);
    CHECK(temp_layer_touch_contact_sample(&contact, true));
    CHECK(!temp_layer_touch_contact_sample(&contact, true));
}

static void test_one_count_strip(void) {
    /* The narrowest strip is the last coordinate, or the first. */
    CHECK(temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_RIGHT, 1024, 1024, 1));
    CHECK(!temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_RIGHT, 1023, 1024, 1));
    CHECK(temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_LEFT, 0, 1024, 1));
    CHECK(!temp_layer_touch_in_strip(TEMP_LAYER_TOUCH_EDGE_LEFT, 1, 1024, 1));
}

static void test_new_contact_gets_a_full_window(void) {
    struct temp_layer_touch_contact contact;

    /* Reports counted for one contact do not shorten the next one's window. */
    temp_layer_touch_contact_open(&contact);
    temp_layer_touch_contact_report(&contact, 3);
    temp_layer_touch_contact_report(&contact, 3);
    temp_layer_touch_contact_close(&contact);
    temp_layer_touch_contact_open(&contact);
    CHECK(contact.reports == 0);
    temp_layer_touch_contact_report(&contact, 3);
    temp_layer_touch_contact_report(&contact, 3);
    CHECK(temp_layer_touch_contact_sample(&contact, true));
}

static void test_closed_contact_is_decided(void) {
    struct temp_layer_touch_contact contact;

    /* A release ends the contact before it was ever judged: nothing that
     * arrives after it, a late sample or a report, can raise the layer. */
    temp_layer_touch_contact_open(&contact);
    temp_layer_touch_contact_close(&contact);
    CHECK(!contact.open && contact.decided);
    CHECK(!temp_layer_touch_contact_sample(&contact, true));
    temp_layer_touch_contact_report(&contact, 1);
    CHECK(contact.reports == 0);
}

static void test_last_report_of_window_still_counts(void) {
    struct temp_layer_touch_contact contact;

    temp_layer_touch_contact_open(&contact);
    CHECK(!temp_layer_touch_contact_sample(&contact, false));
    temp_layer_touch_contact_report(&contact, 3);
    CHECK(!temp_layer_touch_contact_sample(&contact, false));
    temp_layer_touch_contact_report(&contact, 3);
    CHECK(temp_layer_touch_contact_sample(&contact, true));
}

static void test_stroke_reaching_edge_later_is_ordinary(void) {
    struct temp_layer_touch_contact contact;

    temp_layer_touch_contact_open(&contact);
    for (int i = 0; i < 3; i++) {
        CHECK(!temp_layer_touch_contact_sample(&contact, false));
        temp_layer_touch_contact_report(&contact, 3);
    }
    CHECK(!temp_layer_touch_contact_sample(&contact, true));
}

static void test_single_report_window(void) {
    struct temp_layer_touch_contact contact;

    temp_layer_touch_contact_open(&contact);
    temp_layer_touch_contact_report(&contact, 1);
    CHECK(!temp_layer_touch_contact_sample(&contact, true));
}

static void test_closed_contact_ignored_until_next(void) {
    struct temp_layer_touch_contact contact = {.open = false, .decided = true, .reports = 0};

    CHECK(!temp_layer_touch_contact_sample(&contact, true));
    temp_layer_touch_contact_open(&contact);
    temp_layer_touch_contact_close(&contact);
    CHECK(!temp_layer_touch_contact_sample(&contact, true));
    temp_layer_touch_contact_open(&contact);
    CHECK(temp_layer_touch_contact_sample(&contact, true));
}

static void test_buttons_stay_paired(void) {
    uint16_t suppressed = 0;

    /* Outside the window both edges pass. */
    CHECK(!temp_layer_touch_button_consumed(&suppressed, false, 0, true));
    CHECK(!temp_layer_touch_button_consumed(&suppressed, false, 0, false));

    /* A press in the window is consumed, and so is its release after the window closes. */
    CHECK(temp_layer_touch_button_consumed(&suppressed, true, 0, true));
    CHECK(temp_layer_touch_button_consumed(&suppressed, false, 0, false));
    CHECK(suppressed == 0);

    /* A release whose press passed is never consumed, even in the window. */
    CHECK(!temp_layer_touch_button_consumed(&suppressed, true, 1, false));

    /* Buttons are tracked independently. */
    CHECK(temp_layer_touch_button_consumed(&suppressed, true, 2, true));
    CHECK(!temp_layer_touch_button_consumed(&suppressed, false, 3, false));
    CHECK(temp_layer_touch_button_consumed(&suppressed, false, 2, false));
    CHECK(temp_layer_touch_button_consumed(&suppressed, true, 15, true));
    CHECK(temp_layer_touch_button_consumed(&suppressed, false, 15, false));
    CHECK(suppressed == 0);
}

int main(void) {
    test_strip_on_each_edge();
    test_zero_width_is_no_strip();
    test_report_guard_and_saturation();
    test_out_of_range_coordinate_is_no_strip();
    test_axis_per_edge();
    test_generation_snapshot();
    test_trigger_layers();
    test_recognised_once_per_contact();
    test_closed_contact_is_decided();
    test_one_count_strip();
    test_new_contact_gets_a_full_window();
    test_last_report_of_window_still_counts();
    test_stroke_reaching_edge_later_is_ordinary();
    test_single_report_window();
    test_closed_contact_ignored_until_next();
    test_buttons_stay_paired();

    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }

    printf("temp_layer_touch_core: all checks passed\n");

    return EXIT_SUCCESS;
}
