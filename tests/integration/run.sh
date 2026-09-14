#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

set -euo pipefail

variant="${1:-upstream}"
case "$variant" in
upstream | dya) ;;
*)
    echo "unknown integration variant '$variant': expected upstream or dya" >&2
    exit 2
    ;;
esac

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/zmk-input-temp-layer-touch-integration.XXXXXX")"

cleanup() {
    rm -rf "$work_dir"
}
trap cleanup EXIT HUP INT TERM

cp -R "/src/tests/integration/config/$variant" "$work_dir/config"
cd "$work_dir"

west init -l config
west update --narrow --fetch-opt=--depth=1
west zephyr-export

export ZEPHYR_BASE="$work_dir/zephyr"

cmake_args=()
if [ "$variant" = dya ]; then
    cmake_args+=("-DEXTRA_CONF_FILE=/src/tests/integration/custom-settings.conf")
fi

west build -s "$work_dir/zmk/app" -d "$work_dir/build" -b xiao_ble/nrf52840/zmk -- \
    -DZMK_EXTRA_MODULES="/src;/src/tests/integration/firmware" \
    -DSHIELD=temp_layer_touch_test \
    "${cmake_args[@]}"

test -f "$work_dir/build/zephyr/zmk.uf2"
grep -q '^CONFIG_ZMK_INPUT_PROCESSOR_TEMP_LAYER_TOUCH=y' "$work_dir/build/zephyr/.config"
if [ "$variant" = upstream ]; then
    if grep -q '^CONFIG_ZMK_INPUT_TEMP_LAYER_TOUCH_CUSTOM_SETTINGS=y' \
        "$work_dir/build/zephyr/.config"; then
        echo "custom settings unexpectedly enabled" >&2
        exit 1
    fi
else
    grep -q '^CONFIG_ZMK_INPUT_TEMP_LAYER_TOUCH_CUSTOM_SETTINGS=y' \
        "$work_dir/build/zephyr/.config"
    strings "$work_dir/build/zephyr/zmk.elf" >"$work_dir/build/zephyr/strings.txt"
    grep -Fxq amgskobo__tlt "$work_dir/build/zephyr/strings.txt"
    grep -Fxq right_scroll_touch.enabled "$work_dir/build/zephyr/strings.txt"
    grep -Fxq right_scroll_touch.layer "$work_dir/build/zephyr/strings.txt"
    grep -Fxq right_scroll_touch.width "$work_dir/build/zephyr/strings.txt"
    grep -Fxq left_scroll_touch.enabled "$work_dir/build/zephyr/strings.txt"
    grep -Fxq left_scroll_touch.layer "$work_dir/build/zephyr/strings.txt"
    grep -Fxq left_scroll_touch.width "$work_dir/build/zephyr/strings.txt"
fi

echo "$variant ZMK firmware fixture: PASS"
