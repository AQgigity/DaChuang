---
name: display-lvgl-plan
description: ST7789V3 + LVGL + SquareLine UI 集成方案（待验证）
metadata:
  type: project
---

# Watch_code 显示集成计划

## 状态：待验证（屏幕未到货）

## 方案

- 屏幕：ST7789V3（替换 NV3030B）
- GUI：LVGL 8.3.11
- UI：SquareLine Studio 生成（3 个标签：HR/Temp/Press）
- ESP-IDF 内置 esp_lcd_new_panel_st7789() 支持

## 当前状态

- NV3030B 驱动因 DC 引脚未接导致白屏，已从 main.c 移除
- display 组件和 UI 组件保留在代码中，未接入构建
- lv_conf.h 已配置（16bit color, LV_COLOR_16_SWAP=0）
