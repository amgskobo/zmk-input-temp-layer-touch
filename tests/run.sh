#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

set -euo pipefail

mode="${ZMK_TEST_MODE:-quick}"
case "$mode" in
    quick|full) ;;
    *) echo "ZMK_TEST_MODE must be quick or full" >&2; exit 2 ;;
esac
export ZMK_TEST_MODE="$mode"

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
    "coverage:-O0 --coverage"
)
if [ "$mode" = quick ]; then
    variants=("sanitized:-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all")
fi

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

if [ "$mode" = full ]; then
coverage_report="$(cd "$build_dir" && gcov -b -c *coverage*.gcno)"
printf '%s\n' "$coverage_report"
core_report="$(printf '%s\n' "$coverage_report" | grep -F -A4 "File '$repo_root/include/zmk-input-temp-layer-touch/temp_layer_touch_core.h'")"
printf '%s\n' "$core_report" | grep -Fq 'Lines executed:100.00%'
printf '%s\n' "$core_report" | grep -Fq 'Taken at least once:100.00%'

fi

python3 "$repo_root/tests/runtime/run.py"
python3 -m unittest discover -s "$repo_root/tests" -p 'test_runtime_coverage.py' -v
python3 -m unittest discover -s "$repo_root/tests/integration" -p 'test_*.py' -v
