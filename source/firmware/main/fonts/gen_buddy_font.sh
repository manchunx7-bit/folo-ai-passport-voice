#!/usr/bin/env bash
set -euo pipefail

PROJ="/mnt/f/WORK/AI硬件/passport-os"
OTF="$PROJ/managed_components/lvgl__lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf"
SYMBOLS_FILE="$PROJ/main/fonts/buddy_16_symbols.txt"
OUT="$PROJ/main/fonts/buddy_font_16.c"

echo "=== Generating buddy_font_16.c ==="
echo "OTF: $OTF"
echo "Symbols count: $(wc -m < "$SYMBOLS_FILE")"

SYMBOLS=$(cat "$SYMBOLS_FILE")

npx --yes lv_font_conv \
  --font "$OTF" \
  --range 0x20-0x7e \
  --symbols "$SYMBOLS" \
  --size 16 --bpp 2 --format lvgl --no-compress --no-prefilter --no-kerning \
  --lv-include lvgl.h --lv-font-name buddy_font_16 \
  -o "$OUT"

echo "Generated buddy_font_16.c successfully: $(ls -lh "$OUT")"
