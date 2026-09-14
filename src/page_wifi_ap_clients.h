//
// Created by E_LJF on 2026/8/25.
//

#ifndef BASED_ON_ESGUI_PAGE_WIFI_AP_CLIENTS_H
#define BASED_ON_ESGUI_PAGE_WIFI_AP_CLIENTS_H

#include "ESGUI.h"

/**
 * @brief 创建"已连接设备"页（压入页面栈）
 *
 * 每 2 秒自动刷新设备列表；选中设备条目弹出详情
 * （设备名称/ MAC 地址 / IP 地址 / 信号强度）。
 *
 * @return ACT_PUSH_PAGE 动作（param 为页面指针）
 */
ESGUI_MenuAction_T page_wifi_ap_clients_create(void);

#endif //BASED_ON_ESGUI_PAGE_WIFI_AP_CLIENTS_H
