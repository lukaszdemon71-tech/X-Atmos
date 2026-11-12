#!/usr/bin/env python3
"""Regenerate the settings table inside ui/dev/mock-bridge.js from settings.cpp.

The development harness needs the same schema the plugin sends. Keeping a
second copy by hand guarantees it drifts, so it is generated instead. Run this
after changing the table in src/settings.cpp:

    python3 ui/dev/gen-mock-schema.py
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src" / "settings.cpp"
TARGET = ROOT / "ui" / "dev" / "mock-bridge.js"

BEGIN = "  // >>> generated from src/settings.cpp — do not edit by hand"
END = "  // <<< generated"

# { "key", "label", "section", default, min, max, CHK|SLD, ADV|SHOW },
ENTRY = re.compile(
    r'\{\s*"([^"]+)"\s*,\s*"([^"]*)"\s*,\s*"([^"]+)"\s*,\s*'
    r'([-\d.ef]+)\s*,\s*([-\d.ef]+)\s*,\s*([-\d.ef]+)\s*,\s*'
    r'(CHK|SLD)\s*,\s*(ADV|SHOW)\s*\}'
)


def number(text):
    value = float(text.rstrip("f"))
    return int(value) if value == int(value) else value


def main():
    body = SOURCE.read_text()
    rows = []
    for key, label, section, default, low, high, kind, visibility in ENTRY.findall(body):
        if section == "hidden":
            continue
        toggle = "true" if kind == "CHK" else "false"
        advanced = "true" if visibility == "ADV" else "false"
        rows.append(
            f'    ["{key}", "{label}", "{section}", '
            f"{number(default)}, {number(low)}, {number(high)}, {toggle}, {advanced}],"
        )

    if not rows:
        sys.exit("no settings entries found in settings.cpp — has the table moved?")

    target = TARGET.read_text()
    if BEGIN not in target or END not in target:
        sys.exit(f"markers missing in {TARGET}")

    head = target[: target.index(BEGIN) + len(BEGIN)]
    tail = target[target.index(END):]
    TARGET.write_text(head + "\n" + "\n".join(rows) + "\n" + tail)
    print(f"{len(rows)} settings written to {TARGET.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
