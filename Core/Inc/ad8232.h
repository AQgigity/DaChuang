#ifndef AD8232_H
#define AD8232_H

#include "main.h"
#include "adc.h"

// 缓冲区大小定义
#define ECG_BUFFER_SIZE 256

// AD8232 控制引脚定义 - 根据您的实际硬件连接修改
#define AD8232_SDN_PORT GPIOC
#define AD8232_SDN_PIN GPIO_PIN_3

// 导联脱落检测引脚定义
#define AD8232_LO_PLUS_PORT GPIOC
#define AD8232_LO_PLUS_PIN GPIO_PIN_1
#define AD8232_LO_MINUS_PORT GPIOC  
#define AD8232_LO_MINUS_PIN GPIO_PIN_2

// 函数声明
void AD8232_Init(void);
void AD8232_Start(void);
void AD8232_Stop(void);
uint8_t AD8232_IsDataReady(void);
uint16_t AD8232_GetLatestValue(void);
uint8_t AD8232_IsLeadOff(void);
void AD8232_GetBufferData(uint16_t* buffer, uint16_t size);
void AD8232_ClearDataReady(void);  // 添加这行
uint32_t AD8232_GetSampleCount(void);  // 添加这行

// 外部变量声明
extern volatile uint16_t ecg_buffer[ECG_BUFFER_SIZE];

#endif
// 确保这里有换行符