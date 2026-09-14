//
// Created by E_LJF on 2026/8/22.
//

#include "wifi_mod_page.h"

#include <string.h>

#include "ESGUI_Widget.h"
#include "ESGUI_PageDefaltVtbl.h"
#include "wifi_mgr.h"
#include "wifi_app.h"
#include "wifi_setting_page.h"
#include "page_wifi_scan.h"
#include "page_wifi_ap_clients.h"
#include "esp_log.h"


typedef struct _page_data{
    ESGUI_T *ui;
    bool wifi_en;

    wifi_mgr_ap_cfg_t ap_cfg;
    wifi_mgr_sta_cfg_t sta_cfg;
    wifi_mgr_config_t wifi_cfg;
}FirstPageData_T;


ESGUI_MenuPage_T wifi_mod_page;
static ESGUI_PopWindow_T pop_window;
static ESGUI_PopWindow_T pop_window2;
static FirstPageData_T wifi_mod_page_data;
static const char str_list[5][15] = {{"关闭"},{"开启"},{"AP模式"},{"STA模式"},{"APSTA模式"}};

/* 首页条目池（容量 5：状态/模式/扫描/设备/配置），运行时按模式增删 */
static ESGUI_MenuItem_T first_page_item[5];
static ESGUI_MenuItem_T first_page_item_scan =
    {0, 0, "WiFi扫描", ESGUI_NULL, ESGUI_NULL, ESGUI_NULL};
static ESGUI_MenuItem_T first_page_item_devices =
    {0, 0, "已连接设备", ESGUI_NULL, ESGUI_NULL, ESGUI_NULL};
static ESGUI_MenuItem_T first_page_item_config =
    {0, 0, "WIFI配置", ESGUI_NULL, ESGUI_NULL, ESGUI_NULL};

static void first_page_rebuild_mode_items(void);   /* 前置声明（模式弹窗回调中使用） */




static ESGUI_MenuAction_T wifi_en_poop_window_on_enter(ESGUI_MenuPage_T *page,void *arg) {
    switch (page->focus_idx) {
        case 0:
            if (wifi_mod_page_data.wifi_en != 1) {
                if (wifi_mgr_init(&wifi_mod_page_data.wifi_cfg) == ESP_OK){
                    wifi_mod_page_data.wifi_en = 1;
                }
                else {
                    ESGUI_DefaultMessagePopWindowCreate(&pop_window2,"   开启失败\n请检查wifi配置",100,50,1);
                    ESGUI_MenuCtrlQueueAction(&wifi_mod_page_data.ui->menu_ctrl,(ESGUI_MenuAction_T){ACT_CLOSE_POPUP,&pop_window});
                    ESGUI_MenuCtrlQueueAction(&wifi_mod_page_data.ui->menu_ctrl,(ESGUI_MenuAction_T){ACT_SHOW_POPUP,&pop_window2});
                    return (ESGUI_MenuAction_T){ACT_NONE,ESGUI_NULL};
                }
            }
            return (ESGUI_MenuAction_T){ACT_CLOSE_POPUP,ESGUI_NULL};

        case 1:
            if (wifi_mgr_deinit() == ESP_OK && wifi_mod_page_data.wifi_en != 0) {
                wifi_mod_page_data.wifi_en = 0;
            }
            return (ESGUI_MenuAction_T){ACT_CLOSE_POPUP,ESGUI_NULL};

        default:
            return (ESGUI_MenuAction_T){ACT_NONE,ESGUI_NULL};
    }
}




