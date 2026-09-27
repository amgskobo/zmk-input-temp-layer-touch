/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <zmk-input-temp-layer-touch/temp_layer_touch.h>
#include <zmk-input-temp-layer-touch/temp_layer_touch_core.h>

typedef long atomic_t;
typedef long atomic_val_t;
typedef uint8_t zmk_keymap_layer_index_t;
typedef uint8_t zmk_keymap_layer_id_t;
typedef int k_spinlock_key_t;
typedef struct { int64_t ms; } k_timeout_t;
struct k_mutex { int depth; };
struct k_spinlock { int held; };
struct device { const void *config; void *data; const char *name; };
struct input_event { uint8_t type; uint16_t code; int32_t value; bool sync; };
struct zmk_input_processor_state { uint8_t input_device_index; };

#define ZMK_KEYMAP_LAYERS_LEN 8
#define TEMP_LAYER_TOUCH_STREAM_COUNT 2
#define TEMP_LAYER_TOUCH_PARAMS_LAYER_SHIFT 16U
#define TEMP_LAYER_TOUCH_PARAMS_ENABLED_BIT 24U
#define TEMP_LAYER_TOUCH_INVALID_CODE 0xFFF
#define INPUT_EV_KEY 0x01
#define INPUT_EV_REL 0x02
#define INPUT_EV_ABS 0x03
#define INPUT_ABS_X 0x00
#define INPUT_ABS_Y 0x01
#define INPUT_BTN_0 0x100
#define INPUT_BTN_TOUCH 0x14a
#define INPUT_KEY_A 30
#define ZMK_INPUT_PROC_CONTINUE 0
#define ZMK_INPUT_PROC_STOP 1
#define BIT(n) (1UL << (n))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ARG_UNUSED(x) ((void)(x))
#define K_FOREVER ((k_timeout_t){-1})
/* Unevaluated printf() keeps the format strings checked and the arguments used. */
#define LOG_DBG(fmt, ...) ((void)sizeof(printf(fmt, ##__VA_ARGS__)))
#define LOG_WRN(fmt, ...) ((void)warnings++, LOG_DBG(fmt, ##__VA_ARGS__))
#define LOG_ERR(fmt, ...) ((void)errors++, LOG_DBG(fmt, ##__VA_ARGS__))

struct temp_layer_touch_config {
    enum temp_layer_touch_edge edge;
    uint16_t max;
    uint8_t start_reports;
    bool pass_buttons;
    const uint8_t *trigger_layers;
    uint8_t trigger_layer_count;
};
struct temp_layer_touch_stream {
    struct k_mutex lock;
    struct temp_layer_touch_contact contact;
    bool held;
    uint8_t held_layer;
    bool button_window;
    uint16_t suppressed_buttons;
};
struct temp_layer_touch_data {
    atomic_t packed_params;
    atomic_t generation;
    struct k_spinlock params_lock;
    struct k_mutex settings_lock;
    struct temp_layer_touch_stream streams[TEMP_LAYER_TOUCH_STREAM_COUNT];
};
struct temp_layer_touch_ownership {
    uint16_t claims;
    bool raised;
};

static struct temp_layer_touch_ownership ownership[ZMK_KEYMAP_LAYERS_LEN];
static struct k_mutex ownership_lock;
static struct device right_pad;
static struct device top_pad;
static const struct device *const temp_layer_touch_devices[] = {&right_pad, &top_pad};

static int warnings;
static int errors;
/* Counts atomic_get() calls down; at zero a writer lands on that read. */
static int writer_lands_on_read;
static uint32_t active_layers;
/* Layers the keymap refuses to raise or to lower. */
static uint32_t stuck_down;
static uint32_t stuck_up;
static int activate_result;
static int deactivate_result;
static zmk_keymap_layer_index_t highest_index;
/* The keymap's order: index -> layer ID. Reordered in one test. */
static zmk_keymap_layer_id_t layer_ids[ZMK_KEYMAP_LAYERS_LEN] = {0, 1, 2, 3, 4, 5, 6, 7};

