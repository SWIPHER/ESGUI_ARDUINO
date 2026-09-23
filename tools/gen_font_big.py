#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成"真·大字号"ESGUI 字库（从系统 TTF 渲染），输出 src/font_big.c / font_big.h。

为什么需要它：
  像素放大（TFT_ZOOM=2）只能把 16px 点阵拉成 2x2 方块，笔画细节不会增加，所以"变大但不清晰"。
  要真正清晰，必须**按最终尺寸渲染**：本脚本用 FreeType（Pillow）把系统字体渲染成
  ESGUI 的 Font 结构，配合 TFT_ZOOM=1 做 1:1 映射（1 字体像素 = 1 面板像素）。

输出格式与框架自带字库（lib/ESGUI/Font/eui_test_font.c）**完全一致**（已按源码逐条核对）：
  · Bitmap：页式，stride = box_w（= 字形像素宽）字节/页，页数 = ceil(box_h/8)，
            字节内 bit n = 页内第 n 行；各字形首尾相接，bitmap_index 为字节偏移。
  · FontGlyph：bitmap_index / adv_w / ofs_x / ofs_y / box_w / box_h
            落点 gy = 行顶 + base_line + ofs_y，所以 ofs_y = 字形顶相对基线的偏移。
  · FontCmap：entry0 = type0 连续段（ASCII 0x20~0x7E，95 个）；
            entry1 = type1 稀疏表（其余字符，**必须升序**，框架用二分查找）。
  · line_height = 全部字形最底像素 - 最顶像素（紧凑不重叠）；base_line = 基线到行顶距离。

用法：
  python3 tools/gen_font_big.py                     # 默认参数生成（PingFang 30px）
  python3 tools/gen_font_big.py --size 28 --font "/System/Library/Fonts/Hiragino Sans GB.ttc"
  python3 tools/gen_font_big.py --preview           # 只在终端预览字形与度量，不写文件
  python3 tools/gen_font_big.py --list-faces        # 列出 .ttc 里的字体面（选 index 用）
