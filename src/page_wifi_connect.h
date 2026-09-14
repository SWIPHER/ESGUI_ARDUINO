//
// Created by E_LJF on 2026/8/25.
//

#ifndef BASED_ON_ESGUI_PAGE_WIFI_CONNECT_H
#define BASED_ON_ESGUI_PAGE_WIFI_CONNECT_H

#include "ESGUI.h"
#include "esp_wifi_types.h"

/**
 * @brief 创建"连接 WiFi"弹窗（文本列表弹窗，标题为所选 AP 的 SSID）
 *
 * 条目：输入密码（弹出键盘）、开始连接（触发 wifi_app_connect）。
 * 连接结果由 wifi_app 回调以弹窗形式通知。
 *
 * @param ssid      目标 AP 的 SSID
 * @param authmode  目标 AP 的认证方式
 * @return ACT_SHOW_POPUP 动作（param 为弹窗指针）
 */
ESGUI_MenuAction_T wifi_connect_popup_create(const char *ssid, wifi_auth_mode_t authmode);

#endif //BASED_ON_ESGUI_PAGE_WIFI_CONNECT_H
