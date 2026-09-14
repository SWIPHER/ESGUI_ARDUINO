//
// Created by E_LJF on 2026/8/24.
//

#include "wifi_app.h"
#include "ESGUI_PageDefaltVtbl.h"
#include "string.h"
#include "stdio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"

#define TAG "wifi_app"

/* 由 first_page.c 提供：页面持有的 WiFi 总配置（连接成功后保存用） */
#include "wifi_mod_page.h"

/* UI 实例（ESGUI_Task.c 定义）：供工作线程判断 UI 忙闲状态 */

/* NVS 存储键 */
#define WIFI_APP_NVS_NS      "wifi_cfg"
#define WIFI_APP_NVS_MODE    "mode"
#define WIFI_APP_NVS_AP      "ap"
#define WIFI_APP_NVS_STA     "sta"

/* 客户端监控刷新周期（ms） */
#define WIFI_APP_CLIENTS_PERIOD_MS   2000

ESGUI_ProducerBox_T wifi_app_producers;

/* 弹窗实例（长文本消息 / 普通消息各一，避免同时占用冲突） */
static ESGUI_PopWindow_T s_long_popup;
static ESGUI_PopWindow_T s_msg_popup;

/* 数据访问互斥锁（工作线程写 / UI 线程读） */
static SemaphoreHandle_t s_data_mutex;

/* ---- 扫描状态 ---- */
static bool s_scan_busy;
static bool s_scan_failed;
static wifi_app_scan_entry_t s_scan_results[WIFI_APP_MAX_SCAN_RESULTS];
static uint16_t s_scan_count;

/* ---- 已连接设备监控 ---- */
static bool s_monitor_running;
static bool s_monitor_stop;
static wifi_app_client_entry_t s_clients[WIFI_APP_MAX_CLIENTS];
static uint16_t s_client_count;

/* ---- 连接状态 ---- */
static bool s_expect_connect;        /* 用户主动发起过连接 */
static bool s_user_disconnect;       /* 用户主动断开：断开事件后通知扫描页刷新标记 */
static uint16_t s_disconnect_count;  /* 主动连接后的断连次数（≥2 判定失败，避免重配置断连误报） */
static esp_ip4_addr_t s_last_ip;

/* ---- 延迟状态通知 ----
 * ESGUI 在页面/弹窗过渡（pending_push/pending_pop）期间会丢弃所有按键事件，
 * 而断开连接/踢设备的刷新事件恰好都发生在弹窗关闭动画（400ms）窗口内，
 * 立即推送必然被丢弃。用一次性软定时器在过渡结束后再推送，保证事件送达。 */
#define WIFI_APP_NOTIFY_DELAY_MS    600
static TimerHandle_t s_notify_timer;
static ESGUI_EventCode_t s_notify_evt;

static void wifi_app_notify_timer_cb(TimerHandle_t t)
{
    ESGUI_ProducerBoxPushKey(&wifi_app_producers, s_notify_evt, 0);
}

static void wifi_app_notify_delayed(ESGUI_EventCode_t evt)
{
    if (s_notify_timer == NULL) {
        s_notify_timer = xTimerCreate("wifi_notify",
                                      pdMS_TO_TICKS(WIFI_APP_NOTIFY_DELAY_MS),
                                      pdFALSE, NULL, wifi_app_notify_timer_cb);
        if (s_notify_timer == NULL) {
            ESGUI_ProducerBoxPushKey(&wifi_app_producers, evt, 0);  /* 兜底：立即推送 */
            return;
        }
    }
    s_notify_evt = evt;
    xTimerReset(s_notify_timer, 0);
}


/* ==================== 内部工具 ==================== */

void wifi_app_init(void)
{
    if (s_data_mutex == NULL) {
        s_data_mutex = xSemaphoreCreateMutex();
        if (s_data_mutex == NULL) {
            ESP_LOGE(TAG, "Mutex create failed");
        }
    }
}

static void wifi_app_show_msg_popup(const char *message)
{
    ESGUI_DefaultMessagePopWindowCreate(&s_msg_popup, message, 100, 50, 1);
    ESGUI_ProducerBoxPushAction(&wifi_app_producers,
                                (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &s_msg_popup});
}

