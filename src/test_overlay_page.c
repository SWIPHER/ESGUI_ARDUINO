/*
 * test_overlay_page.c —— 覆盖层（Overlay）+ 动画曲线演示页
 *
 * 覆盖层是什么：常驻在"页面 + 弹窗"之上的独立绘制层（框架最后画它），
 * 适合做悬浮状态条、雷达扫描、水印、开机动画等。
 * 本例用一个覆盖层画"跑道 + 动画方块 + 曲线名"，并把
 * ESGUI_OverlayAdd / Remove / SetVisible、always_dirty 强制刷新、
 * anim_* 的 7 条内置缓动曲线（线性/缓入/缓出/缓入缓出/冲过/弹跳/步进）
 * 全部走一遍。
 *
 * ★ 注意（框架现状）：ESGUI_Tick 里注入覆盖层 render_ctx 的循环带了 break，
 *   只有 overlays[0] 会拿到画布；本例只用 1 个覆盖层，所以不受影响。
 */
#include "test_overlay_page.h"

#include <stdio.h>
#include <string.h>

#include "ESGUI.h"
#include "ESGUI_Anim.h"
#include "ESGUI_PageDefaltVtbl.h"
#include "ESGUI_Widget.h"
#include "esgui_port.h"
#include "tft_drv.h"            /* ESGUI_LOGIC_W（跑道长度跟着逻辑屏宽走） */

/* ==================== 缓动曲线表 ====================
 * 名称都用 2 个汉字：菜单条目右侧的"当前值"只有半屏宽可用（逻辑屏 216 宽）。 */
static const struct {
    const char      *name;
    anim_path_type_t type;
} s_curves[] = {
    {"线性", ANIM_PATH_LINEAR},
    {"缓入", ANIM_PATH_EASE_IN},
    {"缓出", ANIM_PATH_EASE_OUT},
    {"缓动", ANIM_PATH_EASE_IN_OUT},
    {"冲过", ANIM_PATH_OVERSHOOT},
    {"弹跳", ANIM_PATH_BOUNCE},
    {"步进", ANIM_PATH_STEP},
};
#define CURVE_NUM  (sizeof(s_curves) / sizeof(s_curves[0]))

/* ==================== 覆盖层状态 ==================== */
static ESGUI_Overlay_T   ov_layer;
static eui_int16_t       ov_box_x;          /* 被动画驱动的方块 X 偏移 */
static bool              ov_added;
static bool              ov_visible = true;
static uint8_t           ov_curve;
static bool              ov_playback;       /* true = 往返循环 */
static uint32_t          ov_frame;          /* 覆盖层自增帧计数（证明"每帧都在刷"） */

static char ov_state_buf[24] = "未添加";
static char ov_visible_buf[16] = "显示";
static char ov_curve_buf[24] = "线性";

static ESGUI_MenuPage_T  ov_page;
static ESGUI_PopWindow_T ov_msg_popup;

static ESGUI_T *ov_ui(void)
{
    return (ESGUI_T *)esgui_port_get_ui();
}

/* ==================== 动画回调 ==================== */
static void ov_anim_cb_i16(void *var, eui_int32_t value)
{
    if (var != ESGUI_NULL) {
        *(eui_int16_t *)var = (eui_int16_t)value;
    }
}

static void ov_anim_ready(anim_t *a)
{
    (void)a;
    /* 一趟跑完：非往返模式下把方块停在起点（便于重复观察） */
    if (!ov_playback) {
        ov_box_x = 0;
    }
}

/* 启动/重放方块位移动画 */
static void ov_play(void)
{
    anim_t a = {0};
    a.var          = &ov_box_x;
    a.exec_cb      = ov_anim_cb_i16;
    a.ready_cb     = ov_anim_ready;
    a.start        = 0;
    a.end          = ESGUI_LOGIC_W - 16;         /* 跑道长度（跟着逻辑屏宽走） */
    a.duration     = ov_playback ? 1200 : 900;
    a.path_type    = s_curves[ov_curve].type;
    if (ov_playback) {
        anim_set_repeat(&a, 0xFFFF, true);      /* 无限往返：去+回算一趟 */
    }
    anim_start(&a);
}

/* ==================== 覆盖层绘制 ====================
 * 逻辑屏 216x272、行高 33：把"跑道 + 方块 + 曲线名"画在屏幕底部，
 * 先清一条底色带，避免和菜单文字混在一起。
 */
