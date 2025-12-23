#ifndef __MAX30102_H
#define __MAX30102_H

#include "stm32h7xx_hal.h"
#include <stdint.h>

// I2C地址定义
#define MAX30102_I2C_ADDR      (0xAE)      // (0x57 << 1)

// 寄存器地址定义
#define REG_INTR_STATUS_1      0x00
#define REG_INTR_STATUS_2      0x01
#define REG_INTR_ENABLE_1      0x02
#define REG_INTR_ENABLE_2      0x03
#define REG_FIFO_WR_PTR        0x04
#define REG_OVF_COUNTER        0x05
#define REG_FIFO_RD_PTR        0x06
#define REG_FIFO_DATA          0x07
#define REG_FIFO_CONFIG        0x08
#define REG_MODE_CONFIG        0x09
#define REG_SPO2_CONFIG        0x0A
#define REG_LED1_PA            0x0C
#define REG_LED2_PA            0x0D
#define REG_PILOT_PA           0x10
#define REG_TEMP_INT           0x1F
#define REG_TEMP_FRAC          0x20
#define REG_REV_ID             0xFE
#define REG_PART_ID            0xFF

// 算法参数
#define BUFFER_SIZE            500
#define MA4_SIZE               4
#define HAMMING_SIZE           5

// 数据结构定义
typedef struct {
    uint32_t ir_buffer[BUFFER_SIZE];   // 红外数据缓冲区
    uint32_t red_buffer[BUFFER_SIZE];  // 红光数据缓冲区
    uint16_t index;                     // 当前写入索引
    uint8_t ready;                      // 缓冲区就绪标志
    uint32_t count;                     // 总采样计数
} MAX30102_Data_t;

typedef struct {
    int32_t heart_rate;     // 心率
    int32_t spo2;           // 血氧
    uint8_t hr_valid;       // 心率有效标志
    uint8_t spo2_valid;     // 血氧有效标志
    uint32_t timestamp;     // 时间戳
} HR_SpO2_Data_t;
// ===================================================================
// ===== 基础驱动函数 =====
void MAX30102_Init(I2C_HandleTypeDef *hi2c);
void MAX30102_Reset(I2C_HandleTypeDef *hi2c);
uint8_t MAX30102_ReadFIFO(I2C_HandleTypeDef *hi2c, uint32_t *pun_red_led, uint32_t *pun_ir_led);

// ===== 数据管理函数 =====
void MAX30102_InitData(MAX30102_Data_t *data);
void MAX30102_CollectData(I2C_HandleTypeDef *hi2c, MAX30102_Data_t *data);
void MAX30102_CalculateHR_SpO2(MAX30102_Data_t *data,
                              int32_t *heart_rate, int32_t *spo2,
                              int8_t *hr_valid, int8_t *spo2_valid);

// ===== 官方算法函数 =====
void maxim_heart_rate_and_oxygen_saturation(uint32_t *pun_ir_buffer, int32_t n_ir_buffer_length,
                                            uint32_t *pun_red_buffer, int32_t *pn_spo2,
                                            int8_t *pch_spo2_valid, int32_t *pn_heart_rate,
                                            int8_t *pch_hr_valid);

#endif /* __MAX30102_H */
