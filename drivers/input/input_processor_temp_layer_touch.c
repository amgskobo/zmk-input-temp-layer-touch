/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Holds a layer for as long as a contact that started at one edge of a pad
 * lasts. It moves a driver-owned side slider into the input pipeline while
 * allowing right and left slider instances to coexist on the keymap-owning
 * side of a split.
 *
 * It is a pad driver's scroll slider, moved to where the keymap is, so pad
 * drivers remain raw input sources. On a split peripheral the pad reaches the
 * central through zmk,input-split, so this reads where a contact starts from
 * the coordinates the keymap-owning central receives.
 *
 * Place it first in every route of the listener. The route is chosen per event
 * from the layers up at that moment, and the layer this raises changes the
 * route, so a contact that starts on one route ends on another. An instance
 * missing from the route that ends it never sees the release, and the layer
 * stays up until the pad is touched again.
 */

#define DT_DRV_COMPAT zmk_input_processor_temp_layer_touch

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <drivers/input_processor.h>
#include <zmk/keymap.h>

#include <zmk-input-temp-layer-touch/temp_layer_touch.h>
#include <zmk-input-temp-layer-touch/temp_layer_touch_core.h>

LOG_MODULE_REGISTER(temp_layer_touch, CONFIG_ZMK_LOG_LEVEL);

#define TEMP_LAYER_TOUCH_LISTENER_COUNT DT_NUM_INST_STATUS_OKAY(zmk_input_listener)
#define TEMP_LAYER_TOUCH_STREAM_COUNT MAX(TEMP_LAYER_TOUCH_LISTENER_COUNT, 1)
#define TEMP_LAYER_TOUCH_PARAMS_LAYER_SHIFT 16U
#define TEMP_LAYER_TOUCH_PARAMS_ENABLED_BIT 24U
/* No input event code reaches this value, so no handler acts on it. */
#define TEMP_LAYER_TOUCH_INVALID_CODE 0xFFF

/* The devicetree values that follow from the pad, fixed for the build. */
struct temp_layer_touch_config {
    enum temp_layer_touch_edge edge;
    /* The largest coordinate on the edge's axis. */
    uint16_t max;
    uint8_t start_reports;
    bool pass_buttons;
    const uint8_t *trigger_layers;
    uint8_t trigger_layer_count;
};

struct temp_layer_touch_stream {
    /* Serializes this listener's contact history with runtime disable. */
    struct k_mutex lock;
    struct temp_layer_touch_contact contact;
    /* A layer this stream claimed and still owes a release. */
    bool held;
    uint8_t held_layer;
    /* Open from an edge contact's recognition until the next contact begins. */
    bool button_window;
    uint16_t suppressed_buttons;
};

struct temp_layer_touch_data {
    /* One atomic snapshot keeps a concurrent settings edit coherent and cheap. */
    atomic_t packed_params;
    /* An event sampled across a runtime edit must not escape under stale routing. */
    atomic_t generation;
    /* Serializes the rare writers so generation cannot look even between two
     * overlapping edits. Readers remain lock-free on the input path. */
    struct k_spinlock params_lock;
    /* Extends writer ordering across held-layer cleanup without making input
     * readers take a node-wide lock. */
    struct k_mutex settings_lock;
    /* Contact and button history must never cross input-listener boundaries. */
    struct temp_layer_touch_stream streams[TEMP_LAYER_TOUCH_STREAM_COUNT];
};

/*
 * zmk_keymap_layer_activate() is a bit operation rather than a reference-counted
 * hold. Coordinate claims here so two pads (or two processor nodes) targeting
 * the same layer cannot lower it while either edge contact is still open.
 * Different input devices can report concurrently, so a mutex protects the
 * short claim transitions. It is never taken for ordinary coordinate motion.
 */
struct temp_layer_touch_ownership {
    uint16_t claims;
    bool raised;
};