static ESGUI_MenuAction_T wifi_mode_popwindow_on_enter(ESGUI_MenuPage_T *page,void *arg) {
    switch (page->focus_idx) {
        case 0:
            wifi_mod_page_data.wifi_cfg.mode = WIFI_MGR_MODE_AP;
            break;

        case 1:
            wifi_mod_page_data.wifi_cfg.mode = WIFI_MGR_MODE_STA;
            break;

        case 2:
            wifi_mod_page_data.wifi_cfg.mode = WIFI_MGR_MODE_APSTA;
            break;

        default:
            return (ESGUI_MenuAction_T){ACT_NONE,ESGUI_NULL};
    }

    /* 立即生效：模式变更实时应用（WiFi 已开启时重启生效；未开启时仅保存，下次开启生效） */
    wifi_mgr_config_t cfg = wifi_mod_page_data.wifi_cfg;
    esp_err_t ret = wifi_mgr_update_config(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("wifi_mod_page", "Mode apply failed: %s", esp_err_to_name(ret));
    }
    wifi_app_config_save(&cfg);

    /* 按新模式重建首页条目（弹窗关闭时框架自动重排布局） */
    first_page_rebuild_mode_items();
    return (ESGUI_MenuAction_T){ACT_CLOSE_POPUP,ESGUI_NULL};
}




static ESGUI_MenuAction_T wifi_en_item_on_enter(ESGUI_MenuPage_T *page,void *arg) {
    ESGUI_DefaultBoolPopWindowCreate(&pop_window,"是否开启WIFI","是","否",100,40,&wifi_mod_page_data.wifi_en);
    pop_window.items[0].on_enter = wifi_en_poop_window_on_enter;
    pop_window.items[1].on_enter = wifi_en_poop_window_on_enter;
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP,&pop_window};
}




static ESGUI_MenuItem_T wifi_mode_popwindow_items[] = {
  {0,0,str_list[2],ESGUI_NULL,wifi_mode_popwindow_on_enter,ESGUI_NULL},
  {0,0,str_list[3],ESGUI_NULL,wifi_mode_popwindow_on_enter,ESGUI_NULL},
  {0,0,str_list[4],ESGUI_NULL,wifi_mode_popwindow_on_enter,ESGUI_NULL},
};
static ESGUI_MenuAction_T wifi_mode_item_on_enter(ESGUI_MenuPage_T *page,void *arg) {
    ESGUI_DefaultTextListPopWindowCreate(&pop_window,100,60,wifi_mode_popwindow_items,ESGUI_ITEM_NUM_COUNT(wifi_mode_popwindow_items));
    pop_window.focus_idx = wifi_mod_page_data.wifi_cfg.mode;
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP,&pop_window};
}



static ESGUI_MenuAction_T wifi_setting_item_on_enter(ESGUI_MenuPage_T *page,void *arg) {
    return wifi_setting_page_create(wifi_mod_page_data.wifi_cfg.mode,&wifi_mod_page_data.ap_cfg,&wifi_mod_page_data.sta_cfg);
}



static ESGUI_MenuAction_T wifi_scan_item_on_enter(ESGUI_MenuPage_T *page,void *arg) {
    wifi_mgr_mode_t mode;
    if (wifi_mgr_get_mode(&mode) != ESP_OK) {
        ESGUI_DefaultMessagePopWindowCreate(&pop_window2,"   请先开启WIFI",100,50,1);
        return (ESGUI_MenuAction_T){ACT_SHOW_POPUP,&pop_window2};
    }
    if (mode != WIFI_MGR_MODE_STA && mode != WIFI_MGR_MODE_APSTA) {
        ESGUI_DefaultMessagePopWindowCreate(&pop_window2,"   当前模式不支持扫描\n请切换STA/APSTA模式",100,50,1);
        return (ESGUI_MenuAction_T){ACT_SHOW_POPUP,&pop_window2};
    }
    return page_wifi_scan_create();
}



static ESGUI_MenuAction_T wifi_devices_item_on_enter(ESGUI_MenuPage_T *page,void *arg) {
    wifi_mgr_mode_t mode;
    if (wifi_mgr_get_mode(&mode) != ESP_OK) {
        ESGUI_DefaultMessagePopWindowCreate(&pop_window2,"   请先开启WIFI",100,50,1);
        return (ESGUI_MenuAction_T){ACT_SHOW_POPUP,&pop_window2};
    }
    if (mode != WIFI_MGR_MODE_AP && mode != WIFI_MGR_MODE_APSTA) {
        ESGUI_DefaultMessagePopWindowCreate(&pop_window2,"   当前模式无AP功能\n请切换AP/APSTA模式",100,50,1);
        return (ESGUI_MenuAction_T){ACT_SHOW_POPUP,&pop_window2};
    }
    return page_wifi_ap_clients_create();
}




