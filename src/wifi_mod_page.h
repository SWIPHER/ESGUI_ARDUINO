//
// Created by E_LJF on 2026/8/22.
//

#ifndef S3ESGUI_FIRST_PAGE_H
#define S3ESGUI_FIRST_PAGE_H

#include "ESGUI.h"
#include "wifi_mgr.h"

extern ESGUI_MenuPage_T wifi_mod_page;
void wifi_mod_page_init(ESGUI_T *ui);
ESGUI_T *wifi_mod_page_get_eui(void);

/**
 * @brief 获取首页持有的 WiFi 总配置（连接/保存配置用）
 * @return 指向 first_page 内部 wifi_mgr_config_t 的指针
 */
wifi_mgr_config_t *wifi_mod_page_get_wifi_cfg(void);

#endif //S3ESGUI_FIRST_PAGE_H