static struct temp_layer_touch_ownership ownership[ZMK_KEYMAP_LAYERS_LEN];
K_MUTEX_DEFINE(ownership_lock);

static atomic_val_t pack_params(const struct temp_layer_touch_params *params) {
    return (atomic_val_t)params->width |
           ((atomic_val_t)params->layer << TEMP_LAYER_TOUCH_PARAMS_LAYER_SHIFT) |
           (params->enabled ? BIT(TEMP_LAYER_TOUCH_PARAMS_ENABLED_BIT) : 0);
}

static struct temp_layer_touch_params unpack_params(atomic_val_t packed) {
    return (struct temp_layer_touch_params){
        .enabled = (packed & BIT(TEMP_LAYER_TOUCH_PARAMS_ENABLED_BIT)) != 0,
        .layer = (uint8_t)((uint32_t)packed >> TEMP_LAYER_TOUCH_PARAMS_LAYER_SHIFT),
        .width = (uint16_t)packed,
    };
}

static struct temp_layer_touch_stream *stream_for_event(
    struct temp_layer_touch_data *data,
    const struct zmk_input_processor_state *state) {
    if (state == NULL) {
        return &data->streams[0];
    }

    if (state->input_device_index >= TEMP_LAYER_TOUCH_STREAM_COUNT) {
        return NULL;
    }

    return &data->streams[state->input_device_index];
}

static void drop_layer(const struct device *dev, struct temp_layer_touch_stream *stream);

static bool trigger_layer_allowed(const struct temp_layer_touch_config *config,
                                  uint8_t target_layer) {
    if (config->trigger_layer_count == 0U) {
        return true;
    }

    const zmk_keymap_layer_index_t active_index = zmk_keymap_highest_layer_active();
    const zmk_keymap_layer_id_t active_layer = zmk_keymap_layer_index_to_id(active_index);

    if (temp_layer_touch_trigger_layer_allowed(config->trigger_layers,
                                               config->trigger_layer_count, active_layer)) {
        return true;
    }

    /*
     * A second pad must be able to join a target this processor family already
     * holds, even when the configured origin allowlist contains only the base
     * layer. An externally active target gets no such exception: it has an
     * owner we must neither extend nor eventually lower.
     */
    if (active_layer != target_layer) {
        return false;
    }

    k_mutex_lock(&ownership_lock, K_FOREVER);
    const bool target_owned = ownership[target_layer].claims != 0U;
    k_mutex_unlock(&ownership_lock);

    return temp_layer_touch_shared_target_allowed(active_layer, target_layer, target_owned);
}

/*
 * Raises the layer for this contact, unless something else already has it up:
 * a layer this instance did not raise is not one it may drop when the contact
 * ends.
 *
 * Raised here, on the input thread, rather than from a work item. The next
 * event of the same report has to be routed with the layer already up, or the
 * start of the stroke goes down the route the contact is leaving; ZMK's own
 * behaviors processor raises layers from the same place for the same reason.
 *
 * Returns true when this call changed routing by raising a previously inactive
 * layer.
 */
static bool hold_layer(const struct device *dev, struct temp_layer_touch_stream *stream,
                       uint8_t layer) {
    bool route_changed = false;

    k_mutex_lock(&ownership_lock, K_FOREVER);
    struct temp_layer_touch_ownership *owner = &ownership[layer];

    if (stream->held) {
        goto unlock;
    }

    if (owner->claims == UINT16_MAX) {
        LOG_WRN("%s: too many claims for layer %u", dev->name, layer);
        goto unlock;
    }

    if (owner->claims == 0U && zmk_keymap_layer_active(layer)) {
        LOG_DBG("%s: layer %u already up, left to its owner", dev->name, layer);
        goto unlock;
    }

    if (owner->claims == 0U || !zmk_keymap_layer_active(layer)) {
        int ret = zmk_keymap_layer_activate(layer, false);

        /*
         * ZMK changes the layer bit before it notifies layer-state listeners.
         * A listener error is therefore returned after the requested state may
         * already have taken effect. The state, not the notification result,
         * decides whether this contact now owes a release.
         */
        if (!zmk_keymap_layer_active(layer)) {
            LOG_WRN("%s: layer %u remained down after activation (%d)", dev->name, layer, ret);
            goto unlock;
        }

        if (ret < 0) {
            LOG_WRN("%s: layer %u raised, but its state notification failed (%d)", dev->name,
                    layer, ret);
        }

        owner->raised = true;
        route_changed = true;
    }

    owner->claims++;
    stream->held = true;
    stream->held_layer = layer;
    LOG_DBG("%s: edge contact, layer %u up", dev->name, layer);

unlock:
    k_mutex_unlock(&ownership_lock);

    return route_changed;
}