/* ==================== 首页条目按模式增删 ==================== */

static void first_page_rebuild_mode_items(void)
{
    /* 收缩到基础两条（WIFI状态 / WIFI模式） */
    while (wifi_mod_page.item_num > 2) {
        ESGUI_MenuPageRemoveItem(&wifi_mod_page, 2);
    }

    switch (wifi_mod_page_data.wifi_cfg.mode) {
        case WIFI_MGR_MODE_AP:
            ESGUI_MenuPageInsertItem(&wifi_mod_page, 2, &first_page_item_devices);
            ESGUI_MenuPageInsertItem(&wifi_mod_page, 3, &first_page_item_config);
            break;

        case WIFI_MGR_MODE_STA:
            ESGUI_MenuPageInsertItem(&wifi_mod_page, 2, &first_page_item_scan);
            ESGUI_MenuPageInsertItem(&wifi_mod_page, 3, &first_page_item_config);
            break;

        case WIFI_MGR_MODE_APSTA:
            ESGUI_MenuPageInsertItem(&wifi_mod_page, 2, &first_page_item_scan);
            ESGUI_MenuPageInsertItem(&wifi_mod_page, 3, &first_page_item_devices);
            ESGUI_MenuPageInsertItem(&wifi_mod_page, 4, &first_page_item_config);
            break;

        default:
            break;
    }
}




static eui_uint16_t first_poage_special_item_draw(ESGUI_MenuPage_T *page, eui_uint16_t indx, bool measure) {
    if (page == ESGUI_NULL) return 0;

    CanvasStripIter *c_it = page->render_ctx;
    FirstPageData_T *data = page->user_data;
    ESGUI_MenuItem_T *indx_item = &page->items[indx];
    eui_int16_t item_y = page->items[indx].y;
    eui_uint16_t x_offset = c_it->canvas->width - ESGUI_PROGRESS_BAR_W;
    char c;

    if (!ESGUI_WidgetCheckMarker(page->items[indx].label, ESGUI_WIDGET_DEFAULT_MARK, ESGUI_NULL, &c)) {
        return 0;
    }

    switch (c) {
        case '0':
            if (indx_item->arg){
                eui_uint8_t w = eui_get_text_width(&eui_test_font,str_list[*((eui_uint8_t*)indx_item->arg)]);
                if (measure)return w;

                eui_draw_text(c_it->canvas,x_offset - w - 3,item_y,&eui_test_font,str_list[*((eui_uint8_t*)indx_item->arg)],EUI_MODE_SET);
                return w;
            }
            break;

        case '1':
            if (indx_item->arg){
                eui_uint8_t w = eui_get_text_width(&eui_test_font,str_list[*((eui_uint8_t*)indx_item->arg) + 2]);
                if (measure)return w;

                eui_draw_text(c_it->canvas,x_offset - w - 3,item_y,&eui_test_font,str_list[*((eui_uint8_t*)indx_item->arg) + 2],EUI_MODE_SET);
                return w;
            }
            break;

        case '2':
            if (measure) return 20;                 /* 测量模式：仅返回占宽，不绘制 */
        {
            char buff[10] = {0};
            _int16_to_str(*(eui_int16_t*)page->items[indx].arg, buff);
            eui_draw_text(c_it->canvas,
                x_offset - 15,item_y,
                &ESGUI_DEFAULT_FONT,
                buff,
                EUI_MODE_SET);
        }
            return 20;

        default:
            return 0;
    }
    return 0;
}




static const esgui_page_vtable_t first_page_vtable = {
    .on_create                = esgui_text_menu_defalt_on_create,
    .on_destroy               = esgui_text_menu_defalt_on_destroy,
    .on_draw                  = esgui_text_menu_defalt_on_draw,
    .on_focus_change          = esgui_text_menu_defalt_on_focus_change,
    .on_input                 = esgui_menu_defalt_on_input,
    .special_item_draw        = first_poage_special_item_draw,
    .on_page_chenge           = esgui_text_menu_default_on_page_change,
#if ESGUI_ENABLE_MENU_RUNTIME_ITEMS
    .on_relayout              = esgui_text_menu_relayout,
#endif
};




