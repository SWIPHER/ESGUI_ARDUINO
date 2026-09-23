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

/* ==================== 小工具 ====================
 * 逻辑屏 112x128：标题占第 0 行、正文 18~110、底部提示固定在最下面一行。
 */
#define DRAW_TOP        18                      /* 正文起始 y */
#define DRAW_BOTTOM     (ESGUI_DEFAULT_FONT.line_height)

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

/* ==================== 各个图案（坐标全部按逻辑屏 112x128 走） ==================== */

static void pat_basic(Canvas *c)
{
    int w = c->width;
    eui_draw_hline(c, 4, w - 5, DRAW_TOP, EUI_MODE_SET);               /* 横线 */
    eui_draw_vline(c, 6, DRAW_TOP + 6, 68, EUI_MODE_SET);              /* 竖线 */
    eui_draw_line(c, 12, DRAW_TOP + 4, w - 6, 68, EUI_MODE_SET);       /* 斜线 */

    /* XOR 画两遍 = 抵消（第二遍把第一遍擦掉，屏幕上看不到） */
    eui_draw_line(c, 12, 76, w - 6, 76, EUI_MODE_XOR);
    eui_draw_line(c, 12, 76, w - 6, 76, EUI_MODE_XOR);
    /* XOR 画一遍 = 反色线 */
    eui_draw_line(c, 12, 84, w - 6, 84, EUI_MODE_XOR);

    /* CLEAR：在实心块上"擦"出一条线 */
    eui_draw_rect_fill(c, 6, 92, w - 7, 108, EUI_MODE_SET);
    eui_draw_hline(c, 6, w - 7, 100, EUI_MODE_CLER);

    /* 点阵 */
    for (int i = 0; i < 14; i++) {
        eui_draw_pixel(c, 6 + i * 7, 112, EUI_MODE_SET);
    }
}

static void pat_rect(Canvas *c)
{
    int w = c->width;
    eui_draw_rect_fill(c, 4, DRAW_TOP, 32, DRAW_TOP + 22, EUI_MODE_SET);
    draw_label(c, 8, DRAW_TOP + 24, "填充");
    eui_draw_rect_stroke(c, 40, DRAW_TOP, 68, DRAW_TOP + 22, EUI_MODE_SET);
    draw_label(c, 42, DRAW_TOP + 24, "描边");
    eui_draw_rect_box(c, 76, DRAW_TOP, w - 5, DRAW_TOP + 22, EUI_MODE_SET);
    draw_label(c, 82, DRAW_TOP + 24, "框");

    /* XOR 叠加：重叠部分反色 */
    eui_draw_rect_fill(c, 4, 62, 60, 88, EUI_MODE_SET);
    eui_draw_rect_fill(c, 30, 72, 90, 98, EUI_MODE_XOR);
    draw_label(c, 4, 100, "XOR叠加 反色");

    /* CLEAR：从实心块里抠一个矩形 */
    eui_draw_rect_fill(c, 76, 62, w - 5, 108, EUI_MODE_SET);
    eui_draw_rect_stroke(c, 84, 72, w - 13, 98, EUI_MODE_CLER);
}

static void pat_circle(Canvas *c)
{
    eui_draw_circle_fill(c, 18, DRAW_TOP + 14, 11, EUI_MODE_SET);
    draw_label(c, 6, DRAW_TOP + 26, "填充");
    eui_draw_circle_stroke(c, 56, DRAW_TOP + 14, 11, EUI_MODE_SET);
    draw_label(c, 44, DRAW_TOP + 26, "描边");
    eui_draw_circle_box(c, 94, DRAW_TOP + 14, 10, EUI_MODE_SET);
    draw_label(c, 84, DRAW_TOP + 26, "框");

    /* 两个 XOR 圆环相交：交集处反色 */
    eui_draw_circle_stroke(c, 36, 82, 18, EUI_MODE_XOR);
    eui_draw_circle_stroke(c, 64, 82, 18, EUI_MODE_XOR);

    /* 同心圆 */
    for (int r = 3; r <= 15; r += 4) {
        eui_draw_circle_stroke(c, 96, 86, r, EUI_MODE_SET);
    }
}

