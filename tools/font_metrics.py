#!/usr/bin/env python3
"""读取 eui_test_font 里指定字符的字形度量（adv_w / box_w / box_h / ofs），
用来估算"一行能放几个字"（布局用）。

用法：python3 tools/font_metrics.py A中测A1ab
"""
import re
import sys

FONT = "src/font_big.c"          # 默认测当前生效的真·大字号字库


def parse_font(path):
    src = open(path, encoding="utf-8", errors="ignore").read()

    # 稀疏码点表（跳过 cmap 里的 ESGUI_NULL）
    name = None
    for m in re.finditer(r"\.unicode_list\s*=\s*(\w+)", src):
        if m.group(1) != "ESGUI_NULL":
            name = m.group(1)
            break
    sparse = []
    if name:
        m2 = re.search(r"static const eui_uint16_t\s+%s\[\]\s*=\s*\{(.*?)\};" % re.escape(name),
                       src, re.S)
        if m2:
            sparse = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", m2.group(1))]

    # ASCII 连续段
    m = re.search(r"\.range_start\s*=\s*0x([0-9A-Fa-f]+),\s*/\* U\+.*?\*/\s*"
                  r"\.range_length\s*=\s*(\d+),\s*\.glyph_id_start\s*=\s*(\d+)", src, re.S)
    ascii_start, ascii_len, ascii_gid = (int(m.group(1), 16), int(m.group(2)), int(m.group(3))) \
        if m else (0x20, 95, 0)

    # 稀疏段的 glyph_id_start
    m = re.search(r"\.unicode_list\s*=\s*%s,\s*\.list_length\s*=\s*\d+,\s*"
                  r"\.type\s*=\s*1" % re.escape(name or "x"), src, re.S)
    sparse_start = ascii_gid + ascii_len
    m3 = re.search(r"\.glyph_id_start\s*=\s*(\d+),\s*\.unicode_list\s*=\s*%s"
                   % re.escape(name or "x"), src, re.S)
    if m3:
        sparse_start = int(m3.group(1))

    # 字形表：兼容两种写法（指定初始化器 / 位置初始化器）
    body = src[src.index("_glyphs[]"):]
    body = body[:body.index("\n};")]
    glyphs = []
    if ".bitmap_index" in body:
        pat = (r"\.bitmap_index\s*=\s*(\d+),\s*\.adv_w\s*=\s*(\d+),\s*"
               r"\.ofs_x\s*=\s*(-?\d+),\s*\.ofs_y\s*=\s*(-?\d+),\s*"
               r"\.box_w\s*=\s*(\d+),\s*\.box_h\s*=\s*(\d+)")
    else:
        pat = r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}"
    for g in re.finditer(pat, body):
        glyphs.append({"idx": int(g.group(1)), "adv": int(g.group(2)),
                       "ofs_x": int(g.group(3)), "ofs_y": int(g.group(4)),
                       "box_w": int(g.group(5)), "box_h": int(g.group(6))})

    lm = re.search(r"\.line_height\s*=\s*(\d+)", src)
    bm = re.search(r"\.base_line\s*=\s*(\d+)", src)
    meta = {"line_height": int(lm.group(1)) if lm else 0,
            "base_line": int(bm.group(1)) if bm else 0,
            "ascii_start": ascii_start, "ascii_len": ascii_len,
            "sparse_start": sparse_start, "sparse_n": len(sparse)}

    def gid_of(ch):
        cp = ord(ch)
        if ascii_start <= cp < ascii_start + ascii_len:
            return ascii_gid + (cp - ascii_start)
        if cp in sparse:
            return sparse_start + sparse.index(cp)
        return None

    return meta, glyphs, gid_of


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    font_path = FONT
    if "--font" in sys.argv:
        font_path = sys.argv[sys.argv.index("--font") + 1]
    text = args[0] if args else "A0a中测试 "
    meta, glyphs, gid_of = parse_font(font_path)
    print("字库: %s" % font_path)
    print("line_height=%d base_line=%d glyphs=%d" % (meta["line_height"], meta["base_line"], len(glyphs)))
    total = 0
    for ch in text:
        g = gid_of(ch)
        if g is None or g >= len(glyphs):
            print("  %r -> (缺字)" % ch)
            continue
        d = glyphs[g]
        print("  %r adv=%d ofs=(%d,%d) box=%dx%d" % (ch, d["adv"], d["ofs_x"], d["ofs_y"], d["box_w"], d["box_h"]))
        total += d["adv"]
    print("字符串总宽 = %d 像素" % total)


if __name__ == "__main__":
    main()
