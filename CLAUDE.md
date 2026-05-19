# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 在本仓库中工作时提供指引。

## 项目概述

步态分析/健康监测可穿戴设备的 BLE 蓝牙桥固件（大学生创新创业项目"DaChuang"）。

- **ESP32-S3** (`ESP32code/`) — 使用 ESP-IDF v5.4.3 + NimBLE 协议栈，实现 Nordic UART Service (NUS) 将 CSV 传感器数据通过蓝牙转发至手机。设备名：`ESP32_Gait_Gatt`。

当前状态：以 50Hz 频率采集 MPU6050（IMU）和 FSR402（足压）真实传感器数据，通过 BLE Notify 发送 CSV 至手机。

## 构建命令

通过 VS Code ESP-IDF 扩展或命令行构建（需先激活 ESP-IDF 环境）：
```bash
cd ESP32code
idf.py build
idf.py -p COMx flash monitor
```

若 `idf.py` 不在 PATH 中，需先激活 ESP-IDF 环境，或直接使用 VS Code 扩展的构建/烧录按钮。

## 架构

```
ESP32code/
  main/main.c              — BLE UART 服务、GAP/GATT 回调、50Hz 传感器数据任务
  components/mpu6050/      — MPU6050 I2C 驱动（ESP-IDF 新 master API，GPIO8/SDA，GPIO9/SCL）
  components/fsr402/       — FSR402 ADC 驱动（ADC_UNIT_1，CH4，GPIO5）
  sdkconfig.defaults       — NimBLE 外设配置，MTU=247，无配对
```

- NimBLE GATT 服务使用 NUS UUID（6E400001/02/03）
- TX 特征：Notify（设备 → 手机）；RX 特征：Write（手机 → 设备）
- 指令：`'r'` 启动数据，`'s'` 停止数据
- CSV 格式：`timestamp,ax,ay,az,gx,gy,gz,pressure\n`
- `ble_uart_send_line()` 处理载荷超过 MTU 时的分片发送
- `ble_att_set_preferred_mtu(247)` 在 `app_main()` 中显式调用（sdkconfig 的值可能未生效）
- 仅使用静态缓冲区（`tx_buf[64]`），数据通路无堆分配
- `sensor_data_task` 使用 `vTaskDelayUntil` 实现严格 20ms（50Hz）采集周期
- 传感器初始化在 `app_main()` 末尾（MPU6050 I2C + FSR402 ADC）
- 任务栈 8192 字节（浮点 `snprintf` 需要较大栈空间）

### 关键配置文件

- `ESP32code/sdkconfig.defaults` — NimBLE 配置。修改 BLE 设置时编辑此文件（不要直接改 `sdkconfig`）。

## BLE 协议

手机通过 RX 特征写入单字节指令：
- `0x72` ('r')：启动 50Hz 传感器数据流
- `0x73` ('s')：停止数据流

设备通过 TX Notify 发送 CSV 行。若 MTU < 载荷大小，数据按块分片发送，片间延时 20ms。手机端需按换行符重新拼接。

## 当前局限 / 已知问题

- 本仓库无手机端 App 代码
