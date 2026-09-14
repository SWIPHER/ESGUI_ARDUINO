//
// Created by E_LJF on 2026/8/24.
//

#include "wifi_setting_page.h"

#include <string.h>

#include "ESGUI_PageDefaltVtbl.h"
#include "ESGUI_Widget.h"
#include "wifi_app.h"
#include "esp_log.h"

typedef struct  _setting_page_dat {
    wifi_mgr_mode_t mode;
    wifi_mgr_ap_cfg_t *wifi_ap_cfg;
    wifi_mgr_sta_cfg_t *wifi_sta_cfg;
}WIFISettingPageData_T;

ESGUI_MenuPage_T wifi_setting_page;
static ESGUI_PopWindow_T pop_window;
static WIFISettingPageData_T wifi_setting_page_data;
static const char str_list[18][21] = {{"否"},{"是"},
                                    {"OPEN"},{"WEP"},{"WPA"},{"WPA2"},{"WPA_WPA2"},
                                    {"WPA2_ENTERPRISE"},{"WPA3"},{"WPA2_WPA3"},{"WAPI"},
                                    {"OWE"},{"WPA3_ENT_192"},{"DPP"},{"WPA3_ENTERPRISE"},
                                    {"WPA2_WPA3_ENTERPRISE"},{"WPA_ENTERPRISE"}};

static eui_int8_t value_max,value_min;


/**
     * @brief 获取当前值的千分比位置
     * @param ctx  用户数据指针
     * @return     0~1000 的千分比，用于进度条显示
     */
static eui_uint16_t ValueDesc_int8_get_permille(void *ctx) {
    if (ctx == ESGUI_NULL) return 0;
    if (*(eui_int8_t*)ctx <= 0) {
        return  -*(eui_int8_t*)ctx * 1000 / value_max;
    }
    return  *(eui_int8_t*)ctx * 1000 / value_max;
}


/**
     * @brief 将当前值格式化为显示字符串
     * @param ctx   用户数据指针
     * @param buf   输出缓冲区
     * @param size  缓冲区大小
     * @return      实际写入长度
     *
     * 示例：int 值 → "123"，float → "3.14"，枚举 → "模式A"
     */
static eui_uint8_t ValueDesc_int8_to_string(void *ctx, char *buf, eui_uint16_t size) {
    if (ctx == ESGUI_NULL) return 0;
    snprintf(buf, size, "%d", *(eui_int8_t*)ctx);
    return strlen(buf);
}




/**
 * @brief 步进值
 * @param ctx       用户数据指针
 * @param direction +1=增加，-1=减少
 * @return          true=值发生了变化；false=到达边界无法继续
 *
 * 用户在此函数内部实现限幅、循环、步长控制等逻辑
 */
static bool ValueDesc_int8_step(void *ctx, eui_int8_t direction) {
    if (ctx == ESGUI_NULL) return false;

    eui_int8_t val = *((eui_int8_t*)ctx);

    if (direction > 0) {
        val += val < value_max ? 1 : 0;
    }else {
        val -= val > value_min ? 1 : 0;
    }

    *(eui_int8_t*)ctx =val;

    return true;
}



//uint_16类型对应的值描述符
static ESGUI_ValueDesc_T value_desc =
{
    .ctx = ESGUI_NULL,
    .get_permille = ValueDesc_int8_get_permille,
    .to_string = ValueDesc_int8_to_string,
    .step = ValueDesc_int8_step,
};





/* 文本条目对应的目标缓冲容量：SSID=33 / 密码=65（见 wifi_mgr.h）。
 * 原实现写死 32 → SSID/密码最长只能存 31 字符，长密码（WPA2 最长 63）输不进去，
 * 现按字段实际容量传入，配合 ESGUI_KEYBOARD_EDIT_MAX_LEN=64。 */
