/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include "MAX30102.h"  // 确保包含这个头文件
#include <stdio.h>
#include <inttypes.h>  // 添加这个头文件以支持PRId32格式
#include <stdlib.h> 
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
// 重定向printf
int _write(int file, char *ptr, int len)
{
    (void)file;
    HAL_UART_Transmit(&huart1, (uint8_t*)ptr, len, HAL_MAX_DELAY);
    return len;
}

// 状态变量
MAX30102_Data_t sensor_data;

// 算法结果
int32_t heart_rate = 0;
int32_t spo2_value = 0;
int8_t hr_valid = 0;
int8_t spo2_valid = 0;

// 状态
uint32_t last_print_time = 0;
uint8_t sensor_initialized = 0;

// 改进的滤波缓冲区
#define HR_HISTORY_SIZE 5
#define SPO2_HISTORY_SIZE 3
int32_t hr_history[HR_HISTORY_SIZE] = {0};
int32_t spo2_history[SPO2_HISTORY_SIZE] = {0};
uint8_t hr_index = 0, spo2_index = 0;

// 稳定性检测
int32_t last_stable_hr = 0;
int32_t last_stable_spo2 = 0;
uint8_t stable_count = 0;
#define MIN_STABLE_COUNT 3

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */
  SCB_EnableICache();
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2C2_Init();
  MX_ADC1_Init();
  MX_USART1_UART_Init();
  MX_TIM2_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();

  /* USER CODE BEGIN 2 */
  printf("\r\n======================\r\n");
  printf("MAX30102 测试程序\r\n");
  printf("======================\r\n\r\n");
  
  // 检查设备
  printf("检测传感器... ");
  if(HAL_I2C_IsDeviceReady(&hi2c1, MAX30102_I2C_ADDR, 3, 100) == HAL_OK)
  {
      printf("[OK]\r\n");
  }
  else
  {
      printf("[失败]\r\n");
      while(1)
      {
          HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
          HAL_Delay(200);
      }
  }
  
  // 初始化
  printf("初始化传感器... ");
  MAX30102_Init(&hi2c1);
  MAX30102_InitData(&sensor_data);
  printf("[完成]\r\n");
  
  printf("\r\n收集初始数据...\r\n");
  for(int i = 0; i < BUFFER_SIZE; i++)
  {
      MAX30102_CollectData(&hi2c1, &sensor_data);
      if((i+1) % 20 == 0) printf(".");
      HAL_Delay(10);
  }
  
  printf("\r\n\r\n开始监测...\r\n");
  printf("HR(bpm) | SpO2(%%)\r\n");
  printf("------------------\r\n");
  
  sensor_initialized = 1;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
 while (1)
{
    if(sensor_initialized)
    {
        // 收集数据
        MAX30102_CollectData(&hi2c1, &sensor_data);
        
        // 每秒计算一次
        uint32_t current_time = HAL_GetTick();
        if(current_time - last_print_time >= 1000)
        {
            // 计算心率血氧
            MAX30102_CalculateHR_SpO2(&sensor_data,
                                     &heart_rate, &spo2_value,
                                     &hr_valid, &spo2_valid);
            
            // 改进的滤波算法
            int32_t filtered_hr = 0, filtered_spo2 = 0;
            uint8_t display_valid = 0;
            
            if(hr_valid && spo2_valid)
            {
                // 1. 存储到历史缓冲区
                hr_history[hr_index] = heart_rate;
                hr_index = (hr_index + 1) % HR_HISTORY_SIZE;
                
                spo2_history[spo2_index] = spo2_value;
                spo2_index = (spo2_index + 1) % SPO2_HISTORY_SIZE;
                
                // 2. 计算中值滤波（更稳定）
                int32_t hr_sorted[HR_HISTORY_SIZE];
                int32_t spo2_sorted[SPO2_HISTORY_SIZE];
                
                // 复制数组
                for(int i = 0; i < HR_HISTORY_SIZE; i++)
                    hr_sorted[i] = hr_history[i];
                for(int i = 0; i < SPO2_HISTORY_SIZE; i++)
                    spo2_sorted[i] = spo2_history[i];
                
                // 简单排序（冒泡）
                for(int i = 0; i < HR_HISTORY_SIZE-1; i++) {
                    for(int j = i+1; j < HR_HISTORY_SIZE; j++) {
                        if(hr_sorted[i] > hr_sorted[j]) {
                            int32_t temp = hr_sorted[i];
                            hr_sorted[i] = hr_sorted[j];
                            hr_sorted[j] = temp;
                        }
                    }
                }
                
                for(int i = 0; i < SPO2_HISTORY_SIZE-1; i++) {
                    for(int j = i+1; j < SPO2_HISTORY_SIZE; j++) {
                        if(spo2_sorted[i] > spo2_sorted[j]) {
                            int32_t temp = spo2_sorted[i];
                            spo2_sorted[i] = spo2_sorted[j];
                            spo2_sorted[j] = temp;
                        }
                    }
                }
                
                // 取中值
                filtered_hr = hr_sorted[HR_HISTORY_SIZE/2];
                filtered_spo2 = spo2_sorted[SPO2_HISTORY_SIZE/2];
                
                // 3. 稳定性检查（心率变化不应太大）
                if(last_stable_hr == 0) {
                    last_stable_hr = filtered_hr;
                    last_stable_spo2 = filtered_spo2;
                    stable_count = 1;
                } else {
                    // 检查心率变化是否在合理范围内（±20bpm内）
                  int32_t hr_diff = abs((int)(filtered_hr - last_stable_hr));
                    if(hr_diff <= 20) {
                        stable_count++;
                        if(stable_count >= MIN_STABLE_COUNT) {
                            last_stable_hr = filtered_hr;
                            last_stable_spo2 = filtered_spo2;
                            display_valid = 1;
                        }
                    } else {
                        stable_count = 0;
                        last_stable_hr = filtered_hr;
                        last_stable_spo2 = filtered_spo2;
                    }
                }
                
                // 4. 显示结果
                if(display_valid)
                {
                    // 最终合理性检查
                    if(filtered_hr >= 40 && filtered_hr <= 180 &&
                       filtered_spo2 >= 70 && filtered_spo2 <= 100)
                    {
                        printf("✓ HR:%3d | SpO2:%2d%%\r\n", 
                               (int)filtered_hr, (int)filtered_spo2);
                        
                        // 按心率频率闪烁LED
                        static uint32_t last_blink = 0;
                        uint32_t interval = 60000 / filtered_hr; // 转换为ms
                        if(current_time - last_blink >= interval/2) {
                            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
                            last_blink = current_time;
                        }
                    }
                    else if(filtered_hr >= 30 && filtered_hr <= 200)
                    {
                        printf("HR:%3d | SpO2:%2d%% (注意)\r\n", 
                               (int)filtered_hr, (int)filtered_spo2);
                    }
                }
                else
                {
                    printf("正在稳定... (%d/%d)\r\n", 
                           stable_count, MIN_STABLE_COUNT);
                }
            }
            else if(hr_valid)
            {
                printf("HR:%3d | SpO2: 计算中\r\n", (int)heart_rate);
            }
            else if(spo2_valid)
            {
                printf("HR: 计算中 | SpO2:%2d%%\r\n", (int)spo2_value);
            }
            else
            {
                // 显示原始信号强度以帮助调试
                static uint32_t signal_counter = 0;
                if(signal_counter++ % 5 == 0)
                {
                    uint32_t recent_ir = sensor_data.ir_buffer[sensor_data.index];
                    uint32_t recent_red = sensor_data.red_buffer[sensor_data.index];
              printf("信号: IR=%lu, RED=%lu\r\n", 
       (unsigned long)recent_ir, (unsigned long)recent_red);
                }
                else
                {
                    printf("正在检测信号...\r\n");
                }
            }
            
            last_print_time = current_time;
        }
    }
    
    HAL_Delay(10);
}
}
/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */
void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x20000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_128KB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x24000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected */
  MPU_InitStruct.Number = MPU_REGION_NUMBER2;
  MPU_InitStruct.BaseAddress = 0x30000000;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected */
  MPU_InitStruct.Number = MPU_REGION_NUMBER3;
  MPU_InitStruct.BaseAddress = 0x38000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_64KB;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected */
  MPU_InitStruct.Number = MPU_REGION_NUMBER4;
  MPU_InitStruct.BaseAddress = 0x60000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_256MB;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected */
  MPU_InitStruct.Number = MPU_REGION_NUMBER5;
  MPU_InitStruct.BaseAddress = 0xC0000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_32MB;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected */
  MPU_InitStruct.Number = MPU_REGION_NUMBER6;
  MPU_InitStruct.BaseAddress = 0x80000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_256MB;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  printf("Assert failed: file %s on line %" PRIu32 "\r\n", file, line);
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
