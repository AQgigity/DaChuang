---
name: watch-sensors-plan
description: BME280 + MAX30102 驱动实现方案（已完成）
metadata:
  type: project
---

# Watch_code 传感器驱动实现

## 状态：已完成

## 内容

- BME280 环境传感器（I2C 0x76，forced mode，自动检测 BMP280/BME280）
- MAX30102 心率传感器（I2C 0x57，Heart Rate mode，峰谷检测算法）
- 共用 I2C_NUM_0（SDA=GPIO5, SCL=GPIO4, 400kHz）
- i2c_bus 组件封装新版 i2c_master API

## 任务结构

| 任务 | 周期 | 职责 |
|------|------|------|
| environment_sensor_task | 100ms | BME280 温度气压 |
| heart_rate_task | 20ms | MAX30102 FIFO + 峰值检测 → BPM |