static atomic_val_t atomic_get(const atomic_t *value) {
    if (writer_lands_on_read > 0 && --writer_lands_on_read == 0) {
        *(atomic_t *)value += 2;
    }
    return *value;
}
static void atomic_set(atomic_t *value, atomic_val_t next) { *value = next; }
static void atomic_inc(atomic_t *value) { (*value)++; }
static void k_mutex_init(struct k_mutex *mutex) { mutex->depth = 0; }
static int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout) {
    ARG_UNUSED(timeout);
    mutex->depth++;
    return 0;
}
static int k_mutex_unlock(struct k_mutex *mutex) {
    assert(mutex->depth > 0);
    mutex->depth--;
    return 0;
}
static k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held);
    lock->held = 1;
    return 7;
}
static void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    assert(lock->held && key == 7);
    lock->held = 0;
}
static bool zmk_keymap_layer_active(uint8_t layer) { return (active_layers & BIT(layer)) != 0; }
static int zmk_keymap_layer_activate(uint8_t layer, bool locking) {
    assert(!locking);
    if ((stuck_down & BIT(layer)) == 0) {
        active_layers |= (uint32_t)BIT(layer);
    }
    return activate_result;
}
static int zmk_keymap_layer_deactivate(uint8_t layer, bool locking) {
    assert(!locking);
    if ((stuck_up & BIT(layer)) == 0) {
        active_layers &= ~(uint32_t)BIT(layer);
    }
    return deactivate_result;
}
static zmk_keymap_layer_index_t zmk_keymap_highest_layer_active(void) { return highest_index; }
static zmk_keymap_layer_id_t zmk_keymap_layer_index_to_id(zmk_keymap_layer_index_t index) {
    return layer_ids[index];
}

/* DRIVER_FUNCTIONS */

static struct temp_layer_touch_data right_data;
static struct temp_layer_touch_data top_data;
static struct zmk_input_processor_state stream1 = {.input_device_index = 1};

static int send(struct device *dev, struct zmk_input_processor_state *state, uint8_t type,
                uint16_t code, int32_t value, bool sync) {
    struct input_event event = {type, code, value, sync};
    const int ret = temp_layer_touch_handle_event(dev, &event, 0, 0, state);
    if (ret == ZMK_INPUT_PROC_STOP) {
        assert(event.code == TEMP_LAYER_TOUCH_INVALID_CODE && !event.sync);
    } else {
        assert(event.code == code && event.sync == sync);
    }
    return ret;
}

static int touch(struct device *dev, struct zmk_input_processor_state *state, bool down) {
    return send(dev, state, INPUT_EV_KEY, INPUT_BTN_TOUCH, down, false);
}

static int abs_at(struct device *dev, struct zmk_input_processor_state *state, uint16_t code,
                  int32_t value) {
    return send(dev, state, INPUT_EV_ABS, code, value, true);
}

static struct temp_layer_touch_params params_of(bool enabled, uint8_t layer, uint16_t width) {
    return (struct temp_layer_touch_params){enabled, layer, width};
}

static void reset_keymap(void) {
    for (size_t i = 0; i < ARRAY_SIZE(ownership); i++) {
        ownership[i] = (struct temp_layer_touch_ownership){0};
    }
    active_layers = 0;
    stuck_down = stuck_up = 0;
    activate_result = deactivate_result = 0;
    highest_index = 0;
}

