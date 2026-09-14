/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Runtime parameters for the temp-layer-touch processor.
 *
 * The three values a person tunes by feel are held in RAM: whether the strip
 * is on at all, which layer it holds, and how wide it is. The edge it lies
 * along, the pad's coordinate range, the start window and the allowed trigger
 * layers stay in devicetree; they describe routing and mounting rather than
 * values adjusted by feel.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct device;

struct temp_layer_touch_params {
    /* Whether a contact at the edge holds the layer at all. */
    bool enabled;
    /* The layer to hold, as a layer ID: what zmk_keymap_layer_activate() takes. */
    uint8_t layer;
    /* How far in from the edge the strip reaches, in the pad's coordinates. 0 is no strip. */
    uint16_t width;
};

/* Reads the parameters the processor is applying right now. */
int temp_layer_touch_get_params(const struct device *dev, struct temp_layer_touch_params *out);

/*
 * Applies new parameters. Returns -EINVAL and changes nothing when the layer is
 * not in the keymap or the strip is wider than the pad.
 *
 * A layer already held stays held on its old number until its contact ends,
 * and switching the strip off lets go of a held layer at the pad's next event
 * rather than at once: the layer is only ever raised and dropped from the input
 * thread, where the contact it belongs to is being followed.
 *
 * Nothing is persisted here; that is the settings layer's job, which is what
 * keeps this driver free of a second owner for the same value.
 */
int temp_layer_touch_set_params(const struct device *dev,
                                const struct temp_layer_touch_params *params);
