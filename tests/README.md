# Module-owned tests

Run from this checkout. Docker + Bash is the host entry point; no keyboard
checkout, shared test dispatcher or simulator repository is required.

```bash
bash tests/run-docker.sh
ZMK_TEST_MODE=full bash tests/run-docker.sh
```

On PowerShell, set `$env:ZMK_TEST_MODE="full"` for the second command
and invoke Git Bash explicitly. Remove the variable for the default quick run.

Quick executes **all behavioral fixtures and scenarios** once with
AddressSanitizer/UndefinedBehaviorSanitizer. Full repeats those same assertions
under the retained optimization, coverage and (where already present) 32-bit
variants. CI uses quick for push/PR and full for scheduled/manual runs.
Unknown mode values fail; there is no silent skipped success.

## Retained behavioral contracts

| Fixture | Assertions |
| --- | --- |
| [test_temp_layer_touch_core.c](test_temp_layer_touch_core.c) | Edge classification and timeout boundaries |
| [runtime/driver_harness.c](runtime/driver_harness.c) | Independent contact streams, layer ownership and cancellation |
| [runtime/custom_settings_harness.c](runtime/custom_settings_harness.c) | Settings application and validation |
| [test_runtime_coverage.py](test_runtime_coverage.py) | Actual full runtime entry rejects empty, unrelated and partially measured reports |

Each fixture keeps its existing normal, boundary/invalid, independent-state,
reset/cancellation and failure/recovery scenarios where applicable. Checks
assert ordered outputs and explicit time inputs. Coverage is a full-mode gate,
not a reason to replace assertions or a hardware completeness claim.

The driver fixture pins original button payload/sync at both unstable settings
snapshot checks on both streams. Settings instability may neutralize coordinate
decisions; every KEY edge remains transparent. Contact and layer-claim cleanup
stay local. The shared final filter owns button pairs and report disposition.

The driver harness now derives all four actual production state/config structs
instead of maintaining copied layouts. All source-line-marked function tests,
core-header assertions and settings validation remain independent; complete
registration/layer/input-listener behavior stays at the native boundary.


The canonical host/CI entry also requires executable gcov lines for every
gated real function; macros are measured in their callers. Recording tools
exercise the actual full entry, while normal full runs measure the real compiled
fixtures. Offline staging rejects outputs inside sources/dependencies and parent
symlink aliases, overlapping project paths and west/config metadata collisions
before writing. External dependencies and sibling outputs remain supported.
The parseable wrong-archive control must fail its missing-digest-rejection
assertion when digest checking is bypassed; a tar error is not reproduction.

## Separate integration and physical contracts

Keep the existing owner `run-integration-docker.sh` commands and accepted
upstream/DYA variants for changed APIs, DTS/Kconfig/registration or actual
kernel boundaries. Native fixtures and negative guards are retained. Do not
run all firmware variants for a pure arithmetic edit. Host extracted functions
and controlled work queues do not prove real scheduling or transport delivery.


The existing integration command runs one native binary three times. To use
an already resolved upstream or DYA west workspace without fetching or a board
build, run `bash tests/run-native.sh /absolute/west/workspace`. Record its
revision, instructions and configuration first; this command does not select
or update dependency versions. `ZMK_TEST_OUTPUT` chooses the evidence directory.

## Retired simulation and gaps

The split-edge simulation source and ordered snapshots were externally archived
before removal. State/arithmetic/error assertions remain in the owner fixtures;
physical USB/BLE/split delivery, timing, power and recovery require actual-device
evidence. No failed snapshot was accepted.

## Explicit offline integration

The existing `tests/integration/run.sh` also accepts an already resolved cache
through `ZMK_TEST_OFFLINE_WORKSPACE`, with a fresh `ZMK_TEST_OUTPUT` directory.
Run this entry inside the same build image with the module mounted at `/src`.
The offline path makes an isolated west workspace using the original fixture
manifest, an unchanged local Git clone of ZMK and symlinks to cached dependencies; it never fetches or patches
the core. Exact supported core, Zephyr and required library revisions and clean
source state are checked before a build; existing outputs are rejected.
Select `upstream` or `dya` as the first argument.

DYA fixtures that enable custom settings require
`ZMK_TEST_CUSTOM_SETTINGS_ARCHIVE`: the SHA256-checked git archive of the pinned
`c6a7fef3a3be3d3ace5de9a4b0628c6418cd1f3f` test dependency, without local edits.
The accepted archive digest is recorded in `tests/integration/offline.py`.
All existing fixture assertions and cases run unchanged after preparation.
Test offline admission and rejection with
`python3 -m unittest discover -s tests/integration -p test_offline.py -v`.
Core updates require an explicit pin review rather than an implicit fetch.

The preparation uses Linux symlinks and runs inside the documented build image.
`ZMK_TEST_OFFLINE_NEGATIVE_CONTROL=skip-identity` in the admission test command
intentionally bypasses dependency identity validation; its rejection tests must fail.

Standalone native/kernel entries reject existing output directories and symlinks
before invoking west or touching prior logs. The host entry also runs
`test_output_guard.py`, which preserves a sentinel and checks zero west calls
for existing, linked and dangling output paths.

