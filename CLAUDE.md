# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

步态分析/健康监测可穿戴设备固件（大学生创新创业项目"DaChuang"）。

- **ESP32-S3** (`ESP32code/`) — ESP-IDF v5.4.3 + NimBLE，Nordic UART Service (NUS) 蓝牙通信。设备名：`ESP32_Gait_Gatt`。
- **Watch_code/** — ESP-IDF v5.4.3，智能运动手表（BME280 环境传感器 + MAX30102 心率传感器）。

当前功能：50Hz 采集 MPU6050（IMU 6轴）+ FSR402（足压），Edge Impulse 实时推理 5 类动作（jump/ready/run/still/walk），步频（Cadence）检测，BLE 推送中文识别结果。

## 构建命令

需先激活 ESP-IDF v5.4.3 环境，或使用 VS Code ESP-IDF 扩展：

**ESP32code（步态分析）**：
```bash
cd ESP32code
idf.py build
idf.py -p COMx flash monitor
```

**Watch_code（智能手表）**：
```bash
cd Watch_code
idf.py build
idf.py -p COMx flash monitor
```

清理重建（SDK 结构变更后必须）：
```bash
idf.py fullclean
idf.py build
```

## 架构

### ESP32code（步态分析）

```
ESP32code/
  main/main.cpp              — 单文件：BLE 服务、传感器采集、推理、步频检测
  components/
    mpu6050/                  — MPU6050 I2C 驱动（GPIO8/SDA, GPIO9/SCL, 400kHz）
    fsr402/                   — FSR402 ADC 驱动（ADC_UNIT_1, CH4, GPIO5, 阈值800）
    edge-impulse-sdk/         — EI C++ SDK + TFLite 模型（INT8 量化）
    esp_littlefs/             — LittleFS（已存在但未接入构建）
    ble_gait/, ble_spp/, storage/ — 空脚手架，未实现
  sdkconfig.defaults          — NimBLE 配置（编辑此文件，不要直接改 sdkconfig）
```

### Watch_code（智能运动手表）

```
Watch_code/
  main/main.c                — 传感器任务入口
  components/
    i2c_bus/                  — I2C 总线抽象（新 i2c_master API，SDA=GPIO5, SCL=GPIO4, 400kHz）
    bme280/                   — BME280/BMP280 驱动（I2C 0x76，forced mode，支持两种芯片自动检测）
    max30102/                 — MAX30102 心率驱动（I2C 0x57，Heart Rate mode，峰谷检测算法）
    display/                  — NV3030B SPI 显示驱动（当前未编译，待换 ST7789V3）
    UI/                       — SquareLine Studio 生成的 LVGL UI（3 个标签：HR/Temp/Press）
    lv_conf.h                 — LVGL 配置（16bit color, LV_COLOR_16_SWAP=0）
    code/, STM32F103C8T6_SPILCD/, LCD_1in83/ — NV3030B 参考代码（不编译）
  sdkconfig.defaults          — ESP32-S3 目标，SPI master IRAM
```

#### main.c 任务结构

| 任务 | 栈大小 | 优先级 | 周期 | 职责 |
|------|--------|--------|------|------|
| `environment_sensor_task` | 4096 | 5 | 100ms | BME280 读取温湿度气压 |
| `heart_rate_task` | 4096 | 4 | 20ms | MAX30102 FIFO 读取 + 峰值检测 → BPM |

#### MAX30102 心率算法

- DC 偏移：EMA (alpha=1/64)
- 峰值检测：上升→下降转折点，局部最大-最小幅度 ≥ 400 才算有效峰
- 不应期：350ms（防止同一心跳重复计数）
- 有效间隔：300ms–1500ms（40–200 BPM）
- 异常值剔除：偏离当前 BPM >40% 则跳过
- 平滑：长度 5 滑动平均
- 超时：3 秒无峰则 BPM 归零

#### I2C 总线

两个传感器共用 I2C_NUM_0（SDA=GPIO5, SCL=GPIO4, 400kHz）。`i2c_bus` 组件封装了 ESP-IDF 新版 `i2c_master` API，提供 `i2c_bus_add_device()` / `i2c_master_transmit()` 等接口。

#### 显示状态

NV3030B 驱动已写好但因 DC 引脚未接导致白屏，已从 main.c 移除。计划换用 ST7789V3（ESP-IDF 内置 `esp_lcd_new_panel_st7789()` 支持）。display 组件和 UI 组件保留在代码中，待新屏幕到货后重新集成。

### ESP32code main.cpp 任务结构

| 任务 | 栈大小 | 周期 | 职责 |
|------|--------|------|------|
| `sensor_data_task` | 8192 | 20ms (50Hz) | 读传感器 → 滑动窗口 → 步频上升沿检测 → 每5样本通知推理 |
| `inference_task` | 20480 | 按信号量 | run_classifier → 中文标签 + SPM → BLE Notify |

### Edge Impulse 模型参数

- 窗口：75 样本 × 7 轴 = 525 floats（1.5s @ 50Hz）
- DSP：Spectral FFT(128) + Raw features → NN 输入 376
- 输出：5 类（jump, ready, run, still, walk），INT8 量化
- 每 100ms（5 组新数据）触发一次推理

### 步频 (Cadence) 算法

- FSR402 上升沿检测（0→1），300ms 死区防抖
- 计算相邻落地间隔 → SPM = 60 / T
- 长度 5 滑动平均，有效范围 40–220 SPM
- "still"/"ready" 状态时 SPM 强制清零

## BLE 协议

手机通过 RX 特征写入单字节指令：
- `0x72` ('r')：启动传感器 + 推理任务
- `0x73` ('s')：停止

设备通过 TX Notify 发送 UTF-8 字符串，格式：`"行为：走，步频：112\n"`

标签映射：jump→跳, ready→准备, run→跑, still→静止, walk→走

若 MTU < 载荷大小，数据按块分片发送，片间延时 20ms。手机端需按换行符重新拼接。

## 关键设计约束

### ESP32code
- 数据通路无堆分配：`ei_buf[525]` 静态缓冲区，`tx_buf[64]` 发送缓冲
- `ble_att_set_preferred_mtu(247)` 在 `app_main()` 显式调用
- `vTaskDelayUntil` 保证严格 50Hz 采集周期
- Edge Impulse SDK 编译警告通过 `target_compile_options(PUBLIC)` 传播至 main.cpp
- `esp_lcd` 组件已排除（GCC 14.2.0 ICE bug）
- 不适用于 ESP32-S3 的 ARM 编译标志（`-mfloat-abi=hard` 等）不要添加

### Watch_code
- 两个传感器共用 I2C_NUM_0，地址不冲突（BME280=0x76, MAX30102=0x57）
- MAX30102 心率算法参数已调优：MIN_RANGE=400, REFRACTORY_MS=350（解决静坐 BPM 偏高问题）
- NV3030B 显示驱动因硬件问题（DC 引脚未接）已从构建中移除，待换 ST7789V3
- LVGL 8.3.11 + SquareLine Studio UI 组件保留在代码中，未接入构建
- `sdkconfig.defaults` 设置 `CONFIG_SPI_MASTER_IN_IRAM=y`（显示组件需要）

## 配置修改指南

- BLE 设置 → 编辑 `sdkconfig.defaults`，删除 `build/` 后重建
- 传感器引脚 → 修改对应组件 `include/*.h` 中的宏定义
- EI 模型 → 替换 `edge-impulse-sdk/` 目录并更新 `tflite-model/`
- Watch_code 传感器引脚 → I2C: `main/main.c` 中 `i2c_bus_config_t`；显示 SPI: `components/display/include/display.h`
- Watch_code MAX30102 算法参数 → `components/max30102/src/max30102.c` 顶部宏定义
- Watch_code LVGL 配置 → `components/lv_conf.h`

## 设计文档归档

所有设计方案、计划表、技术决策完成后必须归档到 `memory/` 目录，不能只存在 `.claude/plans/`（会被覆盖）。

归档格式：`memory/plan_archive_<任务名>.md`，使用 `metadata.type: project`，在 `memory/MEMORY.md` 中添加索引条目。
