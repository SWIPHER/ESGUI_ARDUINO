/*
 * test_touch_page.c —— 触摸测试页（CST816D + 触摸→事件翻译）
 *
 * 页面做三件事：
 *   ① 实时显示 CST816D 的原始采样：坐标 X/Y、触点数、手势码、I2C 采样成功率
 *   ② 统计"触摸 → ESGUI 事件"的翻译结果：轻点、上下左右滑动、长按返回各有几次
 *   ③ 画 40px 网格 + 十字准星 + 最近轨迹点，用眼睛校验坐标是否准、有没有偏移
 *
 * 交互：轻点 = 清空计数；滑动 = 计数 +1（同时验证滑动分格）；长按 = 返回首页。
 *
 * ★ 为什么能实时刷新：on_create 起了一个无限脉冲动画，
 *   框架看到 anim_running 就会每个 Tick 重绘一帧（否则静态页面不会重绘）。
 */
#include "test_touch_page.h"

#include <stdio.h>
#include <string.h>

#include "ESGUI.h"
#include "ESGUI_Anim.h"
#include "ESGUI_BSP_Canvas.h"       /* Canvas / CanvasStripIter */
#include "ESGUI_BSP_Text.h"
#include "ESGUI_BSP_draw.h"         /* eui_draw_* / EUI_MODE_* */
#include "ESGUI_PageDefaltVtbl.h"   /* ESGUI_DEFAULT_FONT */
#include "tft_drv.h"                /* tft_panel2logic_* / TFT_SCREEN_*（坐标换算） */
#include "touch_cst816d.h"
#include "touch_input.h"

#define TOUCH_TRAIL_MAX  40         /* 轨迹点数量 */

static ESGUI_MenuPage_T touch_page;
static eui_uint16_t     touch_pulse;

/* 事件计数（触摸 → 事件翻译结果的"体检报告"） */
static uint32_t cnt_click, cnt_up, cnt_down, cnt_left, cnt_right, cnt_back, cnt_ok;

/* 轨迹与最近触点 */
static uint16_t      trail_x[TOUCH_TRAIL_MAX];
static uint16_t      trail_y[TOUCH_TRAIL_MAX];
static uint8_t       trail_n;
static touch_state_t touch_last;
static bool          touch_last_valid;
static uint16_t      touch_prev_x, touch_prev_y;

/* ==================== 动画回调 ==================== */
static void touch_anim_cb(void *var, eui_int32_t value)
{
    if (var != ESGUI_NULL) {
        *(eui_uint16_t *)var = (eui_uint16_t)value;
    }
}

static void touch_start_pulse(void)
{
    if (anim_is_running_var(&touch_pulse)) return;
    anim_t a = {0};
    a.var          = &touch_pulse;
    a.exec_cb      = touch_anim_cb;
    a.start        = 0;
    a.end          = 0;
    a.duration     = 100;
    a.path_type    = ANIM_PATH_LINEAR;
    a.repeat_cnt   = 0xFFFF;
    a.repeat_total = 0xFFFF;
    anim_start(&a);
}

/* ==================== 手势码 → 文字 ==================== */
static const char *touch_gesture_name(uint8_t g)
{
    switch (g) {
        case TOUCH_GESTURE_SLIDE_UP:     return "上滑";
        case TOUCH_GESTURE_SLIDE_DOWN:   return "下滑";
        case TOUCH_GESTURE_SLIDE_LEFT:   return "左滑";
        case TOUCH_GESTURE_SLIDE_RIGHT:  return "右滑";
        case TOUCH_GESTURE_CLICK:        return "单击";
        case TOUCH_GESTURE_LONG_PRESS:   return "长按";
        case TOUCH_GESTURE_DOUBLE_CLICK: return "双击";
        default:                         return "无";
    }
}

/* ==================== 绘制 ====================
 * ★ 关键：触摸芯片报的是**面板像素**（0..239 / 0..283），而这里是**逻辑画布**（216x272），
 *   驱动送屏时会做 panel = TFT_OFFSET_X/Y + logic*TFT_ZOOM 的映射。
 *   所以准星/轨迹必须先用 tft_panel2logic_*() 换算，否则会"偏移 + 放大 2 倍"对不上手指。
 *   页面同时显示两组读数：面板 X/Y（芯片原始值）与 逻辑 x/y（画布上看到的位置）。
 */
