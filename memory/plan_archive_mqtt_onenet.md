---
name: mqtt-onenet-plan
description: WiFi + MQTT 上报 OneNET 云平台 + 远程开关控制（已完成）
metadata:
  type: project
---

# Watch_code WiFi + MQTT OneNET 云平台集成

## 状态：已完成

## 功能

1. BLE 连接脚踝后延迟启动 WiFi（避免共存冲突）
2. MQTT 连接 OneNET，物模型 JSON 格式上报（2 秒周期）
3. 订阅属性设置 topic，远程控制上传开关

## 关键配置

- Broker: mqtt://mqtts.heclouds.com:1883
- 上报 topic: $sys/b2aLMZ812F/TEST1/thing/property/post
- 控制 topic: $sys/b2aLMZ812F/TEST1/thing/property/set
- QoS: 0（共存模式下减少射频争用）
- 调试开关: WIFI_ONLY_TEST=1 跳过 BLE 只测 WiFi

## 物模型属性

| identifier | 类型 | 说明 |
|-----------|------|------|
| heart_rate | int32 | 心率 bpm |
| Temp | float | 温度 °C |
| barometric | float | 气压 hPa |
| status | enum(0-4) | 0=走路,1=跑步,2=静止,3=准备,4=跳 |
| step_frequency | int32 | 步频 spm |
| control_switch | bool | 远程开关控制 |

## BLE + WiFi 共存方案

BLE 连接后 → 暂停通知 → WiFi 连接 → MQTT 连接 → 恢复通知 → 并行运行
ESP32-S3 单射频时分复用，BLE 通知每 100ms 小包 + WiFi 每 2s 发 JSON

## 踩坑记录

1. $dp topic 需要二进制帧格式，连接后会被服务器断开 → 改用物模型 topic + 纯 JSON
2. WiFi + BLE 扫描同时进行会共存冲突 → BLE 连接稳定后再启动 WiFi
3. 分区表不足 1MB → 自定义 partitions.csv 扩大到 2MB
4. flash size 默认 2MB → sdkconfig.defaults 设置 16MB
