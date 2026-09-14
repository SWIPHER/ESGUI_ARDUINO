//
// Created by E_LJF on 2026/8/24.
//

#ifndef BASED_ON_ESGUI_WIFI_SETTING_PAGE_H
#define BASED_ON_ESGUI_WIFI_SETTING_PAGE_H

#include "ESGUI.h"
#include "wifi_mgr.h"

extern ESGUI_MenuPage_T wifi_setting_page;
ESGUI_MenuAction_T wifi_setting_page_create(wifi_mgr_mode_t mode,wifi_mgr_ap_cfg_t *ap_cfg,wifi_mgr_sta_cfg_t *sta_cfg);

#endif //BASED_ON_ESGUI_WIFI_SETTING_PAGE_H
