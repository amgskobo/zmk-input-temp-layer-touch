# ZMK Temp Layer Touch Input Processor

[日本語](README_JA.md)

Holds a layer for as long as a contact that started at one edge of a pad lasts.
It is a trackpad's scroll strip, recognised from coordinates on the keyboard
that owns the keymap, for a pad whose own driver cannot raise the layer: a pad
on a split peripheral.

Its primary purpose is to replace the IQS7211E driver's former built-in
right-side `scroll-slider-layer` with a reusable input processor. The contact
keeps the former slider's UX, while two instances
can provide both right- and left-side sliders and can work after raw absolute
coordinates cross a split link.

The repository and public processor names are aligned:

| Item | Name |
| :--- | :--- |
| West/Zephyr module | `zmk-input-temp-layer-touch` |
| Devicetree compatible | `zmk,input-processor-temp-layer-touch` |
| Kconfig | `ZMK_INPUT_PROCESSOR_TEMP_LAYER_TOUCH` |
| Runtime API prefix | `temp_layer_touch_` |

## Why this is its own module

Current IQS7211E driver versions no longer own a strip or inspect keymap
layers. On a BLE split the peripheral has no keymap: its pad reaches the central
as raw input through `zmk,input-split`, and this processor performs all edge
routing and selective tap suppression there.