static void drop_layer(const struct device *dev, struct temp_layer_touch_stream *stream) {
    if (!stream->held) {
        return;
    }

    k_mutex_lock(&ownership_lock, K_FOREVER);
    const uint8_t layer = stream->held_layer;
    struct temp_layer_touch_ownership *owner = &ownership[layer];

    if (owner->claims == 0U) {
        LOG_ERR("%s: missing ownership claim for layer %u", dev->name, layer);
        stream->held = false;
        goto unlock;
    }

    owner->claims--;
    stream->held = false;

    if (owner->claims != 0U || !owner->raised) {
        goto unlock;
    }

    int ret = zmk_keymap_layer_deactivate(layer, false);

    /* See hold_layer(): a notification error does not roll the layer bit back. */
    if (zmk_keymap_layer_active(layer)) {
        /* A locked/default layer, or another owner, is deliberately left up. */
        LOG_DBG("%s: layer %u retained by another owner", dev->name, layer);
    } else {
        LOG_DBG("%s: layer %u down", dev->name, layer);
    }

    owner->raised = false;
    if (ret < 0) {
        LOG_WRN("%s: layer %u state notification failed while lowering (%d)", dev->name, layer,
                ret);
    }

unlock:
    k_mutex_unlock(&ownership_lock);
}

/*
 * What a discarded event still does: a touch edge ends or restarts the contact
 * whatever the settings say, so it is followed even when the rest of the event
 * is dropped. Losing it would leave the layer held until the pad is next
 * touched.
 */
static void follow_discarded_touch(const struct device *dev,
                                   struct temp_layer_touch_stream *stream,
                                   const struct input_event *event) {
    if (event->type != INPUT_EV_KEY || event->code != INPUT_BTN_TOUCH) {
        return;
    }

    drop_layer(dev, stream);

    if (event->value) {
        stream->button_window = false;
        temp_layer_touch_contact_open(&stream->contact);
    } else {
        temp_layer_touch_contact_close(&stream->contact);
    }
}

/*
 * Stops an event, and leaves nothing in it for the listener to act on.
 *
 * A stop only ends the route it was returned in. On a layer route ZMK's
 * listener still hands the event to its own handlers, which read BTN_TOUCH and
 * BTN_0 as mouse buttons and send a report at every sync, so a stopped button
 * would still click. The other processors in these chains drop what they
 * consume the same way.
 */
static int stop_event(struct input_event *event) {
    event->code = TEMP_LAYER_TOUCH_INVALID_CODE;
    event->sync = false;

    return ZMK_INPUT_PROC_STOP;
}

