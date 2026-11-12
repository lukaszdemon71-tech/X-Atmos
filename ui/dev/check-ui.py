#!/usr/bin/env python3
"""Check the page, the script and the settings table agree.

These three drift apart silently. A section with no host in app.js renders
nowhere and the controls simply go missing; an anchor naming a key that no
longer exists leaves a blank slot; an id app.js binds but the page no longer
has used to throw during wiring and leave every column empty. None of that
shows up in the C++ tests, so it is checked here.

    python3 ui/dev/check-ui.py
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SETTINGS = (ROOT / "src" / "settings.cpp").read_text()
PAGE = (ROOT / "ui" / "index.html").read_text()
SCRIPT = (ROOT / "ui" / "app.js").read_text()
HARNESS = ROOT / "ui" / "dev" / "harness.html"

ENTRY = re.compile(
    r'\{\s*"([^"]+)"\s*,\s*"[^"]*"\s*,\s*"([^"]+)"\s*,'
    r'(?:[^{}]*?)(CHK|SLD)\s*,\s*(ADV|SHOW)\s*\}'
)

problems = []


def check(ok, message):
    if not ok:
        problems.append(message)
    print(f"  {'ok   ' if ok else 'FAIL '} {message}")


entries = ENTRY.findall(SETTINGS)
check(len(entries) > 40, f"the settings table parses ({len(entries)} entries)")

keys = {key for key, _, _, _ in entries}
panel_sections = {section for _, section, _, visibility in entries
                  if visibility == "SHOW" and section != "hidden"}

# 1. Every section with a visible setting has somewhere to be rendered.
hosts = dict(re.findall(r"(\w+):\s*\"(fields-[\w-]+)\"", SCRIPT))
for section in sorted(panel_sections):
    check(section in hosts, f"section '{section}' has a column in app.js")

# 2. Every host app.js names is a real element on the page.
for section, host in sorted(hosts.items()):
    check(f'id="{host}"' in PAGE, f"app.js renders '{section}' into #{host}, which exists")

# 3. Every per-key override names a setting that still exists and is visible,
#    and a host that is on the page.
visible = {key for key, _, _, visibility in entries if visibility == "SHOW"}
for key, host in re.findall(r'"([\w.]+)":\s*"(fields-[\w-]+)"', SCRIPT):
    check(key in keys, f'override "{key}" is a real setting')
    check(key in visible, f'override "{key}" is a panel setting, not an advanced one')
    check(f'id="{host}"' in PAGE, f'override "{key}" renders into #{host}, which exists')

# 3b. Every section the rail offers has a page, and every page has a rail
#     button — a page with no way to reach it is invisible.
cols = re.findall(r'class="col" data-col="([^"]+)"', PAGE)
check(len(cols) == 4, f"there are four columns ({', '.join(cols)})")
for col in cols:
    check(PAGE.count(f'data-col="{col}"') == 1, f'column "{col}" appears once')

# 4. Every quick set the buttons ask for exists in presets.cpp.
quick_source = (ROOT / "src" / "presets.cpp").read_text()
for name in sorted(set(re.findall(r'data-quick="([^"]+)"', PAGE))):
    check(f'"{name}"' in quick_source, f'quick set "{name}" is defined in presets.cpp')

# 5. Every id app.js binds is on the page. This is the one that used to take
#    the whole panel down: wire() threw before a single control was built.
for ident in sorted(set(re.findall(r'(?:\bon|\$)\(\s*"([A-Za-z][\w-]*)"', SCRIPT))):
    check(f'id="{ident}"' in PAGE, f'app.js uses #{ident}, which is on the page')

# 6. The harness is the page it claims to be a copy of.
if HARNESS.exists():
    body = HARNESS.read_text()
    for marker in ('id="fields-wx"', 'id="presets"', 'class="col"'):
        check(marker in body, f"the dev harness is current ({marker})")
else:
    check(False, "the dev harness exists — run ui/dev/gen-harness.py")

print()
if problems:
    print(f"FAIL  {len(problems)} UI wiring problem(s)")
    sys.exit(1)
print("PASS  page, script and settings table agree")
