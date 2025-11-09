#include "main.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include "MPU6050.h"
#include "system_config.h"
int main(void)
{
    // 系统初始化
    HAL_Init();
    SystemClock_Config();
       
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    MX_I2C2_Init();
    MX_TIM2_Init();

    /* USER CODE BEGIN 2 */
    // 发送启动信息
    HAL_UART_Transmit(&huart1, (uint8_t*)"=== MPU6050 Motion Detection ===\r\n", 34, 100);
    HAL_Delay(100);
    
    // 一行代码完成MPU6050初始化和校准
    if(!MPU6050_AutoInit(&huart1)) {
        // 初始化失败，停止程序
        HAL_UART_Transmit(&huart1, (uint8_t*)"System Halted! Check MPU6050 connection.\r\n", 43, 100);
        while(1) {
            HAL_Delay(1000);
        }
    }
    /* USER CODE END 2 */

    // 传感器数据结构
    SensorData_t sensor_data;

    while (1)
    {
        /* USER CODE BEGIN 3 */
        // 读取并处理传感器数据
        MPU6050_ReadProcessedData(&sensor_data);
        
        // 方法1：使用内置打印函数（推荐）
        MPU6050_PrintData(&huart1, &sensor_data);
        
        // 方法2：使用自定义格式输出
        // char* data_str = MPU6050_GetDataString(&sensor_data);
        // HAL_UART_Transmit(&huart1, (uint8_t*)data_str, strlen(data_str), 100);
        
        // 根据运动状态执行不同操作（示例）
        if(sensor_data.is_moving) {
            // 检测到运动，可以添加相应处理
            // 例如：点亮LED、发送警报等
        }
        
        HAL_Delay(200);  // 5Hz 采样率
        /* USER CODE END 3 */
    }
}

// 错误处理函数
void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif
