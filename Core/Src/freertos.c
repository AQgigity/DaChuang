/* USER CODE BEGIN Header */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
 

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
extern UART_HandleTypeDef huart1;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* USER CODE END Variables */
/* Definitions for MPUT6050Task */
osThreadId_t MPUT6050TaskHandle;
const osThreadAttr_t MPUT6050Task_attributes = {
  .name = "MPUT6050Task",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal2,
};
/* Definitions for MAX30102_Task */
osThreadId_t MAX30102_TaskHandle;
const osThreadAttr_t MAX30102_Task_attributes = {
  .name = "MAX30102_Task",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal1,
};
/* Definitions for Fusion_Task */
osThreadId_t Fusion_TaskHandle;
const osThreadAttr_t Fusion_Task_attributes = {
  .name = "Fusion_Task",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for MAX30102_Queue */
osMessageQueueId_t MAX30102_QueueHandle;
const osMessageQueueAttr_t MAX30102_Queue_attributes = {
  .name = "MAX30102_Queue"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void MAX30102_Tasks(void *argument);
/* USER CODE END FunctionPrototypes */

void MPUT6050_Task(void *argument);
void MAX30102_Tasks(void *argument);
void FusionTasks(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  //要注意队列如果是结构体创建时要注意如sizeof(HeartRateData_t)
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of MAX30102_Queue */
  MAX30102_QueueHandle = osMessageQueueNew (16,sizeof(HeartRateData_t), &MAX30102_Queue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of MPUT6050Task */
  MPUT6050TaskHandle = osThreadNew(MPUT6050_Task, NULL, &MPUT6050Task_attributes);

  /* creation of MAX30102_Task */
  MAX30102_TaskHandle = osThreadNew(MAX30102_Tasks, NULL, &MAX30102_Task_attributes);

  /* creation of Fusion_Task */
 Fusion_TaskHandle = osThreadNew(FusionTasks, NULL, &Fusion_Task_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_MPUT6050_Task */
/**
  * @brief  Function implementing the MPUT6050Task thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_MPUT6050_Task */
void MPUT6050_Task(void *argument)
{
  /* USER CODE BEGIN MPUT6050_Task */
  /* Infinite loop */
  MPU6050_AutoInit(&huart1);
  
  SensorData_t sensor_data;
  
  for(;;)
  {
    // 生产
    MPU6050_ReadAndEnqueue();
    
    // 每200ms显示
    // static uint32_t timer = 0;
    // if (HAL_GetTick() - timer >= 200) {
    //     // 取数据
    //     while (MPU6050_Queue_Pop(&sensor_queue, &sensor_data)) {
    //         // 取最新的
    //     }
        
    //     // 显示（这里和以前完全一样）
    //     printf("Acc: X:%7.3fg Y:%7.3fg Z:%7.3fg | Gyro: X:%7.2f Y:%7.2f Z:%7.2f\r\n",
    //            sensor_data.accel_g[0], sensor_data.accel_g[1], sensor_data.accel_g[2],
    //            sensor_data.gyro_dps[0], sensor_data.gyro_dps[1], sensor_data.gyro_dps[2]);
        
    //     timer = HAL_GetTick();
    // }
    
    vTaskDelay(1);
  }
  /* USER CODE END MPUT6050_Task */
}

/* USER CODE BEGIN Header_MAX30102_Tasks */
/**
* @brief Function implementing the MAX30102_Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_MAX30102_Tasks */


/* USER CODE BEGIN Header_FusionTasks */
/**
* @brief Function implementing the Fusion_Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_FusionTasks */


/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
 
/* USER CODE END Application */

