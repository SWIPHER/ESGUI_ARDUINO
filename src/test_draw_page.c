/*
 * test_draw_page.c —— 绘图图元 / 显示特效测试页
 *
 * 这是一个"自定义虚函数表"的全屏页面（item_num = 0，不走菜单条目机制）：
 *   · on_draw   自己按 pattern_idx 画一屏图案
 *   · on_input  上下滑动/点击切图案，长按返回
 *   · on_create 启动一个无限脉冲动画 → 框架每 Tick 都重绘（顺带验证"动画驱动刷新"）
 *
 * 覆盖范围：
 *   BSP 全部图元（点/线/矩形/圆/三角/圆角矩形，SET-CLEAR-XOR 三种模式）、
 *   位图三种绘制（正常/透明/反色）、GIF 动图、Widget 全部组件、
 *   字体渲染与裁剪、调色板换色（验证 RGB565 输出）、像素对齐自检、
 *   绕过框架的 TFT 直写自检、过渡遮罩效果。
 */
#include "test_draw_page.h"

#include <stdio.h>
#include <string.h>

#include "ESGUI.h"
#include "ESGUI_Anim.h"
#include "ESGUI_BSP_BMP.h"
#include "ESGUI_BSP_Text.h"
#include "ESGUI_GIF.h"
#include "ESGUI_PageDefaltVtbl.h"   /* ESGUI_DEFAULT_FONT（默认字体宏） */
#include "ESGUI_UseCanvas.h"
#include "ESGUI_Widget.h"
#include "tft_drv.h"
#include "test_assets.h"

/* 图案清单（顺序即上下切换顺序） */
enum {
    PAT_BASIC = 0,      /* 基础图元 */
    PAT_RECT,           /* 矩形三种画法 */
    PAT_CIRCLE,         /* 圆三种画法 */
    PAT_TRIANGLE,       /* 三角形三种画法 */
    PAT_ROUND,          /* 圆角矩形 */
    PAT_BITMAP,         /* 位图 / 透明 / 反色 / GIF */
    PAT_WIDGET,         /* 进度条 / 复选框 / 焦点框 */
    PAT_FONT,           /* 字体与文本裁剪 */
    PAT_PALETTE,        /* 调色板彩条（走 g_pal_cb 换色） */
    PAT_ALIGN,          /* 像素对齐自检 */
    PAT_DITHER,         /* 灰阶抖动 */
    PAT_DIRECT,         /* 直写屏自检（阻塞） */
    PAT_MASK,           /* 过渡遮罩 */
    PAT_NUM
};

static const char *const s_pattern_names[PAT_NUM] = {
    "图元",
    "矩形",
    "圆",
    "三角形",
    "圆角",
    "位图",
    "组件",
    "字体",
    "调色板",
    "对齐",
    "抖动",
    "直写屏",
    "遮罩",
};

static ESGUI_MenuPage_T draw_page;
static uint8_t         draw_pattern;
static eui_uint16_t    draw_pulse;          /* 脉冲动画变量：让页面持续重绘 */
static ESGUI_GIF_T     draw_gif;

/* ==================== 调色板回调（彩条：按行换色） ==================== */
/* g_pal_cb 由 tft_drv 在送屏时按行区间调用：pal[0]=背景色、pal[1]=前景色。
 * 8 条色带 → 直接证明 RGB565 通道顺序、颜色深度都正确。 */
static const uint16_t s_band_bg[8] = {
    0x0000, 0x7800, 0x03E0, 0x0010, 0x780F, 0x03EF, 0x7BEF, 0x000F,
};
static const uint16_t s_band_fg[8] = {
    0xFFFF, 0xFD20, 0x87F0, 0x5D7F, 0xFFE0, 0x07FF, 0xFFFF, 0xF81F,
};

static void draw_pal_bands_cb(int y, uint16_t pal[2])
{
    int b = (y * 8) / ESGUI_LOGIC_H;
    if (b < 0) b = 0;
    if (b > 7) b = 7;
    pal[0] = s_band_bg[b];
    pal[1] = s_band_fg[b];
}

/* ==================== 动画回调 ==================== */
static void draw_anim_cb_u16(void *var, eui_int32_t value)
{
    if (var != ESGUI_NULL) {
        *(eui_uint16_t *)var = (eui_uint16_t)value;
    }
}

