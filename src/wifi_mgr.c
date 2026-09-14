//
// Created by E_LJF on 2026/8/23.
//

#include "wifi_mgr.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "rom/ets_sys.h"

#define TAG "wifi_mgr"

/* libesp_wifi 内部导出（wpa_supplicant 内部使用）：按 MAC 强制断开指定 STA。
 * 公开 API esp_wifi_deauth_sta 按 AID 操作，实测对部分客户端（手机）无效；
 * 改用 WiFi 栈内部同款实现，保证踢人真正生效。 */
extern int esp_wifi_ap_deauth_internal(uint8_t *mac, uint32_t reason);

/* 事件组位定义 */
#define WIFI_MGR_CONNECTED_BIT    BIT0
#define WIFI_MGR_FAIL_BIT         BIT1

/* 模块内部状态 */
typedef struct {
    bool initialized;
    wifi_mgr_mode_t mode;
    wifi_mgr_callbacks_t callbacks;
    EventGroupHandle_t event_group;
    esp_netif_t *netif_sta;
    esp_netif_t *netif_ap;
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    SemaphoreHandle_t mutex;
    int retry_count;
    int max_retry;
    esp_ip4_addr_t ip_addr;
    bool own_nvs;          /* 是否由本模块负责 deinit NVS */
    bool sta_auto_connect; /* STA 是否自动连接 */
    bool scan_in_progress; /* 正在扫描：期间禁止 STA 自动重连（防重试循环与扫描抢无线） */
    bool user_disconnect;  /* 用户主动断开：抑制 STA_DISCONNECTED 事件中的自动重连 */
} wifi_mgr_state_t;

static wifi_mgr_state_t s_state = {0};

/* 默认配置 */
#define WIFI_MGR_DEFAULT_STA_AUTHMODE   WIFI_AUTH_WPA2_PSK
#define WIFI_MGR_DEFAULT_STA_MAX_RETRY  5
#define WIFI_MGR_DEFAULT_AP_CHANNEL     1
#define WIFI_MGR_DEFAULT_AP_MAX_CONN    4
#define WIFI_MGR_DEFAULT_AP_AUTHMODE    WIFI_AUTH_WPA_WPA2_PSK

/* 内部事件处理函数 */
static void wifi_mgr_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        /* 只有设置了自动连接时才发起连接 */
        if ((s_state.mode == WIFI_MGR_MODE_STA || s_state.mode == WIFI_MGR_MODE_APSTA)
            && s_state.sta_auto_connect) {
            esp_wifi_connect();
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGI(TAG, "STA disconnected, reason: %d", event->reason);

        if (s_state.callbacks.on_disconnected) {
            s_state.callbacks.on_disconnected();
        }

        xEventGroupClearBits(s_state.event_group, WIFI_MGR_CONNECTED_BIT);

        /* 扫描期间禁止自动重连：连接重试循环会与扫描抢占无线，
         * 导致一次扫描要数分钟（驱动在 STA 连接中状态下不执行扫描） */
        if (s_state.scan_in_progress) {
            return;
        }

        /* 用户主动断开：不再自动重连 */
        if (s_state.user_disconnect) {
            s_state.user_disconnect = false;
            ESP_LOGI(TAG, "User disconnect, skip auto reconnect");
            return;
        }

        if (s_state.max_retry < 0 || s_state.retry_count < s_state.max_retry) {
            esp_wifi_connect();
            s_state.retry_count++;
            ESP_LOGI(TAG, "Retry connecting (%d)...", s_state.retry_count);
        } else {
            xEventGroupSetBits(s_state.event_group, WIFI_MGR_FAIL_BIT);
            ESP_LOGE(TAG, "Connect to AP failed after %d retries", s_state.retry_count);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        memcpy(&s_state.ip_addr, &event->ip_info.ip, sizeof(esp_ip4_addr_t));
        s_state.retry_count = 0;
        xEventGroupSetBits(s_state.event_group, WIFI_MGR_CONNECTED_BIT);

        if (s_state.callbacks.on_got_ip) {
            s_state.callbacks.on_got_ip(event->ip_info.ip);
        }
        if (s_state.callbacks.on_connected) {
            s_state.callbacks.on_connected();
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "AP started");
        if (s_state.callbacks.on_ap_started) {
            s_state.callbacks.on_ap_started();
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " connected, AID=%d",
                 MAC2STR(event->mac), event->aid);
        if (s_state.callbacks.on_ap_staconnected) {
            s_state.callbacks.on_ap_staconnected(event->mac, event->aid);
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " disconnected, AID=%d",
                 MAC2STR(event->mac), event->aid);
        if (s_state.callbacks.on_ap_stadisconnected) {
            s_state.callbacks.on_ap_stadisconnected(event->mac, event->aid);
        }
    }
}