"""

import argparse
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

OLD_FONT = "lib/ESGUI/Font/eui_test_font.c"     # 从这里继承"要生成哪些字"
OUT_C = "src/font_big.c"
OUT_H = "src/font_big.h"
FONT_NAME = "font_big"

DEFAULT_TTF = "/System/Library/Fonts/PingFang.ttc"
DEFAULT_INDEX = 0
DEFAULT_SIZE = 30
DEFAULT_THRESHOLD = 128

ASCII_START, ASCII_END = 0x20, 0x7E

# 额外收录的常用中文标点（老字库里没有；并入稀疏表后会升序排序）
EXTRA_CHARS = "：，。、；？！（）《》【】“”‘’—…·％"


def load_charset(path=OLD_FONT):
    """从老字库里读出稀疏汉字码点（升序）。"""
    src = open(path, encoding="utf-8", errors="ignore").read()
    m = re.search(r"eui_test_font_unicode_list_95\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit("在老字库里找不到稀疏 unicode 表: %s" % path)
    codes = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", m.group(1))]
    return sorted(set(codes))


def render_glyphs(font, codes, threshold):
    """渲染全部字形。

    返回 (glyphs, top_min, bot_max, ascent)；每个 glyph 含：
      code / adv / ofs_x / top(相对上伸线) / w / h / pages / data(页式字节)
    """
    ascent, descent = font.getmetrics()
    pad = 4
    cw = int(font.getlength("测")) + 8          # 画布宽：最宽的汉字也放得下，负 ofs_x 也容得下
    ch_h = ascent + descent + pad * 2
    glyphs = []
    top_min, bot_max = 10 ** 9, -10 ** 9

    for code in codes:
        ch = chr(code)
        img = Image.new("L", (max(cw, 16), ch_h), 0)
        ImageDraw.Draw(img).text((pad, pad + ascent), ch, fill=255, font=font, anchor="ls")
        bbox = img.getbbox()
        adv = int(round(font.getlength(ch)))

        if bbox is None:                        # 空白字形（如空格）：不占位图
            glyphs.append({"code": code, "adv": adv, "ofs_x": 0, "top": 0,
                           "w": 0, "h": 0, "pages": 0, "data": b""})
            continue

        x0, y0, x1, y1 = bbox
        w, h = x1 - x0, y1 - y0
        top = y0 - pad                          # 相对"上伸线"的顶部偏移
        top_min = min(top_min, top)
        bot_max = max(bot_max, y1 - pad)

        # 转页式位图：每页 stride = w 字节，字节内 bit n = 页内第 n 行
        pages = (h + 7) // 8
        data = bytearray()
        for p in range(pages):
            for col in range(w):
                byte = 0
                for n in range(8):
                    y = p * 8 + n
                    if y < h and img.getpixel((x0 + col, y0 + y)) >= threshold:
                        byte |= 1 << n
                data.append(byte)

        glyphs.append({"code": code, "adv": adv, "ofs_x": x0 - pad, "top": top,
                       "w": w, "h": h, "pages": pages, "data": bytes(data)})

    if top_min > bot_max:                       # 极端情况：全是空白字形
        top_min, bot_max = 0, 1
    return glyphs, top_min, bot_max, ascent


# ==================================================================
# 输出 C 源码
# ==================================================================
def emit_array(name, data, per_line=16):
    out = ["static const eui_uint8_t %s[] = {" % name]
    for i in range(0, len(data), per_line):
        out.append("    " + " ".join("0x%02X," % b for b in data[i:i + per_line]))
    out.append("};")
    return "\n".join(out)


def emit_u16_array(name, values, per_line=12):
    out = ["static const eui_uint16_t %s[] = {" % name]
    for i in range(0, len(values), per_line):
        out.append("    " + " ".join("0x%04X," % v for v in values[i:i + per_line]))
    out.append("};")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default=DEFAULT_TTF)
    ap.add_argument("--index", type=int, default=DEFAULT_INDEX)
    ap.add_argument("--size", type=int, default=DEFAULT_SIZE)
    ap.add_argument("--threshold", type=int, default=DEFAULT_THRESHOLD)
    ap.add_argument("--preview", action="store_true", help="只预览字形与度量，不写文件")
    ap.add_argument("--list-faces", action="store_true", help="列出字体面后退出")
    args = ap.parse_args()

    if args.list_faces:
        for i in range(12):
            try:
                f = ImageFont.truetype(args.font, 20, index=i)
                print("index %d: %s" % (i, f.getname()))
            except Exception:
                break
        return

    font = ImageFont.truetype(args.font, args.size, index=args.index)
    print("字体: %s (index %d, size %d) -> %s"
          % (args.font, args.index, args.size, font.getname()))

    sparse = load_charset()
    codes = list(range(ASCII_START, ASCII_END + 1)) + sorted(set(sparse) |
                                                             {ord(c) for c in EXTRA_CHARS})
    print("字符数: ASCII %d + 稀疏 %d" % (ASCII_END - ASCII_START + 1, len(codes) - 95))

    glyphs, top_min, bot_max, ascent = render_glyphs(font, codes, args.threshold)
    base_line = ascent - top_min
    line_height = bot_max - top_min

    cjk = [g for g in glyphs if g["code"] >= 0x2E80]
    adv_cjk = max(g["adv"] for g in cjk) if cjk else 0
    blank = sum(1 for g in glyphs if g["w"] == 0)
    blob = sum(len(g["data"]) for g in glyphs)
    print("度量: ascent=%d base_line=%d line_height=%d  空白字形=%d  位图=%.1f KB"
          % (ascent, base_line, line_height, blank, blob / 1024.0))
    print("排版估算: 汉字步进=%dpx（每行可放 %d 个汉字），行距=%dpx"
          % (adv_cjk, 216 // max(adv_cjk, 1), line_height))

    if args.preview:
        for ch in "测覆藏ESGUI测试123":
            g = next((x for x in glyphs if x["code"] == ord(ch)), None)
            if not g:
                continue
            print("\n--- '%s'  adv=%d ofs_x=%d top=%d box=%dx%d ---"
                  % (ch, g["adv"], g["ofs_x"], g["top"], g["w"], g["h"]))
            for row in range(g["h"]):
                line = ""
                for col in range(g["w"]):
                    byte = g["data"][(row // 8) * g["w"] + col]
                    line += "#" if (byte >> (row % 8)) & 1 else "."
                print(line)
        return

    # ---------- 写文件 ----------
    sparse_codes = [g["code"] for g in glyphs if g["code"] > ASCII_END]   # codes 本身升序
    out = []
    out.append("/*\n"
               " * %s.c —— 真·大字号 ESGUI 字库（自动生成，请勿手改）\n"
               " *\n"
               " * 生成脚本：tools/gen_font_big.py\n"
               " * 来源字体：%s (index %d)，渲染 %d px，二值化阈值 %d\n"
               " * 度量：line_height=%d  base_line=%d  字形 %d 个（ASCII 95 + 稀疏 %d）\n"
               " * 格式：与框架自带 eui_test_font 完全一致（页式位图 + FontGlyph + FontCmap）\n"
               " */\n\n"
               "#include \"%s.h\"\n\n"
               % (FONT_NAME, os.path.basename(args.font), args.index, args.size,
                  args.threshold, line_height, base_line, len(glyphs), len(sparse_codes),
                  FONT_NAME))

    # 位图 blob（先算 bitmap_index 再拼接）
    blob_bytes = bytearray()
    for g in glyphs:
        g["bitmap_index"] = len(blob_bytes)
        blob_bytes += g["data"]
    out.append("/* 位图 blob：%d 字节，页式，stride = box_w 字节/页 */\n" % len(blob_bytes))
    out.append(emit_array("%s_bitmap" % FONT_NAME, blob_bytes))
    out.append("")

    # 字形描述表（顺序 = cmap 的 glyph_id 顺序）
    out.append("/* 字形描述表：%d 项（bitmap_index, adv_w, ofs_x, ofs_y, box_w, box_h） */\n"
               % len(glyphs))
    out.append("static const FontGlyph %s_glyphs[] = {\n" % FONT_NAME)
    for g in glyphs:
        out.append("    { %6d, %3d, %4d, %4d, %3d, %3d },   /* U+%04X */\n"
                   % (g["bitmap_index"], g["adv"], g["ofs_x"], g["top"] - base_line,
                      g["w"], g["h"], g["code"]))
    out.append("};\n\n")

    # cmap：ASCII 连续段 + 稀疏升序表
    out.append("/* 稀疏码点表（升序，框架按二分查找） */\n")
    out.append(emit_u16_array("%s_unicode_list" % FONT_NAME, sparse_codes))
    out.append("\nstatic const FontCmap %s_cmaps[] = {\n"
               "    {\n"
               "        .range_start    = 0x%04X,  /* U+0020 ~ U+007E */\n"
               "        .range_length   = %d,\n"
               "        .glyph_id_start = 0,\n"
               "        .unicode_list   = ESGUI_NULL,\n"
               "        .list_length    = 0,\n"
               "        .type           = 0,\n"
               "    },\n"
               "    {\n"
               "        .range_start    = 0,\n"
               "        .range_length   = 0,\n"
               "        .glyph_id_start = %d,\n"
               "        .unicode_list   = %s_unicode_list,\n"
               "        .list_length    = %d,\n"
               "        .type           = 1,\n"
               "    },\n"
               "};\n\n" % (FONT_NAME, ASCII_START, ASCII_END - ASCII_START + 1,
                           ASCII_END - ASCII_START + 1, FONT_NAME, len(sparse_codes)))

    out.append("const Font %s = {\n"
               "    .bitmap      = %s_bitmap,\n"
               "    .glyphs      = %s_glyphs,\n"
               "    .cmaps       = %s_cmaps,\n"
               "    .cmap_num    = 2,\n"
               "    .line_height = %d,\n"
               "    .base_line   = %d,\n"
               "};\n" % (FONT_NAME, FONT_NAME, FONT_NAME, FONT_NAME, line_height, base_line))

    guard = FONT_NAME.upper() + "_H"
    header = ("/*\n"
              " * %s.h —— 真·大字号 ESGUI 字库声明（自动生成，请勿手改）\n"
              " *\n"
              " * 生成脚本：tools/gen_font_big.py（来源 %s %dpx）\n"
              " * 接线方式（platformio.ini）：\n"
              " *   -DESGUI_DEFAULT_FONT=%s -DESGUI_KEY_BOARD_FONT=%s -include %s.h\n"
              " */\n"
              "#ifndef %s\n"
              "#define %s\n\n"
              "#include \"ESGUI_Def.h\"\n"
              "#include \"ESGUI_BSP_Text.h\"\n\n"
              "#ifdef __cplusplus\n"
              "extern \"C\" {\n"
              "#endif\n\n"
              "extern const Font %s;\n\n"
              "#ifdef __cplusplus\n"
              "}\n"
              "#endif\n"
              "#endif /* %s */\n"
              % (FONT_NAME, os.path.basename(args.font), args.size,
                 FONT_NAME, FONT_NAME, FONT_NAME, guard, guard, FONT_NAME, guard))

    with open(OUT_C, "w", encoding="utf-8") as f:
        f.write("".join(out))
    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write(header)
    print("已写出 %s (%.1f MB) 和 %s" % (OUT_C, os.path.getsize(OUT_C) / 1e6, OUT_H))


if __name__ == "__main__":
    sys.exit(main())
