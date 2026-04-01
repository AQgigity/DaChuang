#include "MyI2C.h"
#include "main.h"

// 使用PB0和PB1
#define I2C_SCL_PIN GPIO_PIN_0
#define I2C_SDA_PIN GPIO_PIN_1
#define I2C_GPIO_PORT GPIOB

// DWT寄存器定义
#define DWT_CTRL   (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT (*(volatile uint32_t*)0xE0001004)
#define DEM_CR     (*(volatile uint32_t*)0xE000EDFC)

#define DEM_CR_TRCENA     (1 << 24)
#define DWT_CTRL_CYCCNTENA (1 << 0)

// 微秒延时函数 - 使用DWT实现精确延时
static void Delay_us(uint16_t us)
{
    uint32_t start = DWT_CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);
    
    while ((DWT_CYCCNT - start) < cycles)
    {
        // 等待
    }
}

// 初始化DWT(Data Watchpoint and Trace)用于精确延时
static void DWT_Init(void)
{
    // 使能DWT
    DEM_CR |= DEM_CR_TRCENA;
    
    // 清零周期计数器
    DWT_CYCCNT = 0;
    
    // 使能周期计数器
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
}

// 写SCL引脚
static void MyI2C_W_SCL(uint8_t BitValue)
{
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, (GPIO_PinState)BitValue);
    Delay_us(2);
}

// 写SDA引脚  
static void MyI2C_W_SDA(uint8_t BitValue)
{
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SDA_PIN, (GPIO_PinState)BitValue);
    Delay_us(2);
}

// 读SDA引脚
static uint8_t MyI2C_R_SDA(void)
{
    uint8_t BitValue;
    BitValue = HAL_GPIO_ReadPin(I2C_GPIO_PORT, I2C_SDA_PIN);
    Delay_us(2);
    return BitValue;
}

// 初始化I2C
void MyI2C_Init(void)
{
    // 初始化DWT用于精确延时
    DWT_Init();
    
    // GPIO时钟使能 (CubeMX已配置)
    // 引脚和GPIO模式已在CubeMX中配置为开漏输出和上拉
}

// 起始信号
void MyI2C_Start(void)
{
    MyI2C_W_SDA(1);
    MyI2C_W_SCL(1);
    MyI2C_W_SDA(0);
    MyI2C_W_SCL(0);
}

// 停止信号
void MyI2C_Stop(void)
{
    MyI2C_W_SDA(0);
    MyI2C_W_SCL(1);
    MyI2C_W_SDA(1);
}

// 发送一个字节
void MyI2C_SendByte(uint8_t Byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        MyI2C_W_SDA(Byte & (0x80 >> i));
        MyI2C_W_SCL(1);
        MyI2C_W_SCL(0);
    }
}

// 接收一个字节
uint8_t MyI2C_ReceiveByte(void)
{
    uint8_t i, Byte = 0x00;
    
    // 先将SDA设置为输入模式
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = I2C_SDA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(I2C_GPIO_PORT, &GPIO_InitStruct);
    
    for (i = 0; i < 8; i++)
    {
        MyI2C_W_SCL(1);
        if (MyI2C_R_SDA())
        {
            Byte |= (0x80 >> i);
        }
        MyI2C_W_SCL(0);
    }
    
    // 接收完成后将SDA恢复为输出模式
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    HAL_GPIO_Init(I2C_GPIO_PORT, &GPIO_InitStruct);
    
    return Byte;
}

// 发送应答
void MyI2C_SendAck(uint8_t AckBit)
{
    MyI2C_W_SDA(AckBit);
    MyI2C_W_SCL(1);
    MyI2C_W_SCL(0);
}

// 接收应答
uint8_t MyI2C_ReceiveAck(void)
{
    uint8_t AckBit;
    
    // 先将SDA设置为输入模式
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = I2C_SDA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(I2C_GPIO_PORT, &GPIO_InitStruct);
    
    MyI2C_W_SCL(1);
    AckBit = MyI2C_R_SDA();
    MyI2C_W_SCL(0);
    
    // 恢复为输出模式
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    HAL_GPIO_Init(I2C_GPIO_PORT, &GPIO_InitStruct);
    
    return AckBit;
}

// 高级功能：读取一个字节
uint8_t MyI2C_ReadByte(uint8_t DeviceAddr, uint8_t RegAddr)
{
    uint8_t Data;
    
    MyI2C_Start();
    MyI2C_SendByte(DeviceAddr << 1);          // 发送设备地址(写)
    MyI2C_ReceiveAck();                       // 等待应答
    MyI2C_SendByte(RegAddr);                  // 发送寄存器地址
    MyI2C_ReceiveAck();                       // 等待应答
    
    MyI2C_Start();
    MyI2C_SendByte((DeviceAddr << 1) | 0x01); // 发送设备地址(读)
    MyI2C_ReceiveAck();                       // 等待应答
    Data = MyI2C_ReceiveByte();               // 读取数据
    MyI2C_SendAck(1);                         // 发送非应答
    MyI2C_Stop();
    
    return Data;
}

// 高级功能：写入一个字节
void MyI2C_WriteByte(uint8_t DeviceAddr, uint8_t RegAddr, uint8_t Data)
{
    MyI2C_Start();
    MyI2C_SendByte(DeviceAddr << 1);     // 发送设备地址(写)
    MyI2C_ReceiveAck();                  // 等待应答
    MyI2C_SendByte(RegAddr);             // 发送寄存器地址
    MyI2C_ReceiveAck();                  // 等待应答
    MyI2C_SendByte(Data);                // 发送数据
    MyI2C_ReceiveAck();                  // 等待应答
    MyI2C_Stop();
}

// 高级功能：检测设备是否存在
uint8_t MyI2C_CheckDevice(uint8_t DeviceAddr)
{
    uint8_t ack;
    
    MyI2C_Start();
    MyI2C_SendByte(DeviceAddr << 1);     // 发送设备地址(写)
    ack = MyI2C_ReceiveAck();            // 等待应答
    MyI2C_Stop();
    
    return !ack; // 如果收到应答返回1，否则返回0
}