#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Build the two-listener firmware fixture and run the native_sim self-tests
# against upstream ZMK or the DYA fork.

set -euo pipefail

variant="${1:-upstream}"
case "$variant" in
upstream | dya) ;;
*)
    echo "unknown integration variant '$variant': expected upstream or dya" >&2
    exit 2
    ;;
esac

tests_dir=/src/tests/integration

if [ -n "${ZMK_TEST_WORKSPACE:-}" ]; then
    work_dir="$ZMK_TEST_WORKSPACE/$variant"
    mkdir -p "$work_dir"
else
    work_dir="$(mktemp -d "${TMPDIR:-/tmp}/zmk-input-temp-layer-touch-integration.XXXXXX")"
    cleanup() {
        rm -rf "$work_dir"
    }
    trap cleanup EXIT HUP INT TERM
fi

mkdir -p "$work_dir/config"
cp "$tests_dir/config/$variant/west.yml" "$work_dir/config/west.yml"
cd "$work_dir"

if [ ! -d .west ]; then
    west init -l config
fi
west update --narrow --fetch-opt=--depth=1
west zephyr-export

export ZEPHYR_BASE="$work_dir/zephyr"

mkdir -p "$work_dir/build"

cmake_args=()
if [ "$variant" = dya ]; then
    cmake_args+=("-DEXTRA_CONF_FILE=$tests_dir/custom-settings.conf")
fi

firmware_dir="$work_dir/build/firmware"
rm -rf "$firmware_dir"

if ! west build -s "$work_dir/zmk/app" -d "$firmware_dir" -b xiao_ble/nrf52840/zmk -- \
    -DZMK_EXTRA_MODULES="/src;$tests_dir/firmware" \
    -DSHIELD=temp_layer_touch_test \
    "${cmake_args[@]}" >"$firmware_dir.log" 2>&1; then
    echo "FAILED: $variant firmware fixture did not build"
    tail -n 80 "$firmware_dir.log"
    exit 1
fi

firmware_config="$firmware_dir/zephyr/.config"
test -f "$firmware_dir/zephyr/zmk.uf2"
grep -q '^CONFIG_ZMK_INPUT_PROCESSOR_TEMP_LAYER_TOUCH=y' "$firmware_config"
if [ "$variant" = upstream ]; then
    if grep -q '^CONFIG_ZMK_INPUT_TEMP_LAYER_TOUCH_CUSTOM_SETTINGS=y' "$firmware_config"; then
        echo "FAILED: custom settings unexpectedly enabled"
        exit 1
    fi
else
    grep -q '^CONFIG_ZMK_INPUT_TEMP_LAYER_TOUCH_CUSTOM_SETTINGS=y' "$firmware_config"
    strings "$firmware_dir/zephyr/zmk.elf" >"$firmware_dir/zephyr/strings.txt"
    for key in amgskobo__tlt \
        right_scroll_touch.enabled right_scroll_touch.layer right_scroll_touch.width \
        left_scroll_touch.enabled left_scroll_touch.layer left_scroll_touch.width; do
        grep -Fxq "$key" "$firmware_dir/zephyr/strings.txt"
    done
fi
if grep -n 'input_processor_temp_layer_touch[a-z_]*\.c:[0-9]*:[0-9]*: warning' \
    "$firmware_dir.log"; then
    echo "FAILED: $variant firmware fixture warned in the processor"
    exit 1
fi
echo "PASS: $variant firmware fixture"

runtime_dir="$work_dir/build/runtime"
rm -rf "$runtime_dir"

if ! west build -s "$work_dir/zmk/app" -d "$runtime_dir" -b native_sim//zmk_test_mock -- \
    -DCONFIG_ASSERT=y -DZMK_CONFIG="$tests_dir/runtime" \
    -DZMK_EXTRA_MODULES="/src;$tests_dir/module" >"$runtime_dir.log" 2>&1; then
    echo "FAILED: $variant runtime build"
    tail -n 80 "$runtime_dir.log"
    exit 1
fi

runtime_log="$work_dir/build/runtime.run.log"
if ! timeout 30 "$runtime_dir/zephyr/zmk.exe" >"$runtime_log" 2>&1; then
    echo "FAILED: $variant runtime execution"
    tail -n 80 "$runtime_log"
    exit 1
fi
if ! grep -Fq "temp-layer-touch runtime tests: PASS" "$runtime_log"; then
    echo "FAILED: $variant runtime tests did not report success"
    tail -n 80 "$runtime_log"
    exit 1
fi
grep -F 'PASS: ' "$runtime_log" | sed -e 's/.*PASS: /PASS: /'
echo "PASS: $variant runtime execution"

echo "temp-layer-touch $variant integration: PASS"
