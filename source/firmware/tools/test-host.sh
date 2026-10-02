#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_dir="$(mktemp -d /tmp/passport-host.XXXXXX)"
trap 'rm -rf -- "$test_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/apps/voice \
    tests/test_ui_pixel_math.c main/apps/voice/ui_pixel_math.c -o "$test_dir/math"
"$test_dir/math"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
    tests/test_tetris.c main/apps/game/tetris_core.c -o "$test_dir/tetris"
"$test_dir/tetris"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/test_terminal.cc -o "$test_dir/terminal"
"$test_dir/terminal"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/audio_mocks -Icomponents/bsp/src \
    tests/test_audio_volume.c components/bsp/src/bsp_audio_volume.c -lm -o "$test_dir/audio-volume"
"$test_dir/audio-volume"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
    tests/test_stopwatch.c main/apps/stopwatch/stopwatch_core.c -o "$test_dir/stopwatch"
"$test_dir/stopwatch"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/test_wifi_keyboard.cc -o "$test_dir/wifi-keyboard"
"$test_dir/wifi-keyboard"
node tests/test_profile_page.js
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
    tests/test_sound_jump.c main/apps/sound_jump/jump_core.c main/apps/sound_jump/jump_voice.c -o "$test_dir/sound-jump"
"$test_dir/sound-jump"
python3 tests/test_verify_firmware.py
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
    tests/test_voice_remote.c main/apps/voice/app_state.c -o "$test_dir/voice-remote"
"$test_dir/voice-remote"
echo "Host regression tests: PASS"
