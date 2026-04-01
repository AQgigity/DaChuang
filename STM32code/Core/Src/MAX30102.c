#include "MAX30102.h"
#include "main.h"
#include <string.h>
#include <math.h>

// ===================================================================
// 第一部分：函数声明 (必须放在文件开头)
// ===================================================================
static void maxim_peaks_above_min_height(int32_t *pn_locs, int32_t *pn_npks, int32_t *pn_x, 
                                       int32_t n_size, int32_t n_min_height);
static void maxim_remove_close_peaks(int32_t *pn_locs, int32_t *pn_npks, int32_t *pn_x, 
                                   int32_t n_min_distance);
static void maxim_sort_ascend(int32_t *pn_x, int32_t n_size);
static void maxim_sort_indices_descend(int32_t *pn_x, int32_t *pn_indx, int32_t n_size);
static void maxim_find_peaks(int32_t *pn_locs, int32_t *pn_npks, int32_t *pn_x, 
                           int32_t n_size, int32_t n_min_height, 
                           int32_t n_min_distance, int32_t n_max_num);

// ===================================================================
// 第二部分：硬件接口层
// ===================================================================
static HAL_StatusTypeDef MAX30102_WriteReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t value)
{
    return HAL_I2C_Mem_Write(hi2c, MAX30102_I2C_ADDR, reg,
                             I2C_MEMADD_SIZE_8BIT, &value, 1, 100);
}

static HAL_StatusTypeDef MAX30102_ReadReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(hi2c, MAX30102_I2C_ADDR, reg,
                            I2C_MEMADD_SIZE_8BIT, buf, len, 100);
}

// ===================================================================
// 第三部分：传感器驱动层
// ===================================================================
void MAX30102_Reset(I2C_HandleTypeDef *hi2c)
{
    MAX30102_WriteReg(hi2c, REG_MODE_CONFIG, 0x40);
    MAX30102_WriteReg(hi2c, REG_MODE_CONFIG, 0x40);
}

void MAX30102_Init(I2C_HandleTypeDef *hi2c)
{
    MAX30102_Reset(hi2c);
    HAL_Delay(10);

    MAX30102_WriteReg(hi2c, REG_INTR_ENABLE_1, 0xc0);
    MAX30102_WriteReg(hi2c, REG_INTR_ENABLE_2, 0x00);
    MAX30102_WriteReg(hi2c, REG_FIFO_WR_PTR, 0x00);
    MAX30102_WriteReg(hi2c, REG_OVF_COUNTER, 0x00);
    MAX30102_WriteReg(hi2c, REG_FIFO_RD_PTR, 0x00);
    MAX30102_WriteReg(hi2c, REG_FIFO_CONFIG, 0x0f);
    MAX30102_WriteReg(hi2c, REG_MODE_CONFIG, 0x03);
    MAX30102_WriteReg(hi2c, REG_SPO2_CONFIG, 0x27);
    MAX30102_WriteReg(hi2c, REG_LED1_PA, 0x32);
    MAX30102_WriteReg(hi2c, REG_LED2_PA, 0x32);
    MAX30102_WriteReg(hi2c, REG_PILOT_PA, 0x7f);
}

uint8_t MAX30102_ReadFIFO(I2C_HandleTypeDef *hi2c, uint32_t *pun_red_led, uint32_t *pun_ir_led)
{
    uint8_t ach_i2c_data[6];
    uint8_t uch_temp;

    MAX30102_ReadReg(hi2c, REG_INTR_STATUS_1, &uch_temp, 1);
    MAX30102_ReadReg(hi2c, REG_INTR_STATUS_2, &uch_temp, 1);

    if(MAX30102_ReadReg(hi2c, REG_FIFO_DATA, ach_i2c_data, 6) != HAL_OK) {
        return 0;
    }

    *pun_red_led = ((uint32_t)ach_i2c_data[0] << 16) |
                   ((uint32_t)ach_i2c_data[1] << 8)  |
                   (uint32_t)ach_i2c_data[2];
    *pun_ir_led  = ((uint32_t)ach_i2c_data[3] << 16) |
                   ((uint32_t)ach_i2c_data[4] << 8)  |
                   (uint32_t)ach_i2c_data[5];

    *pun_red_led &= 0x03FFFF;
    *pun_ir_led  &= 0x03FFFF;

    return 1;
}

