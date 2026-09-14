//
// Created by E_LJF on 2026/8/25.
//

#include "page_wifi_connect.h"

#include <string.h>

#include "ESGUI_PageDefaltVtbl.h"
#include "wifi_app.h"
#include "wifi_mod_page.h"

/* 注意：不要在这里 extern ESGUI_T ui; —— esgui_port.c 里的 ui 是 static（内部链接），
 * 不存在全局符号，直接 extern 会报 "undefined reference to `ui'"。
 * 统一用首页暴露的访问器拿 UI 实例（wifi_app.c 也是这个写法）。 */

static ESGUI_PopWindow_T connect_popup;     /* 连接弹窗（文本列表） */
static ESGUI_PopWindow_T kb_popup;          /* 密码键盘弹窗 */
static ESGUI_PopWindow_T msg_popup;         /* 提示弹窗 */

static char connect_ssid[33];
static char connect_password[65];
static wifi_auth_mode_t connect_authmode;

static ESGUI_MenuAction_T password_item_enter(ESGUI_MenuPage_T *page, void *arg)
{
    /* 弹窗 232x140（原 128x64 是 128 屏时代的尺寸）：
     * 键高 ESGUI_KEY_BOARD_KEY_H=24 → 字母页 4 行 = 96px、数字页 5 行 = 120px，
     * 加输入框 19px(字体行高 15 + 4) 后都在 140px 内，键盘不会被裁成滚动条。 */
    ESGUI_DefaultKeyBoardPopWindowCreate(&kb_popup, 232, 140,
                                         connect_password, sizeof(connect_password),
                                         connect_password);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &kb_popup};
}

static ESGUI_MenuAction_T connect_start_item_enter(ESGUI_MenuPage_T *page, void *arg)
{
    wifi_mgr_config_t *cfg = wifi_mod_page_get_wifi_cfg();
    esp_err_t err = wifi_app_connect(cfg, connect_ssid, connect_password, connect_authmode);
    if (err != ESP_OK) {
        ESGUI_DefaultMessagePopWindowCreate(&msg_popup, "连接启动失败\n请先开启WIFI", 100, 50, 1);
        ESGUI_T *eui = wifi_mod_page_get_eui();
        if (eui != ESGUI_NULL) {
            ESGUI_MenuCtrlQueueAction(&eui->menu_ctrl,
                                      (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &msg_popup});
        }
        return (ESGUI_MenuAction_T){ACT_CLOSE_POPUP, ESGUI_NULL};
    }
    /* 连接已触发，结果由 wifi_app 回调弹窗通知 */
    return (ESGUI_MenuAction_T){ACT_CLOSE_POPUP, ESGUI_NULL};
}

static ESGUI_MenuAction_T disconnect_item_enter(ESGUI_MenuPage_T *page, void *arg)
{
    wifi_app_disconnect();
    return (ESGUI_MenuAction_T){ACT_CLOSE_POPUP, ESGUI_NULL};
}

static ESGUI_MenuItem_T connect_items[] = {
    {0, 0, "输入密码", ESGUI_NULL, password_item_enter, ESGUI_NULL},
    {0, 0, "开始连接", ESGUI_NULL, connect_start_item_enter, ESGUI_NULL},
    {0, 0, "断开连接", ESGUI_NULL, disconnect_item_enter, ESGUI_NULL},
};

ESGUI_MenuAction_T wifi_connect_popup_create(const char *ssid, wifi_auth_mode_t authmode)
{
    if (ssid == ESGUI_NULL) {
        return (ESGUI_MenuAction_T){ACT_NONE, ESGUI_NULL};
    }

    strncpy(connect_ssid, ssid, sizeof(connect_ssid) - 1);
    connect_ssid[sizeof(connect_ssid) - 1] = '\0';
    connect_authmode = authmode;
    connect_password[0] = '\0';

    /* 点击的是当前已连接的网络：弹窗追加"断开连接"选项（3 项，高度加大） */
    bool connected = wifi_app_is_connected_to(ssid);

    /* 若所选 AP 与已保存的 STA 配置相同，预填已保存的密码（连接成功后自动保存过） */
    wifi_mgr_config_t *cfg = wifi_mod_page_get_wifi_cfg();
    if (cfg != NULL && cfg->sta != NULL && strcmp(ssid, cfg->sta->ssid) == 0) {
        strncpy(connect_password, cfg->sta->password, sizeof(connect_password) - 1);
        connect_password[sizeof(connect_password) - 1] = '\0';
    }

    ESGUI_DefaultTextListScrollTitlePopWindowCreate(&connect_popup, connect_ssid, 100,
                                                    connected ? 75 : 60,
                                                    connect_items,
                                                    connected ? 3 : 2);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &connect_popup};
}