static void touch_page_on_draw(ESGUI_MenuPage_T *page)
{
    if (page == ESGUI_NULL || page->render_ctx == ESGUI_NULL) return;

    CanvasStripIter *c_it = (CanvasStripIter *)page->render_ctx;
    Canvas *c = c_it->canvas;
    int w = c->width;
    int h = c->height;
    const int lh = ESGUI_DEFAULT_FONT.line_height;

    /* 1) 网格：按**面板坐标**每 40px 一条（线的位置由换算得到）
     *    → 连线上的数字就是"手指所在面板坐标"，方便定量校验映射是否正确 */
    for (int p = 40; p < TFT_SCREEN_W; p += 40) {
        eui_draw_vline(c, tft_panel2logic_x(p), 0, h - 1, EUI_MODE_SET);
    }
    for (int p = 40; p < TFT_SCREEN_H; p += 40) {
        eui_draw_hline(c, 0, w - 1, tft_panel2logic_y(p), EUI_MODE_SET);
    }
    /* 逻辑区边框（触点换算后必须落在这个框里） */
    eui_draw_rect_stroke(c, 0, 0, w - 1, h - 1, EUI_MODE_SET);

    /* 2) 信息框（顶部 6 行；逻辑屏 216x272、行高 33） */
    char line[40];
    uint32_t ok = 0, err = 0;
    touch_input_get_stats(&ok, &err);
    touch_last_valid = touch_input_get_last(&touch_last);

    int box_h = 6 * lh + 2;
    eui_draw_rect_fill(c, 1, 1, w - 2, box_h, EUI_MODE_CLER);
    eui_draw_rect_stroke(c, 1, 1, w - 2, box_h, EUI_MODE_SET);

    eui_draw_text(c, 6, 0 * lh, &ESGUI_DEFAULT_FONT, "触摸测试", EUI_MODE_SET);

    /* 标题行右侧：采样成功数（I2C 通信质量） */
    snprintf(line, sizeof(line), "采%lu", (unsigned long)ok);
    eui_draw_text_clip(c, 160, 0 * lh, &ESGUI_DEFAULT_FONT, line, EUI_MODE_SET, 50);

    if (touch_last_valid) {
        snprintf(line, sizeof(line), "面板 %3u,%3u", (unsigned)touch_last.x,
                 (unsigned)touch_last.y);
    } else {
        snprintf(line, sizeof(line), "面板 ---,---");
    }
    eui_draw_text(c, 6, 1 * lh, &ESGUI_DEFAULT_FONT, line, EUI_MODE_SET);

    if (touch_last_valid) {
        snprintf(line, sizeof(line), "逻辑 %3d,%3d  %u点",
                 tft_panel2logic_x((int)touch_last.x),
                 tft_panel2logic_y((int)touch_last.y), (unsigned)touch_last.fingers);
    } else {
        snprintf(line, sizeof(line), "逻辑 ---,--- 等待");
    }
    eui_draw_text(c, 6, 2 * lh, &ESGUI_DEFAULT_FONT, line, EUI_MODE_SET);

    snprintf(line, sizeof(line), "手势 %02X %s",
             touch_last_valid ? (unsigned)touch_last.gesture : 0u,
             touch_last_valid ? touch_gesture_name(touch_last.gesture) : "");
    eui_draw_text(c, 6, 3 * lh, &ESGUI_DEFAULT_FONT, line, EUI_MODE_SET);

    snprintf(line, sizeof(line), "点%lu 上%lu 下%lu", (unsigned long)cnt_click,
             (unsigned long)cnt_up, (unsigned long)cnt_down);
    eui_draw_text(c, 6, 4 * lh, &ESGUI_DEFAULT_FONT, line, EUI_MODE_SET);

    snprintf(line, sizeof(line), "左%lu 右%lu 失%lu", (unsigned long)cnt_left,
             (unsigned long)cnt_right, (unsigned long)err);
    eui_draw_text(c, 6, 5 * lh, &ESGUI_DEFAULT_FONT, line, EUI_MODE_SET);

    /* 3) 竖线刻度（面板坐标，画在信息框下方） */
    for (int p = 40; p < TFT_SCREEN_W; p += 40) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", p);
        eui_draw_text_clip(c, tft_panel2logic_x(p) + 2, box_h + 4,
                           &ESGUI_DEFAULT_FONT, buf, EUI_MODE_SET, 44);
    }

    /* 4) 轨迹点：最近 40 个不同位置（逻辑坐标，反色小方块，压在网格上也看得清） */
    if (touch_last_valid && touch_last.fingers > 0) {
        int lx = tft_panel2logic_x((int)touch_last.x);
        int ly = tft_panel2logic_y((int)touch_last.y);
        if (lx != touch_prev_x || ly != touch_prev_y) {
            if (trail_n < TOUCH_TRAIL_MAX) {
                trail_x[trail_n] = (uint16_t)lx;
                trail_y[trail_n] = (uint16_t)ly;
                trail_n++;
            } else {
                memmove(trail_x, trail_x + 1, sizeof(trail_x) - sizeof(trail_x[0]));
                memmove(trail_y, trail_y + 1, sizeof(trail_y) - sizeof(trail_y[0]));
                trail_x[TOUCH_TRAIL_MAX - 1] = (uint16_t)lx;
                trail_y[TOUCH_TRAIL_MAX - 1] = (uint16_t)ly;
            }
            touch_prev_x = (uint16_t)lx;
            touch_prev_y = (uint16_t)ly;
        }
    }
    for (uint8_t i = 0; i < trail_n; i++) {
        eui_draw_rect_fill(c, trail_x[i] - 2, trail_y[i] - 2,
                           trail_x[i] + 2, trail_y[i] + 2, EUI_MODE_XOR);
    }

    /* 5) 十字准星 + 实心中心点（反色，压在文字/网格上都看得见）
     *    ★ 位置必须是"换算后的逻辑坐标"，与手指所指的屏幕位置一致 */
    if (touch_last_valid && touch_last.fingers > 0) {
        int lx = tft_panel2logic_x((int)touch_last.x);
        int ly = tft_panel2logic_y((int)touch_last.y);
        eui_draw_hline(c, 0, w - 1, ly, EUI_MODE_XOR);
        eui_draw_vline(c, lx, 0, h - 1, EUI_MODE_XOR);
        eui_draw_circle_stroke(c, lx, ly, 8, EUI_MODE_XOR);
        eui_draw_rect_fill(c, lx - 1, ly - 1, lx + 1, ly + 1, EUI_MODE_SET);
    }

    /* 6) 底部提示 */
    eui_draw_text(c, 3, (int)(h - lh), &ESGUI_DEFAULT_FONT, "长按返回", EUI_MODE_SET);
}

