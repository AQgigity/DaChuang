/**
 * @file wifi_mqtt.h
 * @brief WiFi + MQTT + OneNET 云平台模块
 */

#ifndef WIFI_MQTT_H
#define WIFI_MQTT_H

#include "watch_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 WiFi STA 模式
 */
void wifi_init(void);

/**
 * @brief 初始化 MQTT 客户端（不自动连接，等 WiFi 获取 IP 后启动）
 */
void mqtt_init(void);

/**
 * @brief 延迟启动 WiFi + MQTT 的 FreeRTOS 任务
 *
 * BLE 连接脚踝后由 GAP 回调创建。等待 2 秒让 BLE 稳定，
 * 暂停 BLE 通知 → 启动 WiFi → 等待连接 → 恢复 BLE 通知。
 * 任务完成后自动删除。
 */
void wifi_start_task(void *arg);

/**
 * @brief MQTT 数据上报 FreeRTOS 任务
 *
 * 每 2 秒读取融合数据，打包为 OneNET 物模型 JSON 并发布。
 */
void mqtt_upload_task(void *arg);

/**
 * @brief 创建 BLE/WiFi 共存用的 CCCD 写入信号量
 *
 * 需在 app_main 中、BLE 初始化之前调用。
 */
void wifi_mqtt_create_cccd_sem(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MQTT_H */