/* 无限脉冲动画：anim_running=1 → 框架每 Tick 重绘本页（GIF/遮罩才能动起来） */
static void draw_start_pulse(void)
{
    if (anim_is_running_var(&draw_pulse)) return;
    anim_t a = {0};
    a.var          = &draw_pulse;
    a.exec_cb      = draw_anim_cb_u16;
    a.start        = 0;
    a.end          = 0;
    a.duration     = 100;
    a.path_type    = ANIM_PATH_LINEAR;
    a.repeat_cnt   = 0xFFFF;
    a.repeat_total = 0xFFFF;
    anim_start(&a);
}

/* ==================== 布局常量（全部按字体行高表达，换字号不用改这里） ====================
 * 逻辑屏 216x272、字（真·30px 字库）行高 33：
 *   第 0 行 = 标题；分隔线在 y=33；正文 = [TOP, 239]；最后一行（239..271）留给底部提示。
 *   CELL = 68：一个"图形 + 标注"单元（上半行画图形、下半行写标注）。
 */
#define LH          (ESGUI_DEFAULT_FONT.line_height)    /* 33 */
#define TOP         (LH + 2)                            /* 35 正文起点 */
#define CELL        ((LH) * 2 + 2)                      /* 68 单元高 */
#define CELL_Y(r)   (TOP + (r) * (CELL))                /* 单元 r 的 y：35 / 103 / 171 */
#define CELL_H      (LH - 2)                            /* 31 单元内图形高度 */
#define COL3(w, i)  (8 + (i) * (((w) - 16) / 3))        /* 三列布局的 x */

static Canvas *draw_canvas(ESGUI_MenuPage_T *page)
{
    if (page == ESGUI_NULL || page->render_ctx == ESGUI_NULL) return ESGUI_NULL;
    return ((CanvasStripIter *)page->render_ctx)->canvas;
}

/* 图元下方的小标注（自动裁剪到屏宽，绝不越界） */
static void draw_label(Canvas *c, int x, int y, const char *text)
{
    if (x < 0) x = 0;
    eui_draw_text_clip(c, x, y, &ESGUI_DEFAULT_FONT, text, EUI_MODE_SET, c->width - x);
}

/* 画标题栏 + 底部提示 */
static void draw_frame(ESGUI_MenuPage_T *page, Canvas *c, const char *hint)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%u/%u %s", (unsigned)(draw_pattern + 1),
             (unsigned)PAT_NUM, s_pattern_names[draw_pattern]);
    eui_draw_text(c, 2, 0, &ESGUI_DEFAULT_FONT, buf, EUI_MODE_SET);
    eui_draw_hline(c, 0, c->width - 1 - ESGUI_PROGRESS_BAR_W,
                   ESGUI_DEFAULT_FONT.line_height, EUI_MODE_SET);
    /* 右侧纵向进度条显示"第几个图案" */
    ESGUI_WidgetProgrssBarPermille(c, c->width - ESGUI_PROGRESS_BAR_W, 0,
                                   ESGUI_PROGRESS_BAR_W,
                                   (eui_uint16_t)(((eui_uint32_t)(draw_pattern + 1) * 1000u) / PAT_NUM),
                                   ESGUI_WIDGET_PROGBAR_DOWN);
    eui_draw_text(c, 2, c->height - ESGUI_DEFAULT_FONT.line_height,
                  &ESGUI_DEFAULT_FONT, hint, EUI_MODE_SET);
    (void)page;
}

/* ==================== 各个图案（坐标全部按逻辑屏 216x272 + 行高 33 走） ==================== */

static void pat_basic(Canvas *c)
{
    int w = c->width;

    /* 单元 0：横线 / 竖线 / 斜线 */
    eui_draw_hline(c, 8, w - 9, TOP + 4, EUI_MODE_SET);
    eui_draw_vline(c, 16, TOP + 8, CELL_Y(0) + CELL_H, EUI_MODE_SET);
    eui_draw_line(c, 32, TOP + 8, w - 12, CELL_Y(0) + CELL_H, EUI_MODE_SET);
    draw_label(c, 8, CELL_Y(0) + LH, "横线 竖线 斜线");

    /* 单元 1：XOR 画两遍 = 抵消（应看不见）；画一遍 = 反色 */
    eui_draw_line(c, 8, CELL_Y(1) + 8, w - 9, CELL_Y(1) + 8, EUI_MODE_XOR);
    eui_draw_line(c, 8, CELL_Y(1) + 8, w - 9, CELL_Y(1) + 8, EUI_MODE_XOR);
    eui_draw_line(c, 8, CELL_Y(1) + 22, w - 9, CELL_Y(1) + 22, EUI_MODE_XOR);
    draw_label(c, 8, CELL_Y(1) + LH, "XOR 抵消/反色");

    /* 单元 2：实心块里用 CLEAR 擦一条线 */
    eui_draw_rect_fill(c, 8, CELL_Y(2), w - 9, CELL_Y(2) + CELL_H, EUI_MODE_SET);
    eui_draw_hline(c, 8, w - 9, CELL_Y(2) + 15, EUI_MODE_CLER);
    draw_label(c, 8, CELL_Y(2) + LH, "CLEAR 擦除");
}