static eui_uint16_t keyb_target_size(const void *arg) {
    WIFISettingPageData_T *dat = &wifi_setting_page_data;
    if (dat->wifi_ap_cfg != ESGUI_NULL) {
        if (arg == (const void *)dat->wifi_ap_cfg->ssid)     return sizeof(dat->wifi_ap_cfg->ssid);
        if (arg == (const void *)dat->wifi_ap_cfg->password) return sizeof(dat->wifi_ap_cfg->password);
    }
    if (dat->wifi_sta_cfg != ESGUI_NULL) {
        if (arg == (const void *)dat->wifi_sta_cfg->ssid)     return sizeof(dat->wifi_sta_cfg->ssid);
        if (arg == (const void *)dat->wifi_sta_cfg->password) return sizeof(dat->wifi_sta_cfg->password);
    }
    return 32;                          /* 兜底：非 SSID/密码条目保持原行为 */
}

static ESGUI_MenuAction_T wifi_text_item_on_enter(ESGUI_MenuPage_T *page,void *arg) {
    /* 弹窗 232x140：键高 24 × 字母页 4 行 = 96px、数字页 5 行 = 120px，加输入框 19px 后都不滚动 */
    ESGUI_DefaultKeyBoardPopWindowCreate(&pop_window,232,140,arg,keyb_target_size(arg),arg);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP,&pop_window};
}



static ESGUI_MenuAction_T wifi_num_item_on_enter(ESGUI_MenuPage_T *page,void *arg) {
    value_desc.ctx = arg;
    if (wifi_setting_page_data.mode == WIFI_MGR_MODE_AP) {
        if (page->focus_idx == 2){value_max = 4;value_min = 1;}
        if (page->focus_idx == 3){value_max = 10;value_min = 1;}
    }else if (wifi_setting_page_data.mode == WIFI_MGR_MODE_STA) {
        if (page->focus_idx == 3){value_max = 100;value_min = -1;}
    }else if (wifi_setting_page_data.mode == WIFI_MGR_MODE_APSTA) {
        if (page->focus_idx == 2){value_max = 4;value_min = 1;}
        if (page->focus_idx == 3){value_max = 10;value_min = 1;}
        if (page->focus_idx == 10){value_max = 100;value_min = -1;}
    }
    ESGUI_DefaultValuePopWindowCreate(&pop_window,"     值修改",100,50,&value_desc);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP,&pop_window};
}


static ESGUI_MenuAction_T wifi_bool_item_on_enter(ESGUI_MenuPage_T *page,void *arg) {
    if (wifi_setting_page_data.mode == WIFI_MGR_MODE_AP) {
        if (page->focus_idx == 5) {
            ESGUI_DefaultBoolPopWindowCreate(&pop_window,"      是否隐藏",str_list[1],str_list[0],100,50,arg);
        }
    }else if (wifi_setting_page_data.mode == WIFI_MGR_MODE_STA) {
        if (page->focus_idx == 4) {
            ESGUI_DefaultBoolPopWindowCreate(&pop_window,"      是否自动连接",str_list[1],str_list[0],100,50,arg);
        }
    }else if (wifi_setting_page_data.mode == WIFI_MGR_MODE_APSTA) {
        if (page->focus_idx == 5) {
            ESGUI_DefaultBoolPopWindowCreate(&pop_window,"      是否隐藏",str_list[1],str_list[0],100,50,arg);
        }
        if (page->focus_idx == 11) {
            ESGUI_DefaultBoolPopWindowCreate(&pop_window,"      是否自动连接",str_list[1],str_list[0],100,50,arg);
        }
    }
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP,&pop_window};
}



static ESGUI_MenuAction_T authmode_pop_window_item_on_enter(ESGUI_MenuPage_T *page,void *arg) {
    if (wifi_setting_page_data.mode == WIFI_MGR_MODE_APSTA || wifi_setting_page_data.mode == WIFI_MGR_MODE_AP) {
        if (wifi_setting_page.focus_idx < 6){
            wifi_setting_page_data.wifi_ap_cfg->authmode = page->focus_idx;
        }else {
            wifi_setting_page_data.wifi_sta_cfg->authmode = page->focus_idx;
        }
    }else if (wifi_setting_page_data.mode == WIFI_MGR_MODE_STA) {
        wifi_setting_page_data.wifi_sta_cfg->authmode = page->focus_idx;
    }

    return (ESGUI_MenuAction_T){ACT_CLOSE_POPUP,ESGUI_NULL};
}