/* 确保 NVS 已初始化（wifi_mgr 可能已初始化，忽略重复初始化） */
static esp_err_t wifi_app_nvs_ensure(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}


/* ==================== 扫描 ==================== */

static void wifi_app_scan_task(void *arg)
{
    wifi_ap_record_t records[WIFI_APP_MAX_SCAN_RESULTS];
    wifi_app_scan_entry_t tmp[WIFI_APP_MAX_SCAN_RESULTS];
    uint16_t n = WIFI_APP_MAX_SCAN_RESULTS;
    uint16_t tmp_count = 0;

    esp_err_t err = wifi_mgr_scan(records, &n, WIFI_APP_MAX_SCAN_RESULTS);

    if (err == ESP_OK) {
        for (uint16_t i = 0; i < n; i++) {
            if (records[i].ssid[0] == '\0') {
                continue;                                   /* 隐藏网络/空 SSID 跳过 */
            }
            /* 同 SSID 去重，保留信号最强 */
            uint16_t j = 0;
            for (; j < tmp_count; j++) {
                if (strcmp((const char *)records[i].ssid, tmp[j].ssid) == 0) {
                    break;
                }
            }
            if (j < tmp_count) {
                if (records[i].rssi > tmp[j].rssi) {
                    tmp[j].rssi = records[i].rssi;
                    tmp[j].authmode = records[i].authmode;
                    tmp[j].channel = records[i].primary;
                }
            } else if (tmp_count < WIFI_APP_MAX_SCAN_RESULTS) {
                memset(&tmp[tmp_count], 0, sizeof(tmp[tmp_count]));
                strncpy(tmp[tmp_count].ssid, (const char *)records[i].ssid,
                        sizeof(tmp[tmp_count].ssid) - 1);
                tmp[tmp_count].rssi = records[i].rssi;
                tmp[tmp_count].authmode = records[i].authmode;
                tmp[tmp_count].channel = records[i].primary;
                tmp_count++;
            }
        }
        /* 按信号强度降序（选择排序） */
        for (uint16_t i = 0; i + 1 < tmp_count; i++) {
            uint16_t best = i;
            for (uint16_t j = i + 1; j < tmp_count; j++) {
                if (tmp[j].rssi > tmp[best].rssi) {
                    best = j;
                }
            }
            if (best != i) {
                wifi_app_scan_entry_t t = tmp[i];
                tmp[i] = tmp[best];
                tmp[best] = t;
            }
        }
    }

    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    if (err == ESP_OK) {
        memcpy(s_scan_results, tmp, sizeof(tmp[0]) * tmp_count);
        s_scan_count = tmp_count;
        s_scan_failed = false;
    } else {
        s_scan_count = 0;
        s_scan_failed = true;
        ESP_LOGW(TAG, "Scan failed: %s", esp_err_to_name(err));
    }
    s_scan_busy = false;
    xSemaphoreGive(s_data_mutex);

    /* 扫描结果发布（release 语义，UI 读到事件时结果已完整写入） */
    ESGUI_ProducerBoxPushKey(&wifi_app_producers, WIFI_APP_EVT_SCAN_DONE, 0);
    vTaskDelete(NULL);
}

