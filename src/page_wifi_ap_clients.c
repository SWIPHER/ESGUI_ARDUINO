//
// Created by E_LJF on 2026/8/25.
//

#include "page_wifi_ap_clients.h"

#include <string.h>
#include <stdio.h>

#include "ESGUI_PageDefaltVtbl.h"
#include "ESGUI_Widget.h"
#include "wifi_app.h"
#include "esp_log.h"

static ESGUI_MenuPage_T clients_page;
static ESGUI_MenuItem_T clients_items[WIFI_APP_MAX_CLIENTS];
static wifi_app_client_entry_t client_entries[WIFI_APP_MAX_CLIENTS];  /* 页面数据快照 */
static char label_bufs[WIFI_APP_MAX_CLIENTS][16];

static ESGUI_PopWindow_T detail_popup;
static char detail_msg[160];
static ESGUI_PopWindow_T clients_op_popup;
static ESGUI_MenuItem_T clients_op_items[2];

/* ==================== 特殊绘制：右侧显示信号强度 ==================== */

static eui_uint16_t clients_page_special_draw(ESGUI_MenuPage_T *page, eui_uint16_t indx, bool measure)
{
    if (page == ESGUI_NULL) return 0;
    ESGUI_MenuItem_T *item = &page->items[indx];
    if (item->arg == ESGUI_NULL) {
        return 0;                                   /* 非设备条目 */
    }

    wifi_app_client_entry_t *e = (wifi_app_client_entry_t *)item->arg;
    char buff[8];
    snprintf(buff, sizeof(buff), "%ddBm", e->rssi);
    eui_uint8_t w = eui_get_text_width(&eui_test_font, buff);
    if (measure) {
        return w;
    }

    CanvasStripIter *c_it = page->render_ctx;
    eui_int16_t item_y = item->y;
    eui_uint16_t x_offset = c_it->canvas->width - ESGUI_PROGRESS_BAR_W;
    eui_draw_text(c_it->canvas, x_offset - w, item_y, &eui_test_font, buff, EUI_MODE_SET);
    return w;
}

/* ==================== 条目回调 ==================== */

/* "查看设备信息"：弹设备详情长文本弹窗 */
static ESGUI_MenuAction_T clients_detail_item_enter(ESGUI_MenuPage_T *page, void *arg)
{
    wifi_app_client_entry_t *e = (wifi_app_client_entry_t *)arg;
    eui_uint16_t idx = (eui_uint16_t)(e - client_entries);

    eui_int16_t n = 0;
    n += snprintf(detail_msg + n, sizeof(detail_msg) - n, "设备名称: 设备%d\n", idx + 1);
    n += snprintf(detail_msg + n, sizeof(detail_msg) - n,
                  "MAC地址: %02x:%02x:%02x:%02x:%02x:%02x\n",
                  e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5]);
    if (e->ip_valid) {
        n += snprintf(detail_msg + n, sizeof(detail_msg) - n, "IP地址: " IPSTR "\n", IP2STR(&e->ip));
    } else {
        n += snprintf(detail_msg + n, sizeof(detail_msg) - n, "IP地址: --\n");
    }
    snprintf(detail_msg + n, sizeof(detail_msg) - n, "信号强度: %ddBm", e->rssi);

    ESGUI_DefaultMessageLongTextPopWindowCreate(&detail_popup, detail_msg, 100, 60);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &detail_popup};
}

/* "断开设备的连接"：踢掉该客户端（持续 deauth 防秒重连，列表由 2s 监控 + 延迟刷新更新） */
static ESGUI_MenuAction_T clients_kick_item_enter(ESGUI_MenuPage_T *page, void *arg)
{
    wifi_app_client_entry_t *e = (wifi_app_client_entry_t *)arg;
    esp_err_t ret = wifi_app_kick_sta(e->mac);
    if (ret != ESP_OK) {
        ESP_LOGE("clients", "Kick start failed: %s", esp_err_to_name(ret));
    }
    /* 弹窗关闭过渡期间推送的刷新事件会被 ESGUI 丢弃，
     * 延迟推送保证踢人后列表尽快刷新 */
    wifi_app_clients_refresh_delayed();
    return (ESGUI_MenuAction_T){ACT_CLOSE_POPUP, ESGUI_NULL};
}