/* ==================== 输入 ==================== */
static ESGUI_MenuAction_T touch_page_on_input(ESGUI_MenuPage_T *page, ESGUI_EventCode_t e)
{
    (void)page;
    switch (e) {
        case EVT_CLICKED:
            /* 轻点：清空所有计数与轨迹（便于"一次手势测一项"） */
            cnt_click = cnt_up = cnt_down = cnt_left = cnt_right = cnt_ok = 0;
            cnt_back = 0;
            trail_n = 0;
            break;
        case EVT_KEY_UP:    cnt_up++;    break;
        case EVT_KEY_DOWN:  cnt_down++;  break;
        case EVT_KEY_LEFT:  cnt_left++;  break;
        case EVT_KEY_RIGHT: cnt_right++; break;
        case EVT_KEY_OK:    cnt_ok++;    break;
        case EVT_KEY_BACK:                             /* 长按：返回首页 */
            cnt_back++;
            return (ESGUI_MenuAction_T){ACT_POP_PAGE, ESGUI_NULL};
        default:
            return (ESGUI_MenuAction_T){ACT_NONE, ESGUI_NULL};
    }
    return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
}

/* ==================== 生命周期 ==================== */
static void touch_page_on_create(ESGUI_MenuPage_T *page)
{
    (void)page;
    cnt_click = cnt_up = cnt_down = cnt_left = cnt_right = cnt_back = cnt_ok = 0;
    trail_n = 0;
    touch_prev_x = 0xFFFF;
    touch_prev_y = 0xFFFF;
    (void)touch_input_get_last(&touch_last);
    touch_start_pulse();
}

static void touch_page_on_destroy(ESGUI_MenuPage_T *page)
{
    (void)page;
    anim_stop_all(&touch_pulse);
}

static const esgui_page_vtable_t touch_page_vtable = {
    .on_create   = touch_page_on_create,
    .on_destroy  = touch_page_on_destroy,
    .on_draw     = touch_page_on_draw,
    .on_input    = touch_page_on_input,
    .on_relayout = ESGUI_NULL,
};

ESGUI_MenuAction_T test_touch_page_create(void)
{
    memset(&touch_page, 0, sizeof(touch_page));
    touch_page.title    = "触摸测试";
    touch_page.vtbl     = &touch_page_vtable;
    touch_page.item_num = 0;
    touch_page.items    = ESGUI_NULL;
    return (ESGUI_MenuAction_T){ACT_PUSH_PAGE, &touch_page};
}
