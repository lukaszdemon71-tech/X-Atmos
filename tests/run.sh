#!/usr/bin/env bash
# Everything that can be checked without X-Plane:
#   1. the core test suite (parser, hazards, blending, METAR, presets, settings)
#   2. a type-check of every source file, including the ones that call into the
#      SDK, against the stub headers in tests/sdk_stub
#
# Usage:  tests/run.sh
set -u
cd "$(dirname "$0")/.."

CXX=${CXX:-g++}
FLAGS="-std=c++17 -Wall -Wextra -I src"
STUBS="-I tests/sdk_stub"
failures=0

echo "== core test suite =="
if $CXX $FLAGS -O1 \
      tests/test_core.cpp src/weather_parse.cpp src/metar.cpp \
      src/presets.cpp src/settings.cpp -o /tmp/atmoforge_tests; then
    /tmp/atmoforge_tests || failures=$((failures + 1))
else
    echo "  build failed"
    failures=$((failures + 1))
fi

echo
echo "== type-check against the SDK stubs =="
for file in src/*.cpp; do
    if $CXX $FLAGS $STUBS -fsyntax-only -DLIN=1 "$file" 2>/tmp/atmoforge_cc.log; then
        echo "  ok    $file"
    else
        echo "  FAIL  $file"
        sed 's/^/        /' /tmp/atmoforge_cc.log | head -20
        failures=$((failures + 1))
    fi
done

echo
echo "== UI wiring =="
if command -v python3 >/dev/null; then
    python3 ui/dev/check-ui.py | tail -3 || failures=$((failures + 1))
else
    echo "  skipped (no python3)"
fi

if command -v node >/dev/null; then
    for file in ui/app.js ui/dev/mock-bridge.js; do
        if node --check "$file" 2>/tmp/atmoforge_js.log; then
            echo "  ok    $file"
        else
            echo "  FAIL  $file"
            sed 's/^/        /' /tmp/atmoforge_js.log | head -10
            failures=$((failures + 1))
        fi
    done
fi

echo
if [ "$failures" -eq 0 ]; then
    echo "PASS  everything checked"
else
    echo "FAIL  $failures step(s) failed"
fi
exit "$failures"