static void pat_triangle(Canvas *c)
{
    int w = c->width;
    eui_draw_triangle_fill(c, 6, DRAW_TOP + 24, 22, DRAW_TOP, 38, DRAW_TOP + 24, EUI_MODE_SET);
    draw_label(c, 10, DRAW_TOP + 26, "填充");
    eui_draw_triangle_stroke(c, 46, DRAW_TOP + 24, 62, DRAW_TOP, 78, DRAW_TOP + 24, EUI_MODE_SET);
    draw_label(c, 50, DRAW_TOP + 26, "描边");
    eui_draw_triangle_box(c, 86, DRAW_TOP + 24, 100, DRAW_TOP, w - 5, DRAW_TOP + 24, EUI_MODE_SET);
    draw_label(c, 88, DRAW_TOP + 26, "框");

    /* XOR 三角形叠在一起 */
    eui_draw_triangle_fill(c, 10, 104, 46, 58, 82, 104, EUI_MODE_XOR);
    eui_draw_triangle_fill(c, 44, 104, 80, 58, w - 6, 104, EUI_MODE_SET);
}

static void pat_round(Canvas *c)
{
    int w = c->width;
    eui_draw_round_rect_fill(c, 4, DRAW_TOP, 52, DRAW_TOP + 26, 6, EUI_MODE_SET);
    draw_label(c, 6, DRAW_TOP + 28, "圆角填充r6");
    eui_draw_round_rect_stroke(c, 60, DRAW_TOP, w - 5, DRAW_TOP + 26, 6, EUI_MODE_SET);
    draw_label(c, 62, DRAW_TOP + 28, "描边r6");

    eui_draw_round_rect_box(c, 4, 64, 52, 92, 10, EUI_MODE_SET);
    draw_label(c, 6, 94, "框r10");
    eui_draw_round_rect_fill(c, 60, 64, w - 5, 92, 2, EUI_MODE_SET);
    draw_label(c, 62, 94, "小圆角r2");

    /* XOR 叠加：验证圆角处也走同一条绘制路径 */
    eui_draw_round_rect_fill(c, 16, 100, 72, 116, 8, EUI_MODE_SET);
    eui_draw_round_rect_fill(c, 44, 100, w - 6, 116, 8, EUI_MODE_XOR);
}

static void pat_bitmap(Canvas *c)
{
    int w = c->width;
    /* 普通绘制：位图不透明，直接覆盖 */
    eui_draw_bitmap(c, 4, DRAW_TOP, &tst_icon_settings, 1);
    /* 反色绘制 */
    eui_draw_bitmap_invert(c, 40, DRAW_TOP, &tst_icon_music);
    /* 透明：先铺实心块，再用 transparent 只画"1 像素"（0 像素透明）→ 挖空效果 */
    eui_draw_rect_fill(c, 76, DRAW_TOP, 76 + 31, DRAW_TOP + 31, EUI_MODE_SET);
    eui_draw_bitmap_transparent(c, 76, DRAW_TOP, &tst_icon_heart, 0, 0);
    draw_label(c, 4, DRAW_TOP + 33, "普通 反色 挖空");

    /* 缩略图（24x24，图片列表弹窗里用的那套） */
    eui_draw_bitmap(c, 4, 60, &tst_small_star, 1);
    eui_draw_bitmap(c, 32, 60, &tst_small_wifi, 1);
    eui_draw_bitmap(c, 60, 60, &tst_small_folder, 1);
    /* 透明（只画 0 像素：把形状的"负空间"填实） */
    eui_draw_rect_fill(c, 88, 60, 88 + 23, 60 + 23, EUI_MODE_SET);
    eui_draw_bitmap_transparent(c, 88, 60, &tst_small_heart, 0, 1);

    /* GIF 动图：本页有脉冲动画 → 每 tick 重绘，帧才会推进 */
    ESGUI_GIFDraw(c, &draw_gif, 4, 90, EUI_MODE_SET, anim_get_tick(),
                  ESGUI_GIF_PLAY_LOOP);
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "GIF %u/%u",
                 (unsigned)(draw_gif.frame + 1), (unsigned)draw_gif.frame_count);
        draw_label(c, 40, 96, buf);
    }
    /* 位图裁剪：故意画到屏幕外，验证越界不会画花 */
    eui_draw_bitmap(c, -14, 90, &tst_icon_star, 1);
    eui_draw_bitmap(c, w - 18, 90, &tst_icon_star, 1);
}