// ===================================================================
// 第四部分：算法核心
// ===================================================================
const uint16_t auw_hamm[31]={ 41,    276,    512,    276,     41 };

const uint8_t uch_spo2_table[184]={ 
    95, 95, 95, 96, 96, 96, 97, 97, 97, 97, 97, 98, 98, 98, 98, 98, 99, 99, 99, 99,
    99, 99, 99, 99, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
    100, 100, 100, 100, 99, 99, 99, 99, 99, 99, 99, 99, 98, 98, 98, 98, 98, 98, 97, 97,
    97, 97, 96, 96, 96, 96, 95, 95, 95, 94, 94, 94, 93, 93, 93, 92, 92, 92, 91, 91,
    90, 90, 89, 89, 89, 88, 88, 87, 87, 86, 86, 85, 85, 84, 84, 83, 82, 82, 81, 81,
    80, 80, 79, 78, 78, 77, 76, 76, 75, 74, 74, 73, 72, 72, 71, 70, 69, 69, 68, 67,
    66, 66, 65, 64, 63, 62, 62, 61, 60, 59, 58, 57, 56, 56, 55, 54, 53, 52, 51, 50,
    49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 31, 30, 29,
    28, 27, 26, 25, 23, 22, 21, 20, 19, 17, 16, 15, 14, 12, 11, 10, 9, 7, 6, 5,
    3, 2, 1 
};

static int32_t an_dx[BUFFER_SIZE-MA4_SIZE];
static int32_t an_x[BUFFER_SIZE];
static int32_t an_y[BUFFER_SIZE];

