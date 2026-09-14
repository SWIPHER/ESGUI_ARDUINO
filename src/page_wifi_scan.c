//
// Created by E_LJF on 2026/8/25.
//

#include "page_wifi_scan.h"
#include "page_wifi_connect.h"

#include <string.h>

#include "ESGUI_PageDefaltVtbl.h"
#include "ESGUI_Widget.h"
#include "ESGUI_Anim.h"
#include "wifi_app.h"

/* 页面条目数据（与扫描结果一一对应，arg 指向此项） */
typedef struct {
    char ssid[33];
    char label[48];         /* 显示文本（当前已连接的网络带"已连接"前缀） */
    int8_t rssi;
    wifi_auth_mode_t authmode;
    uint8_t channel;
} ScanEntry_T;

static ESGUI_MenuPage_T scan_page;
static ScanEntry_T scan_entries[WIFI_APP_MAX_SCAN_RESULTS];

/* ==================== 特殊绘制：信号条 + 加密锁 ==================== */

#define SCAN_LOCK_W      6   /* 锁图标宽 */
#define SCAN_LOCK_GAP    6   /* 锁图标与信号条间距 */
#define SCAN_BAR_NUM     4   /* 信号条数量 */
#define SCAN_BAR_W       2   /* 单条宽 */
#define SCAN_BAR_GAP     1   /* 条间距 */
#define SCAN_RIGHT_W     (SCAN_LOCK_W + SCAN_LOCK_GAP + SCAN_BAR_NUM * (SCAN_BAR_W + SCAN_BAR_GAP) - SCAN_BAR_GAP)

static eui_uint8_t scan_rssi_level(int8_t rssi)
{
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    return 1;
}

static eui_uint16_t scan_page_special_draw(ESGUI_MenuPage_T *page, eui_uint16_t indx, bool measure)
{
    if (page == ESGUI_NULL) return 0;
    ESGUI_MenuItem_T *item = &page->items[indx];
    if (item->arg == ESGUI_NULL) {
        return 0;                                   /* 非 AP 条目（重新扫描等） */
    }

    CanvasStripIter *c_it = page->render_ctx;
    ScanEntry_T *e = (ScanEntry_T *)item->arg;
    eui_int16_t item_y = item->y;
    eui_int16_t x_right = c_it->canvas->width - ESGUI_PROGRESS_BAR_W;
    eui_int16_t x = x_right - SCAN_RIGHT_W;

    if (measure) {
        return SCAN_RIGHT_W;
    }

    /* 加密锁图标 */
    if (e->authmode != WIFI_AUTH_OPEN) {
        eui_draw_rect_fill(c_it->canvas, x, item_y + 5, x + SCAN_LOCK_W - 1, item_y + 10, EUI_MODE_SET);
        // eui_draw_rect_fill(c_it->canvas,x + 1,item_y + 6,x + SCAN_LOCK_W - 2,item_y + 9, EUI_MODE_CLER);
        // eui_draw_circle_fill(c_it->canvas,(x + SCAN_LOCK_W) / 2,item_y + 7,1,EUI_MODE_CLER);
        eui_draw_rect_stroke(c_it->canvas, x + 1, item_y + 2, x + SCAN_LOCK_W - 2, item_y + 5, EUI_MODE_SET);
    }

    /* 信号强度条（底部对齐） */
    eui_uint8_t level = scan_rssi_level(e->rssi);
    eui_int16_t bar_x = x + SCAN_LOCK_W + SCAN_LOCK_GAP;
    eui_int16_t bottom = item_y + 10;
    for (eui_uint8_t i = 0; i < SCAN_BAR_NUM; i++) {
        eui_int16_t b = bar_x + i * (SCAN_BAR_W + SCAN_BAR_GAP);
        eui_int16_t h = 3 + i * 2;                  /* 高度递增：3,5,7,9 */
        if (i < level) {
            eui_draw_rect_fill(c_it->canvas, b, bottom - h, b + SCAN_BAR_W - 1, bottom, EUI_MODE_SET);
        } else {
            eui_draw_rect_stroke(c_it->canvas, b, bottom - h, b + SCAN_BAR_W - 1, bottom, EUI_MODE_SET);
        }
    }
    return SCAN_RIGHT_W;
}

