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

The driver fixture pins forwarded and suppressed button releases at both
unstable-snapshot checks, on both streams, including `pass-buttons` and touch
release. Settings instability may discard a new decision, but must not leave
a previously forwarded button down. The former assertions that consumed
unrelated/touch releases during a write were assertion defects and are replaced
by release-preservation assertions; stale presses and coordinates remain
consumed. Kernel/layer ownership and actual split delivery stay separate.

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

## Retired legacy simulation

The active `tests/bsim` directory was removed on 2026-10-05. Before removal,
every source and ordered snapshot was SHA256-verified in an external archive.
Its retirement manifest retains the raw traces and assertion mapping. Archive
locations belong to the consuming keyboard integration repository and are not
a prerequisite for these module-owned checks.
No failed snapshot was accepted as passing.

| Retired case | Behavioral replacement | Transport boundary |
| --- | --- | --- |
| split-edge | The owner fixtures above retain state/arithmetic/error assertions | Ordered physical split/HID/LED/battery delivery and timing: pending actual-device evidence |

For the affected case, exercise the same contact/layer/settings/power or
congestion/reconnect transition on the production board. Capture each report
value/order/timestamp and verify balanced releases and specified deadlines.
Do not mark the archived exact radio trace as covered by a host stub.

## Current test ownership and compatibility

Run the relevant public interface/build checks on both unchanged stock ZMK
and DYA when this owner supports both. See the selected core in the manifest.

Canonical entries (run from the repository root):

```bash
bash tests/run-docker.sh
```

[maintenance.json](maintenance.json) records supported cores, current commands
and explicit gaps. It is a reference map, not a passing test or a claim that
registration/coverage totals establish completeness. Preserve ordered outputs,
raw timestamps and the first failure. Read actual dependencies before execution.

## Explicit offline integration

The existing `tests/integration/run.sh` also accepts an already resolved cache
through `ZMK_TEST_OFFLINE_WORKSPACE`, with a fresh `ZMK_TEST_OUTPUT` directory.
Run this entry inside the same build image with the module mounted at `/src`.
The offline path makes an isolated west workspace using the original fixture
manifest, an unchanged local Git clone of ZMK and symlinks to cached dependencies; it never fetches or patches
the core. Exact supported core, Zephyr and required library revisions and clean
source state are checked before a build; existing outputs are rejected.
The combined processors/vector runners also require
`ZMK_TEST_OFFLINE_UPSTREAM_WORKSPACE`. Other runners select `upstream` or `dya`
as their first argument (the inertia default remains upstream).

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