esp_err_t wifi_app_start_scan(void)
{
    if (s_data_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    if (s_scan_busy) {
        xSemaphoreGive(s_data_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    s_scan_busy = true;
    s_scan_failed = false;
    xSemaphoreGive(s_data_mutex);

    if (xTaskCreate(wifi_app_scan_task, "wifi_scan", 4096, NULL, 5, NULL) != pdPASS) {
        xSemaphoreTake(s_data_mutex, portMAX_DELAY);
        s_scan_busy = false;
        xSemaphoreGive(s_data_mutex);
        ESP_LOGE(TAG, "Scan task create failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool wifi_app_scan_busy(void)
{
    return s_scan_busy;
}

bool wifi_app_scan_failed(void)
{
    return s_scan_failed;
}

const wifi_app_scan_entry_t *wifi_app_get_scan_results(uint16_t *count)
{
    if (count == NULL) {
        return NULL;
    }
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    *count = s_scan_count;
    xSemaphoreGive(s_data_mutex);
    return s_scan_results;
}


/* ==================== 已连接设备监控 ==================== */

static void wifi_app_clients_monitor_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(WIFI_APP_CLIENTS_PERIOD_MS));
        if (s_monitor_stop) {
            break;
        }

        wifi_mgr_sta_info_t list[WIFI_APP_MAX_CLIENTS];
        uint16_t n = WIFI_APP_MAX_CLIENTS;
        if (wifi_mgr_get_sta_list(list, &n, WIFI_APP_MAX_CLIENTS) != ESP_OK) {
            continue;                                       /* WiFi 未运行/非 AP，跳过本轮 */
        }

        xSemaphoreTake(s_data_mutex, portMAX_DELAY);
        for (uint16_t i = 0; i < n; i++) {
            memcpy(s_clients[i].mac, list[i].mac, sizeof(list[i].mac));
            s_clients[i].ip = list[i].ip;
            s_clients[i].rssi = list[i].rssi;
            s_clients[i].ip_valid = (list[i].ip.addr != 0);
        }
        s_client_count = n;
        xSemaphoreGive(s_data_mutex);

        ESGUI_ProducerBoxPushKey(&wifi_app_producers, WIFI_APP_EVT_CLIENTS_UPDATE, 0);
    }
    s_monitor_running = false;
    vTaskDelete(NULL);
}

void wifi_app_clients_monitor_start(void)
{
    if (s_monitor_running) {
        s_monitor_stop = false;                     /* 复用已有监控任务 */
        return;
    }
    s_monitor_stop = false;
    if (xTaskCreate(wifi_app_clients_monitor_task, "wifi_cli", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Clients monitor task create failed");
        return;
    }
    s_monitor_running = true;
}

void wifi_app_clients_monitor_stop(void)
{
    s_monitor_stop = true;
}

const wifi_app_client_entry_t *wifi_app_get_clients(uint16_t *count)
{
    if (count == NULL) {
        return NULL;
    }
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    *count = s_client_count;
    xSemaphoreGive(s_data_mutex);
    return s_clients;
}


/* ==================== 连接 ==================== */

esp_err_t wifi_app_connect(wifi_mgr_config_t *cfg, const char *ssid,
                           const char *password, wifi_auth_mode_t authmode)
{
    if (cfg == NULL || cfg->sta == NULL || ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* cfg->sta 声明为 const 指针，但实际指向调用方（页面）持有的可写配置 */
    wifi_mgr_sta_cfg_t *sta = (wifi_mgr_sta_cfg_t *)cfg->sta;

    strncpy(sta->ssid, ssid, sizeof(sta->ssid) - 1);
    sta->ssid[sizeof(sta->ssid) - 1] = '\0';
    if (password != NULL) {
        strncpy(sta->password, password, sizeof(sta->password) - 1);
        sta->password[sizeof(sta->password) - 1] = '\0';
    } else {
        sta->password[0] = '\0';
    }
    sta->authmode = authmode;
    sta->auto_connect = true;

    s_expect_connect = true;
    s_disconnect_count = 0;

    esp_err_t ret = wifi_mgr_update_config(cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Connect failed: %s", esp_err_to_name(ret));
        s_expect_connect = false;
    }
    return ret;
}

bool wifi_app_is_connected_to(const char *ssid)
{
    if (ssid == NULL || !wifi_mgr_is_connected()) {
        return false;
    }
    wifi_mgr_config_t *cfg = wifi_mod_page_get_wifi_cfg();
    if (cfg == NULL || cfg->sta == NULL || cfg->sta->ssid[0] == '\0') {
        return false;
    }
    return (strcmp(ssid, cfg->sta->ssid) == 0);
}

esp_err_t wifi_app_disconnect(void)
{
    if (!wifi_mgr_is_connected()) {
        return ESP_OK;                              /* 未连接，无操作 */
    }
    /* 先置标志：断开事件回调（on_disconnected）据此推送列表刷新事件 */
    s_user_disconnect = true;
    esp_err_t ret = wifi_mgr_sta_disconnect();
    /* 弹窗关闭过渡期间推送的事件会被 ESGUI 丢弃，延迟推送保证扫描页能收到 */
    wifi_app_notify_delayed(WIFI_APP_EVT_CONNECTED);
    return ret;
}

void wifi_app_clients_refresh_delayed(void)
{
    wifi_app_notify_delayed(WIFI_APP_EVT_CLIENTS_UPDATE);
}

/* ---- AP 踢设备（持续踢，防客户端秒重连） ----
 * 单次 deauth 后手机等客户端会立即自动重连，视觉上"没反应"。
 * 踢人后按固定周期重复 deauth，持续一段时间把设备按住断线。 */
#define WIFI_APP_KICK_PERIOD_MS   300
#define WIFI_APP_KICK_TOTAL_MS    15000
static bool s_kick_busy;
static uint8_t s_kick_mac[6];

static void wifi_app_kick_task(void *arg)
{
    uint32_t count = WIFI_APP_KICK_TOTAL_MS / WIFI_APP_KICK_PERIOD_MS;
    for (uint32_t i = 0; i < count; i++) {
        esp_err_t ret = wifi_mgr_deauth_sta(s_kick_mac);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Kick deauth ret=%s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(WIFI_APP_KICK_PERIOD_MS));
    }
    s_kick_busy = false;
    vTaskDelete(NULL);
}

esp_err_t wifi_app_kick_sta(const uint8_t *mac)
{
    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_kick_busy) {
        return ESP_ERR_INVALID_STATE;               /* 已有踢人任务在运行 */
    }
    memcpy(s_kick_mac, mac, sizeof(s_kick_mac));
    s_kick_busy = true;
    if (xTaskCreate(wifi_app_kick_task, "wifi_kick", 3072, NULL, 4, NULL) != pdPASS) {
        s_kick_busy = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}


/* ==================== 回调 ==================== */

static void wifi_app_on_got_ip(esp_ip4_addr_t ip)
{
    s_last_ip = ip;
}

static void wifi_app_on_connected(void)
{
    bool was_explicit = s_expect_connect;
    s_expect_connect = false;
    s_disconnect_count = 0;

    /* 连接成功：立即保存配置（下次开机自动连接用） */
    wifi_mgr_config_t *cfg = wifi_mod_page_get_wifi_cfg();
    if (cfg != NULL) {
        wifi_app_config_save(cfg);
    }

    /* 连接状态变化：通知扫描页刷新"已连接"标记。
     * 必须先于成功弹窗推送（弹窗弹出后按键事件会路由到弹窗，扫描页收不到），
     * 且此时 CONNECTED_BIT 已置位、cfg->sta->ssid 已更新，is_connected_to 可正确判定 */
    ESGUI_ProducerBoxPushKey(&wifi_app_producers, WIFI_APP_EVT_CONNECTED, 0);

    /* 仅用户主动连接时弹窗提示 */
    if (was_explicit) {
        /* 弹窗只保存消息指针不拷贝文本，且经 producer box 异步弹出，
         * 必须用 static 缓冲区，栈局部变量会在本函数返回后失效 */
        static char msg[64];
        snprintf(msg, sizeof(msg), "连接成功\nIP: " IPSTR, IP2STR(&s_last_ip));
        wifi_app_show_msg_popup(msg);
    }
}

static void wifi_app_on_disconnected(void)
{
    /* 用户主动断开：通知扫描页移除"已连接"标记。
     * 事件经 producer box 由 UI 线程稍后处理，届时 CONNECTED_BIT 已清除，
     * is_connected_to 判定正确 */
    if (s_user_disconnect) {
        s_user_disconnect = false;
        ESGUI_ProducerBoxPushKey(&wifi_app_producers, WIFI_APP_EVT_CONNECTED, 0);
    }
    /* 仅在用户主动连接期间处理；扫描/自动重连/重配置断连不打扰 */
    if (!s_expect_connect) {
        return;
    }
    /* 断连 ≥2 次判定为连接失败（第 1 次可能是应用新配置时的主动断开） */
    s_disconnect_count++;
    if (s_disconnect_count >= 2) {
        s_expect_connect = false;
        wifi_app_show_msg_popup("连接失败\n请检查密码或信号");
    }
}

static void wifi_app_on_ap_staconnected(uint8_t mac[6], uint8_t aid)
{
    /* UI 忙（页面切换过渡中 / 已有弹窗显示）时丢弃本次提示。
     * 否则弹窗推送会打断页面退出流程：ExecPendingPop 看到栈顶弹窗只会关弹窗、
     * 不弹页面，用户再按返回又触发新的推送，形成"关弹窗-退页面-弹窗-退出失败"循环。
     * 设备连接通知非关键信息，用户可在"已连接设备"页查看。 */
    if (wifi_mod_page_get_eui()->menu_ctrl.pending_pop || wifi_mod_page_get_eui()->menu_ctrl.pending_push || wifi_mod_page_get_eui()->menu_ctrl.pop_depth > 0) {
        ESP_LOGI(TAG, "AP sta connected notification dropped (UI busy)");
        return;
    }
    static char message[50];
    memset(message, 0, sizeof(message));
    sprintf(message, "有新设备连接！MAC: %02x%02x%02x%02x%02x%02x\naid:%d",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], aid);
    ESGUI_DefaultMessageLongTextPopWindowCreate(&s_long_popup, message, 100, 50);
    ESGUI_ProducerBoxPushAction(&wifi_app_producers,
                                (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &s_long_popup});
}

const wifi_mgr_callbacks_t wifi_mgr_callbacks =
{
    .on_connected = wifi_app_on_connected,
    .on_disconnected = wifi_app_on_disconnected,
    .on_got_ip = wifi_app_on_got_ip,
    .on_ap_started = ESGUI_NULL,
    .on_ap_staconnected = wifi_app_on_ap_staconnected,
    .on_ap_stadisconnected = ESGUI_NULL
};


/* ==================== 配置持久化 ==================== */

esp_err_t wifi_app_config_save(const wifi_mgr_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = wifi_app_nvs_ensure();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t h;
    err = nvs_open(WIFI_APP_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t mode = (uint8_t)cfg->mode;
    nvs_set_u8(h, WIFI_APP_NVS_MODE, mode);
    if (cfg->ap != NULL) {
        nvs_set_blob(h, WIFI_APP_NVS_AP, cfg->ap, sizeof(wifi_mgr_ap_cfg_t));
    }
    if (cfg->sta != NULL) {
        nvs_set_blob(h, WIFI_APP_NVS_STA, cfg->sta, sizeof(wifi_mgr_sta_cfg_t));
    }
    err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi config saved (mode=%d)", mode);
    } else {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t wifi_app_config_load(wifi_mgr_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = wifi_app_nvs_ensure();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t h;
    err = nvs_open(WIFI_APP_NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;                                         /* 无保存记录 */
    }

    uint8_t mode = 0;
    err = nvs_get_u8(h, WIFI_APP_NVS_MODE, &mode);
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }
    cfg->mode = (wifi_mgr_mode_t)mode;

    size_t len;
    if (cfg->ap != NULL) {
        /* cfg->ap 声明为 const 指针，但实际指向调用方提供的可写缓冲区 */
        len = sizeof(wifi_mgr_ap_cfg_t);
        if (nvs_get_blob(h, WIFI_APP_NVS_AP, (void *)cfg->ap, &len) != ESP_OK) {
            memset((void *)cfg->ap, 0, sizeof(wifi_mgr_ap_cfg_t));
        }
    }
    if (cfg->sta != NULL) {
        len = sizeof(wifi_mgr_sta_cfg_t);
        if (nvs_get_blob(h, WIFI_APP_NVS_STA, (void *)cfg->sta, &len) != ESP_OK) {
            memset((void *)cfg->sta, 0, sizeof(wifi_mgr_sta_cfg_t));
        }
    }
    nvs_close(h);

    ESP_LOGI(TAG, "WiFi config loaded (mode=%d)", mode);
    return ESP_OK;
}
