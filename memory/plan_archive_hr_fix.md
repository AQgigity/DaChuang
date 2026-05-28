---
name: hr-fix-plan
description: MAX30102 心率算法调优，解决静坐 BPM 偏高问题（已完成）
metadata:
  type: project
---

# MAX30102 心率算法修复

## 状态：已完成

## 问题

静坐时 BPM 偏高，峰值检测误触发。

## 修复

- MIN_RANGE 从 200 提高到 400（峰谷幅度阈值）
- REFRACTORY_MS 从 200 提高到 350（不应期）
- 有效间隔 300ms–1500ms（40–200 BPM）
- 异常值剔除：偏离当前 BPM >40% 则跳过
- 超时：3 秒无峰则 BPM 归零
