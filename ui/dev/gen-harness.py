#!/usr/bin/env python3
"""Regenerate ui/dev/harness.html from ui/index.html.

The harness is the real panel with the mock bridge loaded in front of app.js.
Keeping a hand-written copy of the markup guarantees the two drift apart and
the UI gets tested in a shape the plugin never ships, so it is generated:

    python3 ui/dev/gen-harness.py
"""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "ui" / "index.html"
TARGET = ROOT / "ui" / "dev" / "harness.html"

NOTE = "<!-- Generated from ui/index.html by ui/dev/gen-harness.py — do not edit. -->"


def main():
    page = SOURCE.read_text()

    for old, new in (
        ('href="style.css"', 'href="../style.css"'),
        ('<script src="app.js"></script>',
         '<script src="mock-bridge.js"></script>\n<script src="../app.js"></script>'),
    ):
        if old not in page:
            sys.exit(f"{SOURCE.name} no longer contains {old!r} — has the page changed?")
        page = page.replace(old, new)

    page = page.replace("<!doctype html>", "<!doctype html>\n" + NOTE, 1)
    TARGET.write_text(page)
    print(f"wrote {TARGET.relative_to(ROOT)} ({len(page.splitlines())} lines)")


if __name__ == "__main__":
    main()