For example, run the stock fixture inside the build image:

```bash
ZMK_TEST_OFFLINE_WORKSPACE=/resolved/upstream \
ZMK_TEST_OUTPUT=/tmp/temp-layer-touch-upstream-v1 \
bash /src/tests/integration/run.sh upstream
```

Select `dya` with the resolved DYA workspace for its variant; provide the pinned
custom-settings archive when that fixture enables custom settings.

CI checks out the companion from explicit repository variables
`ZMK_SUPPRESS_REPOSITORY` and `ZMK_SUPPRESS_REVISION` (full reviewed commit).
An unset dependency fails explicitly; no scenarios are disabled. Configure these
only after the independent repository is published. Local direct entries keep
using `ZMK_TEST_SUPPRESS_ROOT`. GitHub execution is separate from local evidence.

## Responsibility and assertion map

Edge-touch owns initial edge recognition and temporary-layer claims. The
contact start window counts reports only for edge recognition. It is not a
button suppression window. Shared claims, external-owner protection and
per-stream/settings locks remain necessary to lower only a layer owned here.
Only a coordinate that raises a new route is neutralized with original sync.
All original button events pass unchanged, also across unstable settings.

| Boundary | Retained fixture/assertions |
| --- | --- |
| Pure edge/start-window decisions | test_temp_layer_touch_core.c: same boundaries, trigger origins, complete contact/report and generation checks |
| Layer claims / settings races | runtime/driver_harness.c: all prior independent streams, activation notification errors, external/shared ownership, lost-release restart, disable and stale snapshot cases |
| Runtime tuning | runtime/custom_settings_harness.c: same enabled/layer/width atomic apply, validation and unique keys |
| Whole actual driver/API/layers | integration/module/src/runtime_test.c: same eight groups, three identical-binary runs per core |
| Final output composition | Last native group: actual input/listener/HID with explicitly supplied common filter, delayed edge tap silent and first new-contact pointer click accepted on default/child routes |

The copied config, stream, node data and ownership structs were removed from
the driver harness; it derives the real production definitions, retaining
function lifting/source line gates. No test scenario or assertion was removed.
Targeted prior source/fixtures were externally archived with hash inventory.
The earlier extraction removed one unused invalid-event-code macro and corrected
stale comments without changing its algorithm. The current settings dispatch fix
is described below; public API/DTS/Kconfig remain unchanged. The neutral marker
already uses INPUT_MSC_SCAN. Native direct-event indices are synthetic; the
final group separately uses the real input route.

The producer's existing raw touch convention uses nonzero for contact-on;
physical helper input normalizes nonzero values. Arbitrary raw negative TOUCH
semantics are not unified by this review. Source loss, arbitrary interleavings,
physical transport/timing/power and split recovery remain gaps. Host stands-ins
and native HID storage do not establish actual device/host delivery.

## Independent final suppression

Production has no filter dependency; original button payload/sync passes unchanged.
Composition entries require an explicit `ZMK_TEST_SUPPRESS_ROOT` checkout.
The final filter takes a direct route mask and retains masks actually traversed
during a contact through delayed taps, until a distinct new touch. It has no
profile/settings registry, legacy adapter or layer watcher.

Common repeat/overlap, all 17 slots, owed release and frame assertions belong to
`zmk-input-suppress` host/native tests. Producer algorithms and original KEY
transparency stay here. Actual listener composition verifies delayed edge taps
and first new-contact clicks. Profile/CAS/settings and silent-layer-observation
assertions were retired with those interfaces; direct masks and no inherited
suppression without a routed event are covered by current composition.
Prior targeted fixtures, raw failures and hashes remain externally archived.
Duplicate migration/review prose is consolidated here; no current assertion was
removed. Empty runtime link/exclusion lists were removed without changing cases.

## Scheduled-work and settings regressions

The two-node settings fixture preserves all primary range/read assertions and
runs them on both macro expansions. It tests every own/peer descriptor, ignored
foreign events and boot application to both nodes. Targeted dispatch avoids foreign
edits incrementing another node's contact generation. Source-button transparency,
layer claims and final-mask composition retain their existing assertions.

DYA native integration now explicitly enables the actual settings adapter and
registry. A linker wrapper pauses the last real registry read in writer A while
writer B commits another field, then verifies the newest width and enabled values.
The adapter mutex serializes the full read/apply transaction. Eight existing native
groups remain on both cores; DYA adds this ninth group, one binary three times.
For standalone DYA native checks set ZMK_TEST_CUSTOM_SETTINGS=1 with the pinned
registry workspace. No production test hook or filter dependency is introduced.

## Selected workspace and module providers

Workspace selection uses tests/integration/workspace-env.sh to export the
selected manifest's ZEPHYR_MODULES and explicit Zephyr package/base paths. A
Zephyr symlink to a different cache must not select that cache's module providers.
The owner test_workspace_env.py exercises actual selection with a poisoned ambient
list, a cache symlink, module paths containing spaces, failed project discovery and
an empty selection. The keep-ambient negative control must fail these assertions.
Full integration retains all supported-core, role, persistence and invalid-DTS
cases. No dependency is patched or required feature disabled.