static int temp_layer_touch_handle_event(const struct device *dev, struct input_event *event,
                                         uint32_t param1, uint32_t param2,
                                         struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    const struct temp_layer_touch_config *config = dev->config;
    struct temp_layer_touch_data *data = dev->data;
    struct temp_layer_touch_stream *stream = stream_for_event(data, state);

    if (stream == NULL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    k_mutex_lock(&stream->lock, K_FOREVER);
    const atomic_val_t generation = atomic_get(&data->generation);
    struct temp_layer_touch_params params = unpack_params(atomic_get(&data->packed_params));
    struct temp_layer_touch_stream next = {
        .contact = stream->contact,
        .held = stream->held,
        .held_layer = stream->held_layer,
        .button_window = stream->button_window,
        .suppressed_buttons = stream->suppressed_buttons,
    };
    bool drop = false;
    bool hold = false;
    bool consume = false;

    if (!temp_layer_touch_generation_stable((uint32_t)generation,
                                            (uint32_t)atomic_get(&data->generation))) {
        follow_discarded_touch(dev, stream, event);
        k_mutex_unlock(&stream->lock);
        return stop_event(event);
    }

    /* Switched off while holding: let go now rather than at the release. */
    if (next.held && !params.enabled) {
        drop = true;
        next.held = false;
    }

    switch (event->type) {
    case INPUT_EV_KEY:
        if (event->code == INPUT_BTN_TOUCH) {
            /*
             * Both edges drop a held layer. A release ends the contact that
             * raised it. A press with a layer still held means that contact's
             * release never arrived here - lost with a split link, say - and
             * nothing else is going to send it.
             *
             * Dropped as the release arrives, not at the sync after it: a pad
             * that sends no coordinates with its release would otherwise leave
             * the layer up until its next report.
             */
            if (next.held) {
                drop = true;
                next.held = false;
            }

            if (event->value) {
                next.button_window = false;
                temp_layer_touch_contact_open(&next.contact);
            } else {
                temp_layer_touch_contact_close(&next.contact);
            }
        } else if (!config->pass_buttons && event->code >= INPUT_BTN_0 &&
                   event->code < INPUT_BTN_0 + TEMP_LAYER_TOUCH_TRACKED_BUTTONS &&
                   temp_layer_touch_button_consumed(
                       &next.suppressed_buttons, next.button_window,
                       (uint8_t)(event->code - INPUT_BTN_0), event->value != 0)) {
            LOG_DBG("%s: BTN_%d %s from an edge contact consumed", dev->name,
                    event->code - INPUT_BTN_0, event->value ? "press" : "release");
            consume = true;
        }
        break;

    case INPUT_EV_ABS:
        if (params.enabled && next.contact.open && !next.contact.decided &&
            event->code ==
                (temp_layer_touch_edge_on_x_axis(config->edge) ? INPUT_ABS_X : INPUT_ABS_Y) &&
            trigger_layer_allowed(config, params.layer) &&
            temp_layer_touch_contact_sample(
                &next.contact,
                temp_layer_touch_in_strip(config->edge, event->value, config->max, params.width))) {
            hold = true;
            next.button_window = true;
        }
        break;

    default:
        break;
    }

    if (event->sync) {
        temp_layer_touch_contact_report(&next.contact, config->start_reports);
    }

    if (!temp_layer_touch_generation_stable((uint32_t)generation,
                                            (uint32_t)atomic_get(&data->generation))) {
        follow_discarded_touch(dev, stream, event);
        k_mutex_unlock(&stream->lock);
        return stop_event(event);
    }

    /* Everything above is speculative. Commit only after the settings
     * snapshot has survived the complete decision, so a discarded event leaves
     * nothing behind that the snapshot decided; only its touch edge is kept.
     * A writer that starts after this check is ordered after this event and
     * supplies the next event's snapshot. */
    if (drop) {
        drop_layer(dev, stream);
    }

    stream->contact = next.contact;
    stream->button_window = next.button_window;
    stream->suppressed_buttons = next.suppressed_buttons;

    if (hold) {
        const bool route_changed = hold_layer(dev, stream, params.layer);

        /* The listener chose this event's route before calling us. When this
         * coordinate raises the target layer, keep it out of the old route. */
        if (route_changed) {
            consume = true;
        }
    }

    k_mutex_unlock(&stream->lock);
    return consume ? stop_event(event) : ZMK_INPUT_PROC_CONTINUE;
}

static int temp_layer_touch_init(const struct device *dev) {
    struct temp_layer_touch_data *data = dev->data;

    k_mutex_init(&data->settings_lock);
    for (size_t i = 0U; i < TEMP_LAYER_TOUCH_STREAM_COUNT; i++) {
        k_mutex_init(&data->streams[i].lock);
    }

    return 0;
}

static const struct zmk_input_processor_driver_api temp_layer_touch_driver_api = {
    .handle_event = temp_layer_touch_handle_event,
};

#define TEMP_LAYER_TOUCH_ON_X_AXIS(n) (DT_INST_ENUM_IDX(n, edge) <= TEMP_LAYER_TOUCH_EDGE_LEFT)
#define TEMP_LAYER_TOUCH_AXIS_MAX(n)                                                               \
    (TEMP_LAYER_TOUCH_ON_X_AXIS(n) ? DT_INST_PROP(n, x_max) : DT_INST_PROP(n, y_max))

#define TEMP_LAYER_TOUCH_TRIGGER_LAYERS(n)                                                         \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, trigger_layers),                                          \
                (static const uint8_t temp_layer_touch_trigger_layers_##n[] =                      \
                     DT_INST_PROP(n, trigger_layers);),                                            \
                ())