void wifi_mod_page_init(ESGUI_T *eui) {
    memset(&wifi_mod_page_data,0,sizeof(wifi_mod_page_data));

    wifi_mod_page_data.ui = eui;
    wifi_mod_page_data.wifi_en = false;

    wifi_mod_page_data.wifi_cfg.ap = &wifi_mod_page_data.ap_cfg;
    wifi_mod_page_data.wifi_cfg.sta = &wifi_mod_page_data.sta_cfg;
    wifi_mod_page_data.wifi_cfg.mode = WIFI_MGR_MODE_AP;
    wifi_mod_page_data.wifi_cfg.cb = &wifi_mgr_callbacks;
    wifi_mod_page_data.wifi_cfg.init_nvs = true;

    strncpy(wifi_mod_page_data.ap_cfg.ssid, "Default AP", sizeof(wifi_mod_page_data.ap_cfg.ssid) - 1);
    wifi_mod_page_data.ap_cfg.ssid[sizeof(wifi_mod_page_data.ap_cfg.ssid) - 1] = '\0';
    wifi_mod_page_data.ap_cfg.password[0] = '\0';
    wifi_mod_page_data.ap_cfg.channel = 1;
    wifi_mod_page_data.ap_cfg.max_connection = 4;
    wifi_mod_page_data.ap_cfg.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_mod_page_data.ap_cfg.hidden = false;

    strncpy(wifi_mod_page_data.sta_cfg.ssid, "Default STA", sizeof(wifi_mod_page_data.sta_cfg.ssid) - 1);
    wifi_mod_page_data.sta_cfg.ssid[sizeof(wifi_mod_page_data.sta_cfg.ssid) - 1] = '\0';
    wifi_mod_page_data.sta_cfg.password[0] = '\0';
    wifi_mod_page_data.sta_cfg.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_mod_page_data.sta_cfg.max_retry = 5;
    wifi_mod_page_data.sta_cfg.auto_connect = true;

    /* 从 NVS 恢复已保存配置（覆盖默认值）；开机默认不启动 WiFi，由用户手动开启 */
    wifi_app_config_load(&wifi_mod_page_data.wifi_cfg);

    /* 创建首页：基础两条（WIFI状态 / WIFI模式），其余按模式增删 */
    first_page_item[0] = (ESGUI_MenuItem_T){0, 0, "WIFI状态\x03/0", ESGUI_NULL,
                                            wifi_en_item_on_enter, &wifi_mod_page_data.wifi_en};
    first_page_item[1] = (ESGUI_MenuItem_T){0, 0, "WIFI模式\x03/1", ESGUI_NULL,
                                            wifi_mode_item_on_enter, &wifi_mod_page_data.wifi_cfg.mode};
    first_page_item_scan.on_enter = wifi_scan_item_on_enter;
    first_page_item_devices.on_enter = wifi_devices_item_on_enter;
    first_page_item_config.on_enter = wifi_setting_item_on_enter;

    ESGUI_DefaltTextMenuCreate(&wifi_mod_page, first_page_item, "WIFI", 2);
    wifi_mod_page.item_cap = 5;
    wifi_mod_page.user_data = &wifi_mod_page_data;
    wifi_mod_page.vtbl = &first_page_vtable;
    first_page_rebuild_mode_items();

    /* 注册 UI 生产者消息盒（wifi_app 工作线程/事件回调用它推送 UI 命令） */
    wifi_app_init();
    ESGUI_ProducerBoxInit(&wifi_app_producers);
    ESGUI_ProducerBoxRegister(eui,&wifi_app_producers);
}

wifi_mgr_config_t *wifi_mod_page_get_wifi_cfg(void)
{
    return &wifi_mod_page_data.wifi_cfg;
}

ESGUI_T *wifi_mod_page_get_eui(void) {
    return wifi_mod_page_data.ui;
}
