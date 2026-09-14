//
// Created by E_LJF on 2026/8/23.
//

#ifndef BASED_ON_ESGUI_WIFI_MGR_H
#define BASED_ON_ESGUI_WIFI_MGR_H


#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi_types.h"
#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* WiFi 工作模式 */
typedef enum {
    WIFI_MGR_MODE_AP = 0,     /* SoftAP 模式 */
    WIFI_MGR_MODE_STA,        /* Station 模式 */
    WIFI_MGR_MODE_APSTA,      /* Station + SoftAP 共存模式 */
} wifi_mgr_mode_t;

/* 连接状态回调（可选，可为 NULL） */
typedef struct {
    void (*on_connected)(void);              /* 成功连接到 AP（STA 模式） */
    void (*on_disconnected)(void);           /* 与 AP 断开连接 */
    void (*on_got_ip)(esp_ip4_addr_t ip);    /* 获取到 IP 地址 */
    void (*on_ap_started)(void);             /* AP 模式启动成功 */
    void (*on_ap_staconnected)(uint8_t mac[6], uint8_t aid);    /* 有 STA 连入 AP */
    void (*on_ap_stadisconnected)(uint8_t mac[6], uint8_t aid); /* 有 STA 断开 AP */
} wifi_mgr_callbacks_t;

/* STA 配置参数 */
typedef struct {
    char ssid[33];           /* 目标 AP 的 SSID（必填） */
    char password[65];       /* 密码，NULL 或空字符串表示开放网络 */
    wifi_auth_mode_t authmode;  /* 认证模式。建议始终使用 WIFI_MGR_STA_CONFIG_DEFAULT() 初始化，默认 WPA2_PSK */
    int max_retry;              /* 最大重试次数，默认 5，-1 表示无限重试 */
    bool auto_connect;          /* 启动后是否自动连接。建议始终使用 WIFI_MGR_STA_CONFIG_DEFAULT() 初始化，默认 true */
} wifi_mgr_sta_cfg_t;

/* AP 配置参数 */
typedef struct {
    char ssid[33];           /* AP 的 SSID（必填） */
    char password[65];       /* 密码，NULL 或空字符串表示开放网络 */
    uint8_t channel;            /* 信道 1-14，默认 1 */
    uint8_t max_connection;     /* 最大连接数，默认 4 */
    wifi_auth_mode_t authmode;  /* 认证模式。建议始终使用 WIFI_MGR_AP_CONFIG_DEFAULT() 初始化，默认 WPA_WPA2_PSK */
    bool hidden;                /* 是否隐藏 SSID，默认 false */
} wifi_mgr_ap_cfg_t;

/* 总配置结构体 */
typedef struct {
    wifi_mgr_mode_t mode;           /* 工作模式（必填） */
    const wifi_mgr_sta_cfg_t *sta;  /* STA 参数，mode 为 STA/APSTA 时必填 */
    const wifi_mgr_ap_cfg_t *ap;    /* AP 参数，mode 为 AP/APSTA 时必填 */
    const wifi_mgr_callbacks_t *cb; /* 回调函数集，可为 NULL */
    bool init_nvs;                  /* 是否在本模块内初始化 NVS。强烈建议始终使用 WIFI_MGR_CONFIG_DEFAULT() 初始化，默认 true */
} wifi_mgr_config_t;

/* ==================== 默认值宏 ==================== */
/* 强烈建议使用以下宏初始化配置结构体，避免 C 语言未设置字段为 0 导致的意外行为 */

#define WIFI_MGR_STA_CONFIG_DEFAULT() { \
    .ssid = "", \
    .password = "", \
    .authmode = WIFI_AUTH_WPA2_PSK, \
    .max_retry = 5, \
    .auto_connect = true, \
}

#define WIFI_MGR_AP_CONFIG_DEFAULT() { \
    .ssid = "", \
    .password = "", \
    .channel = 1, \
    .max_connection = 4, \
    .authmode = WIFI_AUTH_WPA_WPA2_PSK, \
    .hidden = false, \
}

#define WIFI_MGR_CONFIG_DEFAULT() { \
    .mode = WIFI_MGR_MODE_AP, \
    .sta = NULL, \
    .ap = NULL, \
    .cb = NULL, \
    .init_nvs = true, \
}

/* AP 已连接客户端信息（wifi_mgr_get_sta_list 输出） */
typedef struct {
    uint8_t mac[6];          /* 客户端 MAC 地址 */
    esp_ip4_addr_t ip;       /* DHCP 分配的 IP 地址（未取得 IP 时为 0） */
    int8_t rssi;             /* 客户端信号强度（dBm） */
} wifi_mgr_sta_info_t;

/* ==================== API 声明 ==================== */

/**
 * @brief 初始化 WiFi 模块
 *
 * 根据配置自动完成 WiFi 初始化、事件注册、网络接口创建、参数配置和启动。
 * 支持重复调用：如果已初始化，会先执行反初始化再重新初始化。
 *
 * @param cfg 配置参数指针。强烈建议用 WIFI_MGR_CONFIG_DEFAULT() 初始化后再覆盖所需字段
 * @return
 *      - ESP_OK：初始化成功
 *      - ESP_ERR_INVALID_ARG：参数错误（如必填字段缺失）
 *      - ESP_ERR_NO_MEM：内存不足
 *      - 其他：底层驱动错误码
 */
