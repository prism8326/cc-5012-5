#!/usr/bin/env bash
# Reproducible build of the workbench, headless tests and export CLI.
# No root required: all third-party bits live under .deps/local.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/env.sh"

build_pro() {
  local pro="$1" dir="$2"
  mkdir -p "$dir"
  ( cd "$dir" && "$QMAKE6" "$pro" && make -j"$(nproc)" )
}

echo "==> GUI application"
build_pro "$ROOT/src/subtitler.pro" "$ROOT/src/build"

echo "==> shaping test suite"
build_pro "$ROOT/tests/tests.pro" "$ROOT/tests/build-tests"

echo "==> save/reopen roundtrip tests"
build_pro "$ROOT/tests/roundtrip.pro" "$ROOT/tests/build-rt"

echo "==> headless video export CLI"
build_pro "$ROOT/tests/export.pro" "$ROOT/tests/build-export"

echo "==> GUI offscreen smoke harness"
build_pro "$ROOT/tests/gui.pro" "$ROOT/tests/build-gui"

echo "All targets built."
