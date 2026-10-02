import os
import re

app_files = [
    'main/apps/radio/app_radio.cc',
    'main/apps/voice/app_voice.cc',
    'main/apps/xiaozhi/app_xiaozhi.cc',
    'main/apps/game/app_game.cc',
    'main/apps/game/app_tetris.cc',
    'main/launcher/wifi_setup.cc',
    'main/launcher/settings.cc',
]

from check_fonts import fonts

print("=== Checking Launcher App Names ===")
for path in app_files:
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    m = re.search(r'\.name\s*=\s*"([^"]+)"', content)
    name = m.group(1) if m else "UNKNOWN"
    missing_ui = [c for c in name if c not in fonts['ui_font_20']]
    missing_buddy = [c for c in name if c not in fonts['buddy_font_16']]
    print(f"{os.path.basename(path)}: name=\"{name}\"")
    if missing_ui:
        print(f"  -> MISSING in ui_font_20: {missing_ui}")
    if missing_buddy:
        print(f"  -> MISSING in buddy_font_16: {missing_buddy}")
