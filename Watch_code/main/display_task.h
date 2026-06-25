/**
 * @file display_task.h
 * @brief 显示 FreeRTOS 任务（LVGL 定时器 + UI 刷新）
 */

#ifndef DISPLAY_TASK_H
#define DISPLAY_TASK_H

#include "watch_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LVGL 事件循环任务（10ms 周期调用 lv_timer_handler）
 */
void lvgl_task(void *arg);

/**
 * @brief UI 标签刷新任务（200ms 周期更新心率/气压/温度/脚踝/挥臂数据）
 */
void ui_refresh_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_TASK_H */
