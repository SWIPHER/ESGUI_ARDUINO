//
// Created by E_LJF on 2026/8/24.
//

#ifndef BASED_ON_ESGUI_WIFI_APP_H
#define BASED_ON_ESGUI_WIFI_APP_H

#include "wifi_mgr.h"
#include "ESGUI.h"

/* 扫描结果 / 客户端列表最大数量 */
#define WIFI_APP_MAX_SCAN_RESULTS   10
#define WIFI_APP_MAX_CLIENTS        10

/* 自定义 UI 事件码（由 wifi_app 工作线程推送，页面 on_input 接收） */
#define WIFI_APP_EVT_SCAN_DONE       (EVT_CUSTOM + 0)   /* 扫描完成（结果已就绪） */
#define WIFI_APP_EVT_CLIENTS_UPDATE  (EVT_CUSTOM + 1)   /* 已连接设备列表已刷新 */
#define WIFI_APP_EVT_CONNECTED       (EVT_CUSTOM + 2)   /* STA 连接状态变化（成功/断开），扫描页据此刷新"已连接"标记 */

/* 扫描结果条目（已去重、按信号降序） */
typedef struct {
    char ssid[33];
    int8_t rssi;                /* dBm */
    wifi_auth_mode_t authmode;
    uint8_t channel;
} wifi_app_scan_entry_t;

/* AP 已连接客户端条目 */
typedef struct {
    uint8_t mac[6];
    esp_ip4_addr_t ip;          /* 未取得 IP 时为 0 */
    int8_t rssi;
    bool ip_valid;              /* false=尚未取得 IP */
} wifi_app_client_entry_t;

extern const wifi_mgr_callbacks_t wifi_mgr_callbacks;
extern ESGUI_ProducerBox_T wifi_app_producers;

/** @brief 初始化 wifi_app 内部资源（创建互斥锁等），启动阶段调用一次，可重复调用 */
void wifi_app_init(void);

/**
 * @brief 发起一次异步 WiFi 扫描
 *
 * 扫描在工作线程中执行，不阻塞 UI；完成后结果写入内部缓冲，
 * 并向 UI 推送 WIFI_APP_EVT_SCAN_DONE 事件。
 *
 * @return ESP_OK 或 ESP_ERR_INVALID_STATE（已有扫描在进行）
 */
esp_err_t wifi_app_start_scan(void);

/** @brief 扫描是否进行中 */
bool wifi_app_scan_busy(void);

/** @brief 最近一次扫描是否失败 */
bool wifi_app_scan_failed(void);

/**
 * @brief 获取扫描结果（已去重、按信号降序）
 * @param count 输出：结果数量
 * @return 结果数组指针（内部静态缓冲，下次扫描前有效）
 */
const wifi_app_scan_entry_t *wifi_app_get_scan_results(uint16_t *count);

/** @brief 启动已连接设备监控（每 2 秒刷新一次，变化时推送 WIFI_APP_EVT_CLIENTS_UPDATE；重复启动安全） */
void wifi_app_clients_monitor_start(void);

/** @brief 停止已连接设备监控 */
void wifi_app_clients_monitor_stop(void);

/**
 * @brief 延迟推送一次已连接设备列表刷新事件（WIFI_APP_EVT_CLIENTS_UPDATE）
 *
 * 用于弹窗关闭过渡期间发起的操作（如踢设备）后刷新列表：
 * 过渡期间 ESGUI 会丢弃立即推送的按键事件，延迟 ~600ms 推送保证送达。
 */
void wifi_app_clients_refresh_delayed(void);

/**
 * @brief 踢掉 AP 已连接的指定客户端（按 MAC）
 *
 * 内部按固定周期重复 deauth，持续一段时间（默认 15s）防止客户端（如手机）
 * 立即自动重连；单次 deauth 对会自动重连的客户端视觉上无效。
 *
 * @param mac 客户端 MAC 地址（6 字节）
 * @return ESP_OK 或 ESP_ERR_INVALID_ARG / ESP_ERR_INVALID_STATE（已有踢人任务在运行）
 */
esp_err_t wifi_app_kick_sta(const uint8_t *mac);

/**
 * @brief 获取 AP 已连接客户端快照
 * @param count 输出：客户端数量
 * @return 客户端数组指针（内部静态缓冲）
 */
const wifi_app_client_entry_t *wifi_app_get_clients(uint16_t *count);

/**
 * @brief 连接指定 AP（STA 部分）
 *
 * 将 ssid/password/authmode 写入 cfg->sta 并调用 wifi_mgr_update_config 触发连接。
 * 连接结果通过 wifi_mgr 回调弹窗通知（成功/失败）。
 *
 * @param cfg       WiFi 总配置（指向页面持有的配置，如 first_page 的 wifi_cfg）
 * @param ssid      目标 AP 的 SSID
 * @param password  密码（开放网络传 NULL 或空串）
 * @param authmode  目标 AP 的认证方式
 * @return wifi_mgr_update_config 的返回值
 */
esp_err_t wifi_app_connect(wifi_mgr_config_t *cfg, const char *ssid,
                           const char *password, wifi_auth_mode_t authmode);

/**
 * @brief 判断指定 SSID 是否为当前已连接的 STA 网络
 * @param ssid 目标 AP 的 SSID
 * @return true=WiFi 已连接且连接的正是该 SSID
 */
bool wifi_app_is_connected_to(const char *ssid);

/**
 * @brief 主动断开 STA 连接（用户操作）
 *
 * 内部调用 wifi_mgr_sta_disconnect（断开后不自动重连），
 * 并在断开事件完成后推送 WIFI_APP_EVT_CONNECTED，让扫描页移除"已连接"标记。
 * 未连接时直接返回 ESP_OK（无操作）。
 *
 * @return wifi_mgr_sta_disconnect 的返回值
 */
esp_err_t wifi_app_disconnect(void);

/**
 * @brief 保存 WiFi 配置（模式 + AP 配置 + STA 配置）到 NVS
 * @param cfg WiFi 总配置指针
 * @return ESP_OK 或 NVS 错误码
 */
esp_err_t wifi_app_config_save(const wifi_mgr_config_t *cfg);

/**
 * @brief 从 NVS 加载 WiFi 配置（模式 + AP 配置 + STA 配置）
 * @param cfg 输出结构体（仅覆盖 mode/ap/sta 字段，cb/init_nvs 不动）
 * @return ESP_OK=读到有效配置；其他=无保存记录或读取失败
 */
esp_err_t wifi_app_config_load(wifi_mgr_config_t *cfg);

#endif //BASED_ON_ESGUI_WIFI_APP_H
