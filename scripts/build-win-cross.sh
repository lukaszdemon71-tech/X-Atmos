#!/usr/bin/env bash
# Build the Windows plugin from Linux, with llvm-mingw against the MSVC
# import library. This is the exact command the shipped AtmoForge.xpl was
# produced with; CMakeLists.txt covers a normal Windows or macOS build.
#
#   TOOLCHAIN=/path/to/llvm-mingw/bin SDK=/path/to/xpsdk ./scripts/build-win-cross.sh
#
# Why these flags, since each one was paid for in a failed build:
#
#   XPLM440      the browser window API (XPLMWindowSetURL) is gated behind it.
#                Build without it and the panel silently falls back to the
#                OpenGL renderer on every version of X-Plane.
#   -lwinhttp    the HTTP client for Open-Meteo. wininet does not link here.
#   -static...   X-Plane must not need a VC++ redistributable to load the
#                plugin; a dynamically linked libstdc++ fails on a clean PC.
#   XPLM_64.lib  an MSVC import library, linked directly rather than through
#                -l, because it is not a mingw-style .a.

set -euo pipefail

TOOLCHAIN="${TOOLCHAIN:-}"
SDK="${SDK:-}"
OUT="${OUT:-dist/AtmoForge/win_x64}"

if [[ -z "$TOOLCHAIN" || -z "$SDK" ]]; then
    echo "Set TOOLCHAIN (llvm-mingw bin/) and SDK (X-Plane SDK root)." >&2
    echo "  llvm-mingw: https://github.com/mstorsjo/llvm-mingw/releases" >&2
    echo "  SDK 4.4.0+: https://developer.x-plane.com/sdk/" >&2
    exit 1
fi

CXX="$TOOLCHAIN/x86_64-w64-mingw32-clang++"
LIB="$SDK/Libraries/Win/XPLM_64.lib"

[[ -x "$CXX" ]] || { echo "No compiler at $CXX" >&2; exit 1; }
[[ -f "$LIB" ]] || { echo "No import library at $LIB" >&2; exit 1; }

# The browser API is the whole reason for the SDK version floor, so check for
# it by name rather than trusting a version string in a readme.
if ! grep -q "XPLMWindowSetURL" "$SDK/CHeaders/XPLM/XPLMDisplay.h"; then
    echo "This SDK predates XPLMWindowSetURL — AtmoForge needs SDK 4.4.0 or newer." >&2
    exit 1
fi

mkdir -p "$OUT/ui"

"$CXX" -std=c++17 -O2 -Wall -Wextra \
    -DIBM=1 -DXPLM200 -DXPLM210 -DXPLM300 -DXPLM301 -DXPLM303 \
    -DXPLM400 -DXPLM410 -DXPLM440 \
    -I src -I "$SDK/CHeaders/XPLM" -I "$SDK/CHeaders/Widgets" \
    -shared -o "$OUT/AtmoForge.xpl" src/*.cpp \
    "$LIB" -lopengl32 -lwinhttp -lole32 \
    -static -static-libgcc -static-libstdc++

cp ui/index.html ui/style.css ui/app.js "$OUT/ui/"

# All five entry points must be exported or X-Plane rejects the plugin with
# nothing useful in Log.txt.
exports=$("$TOOLCHAIN/llvm-nm" --extern-only --defined-only "$OUT/AtmoForge.xpl" | grep -c XPlugin || true)
if [[ "$exports" -ne 5 ]]; then
    echo "Only $exports of 5 XPlugin entry points exported — X-Plane will not load this." >&2
    exit 1
fi

echo "Built $OUT/AtmoForge.xpl ($(du -h "$OUT/AtmoForge.xpl" | cut -f1)), $exports entry points."
echo "Copy dist/AtmoForge into X-Plane 12/Resources/plugins/."