/* 填充 STA 默认配置 */
static void wifi_mgr_fill_sta_defaults(wifi_mgr_sta_cfg_t *cfg)
{
    /* 注意：不要对 authmode 做 0 值覆盖，因为 WIFI_AUTH_OPEN = 0 是合法值。
     * 若需要默认 WPA2_PSK，请使用 WIFI_MGR_STA_CONFIG_DEFAULT() 宏初始化结构体 */
    if (cfg->max_retry == 0) {
        cfg->max_retry = WIFI_MGR_DEFAULT_STA_MAX_RETRY;
    }
}

/* 填充 AP 默认配置 */
static void wifi_mgr_fill_ap_defaults(wifi_mgr_ap_cfg_t *cfg)
{
    if (cfg->channel == 0) {
        cfg->channel = WIFI_MGR_DEFAULT_AP_CHANNEL;
    }
    if (cfg->max_connection == 0) {
        cfg->max_connection = WIFI_MGR_DEFAULT_AP_MAX_CONN;
    }
    /* 注意：不要对 authmode 做 0 值覆盖，因为 WIFI_AUTH_OPEN = 0 是合法值。
     * 若需要默认 WPA_WPA2_PSK，请使用 WIFI_MGR_AP_CONFIG_DEFAULT() 宏初始化结构体 */
}