// 主算法函数
void maxim_heart_rate_and_oxygen_saturation(uint32_t *pun_ir_buffer, int32_t n_ir_buffer_length, 
                                          uint32_t *pun_red_buffer, int32_t *pn_spo2, 
                                          int8_t *pch_spo2_valid, int32_t *pn_heart_rate, 
                                          int8_t *pch_hr_valid)
{
    uint32_t un_ir_mean, un_only_once;
    int32_t k, n_i_ratio_count;
    int32_t i, s, m, n_exact_ir_valley_locs_count, n_middle_idx;
    int32_t n_th1, n_npks, n_c_min;
    int32_t an_ir_valley_locs[15];
    int32_t an_exact_ir_valley_locs[15];
    int32_t an_dx_peak_locs[15];
    int32_t n_peak_interval_sum;

    int32_t n_y_ac, n_x_ac;
    int32_t n_spo2_calc;
    int32_t n_y_dc_max, n_x_dc_max;
    int32_t n_y_dc_max_idx, n_x_dc_max_idx;
    int32_t an_ratio[5], n_ratio_average;
    int32_t n_nume, n_denom;

    // 去除IR信号的直流分量
    un_ir_mean = 0;
    for (k = 0; k < n_ir_buffer_length; k++) 
        un_ir_mean += pun_ir_buffer[k];
    un_ir_mean = un_ir_mean / n_ir_buffer_length;
    
    for (k = 0; k < n_ir_buffer_length; k++)  
        an_x[k] = pun_ir_buffer[k] - un_ir_mean;

    // 4点移动平均
    for(k = 0; k < BUFFER_SIZE - MA4_SIZE; k++) {
        n_denom = (an_x[k] + an_x[k+1] + an_x[k+2] + an_x[k+3]);
        an_x[k] = n_denom / (int32_t)4;
    }

    // 计算差分
    for(k = 0; k < BUFFER_SIZE - MA4_SIZE - 1; k++)
        an_dx[k] = (an_x[k+1] - an_x[k]);

    // 2点移动平均
    for(k = 0; k < BUFFER_SIZE - MA4_SIZE - 2; k++) {
        an_dx[k] = (an_dx[k] + an_dx[k+1]) / 2;
    }

    // 汉明窗滤波
    for(i = 0; i < BUFFER_SIZE - HAMMING_SIZE - MA4_SIZE - 2; i++) {
        s = 0;
        for(k = i; k < i + HAMMING_SIZE; k++) {
            s -= an_dx[k] * auw_hamm[k-i];
        }
        an_dx[i] = s / (int32_t)1146;
    }

    // 计算自适应阈值
    n_th1 = 0;
    for(k = 0; k < BUFFER_SIZE - HAMMING_SIZE; k++) {
        n_th1 += ((an_dx[k] > 0) ? an_dx[k] : ((int32_t)0 - an_dx[k]));
    }
    n_th1 = n_th1 / (BUFFER_SIZE - HAMMING_SIZE);

    // 寻找峰值
    maxim_find_peaks(an_dx_peak_locs, &n_npks, an_dx, BUFFER_SIZE - HAMMING_SIZE, n_th1, 8, 5);

    // 计算心率
    n_peak_interval_sum = 0;
    if(n_npks >= 2) {
        for(k = 1; k < n_npks; k++)
            n_peak_interval_sum += (an_dx_peak_locs[k] - an_dx_peak_locs[k-1]);
        n_peak_interval_sum = n_peak_interval_sum / (n_npks - 1);
        *pn_heart_rate = (int32_t)(6000 / n_peak_interval_sum);
        *pch_hr_valid = 1;
    }
    else {
        *pn_heart_rate = -999;
        *pch_hr_valid = 0;
    }

    // 寻找IR信号的精确谷值位置
    for(k = 0; k < n_npks; k++)
        an_ir_valley_locs[k] = an_dx_peak_locs[k] + HAMMING_SIZE / 2;

    // 准备红光和红外原始数据
    for(k = 0; k < n_ir_buffer_length; k++) {
        an_x[k] = pun_ir_buffer[k];
        an_y[k] = pun_red_buffer[k];
    }

    // 寻找精确的IR谷值
    n_exact_ir_valley_locs_count = 0;
    for(k = 0; k < n_npks; k++) {
        un_only_once = 1;
        m = an_ir_valley_locs[k];
        n_c_min = 16777216;
        if(m + 5 < BUFFER_SIZE - HAMMING_SIZE && m - 5 > 0) {
            for(i = m - 5; i < m + 5; i++) {
                if(an_x[i] < n_c_min) {
                    if(un_only_once > 0) {
                        un_only_once = 0;
                    }
                    n_c_min = an_x[i];
                    an_exact_ir_valley_locs[k] = i;
                }
            }
            if(un_only_once == 0)
                n_exact_ir_valley_locs_count++;
        }
    }

    if(n_exact_ir_valley_locs_count < 2) {
        *pn_spo2 = -999;
        *pch_spo2_valid = 0;
        return;
    }

    // 4点移动平均（用于血氧计算）
    for(k = 0; k < BUFFER_SIZE - MA4_SIZE; k++) {
        an_x[k] = (an_x[k] + an_x[k+1] + an_x[k+2] + an_x[k+3]) / (int32_t)4;
        an_y[k] = (an_y[k] + an_y[k+1] + an_y[k+2] + an_y[k+3]) / (int32_t)4;
    }

    // 计算血氧饱和度
    n_ratio_average = 0;
    n_i_ratio_count = 0;
    for(k = 0; k < 5; k++) 
        an_ratio[k] = 0;

    for(k = 0; k < n_exact_ir_valley_locs_count - 1; k++) {
        n_y_dc_max = -16777216;
        n_x_dc_max = -16777216;
        if(an_exact_ir_valley_locs[k+1] - an_exact_ir_valley_locs[k] > 10) {
            for(i = an_exact_ir_valley_locs[k]; i < an_exact_ir_valley_locs[k+1]; i++) {
                if(an_x[i] > n_x_dc_max) {
                    n_x_dc_max = an_x[i];
                    n_x_dc_max_idx = i;
                }
                if(an_y[i] > n_y_dc_max) {
                    n_y_dc_max = an_y[i];
                    n_y_dc_max_idx = i;
                }
            }
            
            n_y_ac = (an_y[an_exact_ir_valley_locs[k+1]] - an_y[an_exact_ir_valley_locs[k]]) * 
                    (n_y_dc_max_idx - an_exact_ir_valley_locs[k]);
            n_y_ac = an_y[an_exact_ir_valley_locs[k]] + 
                    n_y_ac / (an_exact_ir_valley_locs[k+1] - an_exact_ir_valley_locs[k]);
            n_y_ac = an_y[n_y_dc_max_idx] - n_y_ac;
            
            n_x_ac = (an_x[an_exact_ir_valley_locs[k+1]] - an_x[an_exact_ir_valley_locs[k]]) * 
                    (n_x_dc_max_idx - an_exact_ir_valley_locs[k]);
            n_x_ac = an_x[an_exact_ir_valley_locs[k]] + 
                    n_x_ac / (an_exact_ir_valley_locs[k+1] - an_exact_ir_valley_locs[k]);
            n_x_ac = an_x[n_y_dc_max_idx] - n_x_ac;
            
            n_nume = (n_y_ac * n_x_dc_max) >> 7;
            n_denom = (n_x_ac * n_y_dc_max) >> 7;
            
            if(n_denom > 0 && n_i_ratio_count < 5 && n_nume != 0) {
                an_ratio[n_i_ratio_count] = (n_nume * 20) / n_denom;
                n_i_ratio_count++;
            }
        }
    }

    // 计算平均比值并查表获取血氧值
    maxim_sort_ascend(an_ratio, n_i_ratio_count);
    n_middle_idx = n_i_ratio_count / 2;

    if(n_middle_idx > 1)
        n_ratio_average = (an_ratio[n_middle_idx-1] + an_ratio[n_middle_idx]) / 2;
    else
        n_ratio_average = an_ratio[n_middle_idx];

    if(n_ratio_average > 2 && n_ratio_average < 184) {
        n_spo2_calc = uch_spo2_table[n_ratio_average];
        *pn_spo2 = n_spo2_calc;
        *pch_spo2_valid = 1;
    }
    else {
        *pn_spo2 = -999;
        *pch_spo2_valid = 0;
    }
}