#define TEMP_LAYER_TOUCH_ASSERT_TRIGGER_LAYER(idx, n)                                              \
    BUILD_ASSERT(DT_INST_PROP_BY_IDX(n, trigger_layers, idx) < ZMK_KEYMAP_LAYERS_LEN,              \
                 "trigger-layers must name existing keymap layer IDs");

#define TEMP_LAYER_TOUCH_INST(n)                                                                   \
    TEMP_LAYER_TOUCH_TRIGGER_LAYERS(n)                                                             \
    BUILD_ASSERT(DT_INST_PROP(n, layer) >= 0 && DT_INST_PROP(n, layer) < ZMK_KEYMAP_LAYERS_LEN,    \
                 "layer must name a layer this keymap has");                                       \
    BUILD_ASSERT(DT_INST_PROP(n, x_max) > 0 && DT_INST_PROP(n, x_max) <= UINT16_MAX,               \
                 "x-max must be 1-65535");                                                         \
    BUILD_ASSERT(DT_INST_PROP(n, y_max) > 0 && DT_INST_PROP(n, y_max) <= UINT16_MAX,               \
                 "y-max must be 1-65535");                                                         \
    BUILD_ASSERT(DT_INST_PROP(n, width) >= 0 &&                                                    \
                     DT_INST_PROP(n, width) <= TEMP_LAYER_TOUCH_AXIS_MAX(n),                       \
                 "width must fit within the pad along the edge's axis");                           \
    BUILD_ASSERT(DT_INST_PROP(n, start_reports) >= 1 && DT_INST_PROP(n, start_reports) <= 255,     \
                 "start-reports must be 1-255");                                                   \
    BUILD_ASSERT(DT_INST_PROP_LEN_OR(n, trigger_layers, 0) <= UINT8_MAX,                           \
                 "trigger-layers must contain at most 255 layer IDs");                             \
    LISTIFY(DT_INST_PROP_LEN_OR(n, trigger_layers, 0), TEMP_LAYER_TOUCH_ASSERT_TRIGGER_LAYER, (),  \
            n)                                                                                     \
    static struct temp_layer_touch_data temp_layer_touch_data_##n = {                              \
        .packed_params = ATOMIC_INIT(                                                              \
            (atomic_val_t)DT_INST_PROP(n, width) |                                                 \
            ((atomic_val_t)DT_INST_PROP(n, layer) << TEMP_LAYER_TOUCH_PARAMS_LAYER_SHIFT) |        \
            (DT_INST_PROP(n, start_disabled) ? 0 : BIT(TEMP_LAYER_TOUCH_PARAMS_ENABLED_BIT))),     \
        .generation = ATOMIC_INIT(0),                                                               \
    };                                                                                             \
    static const struct temp_layer_touch_config temp_layer_touch_config_##n = {                    \
        .edge = (enum temp_layer_touch_edge)DT_INST_ENUM_IDX(n, edge),                             \
        .max = TEMP_LAYER_TOUCH_AXIS_MAX(n),                                                       \
        .start_reports = DT_INST_PROP(n, start_reports),                                           \
        .pass_buttons = DT_INST_PROP(n, pass_buttons),                                             \
        .trigger_layers = COND_CODE_1(DT_INST_NODE_HAS_PROP(n, trigger_layers),                    \
                                      (temp_layer_touch_trigger_layers_##n), (NULL)),              \
        .trigger_layer_count = DT_INST_PROP_LEN_OR(n, trigger_layers, 0),                          \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, &temp_layer_touch_init, NULL, &temp_layer_touch_data_##n,            \
                          &temp_layer_touch_config_##n,                                           \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                        \
                          &temp_layer_touch_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TEMP_LAYER_TOUCH_INST)

#define TEMP_LAYER_TOUCH_DEVICE_REF(n) DEVICE_DT_INST_GET(n),

static const struct device *const temp_layer_touch_devices[] = {
    DT_INST_FOREACH_STATUS_OKAY(TEMP_LAYER_TOUCH_DEVICE_REF)};

static bool temp_layer_touch_device_valid(const struct device *dev) {
    for (size_t i = 0U; i < ARRAY_SIZE(temp_layer_touch_devices); i++) {
        if (temp_layer_touch_devices[i] == dev) {
            return true;
        }
    }

    return false;
}

int temp_layer_touch_get_params(const struct device *dev, struct temp_layer_touch_params *out) {
    if (dev == NULL || out == NULL) {
        return -EINVAL;
    }
    if (!temp_layer_touch_device_valid(dev)) {
        return -ENODEV;
    }

    struct temp_layer_touch_data *data = dev->data;
    *out = unpack_params(atomic_get(&data->packed_params));

    return 0;
}

int temp_layer_touch_set_params(const struct device *dev,
                                const struct temp_layer_touch_params *params) {
    if (dev == NULL || params == NULL) {
        return -EINVAL;
    }
    if (!temp_layer_touch_device_valid(dev)) {
        return -ENODEV;
    }

    const struct temp_layer_touch_config *config = dev->config;

    if (params->layer >= ZMK_KEYMAP_LAYERS_LEN || params->width > config->max) {
        LOG_WRN("%s: rejected layer %u, width %u", dev->name, params->layer, params->width);
        return -EINVAL;
    }

    struct temp_layer_touch_data *data = dev->data;
    k_mutex_lock(&data->settings_lock, K_FOREVER);
    struct temp_layer_touch_params previous;
    k_spinlock_key_t key = k_spin_lock(&data->params_lock);
    previous = unpack_params(atomic_get(&data->packed_params));
    atomic_inc(&data->generation);
    atomic_set(&data->packed_params, pack_params(params));
    atomic_inc(&data->generation);
    k_spin_unlock(&data->params_lock, key);

    /* Disabling is a complete operation, not a promise to clean up when the
     * pad happens to report again. Per-stream locks order this cleanup after
     * any old-generation event already committing on that stream. */
    if (previous.enabled && !params->enabled) {
        for (size_t i = 0U; i < TEMP_LAYER_TOUCH_STREAM_COUNT; i++) {
            struct temp_layer_touch_stream *stream = &data->streams[i];

            k_mutex_lock(&stream->lock, K_FOREVER);
            drop_layer(dev, stream);
            temp_layer_touch_contact_close(&stream->contact);
            stream->button_window = false;
            stream->suppressed_buttons = 0U;
            k_mutex_unlock(&stream->lock);
        }
    }

    k_mutex_unlock(&data->settings_lock);

    LOG_DBG("%s: %s, layer %u, width %u", dev->name, params->enabled ? "on" : "off", params->layer,
            params->width);

    return 0;
}
