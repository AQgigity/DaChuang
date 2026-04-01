#ifndef __MY_I2C_H
#define __MY_I2C_H

#include "main.h"
#include "stdint.h"

// 函数声明
void MyI2C_Init(void);
void MyI2C_Start(void);
void MyI2C_Stop(void);
void MyI2C_SendByte(uint8_t Byte);
uint8_t MyI2C_ReceiveByte(void);
void MyI2C_SendAck(uint8_t AckBit);
uint8_t MyI2C_ReceiveAck(void);

// 高级功能函数
uint8_t MyI2C_ReadByte(uint8_t DeviceAddr, uint8_t RegAddr);
void MyI2C_WriteByte(uint8_t DeviceAddr, uint8_t RegAddr, uint8_t Data);
uint8_t MyI2C_CheckDevice(uint8_t DeviceAddr);

#endif