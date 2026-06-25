/**
 * @file ble_peripheral.h
 * @brief BLE Peripheral 模块 — NUS 服务、GATT/GAP、广播、Notify、分片发送
 */

#ifndef BLE_PERIPHERAL_H
#define BLE_PERIPHERAL_H

#include "gait_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 NimBLE Peripheral：MTU、GAP、GATT 服务注册、Host 回调
 */
void ble_peripheral_init(void);

/**
 * @brief 分片发送一行数据（自动按 MTU 分片，片间 20ms 延时）
 *
 * 供推理任务调用，发送识别结果到手机。
 */
void ble_uart_send_line(const char *data, int len);

#ifdef __cplusplus
}
#endif

#endif /* BLE_PERIPHERAL_H */
