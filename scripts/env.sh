# Source this file to build/run the subtitle workbench without root installs.
# bash: source scripts/env.sh   (sh/dash: export SUBTITLER_ROOT first)
if [ -n "${BASH_SOURCE:-}" ]; then
  _THIS="${BASH_SOURCE[0]}"
elif [ -n "${SUBTITLER_ROOT:-}" ]; then
  _THIS="$SUBTITLER_ROOT/scripts/env.sh"
else
  _THIS="$0"
fi
DEP_ROOT="$(cd "$(dirname "$_THIS")/.." && pwd)/.deps"
unset _THIS
LOCAL="$DEP_ROOT/local"
export LD_LIBRARY_PATH="$LOCAL/usr/lib/aarch64-linux-gnu:$LOCAL/lib/aarch64-linux-gnu:${LD_LIBRARY_PATH:-}"
export LIBRARY_PATH="$LOCAL/usr/lib/aarch64-linux-gnu:$LOCAL/lib/aarch64-linux-gnu:${LIBRARY_PATH:-}"
export CPATH="$LOCAL/usr/include/aarch64-linux-gnu/qt6:$LOCAL/usr/include:${CPATH:-}"
export QMAKEPATH="$LOCAL/usr/lib/aarch64-linux-gnu/qt6"
export QT_PLUGIN_PATH="$LOCAL/usr/lib/aarch64-linux-gnu/qt6/plugins"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
export FONTCONFIG_PATH="${FONTCONFIG_PATH:-/etc/fonts}"
export FONTCONFIG_FILE="${FONTCONFIG_FILE:-/etc/fonts/fonts.conf}"
# Project-private font directory (Noto Arabic/Hebrew) is registered at runtime.
QMAKE6="$LOCAL/usr/lib/qt6/bin/qmake6"
FFMPEG="$DEP_ROOT/bin/ffmpeg-7.0.2-arm64-static/ffmpeg"
[ -x "$FFMPEG" ] || FFMPEG="$(ls -d "$DEP_ROOT"/bin/ffmpeg-*-arm64-static/ffmpeg 2>/dev/null | head -1)"
export FFMPEG
