/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The decisions behind the temp-layer-touch, with nothing of Zephyr or ZMK in them,
 * so the host tests can drive them directly.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* In the order the devicetree binding lists them, which DT_ENUM_IDX returns. */
enum temp_layer_touch_edge {
    TEMP_LAYER_TOUCH_EDGE_RIGHT = 0,
    TEMP_LAYER_TOUCH_EDGE_LEFT = 1,
    TEMP_LAYER_TOUCH_EDGE_TOP = 2,
    TEMP_LAYER_TOUCH_EDGE_BOTTOM = 3,
};

static inline bool temp_layer_touch_generation_stable(uint32_t before, uint32_t after) {
    return (before & 1U) == 0U && before == after;
}

/* Right and left are decided on X, top and bottom on Y. */
static inline bool temp_layer_touch_edge_on_x_axis(enum temp_layer_touch_edge edge) {
    return edge == TEMP_LAYER_TOUCH_EDGE_RIGHT || edge == TEMP_LAYER_TOUCH_EDGE_LEFT;
}

/* An empty allowlist means that a contact may start from any active layer. */
static inline bool temp_layer_touch_trigger_layer_allowed(const uint8_t *layers,
                                                           uint8_t layer_count,
                                                           uint8_t active_layer) {
    if (layer_count == 0U) {
        return true;
    }

    if (layers == NULL) {
        return false;
    }

    for (uint8_t i = 0U; i < layer_count; i++) {
        if (layers[i] == active_layer) {
            return true;
        }
    }

    return false;
}

/* An already held target is the only exception to the origin-layer allowlist. */
static inline bool temp_layer_touch_shared_target_allowed(uint8_t active_layer,
                                                          uint8_t target_layer,
                                                          bool target_owned) {
    return target_owned && active_layer == target_layer;
}

/*
 * Whether a coordinate lies in the strip along an edge.
 *
 * The far edges use strictly more than max - width, and the near edges mirror
 * it as strictly less than width, so a
 * strip is width counts wide on every side. Both leave a width of zero with no
 * strip at all, which is what makes zero the off position rather than a strip
 * one count wide.
 */
static inline bool temp_layer_touch_in_strip(enum temp_layer_touch_edge edge, int32_t value,
                                             int32_t max, int32_t width) {
    if (width <= 0 || max < 0 || value < 0 || value > max) {
        return false;
    }

    switch (edge) {
    case TEMP_LAYER_TOUCH_EDGE_RIGHT:
    case TEMP_LAYER_TOUCH_EDGE_BOTTOM:
        return value > max - width;
    case TEMP_LAYER_TOUCH_EDGE_LEFT:
    case TEMP_LAYER_TOUCH_EDGE_TOP:
        return value < width;
    }

    return false;
}

/*
 * Where a contact stands on the way to being called an edge contact.
 *
 * A contact is judged only by where it starts: the reports in a window after
 * its touch press. A stroke that begins inside the pad and runs onto the edge
 * later is an ordinary one, which is what keeps the strip from firing in the
 * middle of a pointer move.
 */
struct temp_layer_touch_contact {
    /* Between a touch press and its release. */
    bool open;
    /* Past its start window, whether or not it began in the strip. */
    bool decided;
    /* Reports seen since the touch press, up to the window. */
    uint8_t reports;
};

static inline void temp_layer_touch_contact_open(struct temp_layer_touch_contact *contact) {
    contact->open = true;
    contact->decided = false;
    contact->reports = 0;
}

static inline void temp_layer_touch_contact_close(struct temp_layer_touch_contact *contact) {
    contact->open = false;
    contact->decided = true;
}

/*
 * Takes one coordinate on the edge's axis. True exactly once per contact, on
 * the sample that shows it started in the strip.
 */
static inline bool temp_layer_touch_contact_sample(struct temp_layer_touch_contact *contact,
                                                   bool in_strip) {
    if (!contact->open || contact->decided || !in_strip) {
        return false;
    }

    contact->decided = true;

    return true;
}

/* Takes the end of one report, and closes the window after start_reports of them. */
static inline void temp_layer_touch_contact_report(struct temp_layer_touch_contact *contact,
                                                   uint8_t start_reports) {
    if (!contact->open || contact->decided) {
        return;
    }

    if (contact->reports < UINT8_MAX) {
        contact->reports++;
    }

    if (contact->reports >= start_reports) {
        contact->decided = true;
    }
}

/* INPUT_BTN_0 through INPUT_BTN_15, one bit each. */
#define TEMP_LAYER_TOUCH_TRACKED_BUTTONS 16U

/*
 * Whether a button edge is consumed.
 *
 * A press is consumed while the window is open - from the moment a contact is
 * recognised as an edge contact until the next contact begins - which covers a
 * tap the pad driver reports only after the finger has lifted. A release is
 * consumed only when its press was, whenever it arrives: dropping the release
 * of a press that reached the host would leave that button held down with
 * nothing left to release it.
 */
static inline bool temp_layer_touch_button_consumed(uint16_t *suppressed, bool window,
                                                    uint8_t index, bool pressed) {
    const uint16_t bit = (uint16_t)(1U << index);

    if (pressed) {
        if (!window) {
            return false;
        }

        *suppressed = (uint16_t)(*suppressed | bit);

        return true;
    }

    if ((*suppressed & bit) == 0) {
        return false;
    }

    *suppressed = (uint16_t)(*suppressed & (uint16_t)~bit);

    return true;
}