static void pat_rect(Canvas *c)
{
    int w = c->width;
    int cw = (w - 16) / 3;

    /* 单元 0：填充 / 描边 / 框 */
    eui_draw_rect_fill(c, 8, TOP + 2, 8 + cw - 10, TOP + 2 + CELL_H, EUI_MODE_SET);
    eui_draw_rect_stroke(c, 8 + cw, TOP + 2, 8 + 2 * cw - 10, TOP + 2 + CELL_H, EUI_MODE_SET);
    eui_draw_rect_box(c, 8 + 2 * cw, TOP + 2, w - 9, TOP + 2 + CELL_H, EUI_MODE_SET);
    draw_label(c, 8, CELL_Y(0) + LH, "填充 描边 框");

    /* 单元 1：XOR 叠加（重叠处反色） */
    eui_draw_rect_fill(c, 8, CELL_Y(1) + 2, w / 2 + 20, CELL_Y(1) + 2 + CELL_H, EUI_MODE_SET);
    eui_draw_rect_fill(c, w / 2 - 20, CELL_Y(1) + 2, w - 9, CELL_Y(1) + 2 + CELL_H, EUI_MODE_XOR);
    draw_label(c, 8, CELL_Y(1) + LH, "XOR 叠加");

    /* 单元 2：CLEAR 从实心块里抠一个框 */
    eui_draw_rect_fill(c, 8, CELL_Y(2), w - 9, CELL_Y(2) + CELL_H, EUI_MODE_SET);
    eui_draw_rect_stroke(c, 30, CELL_Y(2) + 6, w - 31, CELL_Y(2) + CELL_H - 6, EUI_MODE_CLER);
    draw_label(c, 8, CELL_Y(2) + LH, "CLEAR 抠图");
}

static void pat_circle(Canvas *c)
{
    int w = c->width;
    int r = CELL_H / 2;                             /* 15 */

    /* 单元 0：填充 / 描边 / 框 */
    eui_draw_circle_fill(c, 8 + r, TOP + r, r, EUI_MODE_SET);
    eui_draw_circle_stroke(c, w / 2, TOP + r, r, EUI_MODE_SET);
    eui_draw_circle_box(c, w - 9 - r, TOP + r, r - 2, EUI_MODE_SET);
    draw_label(c, 8, CELL_Y(0) + LH, "填充 描边 框");

    /* 单元 1：两个 XOR 圆环相交（交集反色） */
    eui_draw_circle_stroke(c, w / 2 - 34, CELL_Y(1) + r, r + 12, EUI_MODE_XOR);
    eui_draw_circle_stroke(c, w / 2 + 34, CELL_Y(1) + r, r + 12, EUI_MODE_XOR);
    draw_label(c, 8, CELL_Y(1) + LH, "XOR 圆环相交");

    /* 单元 2：同心圆 */
    for (int rr = 4; rr <= r; rr += 5) {
        eui_draw_circle_stroke(c, 8 + 2 * r, CELL_Y(2) + r, rr, EUI_MODE_SET);
    }
    draw_label(c, 8, CELL_Y(2) + LH, "同心圆");
}