static ESGUI_MenuItem_T authmode_pop_window_item[] =
    {
    {0,0,str_list[2],ESGUI_NULL,authmode_pop_window_item_on_enter,ESGUI_NULL},
    {0,0,str_list[3],ESGUI_NULL,authmode_pop_window_item_on_enter,ESGUI_NULL},
    {0,0,str_list[4],ESGUI_NULL,authmode_pop_window_item_on_enter,ESGUI_NULL},
    {0,0,str_list[5],ESGUI_NULL,authmode_pop_window_item_on_enter,ESGUI_NULL},
    {0,0,str_list[6],ESGUI_NULL,authmode_pop_window_item_on_enter,ESGUI_NULL},
    {0,0,str_list[7],ESGUI_NULL,authmode_pop_window_item_on_enter,ESGUI_NULL},
    {0,0,str_list[8],ESGUI_NULL,authmode_pop_window_item_on_enter,ESGUI_NULL},
    {0,0,str_list[9],ESGUI_NULL,authmode_pop_window_item_on_enter,ESGUI_NULL},
    {0,0,str_list[10],ESGUI_NULL,authmode_pop_window_item_on_enter,ESGUI_NULL},
    {0,0,str_list[11],ESGUI_NULL,authmode_pop_window_item_on_enter,ESGUI_NULL},
    {0,0,str_list[12],ESGUI_NULL,authmode_pop_window_item_on_enter,ESGUI_NULL},
    {0,0,str_list[13],ESGUI_NULL,authmode_pop_window_item_on_enter,ESGUI_NULL},
    {0,0,str_list[14],ESGUI_NULL,authmode_pop_window_item_on_enter,ESGUI_NULL},
    {0,0,str_list[15],ESGUI_NULL,authmode_pop_window_item_on_enter,ESGUI_NULL},
    {0,0,str_list[16],ESGUI_NULL,authmode_pop_window_item_on_enter,ESGUI_NULL},
};



ESGUI_MenuAction_T wifi_authmode_item_on_enter(ESGUI_MenuPage_T *page,void *arg) {
    ESGUI_DefaultTextListPopWindowCreate(&pop_window,100,60,authmode_pop_window_item,ESGUI_ITEM_NUM_COUNT(authmode_pop_window_item));
    if (page->focus_idx < 6) {
        if (wifi_setting_page_data.mode == WIFI_MGR_MODE_AP || wifi_setting_page_data.mode == WIFI_MGR_MODE_APSTA) {
            pop_window.focus_idx = wifi_setting_page_data.wifi_ap_cfg->authmode;
        }else if (wifi_setting_page_data.mode == WIFI_MGR_MODE_STA) {
            pop_window.focus_idx = wifi_setting_page_data.wifi_sta_cfg->authmode;
        }
    }
    else {
        pop_window.focus_idx = wifi_setting_page_data.wifi_sta_cfg->authmode;
    }
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP,&pop_window};
}




static eui_uint16_t wifi_Setting_page_special_draw(ESGUI_MenuPage_T *page, eui_uint16_t indx, bool measure) {
    if (page == ESGUI_NULL) return 0;

    CanvasStripIter *c_it = page->render_ctx;
    ESGUI_MenuItem_T *indx_item = &page->items[indx];
    eui_int16_t item_y = page->items[indx].y;
    eui_uint16_t x_offset = c_it->canvas->width - ESGUI_PROGRESS_BAR_W;
    eui_uint8_t max_w = x_offset - eui_get_text_width(&eui_test_font,indx_item->label) + 3;

    eui_uint8_t w;
    char c;

    if (!ESGUI_WidgetCheckMarker(page->items[indx].label, ESGUI_WIDGET_DEFAULT_MARK, ESGUI_NULL, &c)) {
        return 0;
    }

    switch (c) {
        case '0':
            w = eui_get_text_width(&eui_test_font,indx_item->arg);
            w = (w > max_w) ? max_w : w;
            if (measure) return w;

            eui_draw_text_clip(c_it->canvas,x_offset - w,item_y,&eui_test_font,indx_item->arg,EUI_MODE_SET,w);
            return w;
            break;

        case '1':
            if (measure) return 20;                 /* 测量模式：仅返回占宽，不绘制 */
        {
            char buff[10] = {0};
            sprintf(buff,"%d",*(eui_int8_t*)page->items[indx].arg);
            eui_draw_text(c_it->canvas,
                x_offset - 15,item_y,
                &ESGUI_DEFAULT_FONT,
                buff,
                EUI_MODE_SET);
        }
            return 20;
            break;

        case '2':
            eui_uint8_t idx = *(eui_uint8_t*)(indx_item->arg) + 2;
            w = eui_get_text_width(&eui_test_font,str_list[idx]);
            w = (w > max_w) ? max_w : w;
            if (measure) return w;

            eui_draw_text_clip(c_it->canvas,x_offset - w,item_y,&eui_test_font,str_list[idx],EUI_MODE_SET,w);
            return w;
            break;

        case '3':
            w = eui_get_text_width(&eui_test_font,str_list[*(eui_uint8_t*)(indx_item->arg)]);
            w = (w > max_w) ? max_w : w;
            if (measure) return w;

            eui_draw_text_clip(c_it->canvas,x_offset - w,item_y,&eui_test_font,str_list[*(eui_uint8_t*)(indx_item->arg)],EUI_MODE_SET,w);
            return w;
            break;

        default:
            return 0;
    }
    return 0;
}



