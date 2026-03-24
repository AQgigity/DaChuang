#include "main.h"

// 在 MAX30102.h 中应该已经有这个结构体定义
// 如果没有，可以在这里定义：


extern osMessageQueueId_t MAX30102_QueueHandle;

void MAX30102_Tasks(void *argument)
{
    (void)argument;  // 消除未使用参数的警告
    
    HeartRateData_t result;
    
    /* USER CODE BEGIN StartDefaultTask */
    while (1)
    {
        static uint32_t ir_data[500];
        static uint32_t red_data[500];
        static uint16_t count = 0;
        static int32_t last_hr = 0;
        static int32_t last_spo2 = 0;
        
        uint32_t red, ir;
        
     
        extern I2C_HandleTypeDef hi2c1;
        
        if(MAX30102_ReadFIFO(&hi2c1, &red, &ir))
        {
            // 存储数据
            ir_data[count] = ir;
            red_data[count] = red;
            count++;
            
            // 使用CMSIS-RTOS延时
            osDelay(10);
            
            if(count >= 500)
            {
                int32_t hr = 0, sp = 0;
                int8_t hr_valid = 0, sp_valid = 0;
                
              
                maxim_heart_rate_and_oxygen_saturation(
                    ir_data,        // pun_ir_buffer
                    500,            // n_ir_buffer_length (注意是int32_t，但500会自动转换)
                    red_data,       // pun_red_buffer
                    &sp,            // pn_spo2
                    &sp_valid,      // pch_spo2_valid
                    &hr,            // pn_heart_rate
                    &hr_valid       // pch_hr_valid
                );
                
                // 初始化结果
                result.hr_valid = 0;
                result.spo2_valid = 0;
                result.heart_rate = 0;
                result.blood_oxygen = 0;
                
                
if(hr_valid && hr >= 50 && hr <= 180)
{
    // 直接调用滤波函数（确保函数已定义）
    result.heart_rate = simple_hr_with_exercise(hr);
    result.hr_valid = 1;
    last_hr = result.heart_rate;
    
    
    if(sp_valid && sp >= 70 && sp <= 100)
    {
        result.blood_oxygen = spo2_filter(sp);
        result.spo2_valid = 1;
        last_spo2 = result.blood_oxygen;
    }
}
                
                // ================== 关键部分 ==================
                // 1. 把结果放入队列（CubeMX生成的队列）
                if(result.hr_valid && MAX30102_QueueHandle != NULL)
                {
                    osMessageQueuePut(MAX30102_QueueHandle, &result, 0, 0);

                }

                
               
                // if(result.hr_valid)
                // {
                //     if(result.spo2_valid)
                //     {
                //         // 使用%ld格式打印int32_t
                //         printf("心率:%3ld 血氧:%2ld%%\r\n", 
                  //              (long)result.heart_rate, (long)result.blood_oxygen);
                //     }
                //     else if(last_spo2 > 0)
                //     {
                //         printf("心率:%3ld 血氧:%2ld%% (保持)\r\n", 
                //                (long)result.heart_rate, (long)last_spo2);
                //     }
                //     else
                //     {
                //         printf("心率:%3ld 血氧:--\r\n", (long)result.heart_rate);
                //     }
                // }
                // else if(last_hr > 0)
                // {
                //     printf("心率:%3ld 血氧:%2ld%% (保持)\r\n", 
                //            (long)last_hr, (long)last_spo2);
                // }
                // else
                // {
                //     printf("等待有效信号...\r\n");
                // }
               
                
                // 滑动窗口
                for(int i = 100; i < 500; i++)
                {
                    ir_data[i-100] = ir_data[i];
                    red_data[i-100] = red_data[i];
                }
                count = 400;
            }
        }
        else
        {
            // 如果没有读取到数据
            osDelay(10);
        }

    }

                 osDelay(10);
    /* USER CODE END StartDefaultTask */
}
void FusionTasks(void *argument)
{

    HeartRateData_t hrData;
    SensorData_t mpuData;
    FusionData_t fusionData;
    
    for(;;)
    {
        // 接收MAX30102
        if(osMessageQueueGet(MAX30102_QueueHandle, &hrData, NULL, 0) == osOK)
        {
            // 直接赋值（编译器自动处理，比memcpy更简洁）
            fusionData.heart_rate = hrData.heart_rate;
            fusionData.blood_oxygen = hrData.blood_oxygen;
            fusionData.hr_valid = hrData.hr_valid;
            fusionData.spo2_valid = hrData.spo2_valid;
        }
        
        // 接收MPU6050
        if(MPU6050_Queue_Pop(&sensor_queue, &mpuData) == 1)
        {
            // 数组用memcpy
            memcpy(fusionData.accel_g, mpuData.accel_g, sizeof(fusionData.accel_g));
            memcpy(fusionData.gyro_dps, mpuData.gyro_dps, sizeof(fusionData.gyro_dps));
            
                      printf("Acc: X:%7.3fg Y:%7.3fg Z:%7.3fg | Gyro: X:%7.2f Y:%7.2f Z:%7.2f\r\n",
                    fusionData.accel_g[0], 
                 fusionData.accel_g[1], 
                   fusionData.accel_g[2],
                    fusionData.gyro_dps[0], 
                   fusionData.gyro_dps[1], 
                    fusionData.gyro_dps[2]);
            

         }
            // 2. 打印MAX30102数据（根据有效性显示）
            if(fusionData.hr_valid)
            {
                if(fusionData.spo2_valid)
                {
                    printf("心率:%3ld 血氧:%2ld%%\r\n", 
                           (long)fusionData.heart_rate, 
                           (long)fusionData.blood_oxygen);
                }
                else if(fusionData.blood_oxygen > 0)
                {
                    printf("心率:%3ld 血氧:%2ld%% (保持)\r\n", 
                           (long)fusionData.heart_rate, 
                           (long)fusionData.blood_oxygen);
                }
                else
                {
                    printf("心率:%3ld 血氧:--\r\n", (long)fusionData.heart_rate);
                }
            }
            else if(fusionData.heart_rate > 0)
            {
                printf("心率:%3ld 血氧:%2ld%% (保持)\r\n", 
                       (long)fusionData.heart_rate, 
                       (long)fusionData.blood_oxygen);
            }
            else
            {
                printf("等待有效信号...\r\n");
            }
            
            printf("----------------------------------------\r\n");

       vTaskDelay(100);

    }
}