// ===================================================================
// 第五部分：辅助算法函数
// ===================================================================
static void maxim_find_peaks(int32_t *pn_locs, int32_t *pn_npks, int32_t *pn_x, 
                           int32_t n_size, int32_t n_min_height, 
                           int32_t n_min_distance, int32_t n_max_num)
{
    maxim_peaks_above_min_height(pn_locs, pn_npks, pn_x, n_size, n_min_height);
    maxim_remove_close_peaks(pn_locs, pn_npks, pn_x, n_min_distance);
    *pn_npks = (*pn_npks < n_max_num) ? *pn_npks : n_max_num;
}

static void maxim_peaks_above_min_height(int32_t *pn_locs, int32_t *pn_npks, 
                                       int32_t *pn_x, int32_t n_size, int32_t n_min_height)
{
    int32_t i = 1, n_width;
    *pn_npks = 0;

    while(i < n_size - 1) {
        if(pn_x[i] > n_min_height && pn_x[i] > pn_x[i-1]) {
            n_width = 1;
            while(i + n_width < n_size && pn_x[i] == pn_x[i + n_width])
                n_width++;
            if(pn_x[i] > pn_x[i + n_width] && (*pn_npks) < 15) {
                pn_locs[(*pn_npks)++] = i;
                i += n_width + 1;
            }
            else {
                i += n_width;
            }
        }
        else {
            i++;
        }
    }
}

static void maxim_remove_close_peaks(int32_t *pn_locs, int32_t *pn_npks, 
                                   int32_t *pn_x, int32_t n_min_distance)
{
    int32_t i, j, n_old_npks, n_dist;

    maxim_sort_indices_descend(pn_x, pn_locs, *pn_npks);
    
    for(i = -1; i < *pn_npks; i++) {
        n_old_npks = *pn_npks;
        *pn_npks = i + 1;
        for(j = i + 1; j < n_old_npks; j++) {
            n_dist = pn_locs[j] - (i == -1 ? -1 : pn_locs[i]);
            if(n_dist > n_min_distance || n_dist < -n_min_distance) {
                pn_locs[(*pn_npks)++] = pn_locs[j];
            }
        }
    }
    
    maxim_sort_ascend(pn_locs, *pn_npks);
}

static void maxim_sort_ascend(int32_t *pn_x, int32_t n_size)
{
    int32_t i, j, n_temp;
    for(i = 1; i < n_size; i++) {
        n_temp = pn_x[i];
        for(j = i; j > 0 && n_temp < pn_x[j-1]; j--) {
            pn_x[j] = pn_x[j-1];
        }
        pn_x[j] = n_temp;
    }
}