static void pat_widget(Canvas *c)
{
    int w = c->width;
    (void)w;

    /* 可变长度横向进度条：右 / 左 两种方向 */
    ESGUI_WidgetProgrssBarChangeLenPermille(c, 4, DRAW_TOP, 6, 80, 250,
                                            ESGUI_WIDGET_PROGBAR_RIGHT);
    ESGUI_WidgetProgrssBarChangeLenPermille(c, 4, DRAW_TOP + 12, 6, 80, 700,
                                            ESGUI_WIDGET_PROGBAR_LEFT);
    ESGUI_WidgetProgrssBarChangeLenPermille(c, 4, DRAW_TOP + 24, 6, 80, 1000,
                                            ESGUI_WIDGET_PROGBAR_RIGHT);
    draw_label(c, 88, DRAW_TOP + 4, "250");
    draw_label(c, 88, DRAW_TOP + 16, "700");
    draw_label(c, 88, DRAW_TOP + 28, "1000");

    /* 复选框：方形（未选/选中）、圆形（未选/选中） */
    ESGUI_WidgetCheckBoxSquare(c, 6, 62, 14, 14, false);
    ESGUI_WidgetCheckBoxSquare(c, 28, 62, 14, 14, true);
    ESGUI_WidgetCheckBoxRound(c, 60, 69, 7, false);
    ESGUI_WidgetCheckBoxRound(c, 82, 69, 7, true);
    draw_label(c, 4, 78, "复选框 方/圆");

    /* 焦点框：位图焦点框（四角）、动画焦点框（可生长）、文本焦点框（XOR 圆角） */
    ESGUI_WidgetBmpFocusBox(c, 4, 82, 26, 12);
    ESGUI_WidgetBmpFocusBoxAnim(c, 36, 82, 40, 12);
    draw_label(c, 80, 84, "焦点框");
    ESGUI_WidgetTextFocusBox(c, 4, 96, 14, 60);
    draw_label(c, 70, 96, "文本");
}

static void pat_font(Canvas *c)
{
    /* ASCII 表：每行 14 个字符（字宽约 8px），6 行画完 0x20~0x73
     * （逻辑屏 112x128，正文区高 92px，正好 6 行） */
    char line[16];
    int n = 0;
    int row = 0;
    const int lh = ESGUI_DEFAULT_FONT.line_height;
    for (int ch = 0x20; ch <= 0x73; ch++) {
        line[n++] = (char)ch;
        if (n == 14) {
            line[n] = '\0';
            eui_draw_text(c, 2, DRAW_TOP + row * lh, &ESGUI_DEFAULT_FONT, line,
                          EUI_MODE_SET);
            n = 0;
            row++;
        }
    }
    if (n > 0) {
        line[n] = '\0';
        eui_draw_text(c, 2, DRAW_TOP + row * lh, &ESGUI_DEFAULT_FONT, line, EUI_MODE_SET);
    }
}

static void pat_palette(Canvas *c)
{
    /* 本图案在 on_draw 里把 g_pal_cb 指向彩条回调 → 整屏按行换色（8 条色带） */
    char buf[8];
    for (int i = 0; i < 8; i++) {
        int y0 = DRAW_TOP + i * 11;
        eui_draw_rect_fill(c, 3, y0, 50, y0 + 8, EUI_MODE_SET);
        eui_draw_rect_stroke(c, 56, y0, 90, y0 + 8, EUI_MODE_SET);
        snprintf(buf, sizeof(buf), "%d", i + 1);
        eui_draw_text(c, 98, y0, &ESGUI_DEFAULT_FONT, buf, EUI_MODE_SET);
    }
}

static void pat_align(Canvas *c)
{
    int w = c->width;
    int h = c->height;

    /* 1px 边框 + 四角 4x4 实心块：判断画面有没有行列偏移、有没有被面板圆角切到 */
    eui_draw_rect_stroke(c, 0, 0, w - 1, h - 1, EUI_MODE_SET);
    eui_draw_rect_fill(c, 0, 0, 3, 3, EUI_MODE_SET);
    eui_draw_rect_fill(c, w - 4, 0, w - 1, 3, EUI_MODE_SET);
    eui_draw_rect_fill(c, 0, h - 4, 3, h - 1, EUI_MODE_SET);
    eui_draw_rect_fill(c, w - 4, h - 4, w - 1, h - 1, EUI_MODE_SET);

    /* 中心十字 */
    eui_draw_hline(c, w / 2 - 10, w / 2 + 10, 64, EUI_MODE_SET);
    eui_draw_vline(c, w / 2, 54, 74, EUI_MODE_SET);

    /* 每 20px 一层网格 */
    for (int x = 20; x < w; x += 20) eui_draw_vline(c, x, 20, 96, EUI_MODE_SET);
    for (int y = 20; y <= 96; y += 20) eui_draw_hline(c, 0, w - 1, y, EUI_MODE_SET);

    /* 每 2px 一条 1px 竖线：能看出来就说明"列没丢、没被缩放" */
    for (int x = 0; x < w; x += 2) eui_draw_vline(c, x, 100, 108, EUI_MODE_SET);
}

