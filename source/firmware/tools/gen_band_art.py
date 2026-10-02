# tools/gen_band_art.py —— 生成首页绿磷光 A8 底图(240×80, 0/255 两级)。
#
# 高对比海报风:剪影纯黑、灯光/月亮纯亮,只有天空/光晕用 Bayer 有序网点 ——
# 保证小图上一眼能认出"画的什么"。
# 设备端 lv_image + image_recolor(绿) 显示;内置图在 flash(.rodata)零 RAM 占用。
#
# 用法: python tools/gen_band_art.py   (在仓库根目录执行)
# 输出: main/apps/home/band_art.c / band_art.h

import math
import random
from PIL import Image, ImageDraw

W, H = 240, 80
OUT_C = "main/apps/home/band_art.c"
OUT_H = "main/apps/home/band_art.h"

# 4×4 Bayer 矩阵(0..15):灰度 > 阈值 → 亮点。规则网点比 Floyd 噪声更像磷光屏。
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def bayer_dither(img):
    px = img.load()
    out = Image.new("L", (W, H), 0)
    po = out.load()
    for y in range(H):
        for x in range(W):
            t = (BAYER[y % 4][x % 4] + 0.5) * 16
            po[x, y] = 255 if px[x, y] > t else 0
    return out


def vgrad(draw, top, bottom):
    for y in range(H):
        v = top + (bottom - top) * y // H
        draw.line([(0, y), (W - 1, y)], fill=v)


def glow(draw, cx, cy, peak, spread):
    """月亮径向光晕:中心 peak,线性衰减到 0。"""
    for y in range(max(0, cy - spread), min(H, cy + spread)):
        for x in range(max(0, cx - spread), min(W, cx + spread)):
            d = math.hypot(x - cx, y - cy)
            if d > spread:
                continue
            v = int(peak * max(0.0, 1.0 - d / spread))
            if v:
                draw.point((x, y), fill=max(v, 0))


def stars(draw, n, ymax, seed):
    rnd = random.Random(seed)
    for _ in range(n):
        x, y = rnd.randrange(W), rnd.randrange(ymax)
        draw.point((x, y), fill=255)
        if rnd.random() < 0.3:   # 十字星
            draw.point((x - 1, y), fill=120)
            draw.point((x + 1, y), fill=120)


