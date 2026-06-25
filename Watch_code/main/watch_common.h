/**
 * @file watch_common.h
 * @brief 手表固件共享数据结构、全局变量 extern、宏定义
 *
 * 所有模块通过 #include "watch_common.h" 获取共享状态。
 * 全局变量在 main.c 中定义，其他模块通过 extern 访问。
 */

#ifndef WATCH_COMMON_H
#define WATCH_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "bme280.h"
#include "max30102.h"

/* ==================== EMG 使能开关 ==================== */
#define ENABLE_EMG  1

/* ==================== 脚踝设备名 ==================== */
#define ANKLE_DEVICE_NAME   "ESP32_Gait_Gatt"

/* ==================== 任务参数 ==================== */
#define FUSION_TASK_STACK   4096
#define FUSION_TASK_PRIO    2

/* ==================== 调试开关 ==================== */
#define WIFI_ONLY_TEST  0

/* ==================== 数据结构 ==================== */

typedef struct {
    char     action[16];
    int      confidence;       // 可信度 0-100
    char     gait_style[16];   // 发力模式
    int      cadence_spm;
    uint32_t timestamp_ms;
    bool     valid;
} ankle_data_t;

/* ==================== 全局变量 extern ==================== */

/* 传感器句柄（在 main.c 中定义） */
extern i2c_bus_handle_t  g_i2c_bus;
extern bme280_handle_t   g_bme280;
extern max30102_handle_t g_max30102;

/* 本地传感器共享数据 */
extern volatile float g_current_bpm;
extern volatile float g_current_temp;
extern volatile float g_current_press;

/* 脚踝数据 */
extern volatile ankle_data_t g_latest_ankle;
extern QueueHandle_t g_ankle_data_queue;

/* BLE 状态 */
extern volatile bool g_ankle_connected;
extern uint16_t g_ankle_conn_handle;
extern uint16_t g_nus_tx_val_handle;
extern uint16_t g_nus_rx_val_handle;
extern uint16_t g_nus_svc_start_handle;
extern uint16_t g_nus_svc_end_handle;

/* WiFi/MQTT 状态 */
extern volatile bool g_wifi_connected;
extern volatile bool g_mqtt_connected;
extern volatile bool g_upload_enabled;

/* LVGL 互斥锁 */
extern SemaphoreHandle_t g_lvgl_mutex;

/* EMG 数据 */
#if ENABLE_EMG
extern volatile int g_current_emg_raw;
extern volatile int g_emg_peak_count;
#endif

#endif /* WATCH_COMMON_H */