static void pat_triangle(Canvas *c)
{
    int w = c->width;

    /* 单元 0：填充 / 描边 / 框 */
    eui_draw_triangle_fill(c, 8, TOP + CELL_H, 46, TOP, 84, TOP + CELL_H, EUI_MODE_SET);
    eui_draw_triangle_stroke(c, 92, TOP + CELL_H, 130, TOP, 168, TOP + CELL_H, EUI_MODE_SET);
    eui_draw_triangle_box(c, 176, TOP + CELL_H, 200, TOP, w - 9, TOP + CELL_H, EUI_MODE_SET);
    draw_label(c, 8, CELL_Y(0) + LH, "填充 描边 框");

    /* 单元 1：先 XOR 再 SET 叠加（看交集效果） */
    eui_draw_triangle_fill(c, 8, CELL_Y(1) + CELL_H, 84, CELL_Y(1), 160, CELL_Y(1) + CELL_H,
                           EUI_MODE_XOR);
    eui_draw_triangle_fill(c, 56, CELL_Y(1) + CELL_H, 132, CELL_Y(1), w - 9, CELL_Y(1) + CELL_H,
                           EUI_MODE_SET);
    draw_label(c, 8, CELL_Y(1) + LH, "XOR 与 SET");

    /* 单元 2：倒三角 + 内部 XOR 小三角 */
    eui_draw_triangle_stroke(c, 8, CELL_Y(2), 70, CELL_Y(2) + CELL_H, 132, CELL_Y(2), EUI_MODE_SET);
    eui_draw_triangle_fill(c, 50, CELL_Y(2) + 6, 96, CELL_Y(2) + CELL_H - 6, 142, CELL_Y(2) + 6,
                           EUI_MODE_XOR);
    draw_label(c, 8, CELL_Y(2) + LH, "倒三角内 XOR");
}

static void pat_round(Canvas *c)
{
    int w = c->width;

    /* 单元 0：圆角填充（小圆角）/ 圆角描边（中）/ 圆角框（大） */
    eui_draw_round_rect_fill(c, 8, TOP, 72, TOP + CELL_H, 3, EUI_MODE_SET);
    eui_draw_round_rect_stroke(c, 84, TOP, 148, TOP + CELL_H, 10, EUI_MODE_SET);
    eui_draw_round_rect_box(c, 160, TOP, w - 9, TOP + CELL_H, 15, EUI_MODE_SET);
    draw_label(c, 8, CELL_Y(0) + LH, "圆角 3 / 10 / 15");

    /* 单元 1：XOR 叠加圆角矩形 */
    eui_draw_round_rect_fill(c, 8, CELL_Y(1) + 2, 120, CELL_Y(1) + 2 + CELL_H, 16, EUI_MODE_SET);
    eui_draw_round_rect_fill(c, 60, CELL_Y(1) + 2, w - 9, CELL_Y(1) + 2 + CELL_H, 16,
                             EUI_MODE_XOR);
    draw_label(c, 8, CELL_Y(1) + LH, "XOR 叠加圆角矩形");

    /* 单元 2：大圆角填充（看圆角处的像素台阶） */
    eui_draw_round_rect_fill(c, 8, CELL_Y(2), w - 9, CELL_Y(2) + CELL_H, 30, EUI_MODE_SET);
    eui_draw_rect_fill(c, w / 2 - 20, CELL_Y(2) + 8, w / 2 + 20, CELL_Y(2) + CELL_H - 8,
                       EUI_MODE_CLER);
    draw_label(c, 8, CELL_Y(2) + LH, "大圆角 r30");
}

static void pat_bitmap(Canvas *c)
{
    int w = c->width;

    /* 第一行：64x64 图标 —— 普通 / 反色 / 透明（先铺实心块再"只画 1 像素"→ 挖空） */
    eui_draw_bitmap(c, 8, TOP, &tst_icon_settings, 1);
    eui_draw_bitmap_invert(c, 80, TOP, &tst_icon_music);
    eui_draw_rect_fill(c, 152, TOP, w - 9, TOP + 63, EUI_MODE_SET);
    eui_draw_bitmap_transparent(c, 152, TOP, &tst_icon_heart, 0, 0);

    /* 第二行：40x40 缩略图 + 透明（只画 0 像素 = 负空间填实） */
    int y2 = TOP + 68;
    eui_draw_bitmap(c, 8, y2, &tst_small_star, 1);
    eui_draw_bitmap(c, 56, y2, &tst_small_wifi, 1);
    eui_draw_bitmap(c, 104, y2, &tst_small_folder, 1);
    eui_draw_rect_fill(c, 152, y2, 191, y2 + 39, EUI_MODE_SET);
    eui_draw_bitmap_transparent(c, 152, y2, &tst_small_heart, 0, 1);

    /* 第三行：GIF 动图（本页有脉冲动画 → 每帧重绘，帧才会推进） */
    int y3 = TOP + 115;
    ESGUI_GIFDraw(c, &draw_gif, 8, y3, EUI_MODE_SET, anim_get_tick(), ESGUI_GIF_PLAY_LOOP);
    ESGUI_GIFDraw(c, &draw_gif, 80, y3, EUI_MODE_SET, anim_get_tick(), ESGUI_GIF_PLAY_LOOP);
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "GIF %u/%u",
                 (unsigned)(draw_gif.frame + 1), (unsigned)draw_gif.frame_count);
        draw_label(c, 152, y3 + 16, buf);
    }
}