def draw_city():
    """场景1: 江城夜景 —— 月 + 星 + 电视塔 + 跨江大桥 + 楼群 + 水面倒影。"""
    horizon = 54
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)

    vgrad(d, 18, 52)
    stars(d, 20, 44, seed=20260913)
    glow(d, 196, 15, 150, 16)
    d.ellipse([196 - 8, 15 - 8, 196 + 8, 15 + 8], fill=255)
    d.ellipse([196 - 2, 15 - 5, 196 + 4, 15 + 2], fill=170)   # 月面暗斑

    # ---- 地标电视塔(锥形塔身 + 观景舱 + 天线) ----
    for i, y in enumerate(range(16, horizon)):
        t = (y - 16) / (horizon - 16)
        half = 2 + int(6 * t)                     # 2 → 8
        d.line([(38 - half, y), (38 + half, y)], fill=6)
        if i % 6 == 3:                            # 塔身灯光环
            d.line([(38 - half - 1, y), (38 - half, y)], fill=255)
            d.line([(38 + half, y), (38 + half + 1, y)], fill=255)
    d.rectangle([32, 32, 45, 39], fill=6)          # 观景舱
    d.rectangle([32, 32, 45, 34], fill=255)        # 舱体灯带
    d.line([(38, 16), (38, 7)], fill=6)            # 天线
    d.point((38, 6), fill=255)                     # 天线顶灯

    # ---- 跨江大桥(桥面 + 双塔 + 拉索 + 桥灯) ----
    deck_y = horizon - 9
    d.line([(148, deck_y), (238, deck_y)], fill=2, width=2)
    for tx in (168, 218):                          # 桥塔
        d.line([(tx, deck_y - 16), (tx, deck_y + 3)], fill=2, width=2)
        d.point((tx, deck_y - 17), fill=255)       # 塔顶灯
    for i in range(9):                             # 拉索
        x = 152 + i * 10
        top = 168 if x < 193 else 218
        d.line([(x, deck_y), (top, deck_y - 14)], fill=30)
    for lx in range(150, 238, 6):                  # 桥面路灯
        d.point((lx, deck_y - 2), fill=255)

    # ---- 远景楼群(中灰 → 稀疏网点,似雾中剪影) ----
    rnd = random.Random(7)
    x = 56
    while x < 148:
        bw = rnd.randrange(12, 22)
        bh = rnd.randrange(8, 18)
        d.rectangle([x, horizon - bh, x + bw, horizon], fill=55)
        x += bw + rnd.randrange(2, 5)

    # ---- 近景楼群(纯黑剪影 + 亮窗;右侧留白给大桥) ----
    x = 50
    while x < 148:
        bw = rnd.randrange(16, 30)
        bh = rnd.randrange(14, 36)
        top = horizon - bh
        d.rectangle([x, top, x + bw, horizon], fill=2)
        for wy in range(top + 3, horizon - 3, 5):
            for wx in range(x + 2, x + bw - 2, 4):
                if rnd.random() < 0.16:
                    d.rectangle([wx, wy, wx + 1, wy + 1], fill=255)
        if rnd.random() < 0.35:                    # 天线
            ax = x + bw // 2
            d.line([(ax, top), (ax, top - 5)], fill=2)
            d.point((ax, top - 6), fill=255)
        x += bw + rnd.randrange(2, 6)

    # ---- 水面: 暗底 + 明确的光源倒影 ----
    d.rectangle([0, horizon, W - 1, H - 1], fill=5)
    for y in range(horizon + 2, H, 3):             # 月倒影柱
        ln = max(2, 10 - (y - horizon) // 4)
        j = rnd.randrange(-2, 3)
        d.line([(196 - ln // 2 + j, y), (196 + ln // 2 + j, y)], fill=255)
    for lx in range(150, 238, 6):                  # 桥灯倒影(短竖闪)
        if rnd.random() < 0.7:
            y0 = deck_y + 4 + rnd.randrange(0, 4)
            if y0 < H - 2:
                d.line([(lx, y0), (lx, y0 + 2)], fill=170)
    for _ in range(10):                            # 零星涟漪
        y = rnd.randrange(horizon + 3, H)
        ln = rnd.randrange(5, 14)
        x0 = rnd.randrange(0, W - ln)
        d.line([(x0, y), (x0 + ln, y)], fill=70)
    return img


def draw_mountain():
    """场景2: 山湖月色 —— 大月 + 三层山脊 + 湖面倒影 + 松林 + 小船渔火。"""
    horizon = 48
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)

    vgrad(d, 14, 46)
    stars(d, 24, 38, seed=31415)
    glow(d, 64, 17, 160, 18)
    d.ellipse([64 - 9, 17 - 9, 64 + 9, 17 + 9], fill=255)
    d.ellipse([64 - 2, 17 - 6, 64 + 5, 17 + 3], fill=170)

    # 三层山脊: 远(网点) → 中 → 近(纯黑),正弦叠加轮廓
    def ridge(base, amp, freq, phase, fill):
        pts = [(0, H)]
        for x in range(0, W + 1, 3):
            y = base - int(amp * (math.sin(x / freq + phase) * 0.6 +
                                  math.sin(x / (freq * 0.37) + phase * 2.7) * 0.4))
            pts.append((x, y))
        pts.append((W, H))
        d.polygon(pts, fill=fill)

    ridge(horizon - 7, 11, 30.0, 0.7, 44)
    ridge(horizon - 3, 8, 22.0, 2.3, 20)
    ridge(horizon + 2, 6, 16.0, 4.4, 2)

    # 近岸松林(左右两团,纯黑三角叠塔)
    def pine(cx, base, h, w):
        for i in range(3):
            top = base - h + i * (h // 3)
            half = int(w * (i + 1) / 3 / 2) + 1
            d.polygon([(cx, top - 2), (cx - half, base - (h // 3) * (2 - i) + 2),
                       (cx + half, base - (h // 3) * (2 - i) + 2)], fill=2)
        d.line([(cx, base), (cx, base + 2)], fill=2)

    for cx, h, w in ((14, 18, 12), (26, 24, 14), (38, 16, 10)):
        pine(cx, horizon + 4, h, w)
    for cx, h, w in ((224, 21, 13), (234, 15, 10)):
        pine(cx, horizon + 3, h, w)

    # 湖面: 暗底 + 月倒影 + 小船渔火 + 涟漪
    rnd = random.Random(4242)
    d.rectangle([0, horizon, W - 1, H - 1], fill=5)
    for y in range(horizon + 2, H, 3):
        ln = max(3, 12 - (y - horizon) // 3)
        j = rnd.randrange(-3, 4)
        d.line([(64 - ln // 2 + j, y), (64 + ln // 2 + j, y)], fill=255)

    bx, by = 152, horizon + 9                      # 小船: 船身 + 渔火
    d.polygon([(bx, by), (bx + 20, by), (bx + 16, by + 4), (bx + 4, by + 4)], fill=34)
    d.line([(bx + 10, by - 6), (bx + 10, by)], fill=2)   # 桅杆
    d.point((bx + 10, by - 7), fill=255)                 # 渔火
    glow(d, bx + 10, by - 7, 90, 4)
    if by + 6 < H:
        d.line([(bx + 8, by + 6), (bx + 13, by + 6)], fill=200)  # 灯的水面反光

    for _ in range(8):
        y = rnd.randrange(horizon + 4, H)
        ln = rnd.randrange(5, 12)
        x0 = rnd.randrange(0, W - ln)
        d.line([(x0, y), (x0 + ln, y)], fill=60)
    return img


def to_a8(img):
    """灰度 → Bayer 1-bit → A8 字节(0/255)。"""
    bw = bayer_dither(img)
    px = bw.load()
    out = bytearray()
    for y in range(H):
        for x in range(W):
            out.append(255 if px[x, y] else 0)
    return bytes(out)


def emit(name, data, lines):
    hexlines = []
    for i in range(0, len(data), 12):
        chunk = data[i:i + 12]
        hexlines.append("    " + " ".join(f"0x{b:02X}," for b in chunk))
    body = "\n".join(hexlines)
    lines.append(f"// {name}: {len(data)} 字节 (240×{H} A8,仅 0/255)")
    lines.append(f"const uint8_t {name}[{len(data)}] = {{\n{body}\n}};\n")


def main():
    scenes = [("kBandArtCity", draw_city()), ("kBandArtMountain", draw_mountain())]

    c = [
        "// main/apps/home/band_art.c —— 首页绿磷光底图(A8 alpha 图,由 tools/gen_band_art.py 生成)。",
        "// lv_image + style image_recolor(磷光绿) 显示:alpha 1 → 绿点,0 → 透出黑底。",
        "// 内置图放 flash 零 RAM;重新生成请跑 python tools/gen_band_art.py,勿手改。",
        "#include \"apps/home/band_art.h\"",
        "",
    ]
    for name, img in scenes:
        emit(name, to_a8(img), c)

    with open(OUT_C, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(c))

    h = [
        "// main/apps/home/band_art.h —— 内置底图(A8)。见 band_art.c 头注释。",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "#include \"lvgl.h\"",
        "",
        "#ifdef __cplusplus",
        "extern \"C\" {",
        "#endif",
        "",
        "#define BAND_ART_W 240",
        "#define BAND_ART_H 80",
        "",
        "extern const uint8_t kBandArtCity[BAND_ART_W * BAND_ART_H];",
        "extern const uint8_t kBandArtMountain[BAND_ART_W * BAND_ART_H];",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
    ]
    with open(OUT_H, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(h) + "\n")

    print("OK: wrote", OUT_C, "and", OUT_H)


if __name__ == "__main__":
    main()
