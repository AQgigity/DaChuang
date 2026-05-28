---
name: ble-sync-plan
description: 手表端 BLE Central 实现，与脚踝端 BLE 双设备同步（已完成）
metadata:
  type: project
---

# Watch_code BLE Central 双设备同步

## 状态：已完成

## 架构

手表（Central）连接脚踝（Peripheral，设备名 ESP32_Gait_Gatt），通过 NUS 服务接收步态推理结果。

## 实现要点

- BLE Central 扫描 → 匹配设备名 → 连接 → 服务/特征发现 → CCCD 写入使能通知 → 写 'r' 启动脚踝
- 通知分片拼接：g_rx_buf[128] 缓冲区按 \n 切分完整消息
- 发现回调处理 BLE_HS_EDONE 作为正常完成（非错误）
- 断线自动重连：1 秒延迟后重新扫描
- NimBLE 回调返回 int（非 void）

## 数据格式

脚踝发送：`"行为：走，步频：112\n"`（UTF-8 中文）
手表解析：action[16] + cadence_spm → 队列 → fusion task
