/**
 * @file mpu6050_reg.h
 * @brief MPU6050 寄存器定义
 */

#ifndef MPU6050_REG_H
#define MPU6050_REG_H

#ifdef __cplusplus
extern "C" {
#endif

/* MPU6050 寄存器地址 */
#define MPU6050_SMPLRT_DIV      0x19    // 采样率分频器
#define MPU6050_CONFIG          0x1A    // 配置寄存器
#define MPU6050_GYRO_CONFIG     0x1B    // 陀螺仪配置寄存器
#define MPU6050_ACCEL_CONFIG    0x1C    // 加速度计配置寄存器
#define MPU6050_FF_THR          0x1D    // 自由坠落阈值
#define MPU6050_FF_DUR          0x1E    // 自由坠落持续时间
#define MPU6050_MOT_THR         0x1F    // 运动检测阈值
#define MPU6050_MOT_DUR         0x20    // 运动检测持续时间
#define MPU6050_ZRMOT_THR       0x21    // 零运动检测阈值
#define MPU6050_ZRMOT_DUR       0x22    // 零运动检测持续时间
#define MPU6050_FIFO_EN         0x23    // FIFO使能
#define MPU6050_I2C_MST_CTRL    0x24    // I2C主机控制
#define MPU6050_I2C_SLV0_ADDR   0x25    // I2C从机0地址
#define MPU6050_I2C_SLV0_REG    0x26    // I2C从机0寄存器
#define MPU6050_I2C_SLV0_CTRL   0x27    // I2C从机0控制
#define MPU6050_I2C_SLV1_ADDR   0x28    // I2C从机1地址
#define MPU6050_I2C_SLV1_REG    0x29    // I2C从机1寄存器
#define MPU6050_I2C_SLV1_CTRL   0x2A    // I2C从机1控制
#define MPU6050_I2C_SLV2_ADDR   0x2B    // I2C从机2地址
#define MPU6050_I2C_SLV2_REG    0x2C    // I2C从机2寄存器
#define MPU6050_I2C_SLV2_CTRL   0x2D    // I2C从机2控制
#define MPU6050_I2C_SLV3_ADDR   0x2E    // I2C从机3地址
#define MPU6050_I2C_SLV3_REG    0x2F    // I2C从机3寄存器
#define MPU6050_I2C_SLV3_CTRL   0x30    // I2C从机3控制
#define MPU6050_I2C_SLV4_ADDR   0x31    // I2C从机4地址
#define MPU6050_I2C_SLV4_REG    0x32    // I2C从机4寄存器
#define MPU6050_I2C_SLV4_DO     0x33    // I2C从机4数据输出
#define MPU6050_I2C_SLV4_CTRL   0x34    // I2C从机4控制
#define MPU6050_I2C_SLV4_DI     0x35    // I2C从机4数据输入
#define MPU6050_I2C_MST_STATUS  0x36    // I2C主机状态
#define MPU6050_INT_PIN_CFG     0x37    // 中断引脚配置
#define MPU6050_INT_ENABLE      0x38    // 中断使能
#define MPU6050_INT_STATUS      0x3A    // 中断状态
#define MPU6050_ACCEL_XOUT_H    0x3B    // 加速度计X轴高字节
#define MPU6050_ACCEL_XOUT_L    0x3C    // 加速度计X轴低字节
#define MPU6050_ACCEL_YOUT_H    0x3D    // 加速度计Y轴高字节
#define MPU6050_ACCEL_YOUT_L    0x3E    // 加速度计Y轴低字节
#define MPU6050_ACCEL_ZOUT_H    0x3F    // 加速度计Z轴高字节
#define MPU6050_ACCEL_ZOUT_L    0x40    // 加速度计Z轴低字节
#define MPU6050_TEMP_OUT_H      0x41    // 温度高字节
#define MPU6050_TEMP_OUT_L      0x42    // 温度低字节
#define MPU6050_GYRO_XOUT_H     0x43    // 陀螺仪X轴高字节
#define MPU6050_GYRO_XOUT_L     0x44    // 陀螺仪X轴低字节
#define MPU6050_GYRO_YOUT_H     0x45    // 陀螺仪Y轴高字节
#define MPU6050_GYRO_YOUT_L     0x46    // 陀螺仪Y轴低字节
#define MPU6050_GYRO_ZOUT_H     0x47    // 陀螺仪Z轴高字节
#define MPU6050_GYRO_ZOUT_L     0x48    // 陀螺仪Z轴低字节
#define MPU6050_EXT_SENS_DATA_00 0x49  // 外部传感器数据00
#define MPU6050_EXT_SENS_DATA_01 0x4A  // 外部传感器数据01
#define MPU6050_EXT_SENS_DATA_02 0x4B  // 外部传感器数据02
#define MPU6050_EXT_SENS_DATA_03 0x4C  // 外部传感器数据03
#define MPU6050_EXT_SENS_DATA_04 0x4D  // 外部传感器数据04
#define MPU6050_EXT_SENS_DATA_05 0x4E  // 外部传感器数据05
#define MPU6050_EXT_SENS_DATA_06 0x4F  // 外部传感器数据06
#define MPU6050_EXT_SENS_DATA_07 0x50  // 外部传感器数据07
#define MPU6050_EXT_SENS_DATA_08 0x51  // 外部传感器数据08
#define MPU6050_EXT_SENS_DATA_09 0x52  // 外部传感器数据09
#define MPU6050_EXT_SENS_DATA_10 0x53  // 外部传感器数据10
#define MPU6050_EXT_SENS_DATA_11 0x54  // 外部传感器数据11
#define MPU6050_EXT_SENS_DATA_12 0x55  // 外部传感器数据12
#define MPU6050_EXT_SENS_DATA_13 0x56  // 外部传感器数据13
#define MPU6050_EXT_SENS_DATA_14 0x57  // 外部传感器数据14
#define MPU6050_EXT_SENS_DATA_15 0x58  // 外部传感器数据15
#define MPU6050_EXT_SENS_DATA_16 0x59  // 外部传感器数据16
#define MPU6050_EXT_SENS_DATA_17 0x5A  // 外部传感器数据17
#define MPU6050_EXT_SENS_DATA_18 0x5B  // 外部传感器数据18
#define MPU6050_EXT_SENS_DATA_19 0x5C  // 外部传感器数据19
#define MPU6050_EXT_SENS_DATA_20 0x5D  // 外部传感器数据20
#define MPU6050_EXT_SENS_DATA_21 0x5E  // 外部传感器数据21
#define MPU6050_EXT_SENS_DATA_22 0x5F  // 外部传感器数据22
#define MPU6050_EXT_SENS_DATA_23 0x60  // 外部传感器数据23
#define MPU6050_MOT_DETECT_STATUS 0x61  // 运动检测状态
#define MPU6050_I2C_SLV0_DO      0x63    // I2C从机0数据输出
#define MPU6050_I2C_SLV1_DO      0x64    // I2C从机1数据输出
#define MPU6050_I2C_SLV2_DO      0x65    // I2C从机2数据输出
#define MPU6050_I2C_SLV3_DO      0x66    // I2C从机3数据输出
#define MPU6050_I2C_MST_DELAY_CTRL 0x67 // I2C主机延时控制
#define MPU6050_SIGNAL_PATH_RESET 0x68  // 信号路径复位
#define MPU6050_MOT_DETECT_CTRL  0x69    // 运动检测控制
#define MPU6050_USER_CTRL        0x6A    // 用户控制
#define MPU6050_PWR_MGMT_1       0x6B    // 电源管理寄存器1
#define MPU6050_PWR_MGMT_2       0x6C    // 电源管理寄存器2
#define MPU6050_BANK_SEL         0x6D    // 存储库选择
#define MPU6050_MEM_START_ADDR   0x6E    // 存储起始地址
#define MPU6050_MEM_R_W          0x6F    // 存储读写
#define MPU6050_DMP_CFG_1        0x70    // DMP配置1
#define MPU6050_DMP_CFG_2        0x71    // DMP配置2
#define MPU6050_FIFO_COUNTH      0x72    // FIFO计数高字节
#define MPU6050_FIFO_COUNTL      0x73    // FIFO计数低字节
#define MPU6050_FIFO_R_W         0x74    // FIFO读写
#define MPU6050_WHO_AM_I         0x75    // 设备ID寄存器

/* 陀螺仪量程配置 */
#define MPU6050_GYRO_250DPS     0x00    // ±250°/s
#define MPU6050_GYRO_500DPS     0x08    // ±500°/s
#define MPU6050_GYRO_1000DPS    0x10    // ±1000°/s
#define MPU6050_GYRO_2000DPS    0x18    // ±2000°/s

/* 加速度计量程配置 */
#define MPU6050_ACCEL_2G        0x00    // ±2g
#define MPU6050_ACCEL_4G        0x08    // ±4g
#define MPU6050_ACCEL_8G        0x10    // ±8g
#define MPU6050_ACCEL_16G       0x18    // ±16g

/* 陀螺仪灵敏度 (LSB/°/s) */
#define MPU6050_GYRO_SENSITIVITY_250DPS  131.0f
#define MPU6050_GYRO_SENSITIVITY_500DPS  65.5f
#define MPU6050_GYRO_SENSITIVITY_1000DPS 32.8f
#define MPU6050_GYRO_SENSITIVITY_2000DPS 16.4f

/* 加速度计灵敏度 (LSB/g) */
#define MPU6050_ACCEL_SENSITIVITY_2G     16384.0f
#define MPU6050_ACCEL_SENSITIVITY_4G     8192.0f
#define MPU6050_ACCEL_SENSITIVITY_8G     4096.0f
#define MPU6050_ACCEL_SENSITIVITY_16G    2048.0f

/* DLPF (数字低通滤波器) 配置 */
#define MPU6050_DLPF_260HZ      0x00    // 260Hz, 0ms延迟
#define MPU6050_DLPF_184HZ      0x01    // 184Hz, 2ms延迟
#define MPU6050_DLPF_94HZ       0x02    // 94Hz, 3ms延迟
#define MPU6050_DLPF_44HZ       0x03    // 44Hz, 5ms延迟
#define MPU6050_DLPF_21HZ       0x04    // 21Hz, 9ms延迟
#define MPU6050_DLPF_10HZ       0x05    // 10Hz, 17ms延迟
#define MPU6050_DLPF_5HZ        0x06    // 5Hz, 33ms延迟

#ifdef __cplusplus
}
#endif

#endif // MPU6050_REG_H