static void pat_dither(Canvas *c)
{
    int w = c->width;

    /* 5 档"灰阶"：每个 8px 单元里点亮的竖条数从 1 到 5 */
    for (int k = 1; k <= 5; k++) {
        int y0 = DRAW_TOP + (k - 1) * 13;
        for (int x = 2; x < w - 24; x += 8) {
            for (int i = 0; i < k; i++) {
                eui_draw_vline(c, x + i, y0, y0 + 10, EUI_MODE_SET);
            }
        }
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", k);
        eui_draw_text(c, w - 16, y0, &ESGUI_DEFAULT_FONT, buf, EUI_MODE_SET);
    }

    /* 棋盘格：1px / 2px / 4px 三种格子（看摩尔纹与像素对齐） */
    int by = 88;
    for (int y = 0; y < 20; y++) {
        for (int x = 0; x < 32; x++) {
            if (((x >> 0) + (y >> 0)) & 1) eui_draw_pixel(c, 2 + x, by + y, EUI_MODE_SET);
        }
    }
    for (int y = 0; y < 20; y++) {
        for (int x = 0; x < 32; x++) {
            if (((x >> 1) + (y >> 1)) & 1) eui_draw_pixel(c, 40 + x, by + y, EUI_MODE_SET);
        }
    }
    for (int y = 0; y < 20; y++) {
        for (int x = 0; x < 32; x++) {
            if (((x >> 2) + (y >> 2)) & 1) eui_draw_pixel(c, 78 + x, by + y, EUI_MODE_SET);
        }
    }
    draw_label(c, 4, by + 22, "棋盘格 1/2/4px");
}

static void pat_direct(Canvas *c)
{
    /* 真正的显示自检在 on_input 里执行（直写屏、阻塞 4~5 秒）；
     * 这里只解释一下，避免用户以为卡住了。 */
    const int lh = ESGUI_DEFAULT_FONT.line_height;
    eui_draw_text(c, 3, DRAW_TOP,        &ESGUI_DEFAULT_FONT, "进入本图案会", EUI_MODE_SET);
    eui_draw_text(c, 3, DRAW_TOP + lh,   &ESGUI_DEFAULT_FONT, "自动直写屏自检:", EUI_MODE_SET);
    eui_draw_text(c, 3, DRAW_TOP + 2 * lh, &ESGUI_DEFAULT_FONT, "1 单色轮播", EUI_MODE_SET);
    eui_draw_text(c, 3, DRAW_TOP + 3 * lh, &ESGUI_DEFAULT_FONT, "2 RGB 竖条", EUI_MODE_SET);
    eui_draw_text(c, 3, DRAW_TOP + 4 * lh, &ESGUI_DEFAULT_FONT, "3 三色渐变", EUI_MODE_SET);
    eui_draw_text(c, 3, DRAW_TOP + 5 * lh, &ESGUI_DEFAULT_FONT, "4 边框+棋盘", EUI_MODE_SET);
}

static void pat_mask(Canvas *c)
{
    /* 满屏内容 + 逐级过渡遮罩：看到的是交错扫描线动画（页面切换的退出效果） */
    for (int y = DRAW_TOP; y < c->height - 20; y += 4) {
        eui_draw_hline(c, 2, c->width - 6, y, EUI_MODE_SET);
    }
    eui_draw_rect_fill(c, 30, 60, 80, 96, EUI_MODE_SET);

    eui_uint8_t lvl = (eui_uint8_t)((anim_get_tick() / 5) % 9);
    canvas_apply_transition_mask(c, lvl);

    char buf[24];
    snprintf(buf, sizeof(buf), "遮罩级别 %u/8", (unsigned)lvl);
    draw_label(c, 3, 96, buf);
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
