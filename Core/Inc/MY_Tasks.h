#ifndef _MY_TASK_H
#define _MY_TASK_H

#ifdef __cplusplus
extern "C" {
#endif


#include "stm32h7xx_hal.h"
#include "main.h"
#include "cmsis_os.h"  
typedef struct {
    int32_t heart_rate;    // 心率
    int32_t blood_oxygen;  // 血氧
    uint8_t hr_valid;      // 心率有效标志
    uint8_t spo2_valid;    // 血氧有效标志
} HeartRateData_t;
/**
  * @brief  MAX30102任务函数
  * @param  argument: 任务参数
  * @retval None
  */
void MAX30102_Tasks(void *argument);
void FusionTasks(void *argument);
extern osMessageQueueId_t MAX30102_QueueHandle;


#ifdef __cplusplus
}
#endif

#endif /* MAX30102_TASK_H */