static ESGUI_MenuItem_T wifi_setting_item[] =
{
    {0,0,"AP SSID\x03/0",ESGUI_NULL,wifi_text_item_on_enter,ESGUI_NULL},
    {0,0,"密码:\x03/0",ESGUI_NULL,wifi_text_item_on_enter,ESGUI_NULL},
    {0,0,"信道:\x03/1",ESGUI_NULL,wifi_num_item_on_enter,ESGUI_NULL},
    {0,0,"最大连接数:\x03/1",ESGUI_NULL,wifi_num_item_on_enter,ESGUI_NULL},
    {0,0,"认证模式:\x03/2",ESGUI_NULL,wifi_authmode_item_on_enter,ESGUI_NULL},
    {0,0,"是否隐藏:\x03/3",ESGUI_NULL,wifi_bool_item_on_enter,ESGUI_NULL},

    {0,0,"========================",ESGUI_NULL,ESGUI_NULL,ESGUI_NULL},

    {0,0,"STA SSID\x03/0",ESGUI_NULL,wifi_text_item_on_enter,ESGUI_NULL},
    {0,0,"密码:\x03/0",ESGUI_NULL,wifi_text_item_on_enter,ESGUI_NULL},
    {0,0,"认证模式:\x03/2",ESGUI_NULL,wifi_authmode_item_on_enter,ESGUI_NULL},
    {0,0,"最大重试次数:\x03/1",ESGUI_NULL,wifi_num_item_on_enter,ESGUI_NULL},
    {0,0,"是否自动连接:\x03/3",ESGUI_NULL,wifi_bool_item_on_enter,ESGUI_NULL},
};


/* 退出配置页：应用配置（WiFi 已开启时立即生效）并保存到 NVS */
static void wifi_setting_page_apply_on_exit(void)
{
    wifi_mgr_config_t cfg = {0};
    cfg.mode = wifi_setting_page_data.mode;
    cfg.ap = wifi_setting_page_data.wifi_ap_cfg;
    cfg.sta = wifi_setting_page_data.wifi_sta_cfg;
    cfg.cb = &wifi_mgr_callbacks;
    cfg.init_nvs = true;

    esp_err_t ret = wifi_mgr_update_config(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("wifi_setting", "Apply config failed: %s", esp_err_to_name(ret));
    }
    /* 无论 WiFi 是否开启都保存（未开启时下次开启生效） */
    wifi_app_config_save(&cfg);
}

static void wifi_Setting_page_on_page_change(ESGUI_MenuPage_T *page, ESGUI_MenuAction_T *action)
{
    if (action != ESGUI_NULL && action->act == ACT_POP_PAGE) {
        wifi_setting_page_apply_on_exit();
    }
    esgui_text_menu_default_on_page_change(page, action);
}