static void ov_on_draw(ESGUI_Overlay_T *ov)
{
    if (ov == ESGUI_NULL || ov->render_ctx == ESGUI_NULL) return;
    CanvasStripIter *c_it = (CanvasStripIter *)ov->render_ctx;
    Canvas *c = c_it->canvas;

    int w = c->width;
    int h = c->height;
    const int lh = ESGUI_DEFAULT_FONT.line_height;
    int track_len = w - 32;                     /* 跑道长度 */
    int y = h - 40;                             /* 方块所在行 */
    int x = ov_box_x;
    if (x > track_len) x = track_len;
    if (x < 0) x = 0;

    /* 底色带（清掉菜单文字，覆盖层信息才看得清）：文字行 + 跑道行 */
    eui_draw_rect_fill(c, 0, y - lh - 6, w - 1, h - 1, EUI_MODE_CLER);

    /* 跑道底座 + 起点/终点刻度 */
    eui_draw_hline(c, 16, 16 + track_len, y + 22, EUI_MODE_SET);
    eui_draw_vline(c, 16, y + 16, y + 28, EUI_MODE_SET);
    eui_draw_vline(c, 16 + track_len, y + 16, y + 28, EUI_MODE_SET);

    /* 正在移动的方块（压在什么内容上都看得清） */
    eui_draw_rect_stroke(c, 16 + x, y, 16 + x + 17, y + 17, EUI_MODE_SET);
    eui_draw_rect_fill(c, 16 + x + 6, y + 6, 16 + x + 11, y + 11, EUI_MODE_SET);

    /* 覆盖层信息：曲线名 + 已刷帧数（帧数一直涨就说明 always_dirty 生效了） */
    char buf[40];
    ov_frame++;
    snprintf(buf, sizeof(buf), "曲线%s %lu", s_curves[ov_curve].name,
             (unsigned long)ov_frame);
    eui_draw_text(c, 6, y - lh - 4, &ESGUI_DEFAULT_FONT, buf, EUI_MODE_SET);
}

/* ==================== 条目回调 ==================== */

static ESGUI_MenuAction_T ov_show_msg(const char *msg)
{
    ESGUI_DefaultMessagePopWindowCreate(&ov_msg_popup, msg, 216, 110, 1);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &ov_msg_popup};
}

static void ov_refresh_state(void)
{
    if (!ov_added) {
        snprintf(ov_state_buf, sizeof(ov_state_buf), "未加");
        snprintf(ov_visible_buf, sizeof(ov_visible_buf), "-");
    } else {
        snprintf(ov_state_buf, sizeof(ov_state_buf), "已加");
        snprintf(ov_visible_buf, sizeof(ov_visible_buf), ov_visible ? "显示" : "隐藏");
    }
    snprintf(ov_curve_buf, sizeof(ov_curve_buf), "%s", s_curves[ov_curve].name);
}

/* 添加 / 移除覆盖层 */
static ESGUI_MenuAction_T ov_enter_toggle_add(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    if (ov_added) {
        anim_stop_all(&ov_box_x);
        ESGUI_OverlayRemove(ov_ui(), &ov_layer);
        ov_added = false;
        ov_refresh_state();
        return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
    }
    ov_layer.on_draw      = ov_on_draw;
    ov_layer.user_data    = ESGUI_NULL;
    ov_layer.always_dirty = 1;                  /* 每帧强制重绘：覆盖层持续动 */
    if (!ESGUI_OverlayAdd(ov_ui(), &ov_layer)) {
        return ov_show_msg("覆盖层\n添加失败");
    }
    ov_added = true;
    ov_visible = true;
    ov_refresh_state();
    ov_play();
    return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
}

/* 显示 / 隐藏（不移除，只是不画） */
static ESGUI_MenuAction_T ov_enter_toggle_visible(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    if (!ov_added) {
        return ov_show_msg("先添加覆盖层");
    }
    ov_visible = !ov_visible;
    ESGUI_OverlaySetVisible(ov_ui(), &ov_layer, ov_visible);
    ov_refresh_state();
    return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
}

/* 切换缓动曲线（7 条内置曲线轮换，顺带演示"往返 + 无限循环"） */
static ESGUI_MenuAction_T ov_enter_curve(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    ov_curve = (uint8_t)((ov_curve + 1) % CURVE_NUM);
    if (ov_curve == 0) {
        ov_playback = !ov_playback;             /* 转完一圈切一次"往返/单程" */
    }
    ov_refresh_state();
    /* 到"步进"曲线时开往返：单程的 STEP 一下就跳完了，看不出效果 */
    if (s_curves[ov_curve].type == ANIM_PATH_STEP) {
        ov_playback = true;
    }
    if (ov_added) {
        anim_stop_all(&ov_box_x);
        ov_box_x = 0;
        ov_play();
    }
    return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
}