This reads the same thing, where a contact starts, from the coordinates the
central receives, so the driver stays a raw forwarder and any absolute pad gets
a strip. It has no upstream counterpart, so like
[abs2rel](https://github.com/amgskobo/zmk-input-abs2rel),
[padstick](https://github.com/amgskobo/zmk-input-padstick) and the other
originals it lives in a module of its own.

## Installation

```yaml
manifest:
  remotes:
    - name: amgskobo
      url-base: https://github.com/amgskobo
  projects:
    - name: zmk-input-temp-layer-touch
      remote: amgskobo
      revision: main
```

## Usage

Define an instance for each edge policy and put it **first in every route** of
every listener that uses it. A processor node may be shared by multiple pads
when their edge, coordinate range and other devicetree settings match; contact
and button state remain isolated by listener index.

```dts
/ {
    input_processors {
        edge_scroll: edge_scroll {
            compatible = "zmk,input-processor-temp-layer-touch";
            #input-processor-cells = <0>;
            layer = <1>;
            width = <50>;
            edge = "right";
        };
    };
};

&trackpad_listener {
    input-processors = <&edge_scroll>, <&zip_absolute_to_relative>, <&zip_inertia>;

    scroller {
        layers = <1>;
        input-processors = <&edge_scroll>, <&zip_absolute_to_relative_scroll>,
                           <&zip_xy_to_scroll_mapper>;
    };
};
```

The route is chosen per event from the layers up at that moment, and the layer
this raises changes the route, so a contact that starts on one route ends on
another. An instance missing from the route that ends the contact never sees its
release, and the layer stays up until the pad is touched again.

The pad has to report absolute coordinates (`INPUT_ABS_X` / `INPUT_ABS_Y`) with
`INPUT_BTN_TOUCH` around each contact, as the IQS7211E driver does with
`report-abs`.

### IQS7211E migration and two-side slider

Enable absolute reports. The processor replaces the removed driver properties
`scroll-slider-layer` and `scroll-start`; rotation remains
in the driver, and `edge` names the side after that rotation has been applied.

```dts
&iqs7211e {
    report-abs;
};

/ {
    input_processors {
        right_scroll_touch: right_scroll_touch {
            compatible = "zmk,input-processor-temp-layer-touch";
            #input-processor-cells = <0>;
            layer = <6>;
            edge = "right";
            width = <50>;
            x-max = <1024>;
            y-max = <1024>;
            trigger-layers = <0>;
        };

        left_scroll_touch: left_scroll_touch {
            compatible = "zmk,input-processor-temp-layer-touch";
            #input-processor-cells = <0>;
            layer = <6>;
            edge = "left";
            width = <50>;
            x-max = <1024>;
            y-max = <1024>;
            trigger-layers = <0>;
        };
    };
};
```

Put both instances before absolute-to-relative conversion in the base route
and in layer 6's scroll route. The absolute-to-relative stage must consume
`INPUT_BTN_TOUCH`, for example with its `suppress-btn-touch` option. Both
slider instances may target the same layer: shared claims are reference
counted, so one contact cannot release a layer still held by the other side.

```dts
&trackpad_listener {
    input-processors = <&right_scroll_touch>, <&left_scroll_touch>,
                       <&zip_absolute_to_relative>;

    scroller {
        layers = <6>;
        input-processors = <&right_scroll_touch>, <&left_scroll_touch>,
                           <&zip_absolute_to_relative_scroll>,
                           <&zip_xy_to_scroll_mapper>;
    };
};
```

### Configuration reference

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `layer` | int | required | Layer ID held while an edge contact lasts. |
| `width` | int | 40 | How far in from the edge the strip reaches, in the pad's coordinates. 0 is no strip. |
| `edge` | string | `"right"` | `right`, `left`, `top` or `bottom`, in the coordinates as they arrive (after any rotation the pad driver applies). |
| `x-max` | int | 1024 | Largest X coordinate the pad reports. |
| `y-max` | int | 1024 | Largest Y coordinate the pad reports. |
| `start-reports` | int | 3 | How many reports into a contact its position still decides whether it is an edge contact. |
| `trigger-layers` | array | any | Highest active layer IDs from which this slider may start. A target already held by another temp-layer-touch contact is also allowed. |
| `pass-buttons` | bool | false | Let the pad's button events through during an edge contact. |
| `start-disabled` | bool | false | Start with the strip switched off. |

`width` is measured in the absolute coordinate units reported by the pad; it
is 50 pixels only when one reported coordinate count corresponds to one pixel.
Set `x-max` and `y-max` to the actual maximum values in those reports.

To assign different layers to multiple edges, define one processor node per
edge and put every node first in every applicable route. Corners belong to all
overlapping strips, so avoid overlapping corner zones unless raising both
layers is intentional.

## How it works

- **Only where a contact starts counts.** After `INPUT_BTN_TOUCH` is pressed,
  the first `start-reports` reports are checked against the strip. A stroke that
  begins inside the pad and runs onto the edge later is an ordinary one, so the
  strip never fires in the middle of a pointer move. The window is more than one
  report because the first can trail the finger.
- **The strip test** preserves the former IQS7211E slider boundary: strictly more
  than `max - width` on the right and bottom edges, strictly less than `width`
  on the left and top, so every strip is `width` counts wide.
- **The start window preserves the former driver UX.** The default accepts the first
  three reports, corresponding to the former IQS7211E driver's `touch_count <= 2`
  decision window.
- **Optional trigger layers preserve the former driver control.** An omitted
  `trigger-layers` list allows every layer; otherwise the current highest
  layer ID must be listed, like the removed
  `scroll-slider-trigger-layers` property. The one extension is a target layer
  already held by another temp-layer-touch contact: a second pad may join that
  hold, so the first release cannot lower the layer under it.
- **The layer is raised on the input thread**, as the edge coordinate arrives,
  and the coordinate that raised it is consumed instead of continuing through
  the old route. The next event is routed with the new layer up. With the
  recommended abs2rel stage, the first coordinate on each axis establishes a
  reference and produces no movement, so this transition does not create a
  pointer step at the edge.
- **It only drops what it raised.** A layer that is already up when an edge
  contact starts is left to whatever raised it.
- **Concurrent edge contacts are reference-counted.** If multiple listeners or
  processor instances claim the same layer, it stays up until the final edge
  contact releases it. Per-event contact and button state never crosses from
  one listener to another.
- **The layer drops when the contact ends**, as `INPUT_BTN_TOUCH` is released. A
  touch press that arrives while the layer is still held also drops it: that
  contact's release was lost - with a split link, say - and nothing else would
  send it.
- **Taps from an edge contact are consumed.** A pad driver can report a tap only
  after the finger lifts, by which time the layer is down and the click would go
  down the pointer route. So `INPUT_BTN_0` to `INPUT_BTN_15` presses are consumed
  from the moment a contact is recognised as an edge contact until the next
  contact begins, and a release is consumed only when its press was.
  `pass-buttons` turns this off.

## Changing the values at runtime

With [zmk-feature-custom-settings](https://github.com/cormoran/zmk-feature-custom-settings)
and `CONFIG_ZMK_INPUT_TEMP_LAYER_TOUCH_CUSTOM_SETTINGS=y`, three values per instance are
published under the `amgskobo__tlt` subsystem, so a Studio client such as
[DYA Studio](https://studio.dya.cormoran.works/) lists and edits them:

| Key | Type | Meaning |
| :--- | :--- | :--- |
| `<node>.enabled` | bool | Whether an edge contact holds the layer at all. |
| `<node>.layer` | layer | The layer held. |
| `<node>.width` | int | The strip width, 0 to the pad's size on the edge's axis. |

The settings registry owns persistence; the module stores nothing. Updates are
bracketed by an atomic generation counter; an input event that overlaps a
settings change is discarded instead of escaping under mixed settings. A layer
already held stays held on its old number until its contact ends, and switching
the strip off lets go of a held layer at the pad's next event.

A physical-layout touch-pad node can link the pad to its strip, so a client
shows these next to the pad:

```dts
linked-device-identifiers = "abs_rel", "edge_scroll";
linked-subsystems = "amgskobo__a2r", "amgskobo__tlt";
```

## Tests

```sh
tests/run.sh
```

`tests/run.sh` compiles the pure decision header strictly and runs its checks,
optimised and under AddressSanitizer and UBSan, with the host C compiler.
`tests/run-integration-docker.sh upstream` and `... dya` build complete ZMK
firmware fixtures against upstream ZMK and the optional DYA/custom-settings
stack. GitHub Actions runs all three checks on every push and pull request.

## License

MIT License. See [LICENSE](LICENSE) for details.