/* 验证配置合法性 */
static esp_err_t wifi_mgr_validate_config(const wifi_mgr_config_t *cfg)
{
    if (cfg == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->mode != WIFI_MGR_MODE_STA &&
        cfg->mode != WIFI_MGR_MODE_AP &&
        cfg->mode != WIFI_MGR_MODE_APSTA) {
        ESP_LOGE(TAG, "Invalid mode: %d", cfg->mode);
        return ESP_ERR_INVALID_ARG;
    }

    if ((cfg->mode == WIFI_MGR_MODE_STA || cfg->mode == WIFI_MGR_MODE_APSTA) &&
        (cfg->sta == NULL || strlen(cfg->sta->ssid) == 0)) {
        ESP_LOGE(TAG, "STA mode requires valid SSID");
        return ESP_ERR_INVALID_ARG;
    }

    if ((cfg->mode == WIFI_MGR_MODE_AP || cfg->mode == WIFI_MGR_MODE_APSTA) &&
        (cfg->ap == NULL || strlen(cfg->ap->ssid) == 0)) {
        ESP_LOGE(TAG, "AP mode requires valid SSID");
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->sta && strlen(cfg->sta->ssid) > 32) {
        ESP_LOGE(TAG, "STA SSID too long (max 32)");
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->ap && strlen(cfg->ap->ssid) > 32) {
        ESP_LOGE(TAG, "AP SSID too long (max 32)");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/* 内部初始化实现 */
static esp_err_t wifi_mgr_do_init(const wifi_mgr_config_t *cfg)
{
    esp_err_t ret;

    /* 1. 初始化 NVS（WiFi 驱动必须依赖 NVS） */
    if (cfg->init_nvs) {
        // 用户明确要求本模块管理 NVS 生命周期
        ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
            return ret;
        }
        s_state.own_nvs = true;
    } else {
        // 用户未显式要求本模块管理，但 WiFi 驱动必须依赖 NVS。
        // 兜底尝试初始化：如果尚未初始化则帮用户完成，但不由本模块负责 deinit，
        // 避免影响其他可能依赖 NVS 的模块。
        ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
            return ret;
        }
        s_state.own_nvs = false;
    }

    /* 2. 初始化 TCP/IP 协议栈和事件循环 */
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Netif init failed: %s", esp_err_to_name(ret));
        goto cleanup_nvs;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Event loop create failed: %s", esp_err_to_name(ret));
        goto cleanup_netif;
    }

    /* 3. 创建网络接口 */
    if (cfg->mode == WIFI_MGR_MODE_STA || cfg->mode == WIFI_MGR_MODE_APSTA) {
        s_state.netif_sta = esp_netif_create_default_wifi_sta();
        if (s_state.netif_sta == NULL) {
            ESP_LOGE(TAG, "Failed to create STA netif");
            ret = ESP_ERR_NO_MEM;
            goto cleanup_event;
        }
    }

    if (cfg->mode == WIFI_MGR_MODE_AP || cfg->mode == WIFI_MGR_MODE_APSTA) {
        s_state.netif_ap = esp_netif_create_default_wifi_ap();
        if (s_state.netif_ap == NULL) {
            ESP_LOGE(TAG, "Failed to create AP netif");
            ret = ESP_ERR_NO_MEM;
            goto cleanup_sta_netif;
        }
    }

    /* 4. 初始化 WiFi 驱动 */
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&init_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        goto cleanup_ap_netif;
    }

    /* 5. 注册事件处理 */
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &wifi_mgr_event_handler, NULL,
                                              &s_state.instance_any_id);
    if (ret != ESP_OK) goto cleanup_wifi;

    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &wifi_mgr_event_handler, NULL,
                                              &s_state.instance_got_ip);
    if (ret != ESP_OK) goto cleanup_handler_any;

    /* 6. 设置模式 */
    wifi_mode_t mode;
    switch (cfg->mode) {
        case WIFI_MGR_MODE_STA:   mode = WIFI_MODE_STA; break;
        case WIFI_MGR_MODE_AP:    mode = WIFI_MODE_AP; break;
        case WIFI_MGR_MODE_APSTA: mode = WIFI_MODE_APSTA; break;
        default: mode = WIFI_MODE_STA; break;
    }

    ret = esp_wifi_set_mode(mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set mode failed: %s", esp_err_to_name(ret));
        goto cleanup_handler_got_ip;
    }

    /* 7. 配置 STA */
    if (cfg->mode == WIFI_MGR_MODE_STA || cfg->mode == WIFI_MGR_MODE_APSTA) {
        wifi_mgr_sta_cfg_t sta_cfg = *cfg->sta;
        wifi_mgr_fill_sta_defaults(&sta_cfg);

        wifi_config_t wifi_cfg = {0};
        strncpy((char *)wifi_cfg.sta.ssid, sta_cfg.ssid, sizeof(wifi_cfg.sta.ssid) - 1);
        if (strlen(sta_cfg.password) > 0) {
            strncpy((char *)wifi_cfg.sta.password, sta_cfg.password, sizeof(wifi_cfg.sta.password) - 1);
        }
        wifi_cfg.sta.threshold.authmode = sta_cfg.authmode;
        wifi_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

        ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Set STA config failed: %s", esp_err_to_name(ret));
            goto cleanup_handler_got_ip;
        }

        s_state.sta_auto_connect = sta_cfg.auto_connect;
        s_state.max_retry = sta_cfg.max_retry;
        s_state.retry_count = 0;
    }

    /* 8. 配置 AP */
    if (cfg->mode == WIFI_MGR_MODE_AP || cfg->mode == WIFI_MGR_MODE_APSTA) {
        wifi_mgr_ap_cfg_t ap_cfg = *cfg->ap;
        wifi_mgr_fill_ap_defaults(&ap_cfg);

        wifi_config_t wifi_cfg = {0};
        strncpy((char *)wifi_cfg.ap.ssid, ap_cfg.ssid, sizeof(wifi_cfg.ap.ssid) - 1);
        wifi_cfg.ap.ssid_len = strlen(ap_cfg.ssid);
        wifi_cfg.ap.channel = ap_cfg.channel;
        wifi_cfg.ap.max_connection = ap_cfg.max_connection;
        wifi_cfg.ap.authmode = ap_cfg.authmode;

        if (strlen(ap_cfg.password) > 0) {
            strncpy((char *)wifi_cfg.ap.password, ap_cfg.password, sizeof(wifi_cfg.ap.password) - 1);
        } else {
            wifi_cfg.ap.authmode = WIFI_AUTH_OPEN;
        }

        if (ap_cfg.hidden) {
            wifi_cfg.ap.ssid_hidden = 1;
        }

        ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Set AP config failed: %s", esp_err_to_name(ret));
            goto cleanup_handler_got_ip;
        }
    }

    /* 9. 启动 WiFi */
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        goto cleanup_handler_got_ip;
    }

    /* 10. 保存回调和状态 */
    if (cfg->cb) {
        memcpy(&s_state.callbacks, cfg->cb, sizeof(wifi_mgr_callbacks_t));
    } else {
        memset(&s_state.callbacks, 0, sizeof(wifi_mgr_callbacks_t));
    }

    s_state.mode = cfg->mode;
    s_state.initialized = true;

    ESP_LOGI(TAG, "WiFi manager initialized successfully, mode=%d", cfg->mode);
    return ESP_OK;

