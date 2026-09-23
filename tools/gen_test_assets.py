#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 ESGUI 测试工程要用的 1bpp 页式位图资源（src/test_assets.c / .h）。

为什么需要它：
  ESGUI 的 Bitmap 是「页式」格式（与 SSD1315 GDDRAM 一致）：
      buf[page * w + x] 的 bit n = 像素 (x, page*8 + n)
  手写这种字节序极易出错，所以本脚本用「普通像素网格」作图，
  最后统一转成页式字节数组并生成 C 源文件（确定性输出，可重复生成）。

资源清单：
  · 6 张 48x48 图标 —— BMP 菜单条目（icon 指向 Bitmap）
  · 6 张 32x32 图标 —— BMP 列表弹窗条目（缩略版）
  · 12 帧 48x48 动图 —— GIF 条目（转圈加载效果）

用法：
  python3 tools/gen_test_assets.py          # 覆盖写入 src/test_assets.c/.h
"""

import math
import os
import sys

OUT_C = os.path.join(os.path.dirname(__file__), "..", "src", "test_assets.c")
OUT_H = os.path.join(os.path.dirname(__file__), "..", "src", "test_assets.h")

BIG = 32          # BMP 菜单图标尺寸（逻辑像素；屏幕上是 BIG*TFT_ZOOM）
SMALL = 24        # 列表弹窗图标尺寸
GIF_SIZE = 32     # 动图帧尺寸
GIF_FRAMES = 12   # 动图帧数


# ==================================================================
# 1. 像素栅格（作图用：普通自上而下、从左到右的坐标）
# ==================================================================
class Raster:
    def __init__(self, w, h):
        self.w = w
        self.h = h
        self.px = bytearray(w * h)

    def put(self, x, y, v=1):
        x = int(round(x))
        y = int(round(y))
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y * self.w + x] = 1 if v else 0

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.px[y * self.w + x]
        return 0

    # ---------- 基本图元 ----------
    def fill_rect(self, x1, y1, x2, y2, v=1):
        for y in range(int(y1), int(y2) + 1):
            for x in range(int(x1), int(x2) + 1):
                self.put(x, y, v)

    def rect(self, x1, y1, x2, y2, v=1):
        self.fill_rect(x1, y1, x2, y1, v)
        self.fill_rect(x1, y2, x2, y2, v)
        self.fill_rect(x1, y1, x1, y2, v)
        self.fill_rect(x2, y1, x2, y2, v)

    def disc(self, cx, cy, r, v=1):
        r2 = r * r
        for y in range(int(cy - r) - 1, int(cy + r) + 2):
            for x in range(int(cx - r) - 1, int(cx + r) + 2):
                dx = x - cx
                dy = y - cy
                if dx * dx + dy * dy <= r2:
                    self.put(x, y, v)

    def ring(self, cx, cy, r_out, r_in, v=1):
        ro2 = r_out * r_out
        ri2 = r_in * r_in
        for y in range(int(cy - r_out) - 1, int(cy + r_out) + 2):
            for x in range(int(cx - r_out) - 1, int(cx + r_out) + 2):
                dx = x - cx
                dy = y - cy
                d2 = dx * dx + dy * dy
                if ri2 <= d2 <= ro2:
                    self.put(x, y, v)

    def poly(self, pts, v=1):
        """扫描线填充（奇偶规则），pts 为 [(x,y), ...]"""
        ys = [p[1] for p in pts]
        for y in range(int(min(ys)), int(max(ys)) + 1):
            xs = []
            n = len(pts)
            for i in range(n):
                x1, y1 = pts[i]
                x2, y2 = pts[(i + 1) % n]
                if y1 == y2:
                    continue
                if (y >= min(y1, y2)) and (y < max(y1, y2)):
                    xs.append(x1 + (y - y1) * (x2 - x1) / (y2 - y1))
            xs.sort()
            for i in range(0, len(xs) - 1, 2):
                self.fill_rect(xs[i], y, xs[i + 1], y, v)

    # ---------- 缩放（面积多数表决，用于生成缩略图） ----------
    def resized(self, w, h):
        out = Raster(w, h)
        for y in range(h):
            for x in range(w):
                x0 = x * self.w / w
                x1 = (x + 1) * self.w / w
                y0 = y * self.h / h
                y1 = (y + 1) * self.h / h
                area = 0.0
                on = 0.0
                for sy in range(int(y0), max(int(y1), int(y0) + 1)):
                    for sx in range(int(x0), max(int(x1), int(x0) + 1)):
                        wgt = min(sx + 1, x1) - max(sx, x0)
                        hgt = min(sy + 1, y1) - max(sy, y0)
                        if wgt <= 0 or hgt <= 0:
                            continue
                        area += wgt * hgt
                        if self.get(sx, sy):
                            on += wgt * hgt
                if area > 0 and on / area >= 0.45:
                    out.put(x, y, 1)
        return out

    # ---------- 导出页式字节 ----------
    def to_page_bytes(self):
        pages = (self.h + 7) // 8
        out = bytearray()
        for p in range(pages):
            for x in range(self.w):
                b = 0
                for n in range(8):
                    y = p * 8 + n
                    if y < self.h and self.px[y * self.w + x]:
                        b |= 1 << n
                out.append(b)
        return bytes(out)


# ==================================================================
# 2. 图标作图（统一在 BIG x BIG 画布上画，之后可缩放）
# ==================================================================
def icon_gear(size=BIG):
    """设置（齿轮）：环 + 8 个齿 + 中心孔"""
    r = Raster(size, size)
    c = size / 2.0
    r.ring(c, c, c - 4, c - 11)
    r.disc(c, c, c - 12)
    r.disc(c, c, c - 16, 0)
    for k in range(8):
        a = math.radians(k * 45)
        ux, uy = math.cos(a), math.sin(a)
        px, py = -math.sin(a), math.cos(a)
        r0, r1, hw = c - 12, c - 1, 3.0
        r.poly([(c + ux * r0 + px * hw, c + uy * r0 + py * hw),
                (c + ux * r1 + px * hw, c + uy * r1 + py * hw),
                (c + ux * r1 - px * hw, c + uy * r1 - py * hw),
                (c + ux * r0 - px * hw, c + uy * r0 - py * hw)])
    return r


def icon_music(size=BIG):
    """音乐：双音符（两个符头 + 符干 + 符杠）"""
    r = Raster(size, size)
    s = size / 48.0
    r.disc(11 * s, 37 * s, 6 * s)
    r.disc(34 * s, 33 * s, 6 * s)
    r.fill_rect(16 * s, 8 * s, 19 * s, 37 * s)
    r.fill_rect(39 * s, 5 * s, 42 * s, 33 * s)
    r.fill_rect(16 * s, 6 * s, 42 * s, 10 * s)
    return r


def icon_folder(size=BIG):
    """文件夹：标签 + 主体"""
    r = Raster(size, size)
    s = size / 48.0
    r.fill_rect(4 * s, 10 * s, 20 * s, 15 * s)
    r.rect(4 * s, 13 * s, 44 * s, 38 * s)
    r.fill_rect(7 * s, 20 * s, 41 * s, 21 * s)
    return r


def icon_heart(size=BIG):
    """心形：隐式方程 (x²+y²-1)³ - x²y³ <= 0"""
    r = Raster(size, size)
    s = size / 48.0
    cx, cy = 24 * s, 22 * s
    scale = 15.0 * s
    for y in range(size):
        for x in range(size):
            nx = (x - cx) / scale
            ny = (cy - y) / scale
            f = (nx * nx + ny * ny - 1) ** 3 - nx * nx * ny * ny * ny
            if f <= 0:
                r.put(x, y, 1)
    return r


def icon_star(size=BIG):
    """五角星"""
    r = Raster(size, size)
    s = size / 48.0
    cx, cy = 24 * s, 25 * s
    pts = []
    for k in range(10):
        ang = math.radians(-90 + k * 36)
        rad = 21 * s if k % 2 == 0 else 9 * s
        pts.append((cx + rad * math.cos(ang), cy + rad * math.sin(ang)))
    r.poly(pts)
    return r


def icon_wifi(size=BIG):
    """WiFi：三段圆弧 + 圆点"""
    r = Raster(size, size)
    s = size / 48.0
    cx, cy = 24 * s, 40 * s
    for r_out, r_in in ((24, 20), (17, 13), (10, 6)):
        for y in range(size):
            for x in range(size):
                dx = x - cx
                dy = y - cy
                d2 = dx * dx + dy * dy
                if (r_in * s) ** 2 <= d2 <= (r_out * s) ** 2 and y < cy - r_out * s * 0.15:
                    r.put(x, y, 1)
    r.disc(cx, cy - 2 * s, 3 * s, 1)
    return r


ICONS = {
    "settings": icon_gear,
    "music": icon_music,
    "folder": icon_folder,
    "heart": icon_heart,
    "star": icon_star,
    "wifi": icon_wifi,
}


def gif_frame(index, count=GIF_FRAMES, size=GIF_SIZE):
    """转圈加载动图：8 个点绕圈，焦点点最大，拖尾依次变小"""
    r = Raster(size, size)
    s = size / 48.0
    cx = cy = size / 2.0
    dots = 8
    radius = 16 * s
    for k in range(dots):
        d = (k - index) % dots          # 距“焦点点”的落后步数
        rad = (4.6 - 0.5 * d) * s
        if rad < 1.2 * s:
            rad = 1.2 * s
        ang = math.radians(k * (360.0 / dots) - 90)
        r.disc(cx + radius * math.cos(ang), cy + radius * math.sin(ang), rad)
    return r


# ==================================================================
# 3. 生成 C 源文件
# ==================================================================
def emit_array(name, data, per_line=12):
    lines = []
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        lines.append("    " + " ".join("0x%02X," % b for b in chunk))
    return "static const eui_uint8_t %s[] = {\n%s\n};\n" % (name, "\n".join(lines))


def preview():
    """在终端用 ASCII 打出图标，方便不烧板也能检查形状（--preview）"""
    def show(title, r):
        print("\n=== %s (%dx%d) ===" % (title, r.w, r.h))
        for y in range(r.h):
            print("".join("#" if r.get(x, y) else "." for x in range(r.w)))

    for key, fn in ICONS.items():
        show(key, fn(BIG))
    show("gif frame 0", gif_frame(0))
    show("gif frame 3", gif_frame(3))


def main():
    out_c = []
    out_c.append("/*\n"
                 " * test_assets.c —— ESGUI 测试工程用的 1bpp 页式位图资源（自动生成，请勿手改）\n"
                 " *\n"
                 " * 生成脚本：tools/gen_test_assets.py（python3 tools/gen_test_assets.py）\n"
                 " * 数据格式：页式（buf[page * w + x] 的 bit n = 像素 (x, page*8+n)），\n"
                 " *          与 eui_draw_bitmap / ESGUI_GIF 完全一致，直接放 Flash 零解析。\n"
                 " */\n\n"
                 "#include \"test_assets.h\"\n\n")

    out_h = []
    out_h.append("/*\n"
                 " * test_assets.h —— ESGUI 测试工程的位图资源声明（自动生成，请勿手改）\n"
                 " *\n"
                 " * 生成脚本：tools/gen_test_assets.py\n"
                 " */\n"
                 "#ifndef TEST_ASSETS_H\n"
                 "#define TEST_ASSETS_H\n\n"
                 "#include \"ESGUI_Def.h\"\n"
                 "#include \"ESGUI_BSP_BMP.h\"\n\n"
                 "#ifdef __cplusplus\n"
                 "extern \"C\" {\n"
                 "#endif\n\n")

    # ---- 大图标（BMP 菜单） ----
    for key, fn in ICONS.items():
        r = fn(BIG)
        data = r.to_page_bytes()
        aname = "s_icon_%s" % key
        out_c.append("/* 图标 %s：%dx%d */\n" % (key, BIG, BIG))
        out_c.append(emit_array(aname, data))
        out_c.append("const Bitmap tst_icon_%s = { %d, %d, %s };\n\n" % (key, BIG, BIG, aname))

    # ---- 小图标（BMP 列表弹窗） ----
    out_c.append("/* ==================== 列表弹窗用缩略图标 ==================== */\n\n")
    for key, fn in ICONS.items():
        r = fn(BIG).resized(SMALL, SMALL)
        data = r.to_page_bytes()
        aname = "s_small_%s" % key
        out_c.append("/* 缩略图标 %s：%dx%d */\n" % (key, SMALL, SMALL))
        out_c.append(emit_array(aname, data))
        out_c.append("const Bitmap tst_small_%s = { %d, %d, %s };\n\n" % (key, SMALL, SMALL, aname))

    # ---- 动图帧 ----
    out_c.append("/* ==================== 动图（GIF）：转圈加载，%d 帧 ==================== */\n\n"
                 % GIF_FRAMES)
    frame_objs = []
    for i in range(GIF_FRAMES):
        r = gif_frame(i)
        data = r.to_page_bytes()
        aname = "s_gif_frame_%d" % i
        out_c.append("/* 第 %d 帧 */\n" % i)
        out_c.append(emit_array(aname, data))
        out_c.append("static const Bitmap s_gif_bmp_%d = { %d, %d, %s };\n\n"
                     % (i, GIF_SIZE, GIF_SIZE, aname))
        frame_objs.append("s_gif_bmp_%d" % i)

    out_c.append("const Bitmap tst_gif_frames[TST_GIF_FRAME_COUNT] = {\n")
    for name in frame_objs:
        out_c.append("    %s,\n" % name)
    out_c.append("};\n\n")
    out_c.append("/* 每帧间隔（ms）：统一 70ms，约 14 帧/秒 */\n")
    delays = [70] * GIF_FRAMES
    out_c.append("const eui_uint16_t tst_gif_delays[TST_GIF_FRAME_COUNT] = {\n    "
                 + " ".join("%d," % d for d in delays) + "\n};\n")

    # ---- 头文件 ----
    out_h.append("/* ---- %dx%d 图标（BMP 菜单条目） ---- */\n" % (BIG, BIG))
    for key in ICONS:
        out_h.append("extern const Bitmap tst_icon_%s;\n" % key)
    out_h.append("\n/* ---- %dx%d 缩略图标（BMP 列表弹窗条目） ---- */\n" % (SMALL, SMALL))
    for key in ICONS:
        out_h.append("extern const Bitmap tst_small_%s;\n" % key)
    out_h.append("\n/* ---- 动图帧序列（GIF 条目 / 位图动画测试） ---- */\n")
    out_h.append("#define TST_GIF_FRAME_COUNT  %d\n" % GIF_FRAMES)
    out_h.append("extern const Bitmap tst_gif_frames[TST_GIF_FRAME_COUNT];\n")
    out_h.append("extern const eui_uint16_t tst_gif_delays[TST_GIF_FRAME_COUNT];\n")
    out_h.append("\n#ifdef __cplusplus\n}\n#endif\n#endif /* TEST_ASSETS_H */\n")

    with open(OUT_C, "w", encoding="utf-8") as f:
        f.write("".join(out_c))
    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write("".join(out_h))
    print("written:", os.path.normpath(OUT_C))
    print("written:", os.path.normpath(OUT_H))


if __name__ == "__main__":
    if "--preview" in sys.argv:
        preview()
    else:
        main()
