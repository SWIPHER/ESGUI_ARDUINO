#!/usr/bin/env python3
"""复刻框架 3D 菜单的尺寸公式，核对"模型显示尺寸"随 ESGUI_3D_MENU_MODEL_SCALE 的变化。

对应 lib/ESGUI/ESGUI_PageDefaltVtbl.c：
    focus_box_h      = mid_h - 2*ESGUI_3D_MENU_FOCUS_MARGIN
    model_display_h  = focus_box_h * ESGUI_3D_MENU_MODEL_SCALE / 100
    slot_w           = model_display_h + ESGUI_3D_MENU_ITEM_GAP
（屏上尺寸 = 逻辑像素 x TFT_ZOOM）

用法：python3 tools/check_3d_model_size.py [当前的 MODEL_SCALE]
"""
import sys

# 本工程当前参数（与 platformio.ini / src/tft_drv.h / 字库一致）
PBW = 4            # -DESGUI_PROGRESS_BAR_W=4
FONT_H = 33        # src/font_big.c 的 line_height（真·30px 字库）
CANVAS_H = 272     # ESGUI_LOGIC_H
FOCUS_MARGIN = 20  # -DESGUI_3D_MENU_FOCUS_MARGIN（未覆盖时用框架默认 20）
ITEM_GAP = 16      # -DESGUI_3D_MENU_ITEM_GAP=16
ZOOM = 1           # src/tft_drv.h 的 TFT_ZOOM（1:1）

DEFAULT_PCT = 65   # 框架默认 ESGUI_3D_MENU_MODEL_SCALE


def layout(canvas_h=CANVAS_H, focus_margin=FOCUS_MARGIN):
    progress_bar_h = PBW if PBW >= 2 else 3
    top_margin = progress_bar_h + 4
    label_y = canvas_h - FONT_H - 2
    mid_h = (label_y - 2) - top_margin
    focus_box_h = mid_h - 2 * focus_margin if mid_h > 2 * focus_margin else mid_h
    return mid_h, focus_box_h


def model_h(focus_box_h, pct):
    return focus_box_h * pct // 100


def main():
    cur = int(sys.argv[1]) if len(sys.argv) > 1 else 33
    mid_h, focus_box_h = layout()
    print("逻辑屏 %dx%d  →  顶部留白 %d, 中间区 mid_h=%d, 焦点框 focus_box_h=%d"
          % (CANVAS_H, CANVAS_H, PBW + 4, mid_h, focus_box_h))
    for name, pct in (("框架默认 %d%%" % DEFAULT_PCT, DEFAULT_PCT), ("当前 %d%%" % cur, cur)):
        m = model_h(focus_box_h, pct)
        print("  %-12s 模型 %2d 逻辑px  →  屏上 %3d px   slot_w=%2d（一屏可见 %.1f 个）"
              % (name, m, m * ZOOM, m + ITEM_GAP, CANVAS_H / (m + ITEM_GAP)))
    base = model_h(focus_box_h, DEFAULT_PCT)
    now = model_h(focus_box_h, cur)
    if base:
        print("当前 / 默认 = %.3f" % (now / base))


if __name__ == "__main__":
    main()
