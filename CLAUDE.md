# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

步态分析/健康监测可穿戴设备固件（大学生创新创业项目"DaChuang"）。

- **ESP32-S3** (`ESP32code/`) — ESP-IDF v5.4.3 + NimBLE，Nordic UART Service (NUS) 蓝牙通信。设备名：`ESP32_Gait_Gatt`。
- **Watch_code/** — ESP-IDF v5.4.3，智能运动手表（BME280 环境传感器 + MAX30102 心率传感器），BLE Central + WiFi + MQTT 上报 OneNET 云平台。

当前功能：50Hz 采集 MPU6050（IMU 6轴）+ 双 FSR402（前掌+后跟足压），Edge Impulse 实时推理 5 类动作（jump/ready/run/still/walk），步频（Cadence）检测，发力模式识别（前脚掌/后脚跟/全掌），BLE 推送中文识别结果。

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
    fsr402/                   — FSR402 ADC 驱动，双通道：Heel=CH4/GPIO5, Toe=CH0/GPIO1, 阈值800
    edge-impulse-sdk/         — EI C++ SDK + TFLite 模型（INT8 量化）
    esp_littlefs/             — LittleFS（已存在但未接入构建）
    ble_gait/, ble_spp/, storage/ — 空脚手架，未实现
  sdkconfig.defaults          — NimBLE 配置（编辑此文件，不要直接改 sdkconfig）
```

### Watch_code（智能运动手表）

```
Watch_code/
  main/main.c                — 单文件：BLE Central + 传感器 + WiFi + MQTT + 云平台控制
  components/
    i2c_bus/                  — I2C 总线抽象（新 i2c_master API，SDA=GPIO5, SCL=GPIO4, 400kHz）
    bme280/                   — BME280/BMP280 驱动（I2C 0x76，forced mode，支持两种芯片自动检测）
    max30102/                 — MAX30102 心率驱动（I2C 0x57，Heart Rate mode，峰谷检测算法）
    display/                  — ST7789V3 SPI 显示驱动（240x280, LVGL 集成）
    UI/                       — SquareLine Studio 生成的 LVGL UI（8 个数据标签：HR/气压/温度/状态/可信度/步频/发力/挥臂）
    lv_conf.h                 — LVGL 配置（16bit color, LV_COLOR_16_SWAP=1）
    code/, STM32F103C8T6_SPILCD/, LCD_1in83/ — NV3030B 参考代码（不编译）
  sdkconfig.defaults          — ESP32-S3 目标，NIMBLE Central，16MB flash，自定义分区表
  partitions.csv              — 自定义分区表（app 分区 2MB）
```

#### main.c 任务结构

| 任务 | 栈大小 | 优先级 | 周期 | 职责 |
|------|--------|--------|------|------|
| `environment_sensor_task` | 4096 | 5 | 100ms | BME280 读取温度气压 |
| `heart_rate_task` | 4096 | 4 | 20ms | MAX30102 FIFO 读取 + 峰值检测 → BPM |
| `lvgl_task` | 4096 | 3 | 10ms | LVGL 事件循环（lv_timer_handler） |
| `ui_refresh_task` | 4096 | 2 | 200ms | 更新 UI 标签（HR/Temp/Press） |
| `data_fusion_task` | 4096 | 2 | 1s 队列阻塞 | 融合 BLE 脚踝数据 + 本地传感器，更新 g_latest_ankle |
| `mqtt_upload_task` | 4096 | 1 | 2s | 读取融合数据 → JSON → MQTT 上报 OneNET |
| `ble_scan_retry_task` | 2048 | 1 | 10s | BLE 未连接时定期重新扫描 |
| `wifi_start_task` | 4096 | 3 | 一次性 | BLE 连接后延迟启动 WiFi + MQTT（避免共存冲突） |
| NimBLE Host | — | — | 事件驱动 | BLE Central 扫描/连接/收通知 |

#### BLE Central + WiFi 共存

手表同时运行 BLE Central（接收脚踝通知）和 WiFi（MQTT 上报），共享 2.4GHz 射频时分复用。

启动流程：
```
开机 → BLE 扫描 → 连接脚踝 → 使能通知 → 发 'r' 启动脚踝
                    ↓ (2秒后)
              暂停 BLE 通知 → WiFi 连接 → MQTT 连接 → 订阅控制 topic → 恢复 BLE 通知
                    ↓
              BLE + WiFi 并行（BLE 每 100ms 收通知，WiFi 每 2s 上报）
```

BLE 通知分片拼接：`g_rx_buf[128]` 缓冲区按 `\n` 切分完整消息，通过队列传给 fusion task。

#### OneNET 云平台集成

- **MQTT Broker**：`mqtt://mqtts.heclouds.com:1883`
- **物模型 Topic**：`$sys/b2aLMZ812F/TEST1/thing/property/post`（上报）
- **控制 Topic**：`$sys/b2aLMZ812F/TEST1/thing/property/set`（下发）
- **物模型属性**：heart_rate(int32), Temp(float), barometric(float), status(enum 0-4), step_frequency(int32), gait_style(string), arm_frequency(int32), control_switch(bool)
- **status 枚举**：0=走路, 1=跑步, 2=静止, 3=准备, 4=跳
- **远程控制**：`{"control_switch":true}` 开启上传，`{"control_switch":false}` 关闭上传
- **调试开关**：`WIFI_ONLY_TEST=1` 跳过 BLE，只测 WiFi+MQTT

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

ST7789V3 240x280 SPI 显示屏已集成，LVGL 8.3.11 + SquareLine UI 正常工作。

关键配置：
- MADCTL=0x00 强制 RGB 通道顺序（ST7789 默认 BGR）
- `LV_COLOR_16_SWAP=1`（Kconfig 设置，非 lv_conf.h）
- SPI 40MHz，Y 偏移 20px，颜色反转开启
- flush callback 无额外位交换

### ESP32code main.cpp 任务结构

| 任务 | 栈大小 | 优先级 | 周期/触发 | 职责 |
|------|--------|--------|-----------|------|
| `sensor_data_task` | 8192 | 5 | 20ms (50Hz) | 读 MPU6050 + 双 FSR402 → 滑动窗口(75样本) → 步频检测 → 发力判定 → 每5样本触发推理 |
| `inference_task` | 20480 | 4 | 每100ms (信号量) | run_classifier → 中文标签 + SPM → BLE Notify 发送结果 |
| NimBLE Host | — | — | 事件驱动 | BLE Peripheral 广播/连接/收指令/发通知 |

**数据流**：
```
FSR402(Heel) ─┐
FSR402(Toe)  ─┤→ sensor_data_task (50Hz) → ei_buf[525] 滑动窗口
MPU6050(6轴) ─┘         ↓ 每5样本 (100ms)
                   xSemaphoreGive(ei_sem)
                            ↓
                   inference_task → run_classifier → BLE Notify
```

**BLE 发送频率**：~10Hz（每 100ms 一次推理，推理完成后立即发送）

### Edge Impulse 模型参数

- 窗口：75 样本 × 7 轴 = 525 floats（1.5s @ 50Hz）
- DSP：Spectral FFT(128) + Raw features → NN 输入 376
- 输出：5 类（jump, ready, run, still, walk），INT8 量化
- 每 100ms（5 组新数据）触发一次推理

### 步频 (Cadence) 算法

- FSR402 Heel 上升沿检测（0→1），300ms 死区防抖
- 计算相邻落地间隔 → SPM = 60 / T
- 长度 5 滑动平均，有效范围 40–220 SPM
- "still"/"ready" 状态时 SPM 强制清零

### 发力模式 (Gait Style) 算法

- 双 FSR402 传感器：Heel（后跟, ADC_CHANNEL_4/GPIO5）+ Toe（前掌, ADC_CHANNEL_0/GPIO1）
- 阈值判定：ADC < 800 → 踩下(1)，ADC >= 800 → 抬起(0)
- **持续状态检测**（每 20ms 更新）：
  - 两脚都离地 → "未发力"
  - 仅后跟踩下 → "后脚跟发力"
  - 仅前掌踩下 → "前脚掌发力"
  - 双踩(11) → "全掌发力"
- FSR402 驱动为 additive API：Heel 函数不变，Toe 用 `fsr402_toe_*` 并行函数，共享 `adc1_handle`
- `FSR_TEST_MODE=1` 启动时打印两个传感器 ADC 原始值（调试用，测完改回 0）
- `EMG_VOFA_OUTPUT=1` 启用 VOFA+ 肌电波形输出（调试用，测完改回 0）
- Toe 传感器**不参与** Edge Impulse 推理，仅用于发力判定

## BLE 协议

### ESP32code（脚踝端 Peripheral）

手机通过 RX 特征写入单字节指令：
- `0x72` ('r')：启动传感器 + 推理任务
- `0x73` ('s')：停止

设备通过 TX Notify 发送 UTF-8 字符串，格式：`"行为：走(95%)，步频：112，发力：后脚跟发力\n"`

括号内为 Edge Impulse 分类可信度（0-100%）。

标签映射：jump→跳, ready→准备, run→跑, still→静止, walk→走

若 MTU < 载荷大小，数据按块分片发送，片间延时 20ms。手机端需按换行符重新拼接。

### Watch_code（手表端 Central）

手表作为 Central 连接脚踝 Peripheral（设备名 `ESP32_Gait_Gatt`），通过 NUS TX Notify 接收步态数据。连接后自动写 'r' 到 RX 启动脚踝推理。

## 关键设计约束

### ESP32code
- 数据通路无堆分配：`ei_buf[525]` 静态缓冲区，`result_buf[96]` 发送缓冲
- `ble_att_set_preferred_mtu(247)` 在 `app_main()` 显式调用
- `vTaskDelayUntil` 保证严格 50Hz 采集周期
- Edge Impulse SDK 编译警告通过 `target_compile_options(PUBLIC)` 传播至 main.cpp
- `esp_lcd` 组件已排除（GCC 14.2.0 ICE bug）
- 不适用于 ESP32-S3 的 ARM 编译标志（`-mfloat-abi=hard` 等）不要添加

### Watch_code
- 两个传感器共用 I2C_NUM_0，地址不冲突（BME280=0x76, MAX30102=0x57）
- MAX30102 心率算法参数已调优：MIN_RANGE=400, REFRACTORY_MS=350（解决静坐 BPM 偏高问题）
- ST7789V3 显示驱动已集成，MADCTL=0x00 强制 RGB 通道顺序
- LVGL 8.3.11 + SquareLine Studio UI 正常工作，`LV_COLOR_16_SWAP=1` 通过 Kconfig 设置
- `sdkconfig.defaults` 设置 `CONFIG_SPI_MASTER_IN_IRAM=y` 和 `CONFIG_LV_COLOR_16_SWAP=y`
- ESP32-S3 N16R8 16MB flash，自定义分区表 `partitions.csv`（app 2MB）
- BLE Central + WiFi 共存：BLE 连接后延迟启动 WiFi，暂停通知→连 WiFi→恢复通知
- OneNET 物模型 JSON 格式上报（非 `$dp` 二进制帧），QoS 0

## 配置修改指南

**重要：涉及 menuconfig/Kconfig 配置时，优先提醒用户手动操作（`idf.py menuconfig`），不要直接修改文件。** 源码文件（`.c`/`.h`/`lv_conf.h`/`CMakeLists.txt`）可以直接改。

- BLE 设置 → 编辑 `sdkconfig.defaults`，删除 `build/` 后重建
- 传感器引脚 → 修改对应组件 `include/*.h` 中的宏定义（Heel: `FSR402_ADC_CHANNEL`, Toe: `FSR402_TOE_ADC_CHANNEL`）
- EI 模型 → 替换 `edge-impulse-sdk/` 目录并更新 `tflite-model/`
- Watch_code 传感器引脚 → I2C: `main/main.c` 中 `i2c_bus_config_t`；显示 SPI: `components/display/include/display.h`
- Watch_code MAX30102 算法参数 → `components/max30102/src/max30102.c` 顶部宏定义
- Watch_code LVGL 配置 → `components/lv_conf.h`（注意：`LV_COLOR_16_SWAP` 通过 Kconfig 控制，非此文件）
- Watch_code WiFi/MQTT 配置 → `main/main.c` 顶部 `#define`（SSID/密码/Broker/Token）
- Watch_code 调试开关 → `WIFI_ONLY_TEST=1` 跳过 BLE 只测 WiFi
- **SquareLine Studio 导出后必做**：每次从 SquareLine 重新导出 UI 文件后，需手动修改两处：
  1. `components/UI/ui.c` — 注释掉 `LV_COLOR_16_SWAP !=0` 的 `#error`（ST7789V3 需要 `=1`）
  2. `components/UI/CMakeLists.txt` — `add_library` 改为 `idf_component_register` 并添加 `REQUIRES lvgl`

## 设计文档归档

所有设计方案、计划表、技术决策完成后必须归档到 `memory/` 目录，不能只存在 `.claude/plans/`（会被覆盖）。

归档格式：`memory/plan_archive_<任务名>.md`，使用 `metadata.type: project`，在 `memory/MEMORY.md` 中添加索引条目。

## 问题排查归档

**每次解决一个问题后，必须将问题和解决方法追加到 `memory/troubleshooting_log.md`。** 格式：

```
### 问题 N：简短标题
**现象**：描述报错或异常表现
**原因**：分析根本原因
**解决方法**：具体操作步骤
**预防**：如何避免再次发生（可选）
```

按类别分组：SquareLine Studio / NimBLE / LVGL / EMG / 配置 / MQTT 等。