esp_err_t wifi_mgr_init(const wifi_mgr_config_t *cfg);

/**
 * @brief 反初始化 WiFi 模块
 *
 * 停止 WiFi、注销事件、释放网络接口、释放本模块占用的所有资源。
 * 调用后模块回到未初始化状态，可再次调用 wifi_mgr_init()。
 *
 * @return
 *      - ESP_OK：反初始化成功
 *      - ESP_ERR_INVALID_STATE：模块未初始化
 */
esp_err_t wifi_mgr_deinit(void);

/**
 * @brief 运行时动态更新 WiFi 配置
 *
 * 在 WiFi 已运行状态下动态修改配置。
 * - STA 模式：更新 SSID/密码后会自动断开并重新连接
 * - AP 模式：更新参数后立即生效，已连接设备不会断开
 * - APSTA 模式：可同时更新 STA 和 AP 参数
 *
 * @param cfg 新的配置参数，与 wifi_mgr_init() 格式相同
 * @return
 *      - ESP_OK：更新成功
 *      - ESP_ERR_INVALID_ARG：参数错误
 *      - ESP_ERR_INVALID_STATE：WiFi 未初始化
 */
esp_err_t wifi_mgr_update_config(const wifi_mgr_config_t *cfg);

/**
 * @brief 获取当前连接状态
 * @return true 表示 STA 已连接且获取到 IP
 */
bool wifi_mgr_is_connected(void);

/**
 * @brief 获取当前 IP 地址（STA 模式）
 * @param ip 输出参数，成功时填充 IP 地址
 * @return ESP_OK 或 ESP_ERR_INVALID_STATE
 */
esp_err_t wifi_mgr_get_ip(esp_ip4_addr_t *ip);

/**
 * @brief 获取当前工作模式（模块初始化后有效）
 * @param mode 输出参数，成功时填充当前模式
 * @return ESP_OK 或 ESP_ERR_INVALID_STATE
 */
esp_err_t wifi_mgr_get_mode(wifi_mgr_mode_t *mode);

/**
 * @brief 扫描周围可用 AP（同步阻塞，需 STA/APSTA 模式已启动）
 *
 * 内部使用 esp_wifi_scan_start 阻塞等待扫描完成，再拷贝结果。
 * 注意：STA 已连接时发起扫描，射频切信道可能导致短暂断开，
 * 扫描完成后 wifi_mgr 会自动重连（与正常断线重连逻辑一致）。
 *
 * @param results   输出缓冲区（调用方提供）
 * @param count     输入：缓冲区容量；输出：实际扫描到的 AP 数量
 * @param max_count 缓冲区最大容量
 * @return
 *      - ESP_OK：成功
 *      - ESP_ERR_INVALID_ARG：参数错误
 *      - ESP_ERR_INVALID_STATE：WiFi 未初始化或当前不是 STA/APSTA 模式
 *      - 其他：底层 esp_wifi 错误码
 */
esp_err_t wifi_mgr_scan(wifi_ap_record_t *results, uint16_t *count, uint16_t max_count);

/**
 * @brief 获取 AP 已连接的客户端列表（MAC/IP/RSSI）
 *
 * 内部通过 esp_wifi_ap_get_sta_list 获取已关联 STA 的 MAC/RSSI，
 * 再通过 DHCP 租约表（esp_netif_dhcps_get_clients_by_mac）补齐各客户端 IP。
 * 客户端尚未取得 IP（如刚连入、DHCP 未完成）时 ip 为 0。
 *
 * @param list      输出缓冲区（调用方提供）
 * @param count     输入：缓冲区容量；输出：实际客户端数量
 * @param max_count 缓冲区最大容量
 * @return
 *      - ESP_OK：成功
 *      - ESP_ERR_INVALID_ARG：参数错误
 *      - ESP_ERR_INVALID_STATE：WiFi 未初始化或 AP 网络接口不存在
 *      - 其他：底层错误码
 */
esp_err_t wifi_mgr_get_sta_list(wifi_mgr_sta_info_t *list, uint16_t *count, uint16_t max_count);

/**
 * @brief 主动断开 STA 连接（用户操作）
 *
 * 与断线自动重连不同：主动断开后不会自动重连（抑制 STA_DISCONNECTED 事件中的重试逻辑）。
 * 未连接时直接返回 ESP_OK（无操作）。
 *
 * @return
 *      - ESP_OK：成功（含未连接时无操作）
 *      - ESP_ERR_INVALID_STATE：WiFi 未初始化
 */
esp_err_t wifi_mgr_sta_disconnect(void);

/**
 * @brief 强制断开 AP 已连接的指定客户端（踢下线）
 * @param mac 客户端 MAC 地址（6 字节）
 * @return
 *      - ESP_OK：成功
 *      - ESP_ERR_INVALID_ARG：mac 为 NULL
 *      - ESP_ERR_INVALID_STATE：WiFi 未初始化
 *      - 其他：底层 esp_wifi 错误码
 */
esp_err_t wifi_mgr_deauth_sta(const uint8_t *mac);

#ifdef __cplusplus
}
#endif


#endif //BASED_ON_ESGUI_WIFI_MGR_H