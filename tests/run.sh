#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/zmk-input-temp-layer-touch-test.XXXXXX")"

cleanup() {
    rm -rf "$build_dir"
}
trap cleanup EXIT HUP INT TERM

warnings=(-std=c11 -Wall -Wextra -Werror -pedantic -Wconversion -Wsign-conversion
    -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wundef)

printf '#include <zmk-input-temp-layer-touch/temp_layer_touch_core.h>\n' >"$build_dir/header.c"
cc "${warnings[@]}" -fsyntax-only -I"$repo_root/include" "$build_dir/header.c"

variants=(
    "optimised:-O2"
    "sanitized:-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
)

export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=print_stacktrace=1

for variant in "${variants[@]}"; do
    label="${variant%%:*}"
    read -r -a flags <<<"${variant#*:}"
    cc "${warnings[@]}" "${flags[@]}" -I"$repo_root/include" \
        "$repo_root/tests/test_temp_layer_touch_core.c" \
        -o "$build_dir/test-$label"
    "$build_dir/test-$label"
done