/* 重放动画 */
static ESGUI_MenuAction_T ov_enter_replay(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    if (!ov_added) {
        return ov_show_msg("先添加覆盖层");
    }
    anim_stop_all(&ov_box_x);
    ov_box_x = 0;
    ov_play();
    return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
}

static ESGUI_MenuAction_T ov_enter_back(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    return (ESGUI_MenuAction_T){ACT_POP_PAGE, ESGUI_NULL};
}

/* ==================== 右侧状态显示（特殊标记 \x03/0） ==================== */
static eui_uint16_t ov_page_special_draw(ESGUI_MenuPage_T *page, eui_uint16_t indx, bool measure)
{
    if (page == ESGUI_NULL || page->render_ctx == ESGUI_NULL) return 0;

    char marker = '\0';
    if (!ESGUI_WidgetCheckMarker(page->items[indx].label, ESGUI_WIDGET_DEFAULT_MARK,
                                 ESGUI_NULL, &marker) || marker != '0') {
        return 0;
    }
    if (page->items[indx].arg == ESGUI_NULL) return 0;

    CanvasStripIter *c_it = (CanvasStripIter *)page->render_ctx;
    eui_uint16_t x_right = (eui_uint16_t)(c_it->canvas->width - ESGUI_PROGRESS_BAR_W);
    const char *text = (const char *)page->items[indx].arg;
    eui_uint8_t w = (eui_uint8_t)eui_get_text_width(&ESGUI_DEFAULT_FONT, text);
    if (measure) return w;
    eui_draw_text(c_it->canvas, (int)(x_right - w - 3), page->items[indx].y,
                  &ESGUI_DEFAULT_FONT, text, EUI_MODE_SET);
    return w;
}

/* ==================== 页面虚函数表 / 创建 ==================== */

/* 前置声明：vtable 里要引用它，而它定义在下面 */
static void ov_page_on_page_change(ESGUI_MenuPage_T *page, ESGUI_MenuAction_T *action);

static const esgui_page_vtable_t ov_page_vtable = {
    .on_create         = esgui_text_menu_defalt_on_create,
    .on_destroy        = esgui_text_menu_defalt_on_destroy,
    .on_draw           = esgui_text_menu_defalt_on_draw,
    .on_focus_change   = esgui_text_menu_defalt_on_focus_change,
    .on_input          = esgui_menu_defalt_on_input,
    .special_item_draw = ov_page_special_draw,
    .on_page_chenge    = ov_page_on_page_change,
#if ESGUI_ENABLE_MENU_RUNTIME_ITEMS
    .on_relayout       = esgui_text_menu_relayout,
#endif
};

static ESGUI_MenuItem_T ov_items[] = {
    {0, 0, "覆盖层:\x03/0", ESGUI_NULL, ov_enter_toggle_add, ov_state_buf},
    {0, 0, "显示:\x03/0",   ESGUI_NULL, ov_enter_toggle_visible, ov_visible_buf},
    {0, 0, "曲线:\x03/0",   ESGUI_NULL, ov_enter_curve, ov_curve_buf},
    {0, 0, "重放动画",      ESGUI_NULL, ov_enter_replay, ESGUI_NULL},
    {0, 0, "返回",          ESGUI_NULL, ov_enter_back, ESGUI_NULL},
};

ESGUI_MenuAction_T test_overlay_page_create(void)
{
    ov_refresh_state();

    ESGUI_DefaltTextMenuCreate(&ov_page, ov_items, "覆盖层",
                               ESGUI_ITEM_NUM_COUNT(ov_items));
    ov_page.vtbl      = &ov_page_vtable;
    ov_page.focus_idx = 0;

    /* 进页面就自动挂上覆盖层并跑一遍动画，省一次点击 */
    if (!ov_added) {
        ov_enter_toggle_add(&ov_page, ESGUI_NULL);
    }
    return (ESGUI_MenuAction_T){ACT_PUSH_PAGE, &ov_page};
}

/* ==================================================================
 * 退出清理：覆盖层是"全局常驻"的，不清理会一直画在后面的页面上
 * ================================================================== */
static void ov_page_cleanup(void)
{
    anim_stop_all(&ov_box_x);
    if (ov_added) {
        ESGUI_OverlayRemove(ov_ui(), &ov_layer);
        ov_added = false;
    }
    ov_visible = true;
    ov_refresh_state();
}

/* 页面切换回调：Pop 时清理覆盖层，其余交给默认实现（过渡动画） */
static void ov_page_on_page_change(ESGUI_MenuPage_T *page, ESGUI_MenuAction_T *action)
{
    if (action != ESGUI_NULL && action->act == ACT_POP_PAGE) {
        ov_page_cleanup();
    }
    esgui_text_menu_default_on_page_change(page, action);
}
