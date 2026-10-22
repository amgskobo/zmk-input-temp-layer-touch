#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Run the host tests in the same image family used for ZMK builds.

set -euo pipefail

# Git Bash rewrites arguments that look like POSIX paths before Docker sees
# them, which turns the container's /src into a path under the Git install.
export MSYS_NO_PATHCONV=1

repo_root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
image="${ZMK_TEST_IMAGE:-zmkfirmware/zmk-build-arm:stable}"

docker run --rm --env ZMK_TEST_MODE="${ZMK_TEST_MODE:-quick}" \
  --volume "$repo_root:/src:ro" \
  --workdir /src \
  "$image" \
  /bin/bash -lc 'cd /src && bash tests/run.sh'
