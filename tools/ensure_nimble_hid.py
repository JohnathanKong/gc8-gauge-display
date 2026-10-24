"""Keep already-generated ESP-IDF sdkconfig in sync with HID bonding needs.

Build.bat normally uses an existing sdkconfig. sdkconfig.defaults only influences
new configurations, so enabling HID NVS bonds there alone is insufficient.
Edits only the three HID-required options; does not reset unrelated settings.
"""
from pathlib import Path

path = Path("sdkconfig")
if not path.exists():
    raise SystemExit(0)

text = path.read_text(encoding="utf-8")
required = {
    "CONFIG_BT_NIMBLE_NVS_PERSIST": "y",
    "CONFIG_BT_NIMBLE_MAX_CONNECTIONS": "3",
    "CONFIG_BT_NIMBLE_SECURITY_ENABLE": "y",
}
lines = text.splitlines()
for option, value in required.items():
    active = f"{option}={value}"
    matches = [
        idx for idx, line in enumerate(lines)
        if line.startswith(option + "=") or line == f"# {option} is not set"
    ]
    if matches:
        lines[matches[0]] = active
        for idx in reversed(matches[1:]):
            del lines[idx]
    else:
        lines.append(active)
path.write_text("\n".join(lines) + "\n", encoding="utf-8")
print("BLE HID: persistent bond storage and multi-profile connection enabled")