/* 错误清理路径 */
cleanup_handler_got_ip:
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_state.instance_got_ip);
cleanup_handler_any:
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_state.instance_any_id);
cleanup_wifi:
    esp_wifi_deinit();
cleanup_ap_netif:
    if (s_state.netif_ap) {
        esp_netif_destroy_default_wifi(s_state.netif_ap);
        s_state.netif_ap = NULL;
    }
cleanup_sta_netif:
    if (s_state.netif_sta) {
        esp_netif_destroy_default_wifi(s_state.netif_sta);
        s_state.netif_sta = NULL;
    }
cleanup_event:
    esp_event_loop_delete_default();
cleanup_netif:
    esp_netif_deinit();
cleanup_nvs:
    if (s_state.own_nvs) {
        nvs_flash_deinit();
        s_state.own_nvs = false;
    }
    return ret;
}

/* ==================== 对外接口 ==================== */

esp_err_t wifi_mgr_init(const wifi_mgr_config_t *cfg)
{
    if (s_state.mutex == NULL) {
        s_state.mutex = xSemaphoreCreateMutex();
        if (s_state.mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    /* 如果已初始化，先反初始化：deinit 内部会重新取锁，
     * 必须先释放本锁再调用，否则非递归互斥锁自死锁（UI 卡死） */
    if (s_state.initialized) {
        ESP_LOGW(TAG, "Already initialized, deinit first...");
        xSemaphoreGive(s_state.mutex);
        wifi_mgr_deinit();
        xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    }

    /* 创建事件组 */
    if (s_state.event_group == NULL) {
        s_state.event_group = xEventGroupCreate();
        if (s_state.event_group == NULL) {
            xSemaphoreGive(s_state.mutex);
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t ret = wifi_mgr_validate_config(cfg);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_state.mutex);
        return ret;
    }

    ret = wifi_mgr_do_init(cfg);

    xSemaphoreGive(s_state.mutex);
    return ret;
}

esp_err_t wifi_mgr_deinit(void)
{
    if (s_state.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    if (!s_state.initialized) {
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing WiFi manager...");

    /* 1. 停止 WiFi */
    esp_wifi_stop();

    /* 2. 注销事件 */
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_state.instance_got_ip);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_state.instance_any_id);

    /* 3. 释放 WiFi 驱动 */
    esp_wifi_deinit();

    /* 4. 释放网络接口 */
    if (s_state.netif_ap) {
        esp_netif_destroy_default_wifi(s_state.netif_ap);
        s_state.netif_ap = NULL;
    }
    if (s_state.netif_sta) {
        esp_netif_destroy_default_wifi(s_state.netif_sta);
        s_state.netif_sta = NULL;
    }

    /* 5. 释放事件循环和 netif（可选：如果确定只有本模块使用） */
    esp_event_loop_delete_default();
    esp_netif_deinit();

    /* 6. 释放 NVS（如果由本模块管理） */
    if (s_state.own_nvs) {
        nvs_flash_deinit();
        s_state.own_nvs = false;
    }

    /* 7. 清理状态 */
    memset(&s_state.callbacks, 0, sizeof(s_state.callbacks));
    s_state.initialized = false;
    s_state.retry_count = 0;
    s_state.sta_auto_connect = false;

    if (s_state.event_group) {
        vEventGroupDelete(s_state.event_group);
        s_state.event_group = NULL;
    }

    ESP_LOGI(TAG, "WiFi manager deinitialized");

    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}

esp_err_t wifi_mgr_update_config(const wifi_mgr_config_t *cfg)
{
    if (s_state.mutex == NULL || !s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    esp_err_t ret = wifi_mgr_validate_config(cfg);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_state.mutex);
        return ret;
    }

    /* 如果模式改变，需要重新初始化 */
    if (cfg->mode != s_state.mode) {
        ESP_LOGW(TAG, "Mode changed, full reinit required");
        xSemaphoreGive(s_state.mutex);
        return wifi_mgr_init(cfg);
    }

    /* 更新 STA 配置 */
    if (cfg->sta && (cfg->mode == WIFI_MGR_MODE_STA || cfg->mode == WIFI_MGR_MODE_APSTA)) {
        wifi_mgr_sta_cfg_t sta_cfg = *cfg->sta;
        wifi_mgr_fill_sta_defaults(&sta_cfg);

        wifi_config_t wifi_cfg = {0};
        strncpy((char *)wifi_cfg.sta.ssid, sta_cfg.ssid, sizeof(wifi_cfg.sta.ssid) - 1);
        if (strlen(sta_cfg.password) > 0) {
            strncpy((char *)wifi_cfg.sta.password, sta_cfg.password, sizeof(wifi_cfg.sta.password) - 1);
        }
        wifi_cfg.sta.threshold.authmode = sta_cfg.authmode;

        ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Update STA config failed: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_state.mutex);
            return ret;
        }

        s_state.sta_auto_connect = sta_cfg.auto_connect;
        s_state.max_retry = sta_cfg.max_retry;
        s_state.retry_count = 0;

        /* 如果当前已连接，断开并重新连接以应用新配置 */
        if (wifi_mgr_is_connected()) {
            ESP_LOGI(TAG, "Reconnecting with new STA config...");
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_wifi_connect();
        } else if (sta_cfg.auto_connect) {
            esp_wifi_connect();
        }
    }

    /* 更新 AP 配置 */
    if (cfg->ap && (cfg->mode == WIFI_MGR_MODE_AP || cfg->mode == WIFI_MGR_MODE_APSTA)) {
        wifi_mgr_ap_cfg_t ap_cfg = *cfg->ap;
        wifi_mgr_fill_ap_defaults(&ap_cfg);

        wifi_config_t wifi_cfg = {0};
        strncpy((char *)wifi_cfg.ap.ssid, ap_cfg.ssid, sizeof(wifi_cfg.ap.ssid) - 1);
        wifi_cfg.ap.ssid_len = strlen(ap_cfg.ssid);
        wifi_cfg.ap.channel = ap_cfg.channel;
        wifi_cfg.ap.max_connection = ap_cfg.max_connection;
        wifi_cfg.ap.authmode = ap_cfg.authmode;

        if (strlen(ap_cfg.password) > 0) {
            strncpy((char *)wifi_cfg.ap.password, ap_cfg.password, sizeof(wifi_cfg.ap.password) - 1);
        } else {
            wifi_cfg.ap.authmode = WIFI_AUTH_OPEN;
        }

        if (ap_cfg.hidden) {
            wifi_cfg.ap.ssid_hidden = 1;
        }

        ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Update AP config failed: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_state.mutex);
            return ret;
        }

        ESP_LOGI(TAG, "AP config updated, takes effect immediately");
    }

    /* 更新回调 */
    if (cfg->cb) {
        memcpy(&s_state.callbacks, cfg->cb, sizeof(wifi_mgr_callbacks_t));
    }

    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}

