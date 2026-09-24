#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""核对"选中焦点框"相对文字是否居中（用字库真实度量算，不靠肉眼）。

原理（对应 lib/ESGUI/ESGUI_PageDefaltVtbl.c 的绘制）：
  · 文字画在 x = ESGUI_TEXT_MARGIN_X、y = 行顶；字墨起点 = 行顶 + base_line + ofs_y
  · 焦点框画在 x = OFF_X（菜单）/ window_x + OFF_X（列表弹窗），y = 行顶 + OFF_Y
    框宽 = 文字宽 + ESGUI_FOCUS_BOX_PAD_X，框高 = line_height
  → 把四项留白（左/右/上/下）算出来，越接近越居中。

OFF_X / OFF_Y / TEXT_MARGIN_X / PAD_X 直接从 platformio.ini 的 build_flags 里读，
所以改完配置跑一遍就知道摆正了没有。

用法：
  python3 tools/check_focus_box.py            # 用默认样本字（汉字/数字/字母各一）
  python3 tools/check_focus_box.py 设置 玩家1  # 指定要检查的文本
"""

import re
import sys

sys.path.insert(0, "tools")
from font_metrics import parse_font   # noqa: E402  （复用字库解析）

FONT = "src/font_big.c"
INI = "platformio.ini"


def read_flags(path=INI):
    """从 platformio.ini 的 build_flags 里取出 -D 宏值。"""
    src = open(path, encoding="utf-8", errors="ignore").read()
    out = {}
    for m in re.finditer(r"-D([A-Z_0-9]+)=(-?\d+)", src):
        out[m.group(1)] = int(m.group(2))
    return out


def main():
    flags = read_flags()
    off_x = flags.get("ESGUI_FOCUS_BOX_OFF_X", 0)
    off_y = flags.get("ESGUI_FOCUS_BOX_OFF_Y", 0)
    margin_x = flags.get("ESGUI_TEXT_MARGIN_X", 6)
    pad_x = flags.get("ESGUI_FOCUS_BOX_PAD_X", 6)
    bar_w = flags.get("ESGUI_PROGRESS_BAR_W", 4)
    logic_w = 216                       # src/tft_drv.h 的 ESGUI_LOGIC_W

    meta, glyphs, gid_of = parse_font(FONT)
    print("字库 %s: line_height=%d base_line=%d" % (FONT, meta["line_height"], meta["base_line"]))
    print("配置: OFF_X=%d OFF_Y=%d  TEXT_MARGIN_X=%d  FOCUS_BOX_PAD_X=%d  逻辑宽=%d"
          % (off_x, off_y, margin_x, pad_x, logic_w))

    samples = sys.argv[1:] or ["测试", "玩家1", "Setting"]
    ok = True
    for text in samples:
        # 1) 文字尺寸（advance 宽 + 字墨范围）
        total_w = 0
        ink_l, ink_t, ink_r, ink_b = None, None, None, None
        for ch in text:
            g = gid_of(ch)
            if g is None:
                print("  %r 缺字，跳过" % ch)
                continue
            d = glyphs[g]
            # 字墨（相对行顶）：x = 光标x + ofs_x；y = base_line + ofs_y
            if d["box_w"] and d["box_h"]:
                gl = total_w + d["ofs_x"]
                gt = meta["base_line"] + d["ofs_y"]
                gr = gl + d["box_w"]
                gb = gt + d["box_h"]
                ink_l = gl if ink_l is None else min(ink_l, gl)
                ink_t = gt if ink_t is None else min(ink_t, gt)
                ink_r = gr if ink_r is None else max(ink_r, gr)
                ink_b = gb if ink_b is None else max(ink_b, gb)
            total_w += d["adv"]
        if ink_l is None:
            continue

        # 2) 焦点框（菜单：x 从 OFF_X 起；宽 = 文字宽 + PAD_X；高 = line_height）
        box_l = off_x
        box_r = off_x + total_w + pad_x
        box_t = off_y
        box_b = off_y + meta["line_height"]

        # 3) 文字实际绘制位置：x = margin_x，y = 行顶
        text_l = margin_x + ink_l
        text_r = margin_x + ink_r
        text_t = ink_t
        text_b = ink_b

        lpad, rpad = text_l - box_l, box_r - text_r
        tpad, bpad = text_t - box_t, box_b - text_b
        print("\n样本 %-8s 文字宽=%d 墨迹 %dx%d" % (text, total_w, text_r - text_l, text_b - text_t))
        print("  左留白 %3d | 右留白 %3d   （差 %d）" % (lpad, rpad, abs(lpad - rpad)))
        print("  上留白 %3d | 下留白 %3d   （差 %d）" % (tpad, bpad, abs(tpad - bpad)))
        # 含 g/y/p 等下伸部字母时，字墨比行高高 → 框架框高 = 行高时下边本来就装不下，
        # 这属正常（偏移只能二选一：照顾汉字居中，或照顾下伸部不越界），故不算失败。
        has_descender = text_b > meta["line_height"]
        warn = []
        if min(lpad, rpad, tpad, bpad) < 0 and not (has_descender and bpad < 0):
            warn.append("有留白为负＝字被框切到")
        if tpad < 0:
            warn.append("字顶越出框上边")
        if not has_descender and (abs(lpad - rpad) > 3 or abs(tpad - bpad) > 3):
            warn.append("留白差 > 3px，建议微调 OFF_X/OFF_Y")
        if box_r > logic_w - bar_w:
            warn.append("框右缘顶到进度条/屏边")
        if warn:
            ok = False
            print("  ⚠ " + "；".join(warn))
        elif has_descender and bpad < 0:
            print("  OK 居中（含下伸部字母，超出下边 %dpx，属正常；全汉字界面可忽略）" % -bpad)
        else:
            print("  OK 居中")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