/* 点击设备条目：弹出操作列表（查看设备信息 / 断开设备的连接） */
static ESGUI_MenuAction_T clients_item_enter(ESGUI_MenuPage_T *page, void *arg)
{
    clients_op_items[0].label = "查看设备信息";
    clients_op_items[0].on_enter = clients_detail_item_enter;
    clients_op_items[0].arg = arg;
    clients_op_items[1].label = "断开设备的连接";
    clients_op_items[1].on_enter = clients_kick_item_enter;
    clients_op_items[1].arg = arg;

    ESGUI_DefaultTextListScrollTitlePopWindowCreate(&clients_op_popup, "设备操作", 100, 60,
                                                    clients_op_items, 2);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &clients_op_popup};
}

/* ==================== 列表刷新 ==================== */

static void clients_page_refresh(void)
{
    uint16_t n = 0;
    const wifi_app_client_entry_t *src = wifi_app_get_clients(&n);
    if (n > WIFI_APP_MAX_CLIENTS) {
        n = WIFI_APP_MAX_CLIENTS;
    }
    memcpy(client_entries, src, sizeof(client_entries[0]) * n);

    if (n == 0) {
        while (clients_page.item_num > 1) {
            ESGUI_MenuPageRemoveItem(&clients_page, clients_page.item_num - 1);
        }
        clients_items[0].label = "无设备连接";
        clients_items[0].on_enter = ESGUI_NULL;
        clients_items[0].arg = ESGUI_NULL;
        clients_page.focus_idx = 0;
        esgui_text_menu_relayout(&clients_page, 0, 0);  /* 重排焦点框/列表滚动 */
        return;
    }

    for (uint16_t i = 0; i < n; i++) {
        snprintf(label_bufs[i], sizeof(label_bufs[i]), "设备%d", i + 1);
        if (i < clients_page.item_num) {
            /* 更新已有槽位 */
            clients_items[i].label = label_bufs[i];
            clients_items[i].on_enter = clients_item_enter;
            clients_items[i].arg = &client_entries[i];
        } else {
            ESGUI_MenuItem_T it = {0, 0, label_bufs[i], ESGUI_NULL,
                                   clients_item_enter, &client_entries[i]};
            ESGUI_MenuPageAddItem(&clients_page, &it);
        }
    }
    while (clients_page.item_num > n) {
        ESGUI_MenuPageRemoveItem(&clients_page, clients_page.item_num - 1);
    }
    if (clients_page.focus_idx >= n) {
        clients_page.focus_idx = n - 1;
    }
    /* 槽位标签/焦点钳位均为直接赋值，须按最终焦点重排，否则焦点框/滚动停留在旧状态 */
    esgui_text_menu_relayout(&clients_page, clients_page.focus_idx, clients_page.focus_idx);
}

/* ==================== 输入处理 / 生命周期 ==================== */

static ESGUI_MenuAction_T clients_page_on_input(ESGUI_MenuPage_T *page, ESGUI_EventCode_t e)
{
    if (e == WIFI_APP_EVT_CLIENTS_UPDATE) {
        clients_page_refresh();
        return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
    }
    return esgui_menu_defalt_on_input(page, e);
}

static void clients_page_on_destroy(ESGUI_MenuPage_T *page)
{
    esgui_text_menu_defalt_on_destroy(page);
    wifi_app_clients_monitor_stop();
}

/* ==================== 页面虚函数表 ==================== */

static const esgui_page_vtable_t clients_page_vtable = {
    .on_create                = esgui_text_menu_defalt_on_create,
    .on_destroy               = clients_page_on_destroy,
    .on_draw                  = esgui_text_menu_defalt_on_draw,
    .on_focus_change          = esgui_text_menu_defalt_on_focus_change,
    .on_input                 = clients_page_on_input,
    .special_item_draw        = clients_page_special_draw,
    .on_page_chenge           = esgui_text_menu_default_on_page_change,
#if ESGUI_ENABLE_MENU_RUNTIME_ITEMS
    .on_relayout              = esgui_text_menu_relayout,
#endif
};

/* ==================== 对外接口 ==================== */

ESGUI_MenuAction_T page_wifi_ap_clients_create(void)
{
    ESGUI_DefaltTextMenuCreate(&clients_page, clients_items, "已连接设备", 1);
    clients_page.item_cap = WIFI_APP_MAX_CLIENTS;
    clients_page.vtbl = &clients_page_vtable;
    clients_items[0].label = "无设备连接";
    clients_items[0].on_enter = ESGUI_NULL;
    clients_items[0].arg = ESGUI_NULL;
    clients_page.focus_idx = 0;

    clients_page_refresh();
    wifi_app_clients_monitor_start();
    return (ESGUI_MenuAction_T){ACT_PUSH_PAGE, &clients_page};
}