static void pat_widget(Canvas *c)
{
    int w = c->width;
    (void)w;

    /* 三条横向进度条（右 / 左 / 右），长度 150 */
    ESGUI_WidgetProgrssBarChangeLenPermille(c, 8, TOP + 2, 6, 150, 250,
                                            ESGUI_WIDGET_PROGBAR_RIGHT);
    ESGUI_WidgetProgrssBarChangeLenPermille(c, 8, TOP + 2 + LH, 6, 150, 700,
                                            ESGUI_WIDGET_PROGBAR_LEFT);
    ESGUI_WidgetProgrssBarChangeLenPermille(c, 8, TOP + 2 + 2 * LH, 6, 150, 1000,
                                            ESGUI_WIDGET_PROGBAR_RIGHT);
    draw_label(c, 170, TOP + 4, "250");
    draw_label(c, 170, TOP + 4 + LH, "700");
    draw_label(c, 170, TOP + 4 + 2 * LH, "1000");

    /* 复选框：方（未选/选中）、圆（未选/选中） */
    int cy = TOP + 3 * LH + 6;
    ESGUI_WidgetCheckBoxSquare(c, 8, cy, 28, 28, false);
    ESGUI_WidgetCheckBoxSquare(c, 52, cy, 28, 28, true);
    ESGUI_WidgetCheckBoxRound(c, 116, cy + 14, 14, false);
    ESGUI_WidgetCheckBoxRound(c, 168, cy + 14, 14, true);
    draw_label(c, 8, cy + LH, "复选框 方/圆");

    /* 焦点框：文本焦点框（XOR 圆角）+ 位图焦点框（四角） */
    int fy = cy + LH + 6;
    ESGUI_WidgetTextFocusBox(c, 8, fy - 4, LH - 2, 90);
    draw_label(c, 106, fy - 4, "焦点框");
    ESGUI_WidgetBmpFocusBox(c, 8, fy + LH, 40, 26);
    ESGUI_WidgetBmpFocusBoxAnim(c, 60, fy + LH, 60, 26);
}

static void pat_font(Canvas *c)
{
    /* ASCII 表：每行 12 个字符（30px 字宽 15~19px），6 行 = 72 个字符（0x20~0x67） */
    char line[16];
    int n = 0, row = 0;
    for (int ch = 0x20; ch <= 0x67; ch++) {
        line[n++] = (char)ch;
        if (n == 12) {
            line[n] = '\0';
            eui_draw_text(c, 8, TOP + row * LH, &ESGUI_DEFAULT_FONT, line, EUI_MODE_SET);
            n = 0;
            row++;
        }
    }
    if (n > 0) {
        line[n] = '\0';
        eui_draw_text(c, 8, TOP + row * LH, &ESGUI_DEFAULT_FONT, line, EUI_MODE_SET);
    }
}

static void pat_palette(Canvas *c)
{
    /* 本图案在 on_draw 里把 g_pal_cb 指向彩条回调 → 整屏按行换色（8 条色带） */
    char buf[8];
    for (int i = 0; i < 8; i++) {
        int y0 = TOP + i * 25;
        eui_draw_rect_fill(c, 8, y0, 90, y0 + 20, EUI_MODE_SET);
        eui_draw_rect_stroke(c, 100, y0, 170, y0 + 20, EUI_MODE_SET);
        snprintf(buf, sizeof(buf), "%d", i + 1);
        eui_draw_text(c, 186, y0 - 4, &ESGUI_DEFAULT_FONT, buf, EUI_MODE_SET);
    }
}

