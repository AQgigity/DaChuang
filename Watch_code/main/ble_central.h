/**
 * @file ble_central.h
 * @brief BLE Central 模块 — 扫描、连接、NUS 服务发现、通知接收、数据解析
 */

#ifndef BLE_CENTRAL_H
#define BLE_CENTRAL_H

#include "watch_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 NimBLE：设置 MTU、GAP 设备名、Host 回调、启动 Host 任务
 */
void ble_central_init(void);

/**
 * @brief 启动 BLE 扫描（外部可调用，如扫描重试任务）
 */
void ble_start_scan(void);

/**
 * @brief BLE 扫描重试 FreeRTOS 任务
 *
 * 每 10 秒检查连接状态，未连接则重新启动扫描。
 */
void ble_scan_retry_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* BLE_CENTRAL_H */
