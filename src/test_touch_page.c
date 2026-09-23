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
 * 逻辑屏 112x128：顶部 6 行文字信息（每行 ≈8 汉字），下面留给网格/十字准星/轨迹。
 */
static void touch_page_on_draw(ESGUI_MenuPage_T *page)
{
    if (page == ESGUI_NULL || page->render_ctx == ESGUI_NULL) return;

    CanvasStripIter *c_it = (CanvasStripIter *)page->render_ctx;
    Canvas *c = c_it->canvas;
    int w = c->width;
    int h = c->height;
    const int lh = ESGUI_DEFAULT_FONT.line_height;

    /* 1) 40px 网格（判断坐标是否线性、有没有翻转/偏移） */
    for (int x = 40; x < w; x += 40) {
        eui_draw_vline(c, x, 0, h - 1, EUI_MODE_SET);
    }
    for (int y = 40; y < h; y += 40) {
        eui_draw_hline(c, 0, w - 1, y, EUI_MODE_SET);
    }

    /* 2) 采样与事件信息（先清一块底，避免和网格叠在一起看不清）
     *    逻辑屏 112x128 → 7 行文字正好铺满上半屏（另一行留给底部提示） */
    char line[32];
    uint32_t ok = 0, err = 0;
    touch_input_get_stats(&ok, &err);
    touch_last_valid = touch_input_get_last(&touch_last);

    int box_h = 7 * lh + 2;
    eui_draw_rect_fill(c, 1, 1, w - 2, box_h, EUI_MODE_CLER);
    eui_draw_rect_stroke(c, 1, 1, w - 2, box_h, EUI_MODE_SET);

    eui_draw_text(c, 3, 0 * lh, &ESGUI_DEFAULT_FONT, "触摸测试", EUI_MODE_SET);

    if (touch_last_valid) {
        snprintf(line, sizeof(line), "X:%3u Y:%3u %u点", (unsigned)touch_last.x,
                 (unsigned)touch_last.y, (unsigned)touch_last.fingers);
    } else {
        snprintf(line, sizeof(line), "X:--- Y:--- 等待");
    }
    eui_draw_text(c, 3, 1 * lh, &ESGUI_DEFAULT_FONT, line, EUI_MODE_SET);

    snprintf(line, sizeof(line), "手势 %02X %s",
             touch_last_valid ? (unsigned)touch_last.gesture : 0u,
             touch_last_valid ? touch_gesture_name(touch_last.gesture) : "");
    eui_draw_text(c, 3, 2 * lh, &ESGUI_DEFAULT_FONT, line, EUI_MODE_SET);

    snprintf(line, sizeof(line), "点%lu 上%lu", (unsigned long)cnt_click,
             (unsigned long)cnt_up);
    eui_draw_text(c, 3, 3 * lh, &ESGUI_DEFAULT_FONT, line, EUI_MODE_SET);

    snprintf(line, sizeof(line), "下%lu 左%lu", (unsigned long)cnt_down,
             (unsigned long)cnt_left);
    eui_draw_text(c, 3, 4 * lh, &ESGUI_DEFAULT_FONT, line, EUI_MODE_SET);

    snprintf(line, sizeof(line), "右%lu 长%lu", (unsigned long)cnt_right,
             (unsigned long)cnt_back);
    eui_draw_text(c, 3, 5 * lh, &ESGUI_DEFAULT_FONT, line, EUI_MODE_SET);

    snprintf(line, sizeof(line), "采样%lu 失败%lu", (unsigned long)ok, (unsigned long)err);
    eui_draw_text(c, 3, 6 * lh, &ESGUI_DEFAULT_FONT, line, EUI_MODE_SET);

    /* 3) 轨迹点：最近 40 个不同位置的采样点（用反色小方块，压在网格上也看得清） */
    if (touch_last_valid && touch_last.fingers > 0) {
        if (touch_last.x != touch_prev_x || touch_last.y != touch_prev_y) {
            if (trail_n < TOUCH_TRAIL_MAX) {
                trail_x[trail_n] = touch_last.x;
                trail_y[trail_n] = touch_last.y;
                trail_n++;
            } else {
                memmove(trail_x, trail_x + 1, sizeof(trail_x) - sizeof(trail_x[0]));
                memmove(trail_y, trail_y + 1, sizeof(trail_y) - sizeof(trail_y[0]));
                trail_x[TOUCH_TRAIL_MAX - 1] = touch_last.x;
                trail_y[TOUCH_TRAIL_MAX - 1] = touch_last.y;
            }
            touch_prev_x = touch_last.x;
            touch_prev_y = touch_last.y;
        }
    }
    for (uint8_t i = 0; i < trail_n; i++) {
        eui_draw_rect_fill(c, trail_x[i] - 1, trail_y[i] - 1,
                           trail_x[i] + 1, trail_y[i] + 1, EUI_MODE_XOR);
    }

    /* 4) 十字准星（反色，十字线压在任何内容上都看得见） */
    if (touch_last_valid && touch_last.fingers > 0) {
        eui_draw_hline(c, 0, w - 1, touch_last.y, EUI_MODE_XOR);
        eui_draw_vline(c, touch_last.x, 0, h - 1, EUI_MODE_XOR);
        eui_draw_circle_stroke(c, touch_last.x, touch_last.y, 6, EUI_MODE_XOR);
    }

    /* 5) 底部提示 */
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