static void pat_align(Canvas *c)
{
    int w = c->width;
    int h = c->height;

    /* 1px 边框 + 四角 8x8 实心块：判断画面有没有行列偏移、有没有被面板圆角切到 */
    eui_draw_rect_stroke(c, 0, 0, w - 1, h - 1, EUI_MODE_SET);
    eui_draw_rect_fill(c, 0, 0, 7, 7, EUI_MODE_SET);
    eui_draw_rect_fill(c, w - 8, 0, w - 1, 7, EUI_MODE_SET);
    eui_draw_rect_fill(c, 0, h - 8, 7, h - 1, EUI_MODE_SET);
    eui_draw_rect_fill(c, w - 8, h - 8, w - 1, h - 1, EUI_MODE_SET);

    /* 中心十字 */
    eui_draw_hline(c, w / 2 - 20, w / 2 + 20, h / 2, EUI_MODE_SET);
    eui_draw_vline(c, w / 2, h / 2 - 20, h / 2 + 20, EUI_MODE_SET);

    /* 每 40px 一层网格 */
    for (int x = 40; x < w; x += 40) eui_draw_vline(c, x, 40, h - 40, EUI_MODE_SET);
    for (int y = 40; y < h; y += 40) eui_draw_hline(c, 0, w - 1, y, EUI_MODE_SET);

    /* 每 2px 一条 1px 竖线：能看出来就说明"列没丢、没被缩放" */
    for (int x = 0; x < w; x += 2) eui_draw_vline(c, x, 188, 202, EUI_MODE_SET);
    draw_label(c, 8, 204, "1px 竖条");
}

static void pat_dither(Canvas *c)
{
    int w = c->width;

    /* 5 档"灰阶"：每个 8px 单元里点亮的竖条数从 1 到 5 */
    for (int k = 1; k <= 5; k++) {
        int y0 = TOP + (k - 1) * 30;
        for (int x = 8; x < w - 44; x += 8) {
            for (int i = 0; i < k; i++) {
                eui_draw_vline(c, x + i, y0, y0 + 24, EUI_MODE_SET);
            }
        }
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", k);
        eui_draw_text(c, w - 30, y0, &ESGUI_DEFAULT_FONT, buf, EUI_MODE_SET);
    }

    /* 棋盘格：1px / 2px / 4px 三种格子（看摩尔纹与像素对齐） */
    int by = TOP + 148;
    for (int y = 0; y < 26; y++) {
        for (int x = 0; x < 48; x++) {
            if (((x >> 0) + (y >> 0)) & 1) eui_draw_pixel(c, 8 + x, by + y, EUI_MODE_SET);
        }
    }
    for (int y = 0; y < 26; y++) {
        for (int x = 0; x < 48; x++) {
            if (((x >> 1) + (y >> 1)) & 1) eui_draw_pixel(c, 68 + x, by + y, EUI_MODE_SET);
        }
    }
    for (int y = 0; y < 26; y++) {
        for (int x = 0; x < 48; x++) {
            if (((x >> 2) + (y >> 2)) & 1) eui_draw_pixel(c, 128 + x, by + y, EUI_MODE_SET);
        }
    }
    draw_label(c, 8, by + 28, "棋盘格");
}

static void pat_direct(Canvas *c)
{
    /* 真正的显示自检在 on_input 里执行（直写屏、阻塞 4~5 秒）；
     * 这里只解释一下，避免用户以为卡住了。 */
    const char *lines[] = {
        "进入本图案会",
        "直写屏自检:",
        "1 单色轮播",
        "2 RGB 竖条",
        "3 三色渐变",
        "4 边框/棋盘",
    };
    for (int i = 0; i < 6; i++) {
        eui_draw_text(c, 8, TOP + i * LH, &ESGUI_DEFAULT_FONT, lines[i], EUI_MODE_SET);
    }
}

static void pat_mask(Canvas *c)
{
    /* 满屏内容 + 逐级过渡遮罩：看到的是交错扫描线动画（页面切换的退出效果） */
    for (int y = TOP; y < c->height - 40; y += 6) {
        eui_draw_hline(c, 8, c->width - 12, y, EUI_MODE_SET);
    }
    eui_draw_rect_fill(c, 40, TOP + 30, 160, TOP + 120, EUI_MODE_SET);

    eui_uint8_t lvl = (eui_uint8_t)((anim_get_tick() / 5) % 9);
    canvas_apply_transition_mask(c, lvl);

    char buf[24];
    snprintf(buf, sizeof(buf), "遮罩级别 %u/8", (unsigned)lvl);
    draw_label(c, 8, 200, buf);
}

