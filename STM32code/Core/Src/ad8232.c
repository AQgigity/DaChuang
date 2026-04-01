// #include "ad8232.h"
// #include <string.h>

// // 外部声明
// extern ADC_HandleTypeDef hadc1;
// extern DMA_HandleTypeDef hdma_adc1;

// // 全局变量
// volatile uint16_t ecg_buffer[ECG_BUFFER_SIZE];
// static volatile uint8_t data_ready = 0;
// static volatile uint32_t sample_counter = 0;

// // 初始化函数
// void AD8232_Init(void) {
//     // 初始化GPIO
//     GPIO_InitTypeDef GPIO_InitStruct = {0};
    
//     // 配置SDN引脚为输出
//     GPIO_InitStruct.Pin = AD8232_SDN_PIN;
//     GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
//     GPIO_InitStruct.Pull = GPIO_NOPULL;
//     GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
//     HAL_GPIO_Init(AD8232_SDN_PORT, &GPIO_InitStruct);
    
//     // 配置导联脱落检测引脚为输入
//     GPIO_InitStruct.Pin = AD8232_LO_PLUS_PIN | AD8232_LO_MINUS_PIN;
//     GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
//     GPIO_InitStruct.Pull = GPIO_PULLUP;
//     HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
//     // 使能AD8232模块
//     HAL_GPIO_WritePin(AD8232_SDN_PORT, AD8232_SDN_PIN, GPIO_PIN_SET);
    
//     // 初始化缓冲区
//     memset((void*)ecg_buffer, 0, sizeof(ecg_buffer));
//     data_ready = 0;
//     sample_counter = 0;
// }

// // 开始采集
// void AD8232_Start(void) {
//     // 确保AD8232使能
//     HAL_GPIO_WritePin(AD8232_SDN_PORT, AD8232_SDN_PIN, GPIO_PIN_SET);
//     HAL_Delay(10);
    
//     // 重置状态
//     data_ready = 0;
//     sample_counter = 0;
    
//     // 启动ADC DMA连续转换
//     if (HAL_ADC_Start_DMA(&hadc1, (uint32_t*)ecg_buffer, ECG_BUFFER_SIZE) != HAL_OK) {
//         // 错误处理
//         Error_Handler();
//     }
// }

// // 停止采集
// void AD8232_Stop(void) {
//     HAL_ADC_Stop_DMA(&hadc1);
//     HAL_GPIO_WritePin(AD8232_SDN_PORT, AD8232_SDN_PIN, GPIO_PIN_RESET);
// }

// // 检查数据是否就绪
// uint8_t AD8232_IsDataReady(void) {
//     return data_ready;
// }

// // 获取最新的ADC值
// uint16_t AD8232_GetLatestValue(void) {
//     if(ECG_BUFFER_SIZE > 0) {
//         return ecg_buffer[ECG_BUFFER_SIZE - 1];
//     }
//     return 0;
// }

// // 获取缓冲区数据
// void AD8232_GetBufferData(uint16_t* buffer, uint16_t size) {
//     uint16_t copy_size = (size < ECG_BUFFER_SIZE) ? size : ECG_BUFFER_SIZE;
//     memcpy(buffer, (void*)ecg_buffer, copy_size * sizeof(uint16_t));
// }

// // 检查导联是否脱落
// uint8_t AD8232_IsLeadOff(void) {
//     // AD8232: LO+ 和 LO- 都为高电平时表示导联脱落
//     if (HAL_GPIO_ReadPin(AD8232_LO_PLUS_PORT, AD8232_LO_PLUS_PIN) == GPIO_PIN_SET &&
//         HAL_GPIO_ReadPin(AD8232_LO_MINUS_PORT, AD8232_LO_MINUS_PIN) == GPIO_PIN_SET) {
//         return 1; // 导联脱落
//     }
//     return 0; // 导联连接正常
// }

// // DMA转换完成回调函数
// void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
//     if (hadc->Instance == ADC1) {
//         data_ready = 1;
//         sample_counter += ECG_BUFFER_SIZE;
//     }
// }

// // 获取采样计数器值
// uint32_t AD8232_GetSampleCount(void) {
//     return sample_counter;
// }

// // 清除数据就绪标志
// void AD8232_ClearDataReady(void) {
//     data_ready = 0;
// }
// // 确保这里有换行符
