#!/usr/bin/env bash
# Run the full verification suite and produce a real video + layout data.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/env.sh"
OUT="$ROOT/output"
mkdir -p "$OUT"

echo "==> shaping / bidi / font-fallback tests"
"$ROOT/tests/build-tests/test_shaping" "$OUT"

echo "==> save/reopen logical-text roundtrip"
"$ROOT/tests/build-rt/test_roundtrip"

echo "==> end-to-end video export"
"$ROOT/tests/build-export/export_cli" \
  -o "$OUT/subtitles.mp4" \
  -l "$OUT/subtitles_layout.json" \
  --ffmpeg "$FFMPEG"

echo "==> GUI offscreen screenshot"
QT_QPA_PLATFORM=offscreen \
  "$ROOT/tests/build-gui/gui_smoke" "$OUT/gui.png"

echo
echo "Artifacts in $OUT:"
ls -1 "$OUT" | sed 's/^/  /'