static void test_api(void) {
    struct temp_layer_touch_params out;
    struct device stranger = {.config = right_pad.config, .data = &right_data};
    const struct temp_layer_touch_params valid = params_of(true, 2, 100);

    right_data.streams[0].lock.depth = 5;
    right_data.streams[1].lock.depth = 5;
    right_data.settings_lock.depth = 5;
    assert(temp_layer_touch_init(&right_pad) == 0);
    assert(right_data.streams[1].lock.depth == 0 && right_data.settings_lock.depth == 0);
    assert(right_data.streams[0].lock.depth == 0);
    assert(temp_layer_touch_init(&top_pad) == 0);
    assert(temp_layer_touch_get_params(NULL, &out) == -EINVAL);
    assert(temp_layer_touch_get_params(&right_pad, NULL) == -EINVAL);
    assert(temp_layer_touch_get_params(&stranger, &out) == -ENODEV);
    assert(temp_layer_touch_get_params(&top_pad, &out) == 0);
    assert(out.enabled && out.layer == 3 && out.width == 50);

    assert(temp_layer_touch_set_params(NULL, &valid) == -EINVAL);
    assert(temp_layer_touch_set_params(&right_pad, NULL) == -EINVAL);
    assert(temp_layer_touch_set_params(&stranger, &valid) == -ENODEV);
    const struct temp_layer_touch_params far_layer = params_of(true, ZMK_KEYMAP_LAYERS_LEN, 10);
    const struct temp_layer_touch_params wide = params_of(true, 2, 1001);
    assert(temp_layer_touch_set_params(&right_pad, &far_layer) == -EINVAL);
    assert(temp_layer_touch_set_params(&right_pad, &wide) == -EINVAL);
    assert(warnings == 2);

    /* A write leaves the generation even, two further on. */
    assert(temp_layer_touch_set_params(&right_pad, &valid) == 0);
    assert(right_data.generation == 2);
    assert(temp_layer_touch_get_params(&right_pad, &out) == 0);
    assert(out.enabled && out.layer == 2 && out.width == 100);

    /* The packing keeps every field apart at its extremes. */
    const struct temp_layer_touch_params edge = params_of(false, ZMK_KEYMAP_LAYERS_LEN - 1, 1000);
    assert(temp_layer_touch_set_params(&right_pad, &edge) == 0);
    assert(temp_layer_touch_get_params(&right_pad, &out) == 0);
    assert(!out.enabled && out.layer == ZMK_KEYMAP_LAYERS_LEN - 1 && out.width == 1000);
    assert(temp_layer_touch_set_params(&right_pad, &valid) == 0);
}

