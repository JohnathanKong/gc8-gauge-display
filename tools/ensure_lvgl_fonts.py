#!/usr/bin/env python3
"""Enable the fonts used by the GC8 driving display in an existing sdkconfig.

ESP-IDF reads sdkconfig.defaults only when creating sdkconfig. Developers
updating the firmware already have an sdkconfig with older font selections.
This helper changes only our three font flags and leaves all board settings
and other application configuration untouched.
"""
from pathlib import Path
import re
import sys

config_path = Path("sdkconfig")
if not config_path.is_file():
    print("ERROR: sdkconfig does not exist. Run idf.py set-target esp32s3 first.")
    sys.exit(1)

original = config_path.read_text(encoding="utf-8")
updated = original
sizes = (32, 40, 48)
for size in sizes:
    option = f"CONFIG_LV_FONT_MONTSERRAT_{size}"
    enabled = f"{option}=y"
    # Kconfig emits either "CONFIG_FOO=y" or "# CONFIG_FOO is not set".
    # When a symbol is missing entirely, append it explicitly.
    expression = rf"(?m)^(?:{re.escape(option)}=.*|# {re.escape(option)} is not set)$"
    if re.search(expression, updated):
        updated = re.sub(expression, enabled, updated)
    else:
        updated = updated.rstrip("\r\n") + "\n" + enabled + "\n"

if updated == original:
    print("LVGL fonts 32, 40 and 48 are already enabled.")
else:
    config_path.write_text(updated, encoding="utf-8")
    print("Enabled LVGL fonts 32, 40 and 48 in local sdkconfig.")

for size in sizes:
    if f"CONFIG_LV_FONT_MONTSERRAT_{size}=y" not in updated.splitlines():
        print(f"ERROR: failed to enable Montserrat {size}.")
        sys.exit(1)