/* ==================== 页面虚函数实现 ==================== */

static void draw_page_on_create(ESGUI_MenuPage_T *page)
{
    (void)page;
    draw_start_pulse();                         /* 常驻脉冲动画：每 Tick 重绘本页 */
}

static void draw_page_on_destroy(ESGUI_MenuPage_T *page)
{
    (void)page;
    anim_stop_all(&draw_pulse);
    g_pal_cb = NULL;                            /* ★ 一定要还原：否则后续页面全被染色 */
}

static void draw_run_direct_selftest(void)
{
    int n = tft_drv_selftest_count();
    for (int i = 0; i < n; i++) {
        tft_drv_selftest_show(i);                /* 每个图案内部自带延时（约 1 秒） */
    }
}

static void draw_page_on_draw(ESGUI_MenuPage_T *page)
{
    Canvas *c = draw_canvas(page);
    if (c == ESGUI_NULL) return;

    /* 调色板图案：整屏按行换色（送屏时 tft_drv 会按行调用本回调）。
     * 每帧都显式赋值，保证"离开本图案"后立刻恢复黑底白字。 */
    g_pal_cb = (draw_pattern == PAT_PALETTE) ? draw_pal_bands_cb : NULL;

    draw_frame(page, c, "轻点换 长按退");

    switch (draw_pattern) {
        case PAT_BASIC:    pat_basic(c);    break;
        case PAT_RECT:     pat_rect(c);     break;
        case PAT_CIRCLE:   pat_circle(c);   break;
        case PAT_TRIANGLE: pat_triangle(c); break;
        case PAT_ROUND:    pat_round(c);    break;
        case PAT_BITMAP:   pat_bitmap(c);   break;
        case PAT_WIDGET:   pat_widget(c);   break;
        case PAT_FONT:     pat_font(c);     break;
        case PAT_PALETTE:  pat_palette(c);  break;
        case PAT_ALIGN:    pat_align(c);    break;
        case PAT_DITHER:   pat_dither(c);   break;
        case PAT_DIRECT:   pat_direct(c);   break;
        case PAT_MASK:     pat_mask(c);     break;
        default: break;
    }
}

static ESGUI_MenuAction_T draw_page_on_input(ESGUI_MenuPage_T *page, ESGUI_EventCode_t e)
{
    (void)page;
    switch (e) {
        case EVT_KEY_UP:
        case EVT_KEY_RIGHT:
        case EVT_CLICKED:
        case EVT_KEY_OK:
            draw_pattern = (uint8_t)((draw_pattern + 1) % PAT_NUM);
            break;

        case EVT_KEY_DOWN:
        case EVT_KEY_LEFT:
            draw_pattern = (uint8_t)((draw_pattern + PAT_NUM - 1) % PAT_NUM);
            break;

        case EVT_KEY_BACK:
            g_pal_cb = NULL;
            return (ESGUI_MenuAction_T){ACT_POP_PAGE, ESGUI_NULL};

        default:
            return (ESGUI_MenuAction_T){ACT_NONE, ESGUI_NULL};
    }

    /* 切到"直写彩条"时立刻跑一次显示自检（阻塞几秒，结束后重绘本页） */
    if (draw_pattern == PAT_DIRECT) {
        draw_run_direct_selftest();
    }
    return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
}

static const esgui_page_vtable_t draw_page_vtable = {
    .on_create   = draw_page_on_create,
    .on_destroy  = draw_page_on_destroy,
    .on_draw     = draw_page_on_draw,
    .on_input    = draw_page_on_input,
    .on_relayout = ESGUI_NULL,
};

ESGUI_MenuAction_T test_draw_page_create(void)
{
    /* GIF 描述符（本页用它验证"同一份动图可在多处绘制"） */
    ESGUI_GIFInit(&draw_gif, tst_gif_frames, TST_GIF_FRAME_COUNT, tst_gif_delays, 0, 0);

    memset(&draw_page, 0, sizeof(draw_page));
    draw_page.title    = "绘图与显示";
    draw_page.vtbl     = &draw_page_vtable;
    draw_page.item_num = 0;
    draw_page.items    = ESGUI_NULL;

    draw_pattern = PAT_BASIC;
    g_pal_cb = NULL;

    return (ESGUI_MenuAction_T){ACT_PUSH_PAGE, &draw_page};
}