static const esgui_page_vtable_t wifi_Setting_page_vtable = {
    .on_create                = esgui_text_menu_defalt_on_create,
    .on_destroy               = esgui_text_menu_defalt_on_destroy,
    .on_draw                  = esgui_text_menu_defalt_on_draw,
    .on_focus_change          = esgui_text_menu_defalt_on_focus_change,
    .on_input                 = esgui_menu_defalt_on_input,
    .special_item_draw        = wifi_Setting_page_special_draw,
    .on_page_chenge           = wifi_Setting_page_on_page_change,
#if ESGUI_ENABLE_MENU_RUNTIME_ITEMS
    .on_relayout              = esgui_text_menu_relayout,
#endif
};



ESGUI_MenuAction_T wifi_setting_page_create(wifi_mgr_mode_t mode,wifi_mgr_ap_cfg_t *ap_cfg,wifi_mgr_sta_cfg_t *sta_cfg) {
    eui_uint8_t item_num = 0;
    ESGUI_MenuItem_T *item = ESGUI_NULL;
    memset(&wifi_setting_page_data,0,sizeof(WIFISettingPageData_T));

    switch (mode) {
        case WIFI_MGR_MODE_AP:
            if (ap_cfg == ESGUI_NULL) {
                return (ESGUI_MenuAction_T){ACT_NONE,ESGUI_NULL};
            }
            wifi_setting_page_data.wifi_ap_cfg = ap_cfg;
            item = &wifi_setting_item[0];
            item_num = 6;
            wifi_setting_page_data.mode = WIFI_MGR_MODE_AP;
            break;

        case WIFI_MGR_MODE_STA:
            if (sta_cfg == ESGUI_NULL) {
                return (ESGUI_MenuAction_T){ACT_NONE,ESGUI_NULL};
            }
            wifi_setting_page_data.wifi_sta_cfg = sta_cfg;
            item = &wifi_setting_item[7];
            item_num = 5;
            wifi_setting_page_data.mode = WIFI_MGR_MODE_STA;
            break;

        case WIFI_MGR_MODE_APSTA:
            if (ap_cfg == ESGUI_NULL || sta_cfg == ESGUI_NULL) {
                return (ESGUI_MenuAction_T){ACT_NONE,ESGUI_NULL};
            }
            wifi_setting_page_data.wifi_ap_cfg = ap_cfg;
            wifi_setting_page_data.wifi_sta_cfg = sta_cfg;
            item = &wifi_setting_item[0];
            item_num = 12;
            wifi_setting_page_data.mode = WIFI_MGR_MODE_APSTA;
            break;

        default:
            break;
    }
    ESGUI_DefaltTextMenuCreate(&wifi_setting_page,item,"WIFI配置",item_num);
    wifi_setting_page.vtbl = &wifi_Setting_page_vtable;

    if ((mode == WIFI_MGR_MODE_AP) || (mode == WIFI_MGR_MODE_APSTA)) {
        wifi_setting_item[0].arg = (void*)wifi_setting_page_data.wifi_ap_cfg->ssid;
        wifi_setting_item[1].arg = (void*)wifi_setting_page_data.wifi_ap_cfg->password;
        wifi_setting_item[2].arg = &wifi_setting_page_data.wifi_ap_cfg->channel;
        wifi_setting_item[3].arg = &wifi_setting_page_data.wifi_ap_cfg->max_connection;
        wifi_setting_item[4].arg = &wifi_setting_page_data.wifi_ap_cfg->authmode;
        wifi_setting_item[5].arg = &wifi_setting_page_data.wifi_ap_cfg->hidden;
    }
    if ((mode == WIFI_MGR_MODE_STA) || (mode == WIFI_MGR_MODE_APSTA)) {
        wifi_setting_item[7].arg = (void*)wifi_setting_page_data.wifi_sta_cfg->ssid;
        wifi_setting_item[8].arg = (void*)wifi_setting_page_data.wifi_sta_cfg->password;
        wifi_setting_item[9].arg = &wifi_setting_page_data.wifi_sta_cfg->authmode;
        wifi_setting_item[10].arg = &wifi_setting_page_data.wifi_sta_cfg->max_retry;
        wifi_setting_item[11].arg = &wifi_setting_page_data.wifi_sta_cfg->auto_connect;
    }

    return (ESGUI_MenuAction_T){ACT_PUSH_PAGE,&wifi_setting_page};
}