/* ==================== 条目回调 ==================== */

/* 停止可能仍在对 items[0].y 写值的动画（入场/列表滚动动画持有 items 数组内指针；
 * 增删条目导致 realloc 搬家后，残留动画会向已释放内存写入 → 堆损坏/条目错乱。
 * 必须在任何 Remove/Insert/Add 之前调用。） */
static void scan_page_stop_item_anim(void)
{
    anim_stop_all(&scan_page.items[0].y);
}

/* 强制列表基线复位到顶部。
 * recenter 在小列表（item_num<=buff）下不会写 items[0].y，中断动画可能遗留偏移，
 * 导致条目文本画在错误的行上。focus=0 时首条必在 top_y，直接校正。 */
static void scan_page_force_top_layout(void)
{
    ESGUI_DEFALT_TEXT_PAGE_DATA_T *pd = (ESGUI_DEFALT_TEXT_PAGE_DATA_T *)scan_page.draw_data;
    if (pd != ESGUI_NULL) {
        scan_page.items[0].y = (eui_int16_t)pd->title_h + ESGUI_TITLE_LINE_OFFSET;
    }
}

static ESGUI_MenuAction_T scan_rescan_item_enter(ESGUI_MenuPage_T *page, void *arg)
{
    /* 收缩到单条并显示"正在扫描" */
    scan_page_stop_item_anim();
    while (page->item_num > 1) {
        ESGUI_MenuPageRemoveItem(page, 0);
    }
    page->items[0].label = "正在扫描...";
    page->items[0].on_enter = ESGUI_NULL;
    page->items[0].arg = ESGUI_NULL;
    page->focus_idx = 0;
    esgui_text_menu_relayout(page, 0, 0);               /* 收缩后按最终焦点重排焦点框 */
    scan_page_force_top_layout();

    esp_err_t err = wifi_app_start_scan();
    if (err != ESP_OK) {
        page->items[0].label = "扫描失败，重新扫描";
        page->items[0].on_enter = scan_rescan_item_enter;
        esgui_text_menu_relayout(page, 0, 0);
        scan_page_force_top_layout();
    }
    return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
}

static ESGUI_MenuAction_T scan_ap_item_enter(ESGUI_MenuPage_T *page, void *arg)
{
    ScanEntry_T *e = (ScanEntry_T *)arg;
    return wifi_connect_popup_create(e->ssid, e->authmode);
}

/* ==================== 列表重建 ==================== */

