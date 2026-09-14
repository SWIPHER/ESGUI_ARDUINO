//
// Created by E_LJF on 2026/8/25.
//

#ifndef BASED_ON_ESGUI_PAGE_WIFI_SCAN_H
#define BASED_ON_ESGUI_PAGE_WIFI_SCAN_H

#include "ESGUI.h"

/**
 * @brief 创建 WiFi 扫描列表页（压入页面栈）
 *
 * 进入后自动发起异步扫描，结果到达后自动重建列表。
 * 选中 AP 条目进入连接弹窗；末尾提供"重新扫描"。
 *
 * @return ACT_PUSH_PAGE 动作（param 为页面指针）
 */
ESGUI_MenuAction_T page_wifi_scan_create(void);

#endif //BASED_ON_ESGUI_PAGE_WIFI_SCAN_H
