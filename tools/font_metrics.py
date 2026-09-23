#!/usr/bin/env python3
"""读取 eui_test_font 里指定字符的字形度量（adv_w / box_w / box_h / ofs），
用来估算"一行能放几个字"（布局用）。

用法：python3 tools/font_metrics.py A中测A1ab
"""
import re
import sys

FONT = "lib/ESGUI/Font/eui_test_font.c"


def parse_font(path):
    src = open(path, encoding="utf-8", errors="ignore").read()

    # ASCII 连续段 + 稀疏段
    cmaps = re.findall(r"\.range_start\s*=\s*0x([0-9A-Fa-f]+).*?\.range_length\s*=\s*(\d+).*?\.glyph_id_start\s*=\s*(\d+)", src, re.S)
    ascii_start, ascii_len, ascii_gid = int(cmaps[0][0], 16), int(cmaps[0][1]), int(cmaps[0][2])
    m = re.search(r"eui_test_font_unicode_list_95\[\]\s*=\s*\{(.*?)\};", src, re.S)
    sparse = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", m.group(1))]
    sparse_start = int(cmaps[1][2]) if len(cmaps) > 1 else 95

    # glyph 数组（指定初始化器格式：每个字形一段 .bitmap_index/.adv_w/.ofs_x/.ofs_y/.box_w/.box_h）
    body = src[src.index("eui_test_font_glyphs[]"):]
    body = body[:body.index("\n};")]
    glyphs = []
    for g in re.finditer(
        r"\.bitmap_index\s*=\s*(\d+),\s*"
        r"\.adv_w\s*=\s*(\d+),\s*"
        r"\.ofs_x\s*=\s*(-?\d+),\s*"
        r"\.ofs_y\s*=\s*(-?\d+),\s*"
        r"\.box_w\s*=\s*(\d+),\s*"
        r"\.box_h\s*=\s*(\d+)", body):
        glyphs.append({
            "idx": int(g.group(1)), "adv": int(g.group(2)),
            "ofs_x": int(g.group(3)), "ofs_y": int(g.group(4)),
            "box_w": int(g.group(5)), "box_h": int(g.group(6)),
        })

    lm = re.search(r"\.line_height\s*=\s*(\d+)", src)
    bm = re.search(r"\.base_line\s*=\s*(\d+)", src)
    meta = {"line_height": int(lm.group(1)), "base_line": int(bm.group(1)),
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
    text = sys.argv[1] if len(sys.argv) > 1 else "A0a中测试 "
    meta, glyphs, gid_of = parse_font(FONT)
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