static void scan_page_rebuild(void)
{
    uint16_t cnt = 0;
    const wifi_app_scan_entry_t *res = wifi_app_get_scan_results(&cnt);

    /* 先停掉可能指向 items 数组的动画，再增删条目（防 realloc 后残留动画写悬空指针） */
    scan_page_stop_item_anim();

    /* 收缩到 1 条，作为"重新扫描" */
    while (scan_page.item_num > 1) {
        ESGUI_MenuPageRemoveItem(&scan_page, 0);
    }
    scan_page.items[0].label = "重新扫描";
    scan_page.items[0].on_enter = scan_rescan_item_enter;
    scan_page.items[0].arg = ESGUI_NULL;

    if (wifi_app_scan_failed()) {
        scan_page.items[0].label = "扫描失败，重新扫描";
        scan_page.focus_idx = 0;
        esgui_text_menu_relayout(&scan_page, 0, 0);     /* 焦点框/列表滚动随新状态重排 */
        scan_page_force_top_layout();
        return;
    }
    if (cnt == 0) {
        scan_page.items[0].label = "未找到WiFi，重新扫描";
        scan_page.focus_idx = 0;
        esgui_text_menu_relayout(&scan_page, 0, 0);     /* 焦点框/列表滚动随新状态重排 */
        scan_page_force_top_layout();
        return;
    }

    for (uint16_t i = 0; i < cnt && i < WIFI_APP_MAX_SCAN_RESULTS; i++) {
        memset(&scan_entries[i], 0, sizeof(scan_entries[i]));
        strncpy(scan_entries[i].ssid, res[i].ssid, sizeof(scan_entries[i].ssid) - 1);
        if (wifi_app_is_connected_to(scan_entries[i].ssid)) {
            snprintf(scan_entries[i].label, sizeof(scan_entries[i].label),
                     "已连接 %s", res[i].ssid);      /* 源用 res 避免与目标同对象触发 -Wrestrict */
        } else {
            strncpy(scan_entries[i].label, scan_entries[i].ssid,
                    sizeof(scan_entries[i].label) - 1);
        }
        scan_entries[i].rssi = res[i].rssi;
        scan_entries[i].authmode = res[i].authmode;
        scan_entries[i].channel = res[i].channel;

        ESGUI_MenuItem_T it = {0, 0, scan_entries[i].label, ESGUI_NULL,
                               scan_ap_item_enter, &scan_entries[i]};
        ESGUI_MenuPageInsertItem(&scan_page, i, &it);   /* 逐个插到"重新扫描"前 */
    }
    /* 插入过程中 relayout 跟随的是末尾焦点（重新扫描）；必须按最终焦点重排，
     * 否则焦点框停留在末行：选中框在"重新扫描"上，按键执行的却是 items[0]（字段与功能错位）。
     * 小列表下 recenter 不写 items[0].y，此处再强制基线复位到顶部。 */
    scan_page.focus_idx = 0;
    esgui_text_menu_relayout(&scan_page, 0, 0);
    scan_page_force_top_layout();
}

/* ==================== 输入处理 ==================== */

static ESGUI_MenuAction_T scan_page_on_input(ESGUI_MenuPage_T *page, ESGUI_EventCode_t e)
{
    if (e == WIFI_APP_EVT_SCAN_DONE) {
        scan_page_rebuild();
        return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
    }
    if (e == WIFI_APP_EVT_CONNECTED) {
        /* 连接成功后重建列表，让"已连接 xxx"标记立即出现（无需退出重进） */
        scan_page_rebuild();
        return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
    }
    return esgui_menu_defalt_on_input(page, e);
}

/* ==================== 页面虚函数表 ==================== */

static const esgui_page_vtable_t scan_page_vtable = {
    .on_create                = esgui_text_menu_defalt_on_create,
    .on_destroy               = esgui_dynamic_text_menu_on_destroy,
    .on_draw                  = esgui_text_menu_defalt_on_draw,
    .on_focus_change          = esgui_text_menu_defalt_on_focus_change,
    .on_input                 = scan_page_on_input,
    .special_item_draw        = scan_page_special_draw,
    .on_page_chenge           = esgui_text_menu_default_on_page_change,
#if ESGUI_ENABLE_MENU_RUNTIME_ITEMS
    .on_relayout              = esgui_text_menu_relayout,
#endif
};

/* ==================== 对外接口 ==================== */

ESGUI_MenuAction_T page_wifi_scan_create(void)
{
    if (!ESGUI_DynamicTextMenuCreate(&scan_page, "WiFi扫描", 2)) {
        return (ESGUI_MenuAction_T){ACT_NONE, ESGUI_NULL};
    }
    scan_page.vtbl = &scan_page_vtable;
    scan_page.items[0].label = "正在扫描...";
    scan_page.items[0].on_enter = ESGUI_NULL;
    scan_page.items[0].arg = ESGUI_NULL;
    scan_page.focus_idx = 0;

    wifi_app_start_scan();
    return (ESGUI_MenuAction_T){ACT_PUSH_PAGE, &scan_page};
}