bool wifi_mgr_is_connected(void)
{
    if (!s_state.initialized || s_state.event_group == NULL) {
        return false;
    }
    EventBits_t bits = xEventGroupGetBits(s_state.event_group);
    return (bits & WIFI_MGR_CONNECTED_BIT) != 0;
}

esp_err_t wifi_mgr_get_ip(esp_ip4_addr_t *ip)
{
    if (ip == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!wifi_mgr_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    *ip = s_state.ip_addr;
    return ESP_OK;
}

esp_err_t wifi_mgr_get_mode(wifi_mgr_mode_t *mode)
{
    if (mode == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    *mode = s_state.mode;
    return ESP_OK;
}

esp_err_t wifi_mgr_scan(wifi_ap_record_t *results, uint16_t *count, uint16_t max_count)
{
    if (results == NULL || count == NULL || max_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 扫描依赖 STA 接口，仅 STA/APSTA 模式下有效 */
    wifi_mode_t mode;
    esp_err_t ret = esp_wifi_get_mode(&mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Get wifi mode failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
        ESP_LOGW(TAG, "Scan requires STA/APSTA mode, current mode=%d", mode);
        return ESP_ERR_INVALID_STATE;
    }

    /* 限制每信道扫描驻留时间，加快扫描速度 */
    wifi_scan_config_t scan_cfg = {0};
    scan_cfg.show_hidden = true;
    scan_cfg.scan_time.active.min = 20;
    scan_cfg.scan_time.active.max = 40;

    /* 扫描期间暂停自动重连，并中断进行中的连接尝试（若有）。
     * 驱动在 STA 连接中状态下会推迟执行扫描，让连接重试循环跑完
     * 一次扫描需要数分钟。此处让驱动回到空闲态再扫描。 */
    s_state.scan_in_progress = true;
    if (!wifi_mgr_is_connected()) {
        esp_wifi_disconnect();      /* 非连接态时返回错误，忽略 */
    }
    ret = esp_wifi_scan_start(&scan_cfg, true);     /* 阻塞等待扫描完成 */
    s_state.scan_in_progress = false;
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uint16_t ap_num = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_num);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan get ap num failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if (ap_num > max_count) {
        ap_num = max_count;
    }

    ret = esp_wifi_scan_get_ap_records(&ap_num, results);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan get ap records failed: %s", esp_err_to_name(ret));
        return ret;
    }

    *count = ap_num;
    ESP_LOGI(TAG, "Scan done, found %u APs", ap_num);
    return ESP_OK;
}

esp_err_t wifi_mgr_get_sta_list(wifi_mgr_sta_info_t *list, uint16_t *count, uint16_t max_count)
{
    if (list == NULL || count == NULL || max_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_state.initialized || s_state.netif_ap == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_sta_list_t sta_list = {0};
    esp_err_t ret = esp_wifi_ap_get_sta_list(&sta_list);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AP get sta list failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uint16_t n = (uint16_t)sta_list.num;
    if (n > max_count) {
        n = max_count;
    }
    if (n == 0) {
        *count = 0;
        return ESP_OK;
    }

    /* 通过 DHCP 租约表补齐客户端 IP（MAC 为输入，IP 为输出） */
    esp_netif_pair_mac_ip_t pairs[ESP_WIFI_MAX_CONN_NUM];
    if (n > ESP_WIFI_MAX_CONN_NUM) {
        n = ESP_WIFI_MAX_CONN_NUM;
    }
    for (uint16_t i = 0; i < n; i++) {
        memcpy(pairs[i].mac, sta_list.sta[i].mac, sizeof(pairs[i].mac));
        memset(&pairs[i].ip, 0, sizeof(pairs[i].ip));
    }
    if (esp_netif_dhcps_get_clients_by_mac(s_state.netif_ap, n, pairs) != ESP_OK) {
        /* 获取 IP 失败不阻断列表，IP 保持 0（显示为 "--"） */
        ESP_LOGW(TAG, "DHCP clients query failed");
    }

    for (uint16_t i = 0; i < n; i++) {
        memcpy(list[i].mac, sta_list.sta[i].mac, sizeof(list[i].mac));
        list[i].ip = pairs[i].ip;
        list[i].rssi = sta_list.sta[i].rssi;
    }

    *count = n;
    return ESP_OK;
}

esp_err_t wifi_mgr_sta_disconnect(void)
{
    if (s_state.mutex == NULL || !s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    if (!wifi_mgr_is_connected()) {
        xSemaphoreGive(s_state.mutex);
        return ESP_OK;                              /* 未连接，无需断开 */
    }

    /* 置标志后断开：STA_DISCONNECTED 事件处理器据此跳过自动重连 */
    s_state.user_disconnect = true;
    esp_wifi_disconnect();                          /* 触发 WIFI_EVENT_STA_DISCONNECTED */

    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}

esp_err_t wifi_mgr_deauth_sta(const uint8_t *mac)
{
    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state.mutex == NULL || !s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    /* 按 MAC 强制断开（reason=3: WLAN_REASON_DEAUTH_LEAVING） */
    int ret = esp_wifi_ap_deauth_internal((uint8_t *)mac, 3);
    if (ret != 0) {
        /* 兜底：公开 API 按 AID 踢 */
        uint16_t aid = 0;
        if (esp_wifi_ap_get_sta_aid(mac, &aid) == ESP_OK && aid != 0) {
            ret = (esp_wifi_deauth_sta(aid) == ESP_OK) ? 0 : -1;
        }
    }

    xSemaphoreGive(s_state.mutex);
    if (ret != 0) {
        ESP_LOGE(TAG, "Deauth " MACSTR " failed: %d", MAC2STR(mac), ret);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Deauth " MACSTR " ok", MAC2STR(mac));
    return ESP_OK;
}