static void test_edge_contact(void) {
    reset_keymap();
    /* A contact that starts in the strip raises the layer and leaves the old
     * route; the next event goes on through the new one. */
    assert(touch(&right_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_Y, 950) == ZMK_INPUT_PROC_CONTINUE); /* wrong axis */
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 950) == ZMK_INPUT_PROC_STOP);
    assert(zmk_keymap_layer_active(2) && ownership[2].claims == 1 && ownership[2].raised);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 960) == ZMK_INPUT_PROC_CONTINUE); /* decided */

    /* The button window consumes a click that started from the edge, and the
     * release that pairs with it; other keys pass. */
    assert(send(&right_pad, NULL, INPUT_EV_KEY, INPUT_BTN_0, 1, false) == ZMK_INPUT_PROC_STOP);
    assert(send(&right_pad, NULL, INPUT_EV_KEY, INPUT_BTN_0, 0, false) == ZMK_INPUT_PROC_STOP);
    assert(send(&right_pad, NULL, INPUT_EV_KEY, INPUT_BTN_0 + 1, 0, false) ==
           ZMK_INPUT_PROC_CONTINUE);
    assert(send(&right_pad, NULL, INPUT_EV_KEY, INPUT_BTN_0 + 16, 1, false) ==
           ZMK_INPUT_PROC_CONTINUE);
    assert(send(&right_pad, NULL, INPUT_EV_KEY, INPUT_KEY_A, 1, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(send(&right_pad, NULL, INPUT_EV_REL, 0, 3, true) == ZMK_INPUT_PROC_CONTINUE);

    /* The release drops it. */
    assert(touch(&right_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(!zmk_keymap_layer_active(2) && ownership[2].claims == 0 && !ownership[2].raised);

    /* A contact that starts inside the pad is decided after start-reports. */
    assert(touch(&right_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 100) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 101) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 102) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 990) == ZMK_INPUT_PROC_CONTINUE);
    assert(!zmk_keymap_layer_active(2));
    /* Outside the window a click is the pad's own. */
    assert(send(&right_pad, NULL, INPUT_EV_KEY, INPUT_BTN_0, 1, false) == ZMK_INPUT_PROC_CONTINUE);

    /* A press with the layer still held means its release was lost. */
    assert(touch(&right_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_STOP);
    assert(touch(&right_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(!zmk_keymap_layer_active(2) && ownership[2].claims == 0);
    assert(touch(&right_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);

    /* With no touch open, coordinates do nothing. */
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_CONTINUE);
    assert(!zmk_keymap_layer_active(2));
}

static void test_streams(void) {
    struct zmk_input_processor_state unknown = {.input_device_index = TEMP_LAYER_TOUCH_STREAM_COUNT};
    reset_keymap();

    /* An index past the listeners is passed through untouched. */
    assert(touch(&right_pad, &unknown, true) == ZMK_INPUT_PROC_CONTINUE);

    /* Two pads share a layer: it stays until both let go. */
    assert(touch(&right_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_STOP);
    assert(touch(&right_pad, &stream1, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, &stream1, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_CONTINUE);
    assert(ownership[2].claims == 2);
    assert(touch(&right_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(zmk_keymap_layer_active(2) && ownership[2].claims == 1);

    /* Something else dropped it; the remaining pad raises it again. */
    active_layers = 0;
    assert(touch(&right_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_STOP);
    assert(zmk_keymap_layer_active(2) && ownership[2].claims == 2);
    assert(touch(&right_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(touch(&right_pad, &stream1, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(!zmk_keymap_layer_active(2) && ownership[2].claims == 0);

    /* A layer something else raised is left alone, and never dropped. */
    active_layers = (uint32_t)BIT(2);
    assert(touch(&right_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_CONTINUE);
    assert(ownership[2].claims == 0);
    assert(touch(&right_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(zmk_keymap_layer_active(2));
}

static void test_keymap_refusals(void) {
    reset_keymap();
    /* The keymap would not raise it: nothing is owed. */
    stuck_down = (uint32_t)BIT(2);
    assert(touch(&right_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_CONTINUE);
    assert(ownership[2].claims == 0 && !right_data.streams[0].held && warnings == 1);
    assert(touch(&right_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    stuck_down = 0;

    /* Raised although its notification failed: owed all the same. */
    activate_result = -EIO;
    assert(touch(&right_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_STOP);
    assert(ownership[2].claims == 1 && warnings == 2);
    activate_result = 0;

    /* Lowered with a failed notification, or kept up by the keymap. */
    deactivate_result = -EIO;
    assert(touch(&right_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(!zmk_keymap_layer_active(2) && warnings == 3);
    deactivate_result = 0;
    assert(touch(&right_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_STOP);
    stuck_up = (uint32_t)BIT(2);
    assert(touch(&right_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(zmk_keymap_layer_active(2) && !ownership[2].raised && ownership[2].claims == 0);
}

static void test_ownership_guards(void) {
    struct temp_layer_touch_stream stream = {0};
    reset_keymap();

    /* A stream that already holds does not claim twice. */
    stream.held = true;
    stream.held_layer = 4;
    assert(!hold_layer(&right_pad, &stream, 4));
    assert(ownership[4].claims == 0);

    /* The claim counter never wraps. */
    stream.held = false;
    ownership[4].claims = UINT16_MAX;
    assert(!hold_layer(&right_pad, &stream, 4));
    assert(!stream.held && warnings == 1);

    /* A release with no claim on record is reported and forgotten. */
    ownership[4].claims = 0;
    stream.held = true;
    drop_layer(&right_pad, &stream);
    assert(!stream.held && errors == 1 && ownership[4].claims == 0);

    /* The last claim on a layer this family did not raise lowers nothing. */
    ownership[4] = (struct temp_layer_touch_ownership){.claims = 1, .raised = false};
    active_layers = (uint32_t)BIT(4);
    stream.held = true;
    drop_layer(&right_pad, &stream);
    assert(!stream.held && zmk_keymap_layer_active(4));

    /* Not held: nothing to drop. */
    drop_layer(&right_pad, &stream);
    assert(ownership[4].claims == 0);
}

static void test_trigger_layers(void) {
    reset_keymap();
    /* top_pad: layer 3, width 50, top edge (y axis), origin allowlist {0, 1},
     * buttons passed through. */
    assert(touch(&top_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    /* A key whose code equals the edge's axis is still a key, not a sample. */
    assert(send(&top_pad, NULL, INPUT_EV_KEY, INPUT_ABS_Y, 10, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(!zmk_keymap_layer_active(3));
    assert(abs_at(&top_pad, NULL, INPUT_ABS_X, 10) == ZMK_INPUT_PROC_CONTINUE); /* wrong axis */
    assert(abs_at(&top_pad, NULL, INPUT_ABS_Y, 10) == ZMK_INPUT_PROC_STOP);
    assert(zmk_keymap_layer_active(3));
    assert(send(&top_pad, NULL, INPUT_EV_KEY, INPUT_BTN_0, 1, false) == ZMK_INPUT_PROC_CONTINUE);

    /* The second pad joins a target this family holds, although the active
     * layer is the target and not an allowed origin. */
    highest_index = 3;
    assert(touch(&top_pad, &stream1, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&top_pad, &stream1, INPUT_ABS_Y, 10) == ZMK_INPUT_PROC_CONTINUE);
    assert(ownership[3].claims == 2);
    assert(touch(&top_pad, &stream1, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(touch(&top_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(!zmk_keymap_layer_active(3));

    /* The target raised by something else: no exception. */
    active_layers = (uint32_t)BIT(3);
    assert(touch(&top_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&top_pad, NULL, INPUT_ABS_Y, 10) == ZMK_INPUT_PROC_CONTINUE);
    assert(ownership[3].claims == 0);
    assert(touch(&top_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    active_layers = 0;

    /* Another layer entirely is not an allowed origin. */
    highest_index = 5;
    assert(touch(&top_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&top_pad, NULL, INPUT_ABS_Y, 10) == ZMK_INPUT_PROC_CONTINUE);
    assert(!zmk_keymap_layer_active(3));
    assert(touch(&top_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);

    /* The allowlist names layer IDs: after a reorder, index 5 is ID 1. */
    layer_ids[5] = 1;
    assert(touch(&top_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&top_pad, NULL, INPUT_ABS_Y, 10) == ZMK_INPUT_PROC_STOP);
    assert(touch(&top_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    layer_ids[5] = 5;
    highest_index = 0;

    /* A single allowed origin is a list like any other. */
    static const uint8_t only_zero[] = {0};
    struct temp_layer_touch_config single = *(const struct temp_layer_touch_config *)top_pad.config;
    single.trigger_layers = only_zero;
    single.trigger_layer_count = 1;
    const void *saved_config = top_pad.config;
    top_pad.config = &single;
    highest_index = 5;
    assert(touch(&top_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&top_pad, NULL, INPUT_ABS_Y, 10) == ZMK_INPUT_PROC_CONTINUE);
    assert(!zmk_keymap_layer_active(3));
    assert(touch(&top_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    top_pad.config = saved_config;
    highest_index = 0;
}

static void test_runtime_edits(void) {
    reset_keymap();
    const struct temp_layer_touch_params on = params_of(true, 2, 100);
    const struct temp_layer_touch_params off = params_of(false, 2, 100);

    /* Disabled, an edge contact raises nothing. */
    assert(temp_layer_touch_set_params(&right_pad, &off) == 0);
    assert(touch(&right_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_CONTINUE);
    assert(!zmk_keymap_layer_active(2));
    assert(temp_layer_touch_set_params(&right_pad, &off) == 0); /* off -> off */
    assert(temp_layer_touch_set_params(&right_pad, &on) == 0);  /* off -> on */
    assert(touch(&right_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);

    /* Switching off drops what every stream holds, then and there. */
    assert(touch(&right_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_STOP);
    assert(send(&right_pad, NULL, INPUT_EV_KEY, INPUT_BTN_0, 1, false) == ZMK_INPUT_PROC_STOP);
    assert(touch(&right_pad, &stream1, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, &stream1, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_CONTINUE);
    assert(right_data.streams[1].held && right_data.streams[1].button_window);
    assert(temp_layer_touch_set_params(&right_pad, &on) == 0); /* on -> on keeps it */
    assert(zmk_keymap_layer_active(2));
    assert(temp_layer_touch_set_params(&right_pad, &off) == 0);
    for (size_t i = 0; i < TEMP_LAYER_TOUCH_STREAM_COUNT; i++) {
        const struct temp_layer_touch_stream *s = &right_data.streams[i];
        assert(!s->held && !s->contact.open && !s->button_window && s->suppressed_buttons == 0);
    }
    assert(!zmk_keymap_layer_active(2) && ownership[2].claims == 0);

    /* Switched off by a snapshot already in hand: the next event lets go. */
    assert(temp_layer_touch_set_params(&right_pad, &on) == 0);
    assert(touch(&right_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_STOP);
    right_data.packed_params = pack_params(&off);
    assert(send(&right_pad, NULL, INPUT_EV_REL, 0, 1, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(!zmk_keymap_layer_active(2) && !right_data.streams[0].held);
    right_data.packed_params = pack_params(&on);
    assert(touch(&right_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);

    /* An event that starts mid-write is dropped but keeps its touch edge. */
    right_data.generation++;
    right_data.streams[0].button_window = true;
    assert(touch(&right_pad, NULL, true) == ZMK_INPUT_PROC_STOP);
    assert(right_data.streams[0].contact.open && !right_data.streams[0].button_window);
    right_data.generation++;
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_STOP);
    assert(right_data.streams[0].held);
    right_data.generation++;
    assert(send(&right_pad, NULL, INPUT_EV_KEY, INPUT_KEY_A, 0, false) == ZMK_INPUT_PROC_STOP);
    assert(right_data.streams[0].held && right_data.streams[0].contact.open);
    right_data.generation++;
    assert(touch(&right_pad, NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(touch(&right_pad, NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    right_data.generation++;
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_STOP);
    assert(send(&right_pad, NULL, INPUT_EV_KEY, INPUT_KEY_A, 1, false) == ZMK_INPUT_PROC_STOP);
    right_data.generation++;
    assert(!zmk_keymap_layer_active(2));

    /* A write that lands during the decision discards it: the layer stays
     * down, and the touch edge is still followed. */
    writer_lands_on_read = 4;
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_STOP);
    assert(!zmk_keymap_layer_active(2) && !right_data.streams[0].contact.decided);
    assert(abs_at(&right_pad, NULL, INPUT_ABS_X, 999) == ZMK_INPUT_PROC_STOP);
    assert(zmk_keymap_layer_active(2));
    writer_lands_on_read = 4;
    assert(touch(&right_pad, NULL, false) == ZMK_INPUT_PROC_STOP);
    assert(!zmk_keymap_layer_active(2) && !right_data.streams[0].contact.open);
}

int main(void) {
    static const uint8_t origins[] = {0, 1};
    const struct temp_layer_touch_config right = {
        .edge = TEMP_LAYER_TOUCH_EDGE_RIGHT, .max = 1000, .start_reports = 3};
    const struct temp_layer_touch_config top = {
        .edge = TEMP_LAYER_TOUCH_EDGE_TOP, .max = 500, .start_reports = 3, .pass_buttons = true,
        .trigger_layers = origins, .trigger_layer_count = ARRAY_SIZE(origins)};
    const struct temp_layer_touch_params right_start = params_of(false, 1, 0);
    const struct temp_layer_touch_params top_start = params_of(true, 3, 50);
    right_data.packed_params = pack_params(&right_start);
    top_data.packed_params = pack_params(&top_start);
    right_pad = (struct device){.config = &right, .data = &right_data, .name = "right"};
    top_pad = (struct device){.config = &top, .data = &top_data, .name = "top"};

    test_api();
    warnings = 0;
    test_edge_contact();
    test_streams();
    test_keymap_refusals();
    warnings = 0;
    test_ownership_guards();
    test_trigger_layers();
    test_runtime_edits();

    assert(ownership_lock.depth == 0 && right_data.settings_lock.depth == 0);
    for (size_t i = 0; i < TEMP_LAYER_TOUCH_STREAM_COUNT; i++) {
        assert(right_data.streams[i].lock.depth == 0 && top_data.streams[i].lock.depth == 0);
    }
    puts("temp-layer-touch driver: PASS");
    return 0;
}