static void maxim_sort_indices_descend(int32_t *pn_x, int32_t *pn_indx, int32_t n_size)
{
    int32_t i, j, n_temp;
    for(i = 1; i < n_size; i++) {
        n_temp = pn_indx[i];
        for(j = i; j > 0 && pn_x[n_temp] > pn_x[pn_indx[j-1]]; j--) {
            pn_indx[j] = pn_indx[j-1];
        }
        pn_indx[j] = n_temp;
    }
}
// 在MAX30102.c文件末尾添加以下函数：

// ===================================================================
// 第六部分：数据收集和计算包装函数
// ===================================================================

// 初始化数据结构
void MAX30102_InitData(MAX30102_Data_t *data)
{
    memset(data, 0, sizeof(MAX30102_Data_t));
}

// 收集数据到缓冲区
void MAX30102_CollectData(I2C_HandleTypeDef *hi2c, MAX30102_Data_t *data)
{
    uint32_t red, ir;
    
    if(MAX30102_ReadFIFO(hi2c, &red, &ir))
    {
        // 存储到环形缓冲区
        data->ir_buffer[data->index] = ir;
        data->red_buffer[data->index] = red;
        
        // 更新索引
        data->index = (data->index + 1) % BUFFER_SIZE;
        data->count++;
        
        // 如果缓冲区填满一圈，标记为就绪
        if(data->index == 0 && !data->ready)
        {
            data->ready = 1;
        }
    }
}

// 计算心率和血氧（包装函数）
void MAX30102_CalculateHR_SpO2(MAX30102_Data_t *data,
                              int32_t *heart_rate, int32_t *spo2,
                              int8_t *hr_valid, int8_t *spo2_valid)
{
    // 如果缓冲区未就绪，返回无效数据
    if(!data->ready || data->count < BUFFER_SIZE)
    {
        *heart_rate = 0;
        *spo2 = 0;
        *hr_valid = 0;
        *spo2_valid = 0;
        return;
    }
    
    // 使用官方算法计算心率和血氧
    maxim_heart_rate_and_oxygen_saturation(
        data->ir_buffer,
        BUFFER_SIZE,
        data->red_buffer,
        spo2,
        spo2_valid,
        heart_rate,
        hr_valid
    );
}
int32_t simple_hr_with_exercise(int32_t raw_hr)
{
    static int32_t smoothed = 85;
    static int32_t last_hr = 85;
    static uint8_t exercise_mode = 0;
    
    // 有效性检查
    if(raw_hr < 50 || raw_hr > 180) return smoothed;
    
    // 检测是否在运动（心率持续高于100）
    static uint8_t high_hr_count = 0;
    if(raw_hr > 110)
    {
        high_hr_count++;
        if(high_hr_count > 6)  // 持续高心率
        {
            exercise_mode = 1;
        }
    }
    else
    {
        high_hr_count = 0;
        if(raw_hr < 90)  // 低心率持续
        {
            exercise_mode = 0;
        }
    }
    
    // 根据模式选择滤波强度
    if(exercise_mode)
    {
        // 运动模式：弱滤波，快速响应 (alpha=0.75)
        smoothed = (raw_hr * 3 + smoothed) / 4;
    }
    else
    {
        // 静息模式：强滤波，稳定 (alpha=0.125)
        smoothed = (raw_hr + smoothed * 7) / 8;
    }
    
    last_hr = raw_hr;
    return smoothed;
}

// 血氧滤波函数
int32_t spo2_filter(int32_t raw_spo2)
{
    static int32_t smoothed = 98;
    
    // 有效性检查
    if(raw_spo2 < 70 || raw_spo2 > 100) return smoothed;
    
    // 强滤波保持稳定 (alpha=0.2)
    smoothed = (raw_spo2 + smoothed * 4) / 5;
    
    return smoothed;
}

// 信号质量检测
uint8_t check_signal_quality(uint32_t *ir_data, uint16_t count)
{
    if(count < 10) return 0;
    
    // 检查最近10个点的变化
    uint32_t min_val = 0xFFFFFFFF;
    uint32_t max_val = 0;
    
    int start_idx = (count > 10) ? (count - 10) : 0;
    
    for(int i = start_idx; i < count; i++)
    {
        if(ir_data[i] < min_val) min_val = ir_data[i];
        if(ir_data[i] > max_val) max_val = ir_data[i];
    }
    
    uint32_t amplitude = max_val - min_val;
    
    // 如果信号变化太小，认为没有手指
    if(amplitude < 50)
    {
        return 0;  // 信号差
    }
    
    return 1;  // 信号好
}