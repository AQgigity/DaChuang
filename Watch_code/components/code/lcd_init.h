#ifndef __LCD_INIT_H
#define __LCD_INIT_H

#include "stm32f1xx_hal.h"

#define USE_HORIZONTAL 0  // 定义显示方向 0和1为竖屏 2和3为横屏

#if USE_HORIZONTAL == 0 || USE_HORIZONTAL == 1
#define LCD_W 240
#define LCD_H 280
#else
#define LCD_W 280
#define LCD_H 240
#endif

//-----------------LCD端口定义---------------- 

#define LCD_RES_PORT GPIOA
#define LCD_RES_PIN  GPIO_PIN_1
#define LCD_DC_PORT  GPIOA
#define LCD_DC_PIN   GPIO_PIN_2
#define LCD_CS_PORT  GPIOA
#define LCD_CS_PIN   GPIO_PIN_3
#define LCD_BLK_PORT GPIOA
#define LCD_BLK_PIN  GPIO_PIN_4

#define LCD_RES_Clr()  HAL_GPIO_WritePin(LCD_RES_PORT, LCD_RES_PIN, GPIO_PIN_RESET) // RES
#define LCD_RES_Set()  HAL_GPIO_WritePin(LCD_RES_PORT, LCD_RES_PIN, GPIO_PIN_SET)

#define LCD_DC_Clr()   HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_RESET)  // DC
#define LCD_DC_Set()   HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_SET)
 		     
#define LCD_CS_Clr()   HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_RESET)  // CS
#define LCD_CS_Set()   HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET)

#define LCD_BLK_Clr()  HAL_GPIO_WritePin(LCD_BLK_PORT, LCD_BLK_PIN, GPIO_PIN_RESET) // BLK
#define LCD_BLK_Set()  HAL_GPIO_WritePin(LCD_BLK_PORT, LCD_BLK_PIN, GPIO_PIN_SET)

void LCD_GPIO_Init(void);                    // 初始化GPIO
void LCD_Writ_Bus(uint8_t dat);              // 模拟SPI时序
void LCD_WR_DATA8(uint8_t dat);              // 写一个字节
void LCD_WR_DATA(uint16_t dat);              // 写两个字节
void LCD_WR_REG(uint8_t dat);                // 写一个指令
void LCD_Address_Set(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2); // 设置坐标函数
void LCD_Init(void);         
#endif




