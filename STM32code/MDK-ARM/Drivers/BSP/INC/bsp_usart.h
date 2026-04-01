#ifndef __BSP_USART_H
#define __BSP_USART_H

#include "headers.h"

/******************** 以下是多路 USART 串口 printf 重定向 ********************/
/* 定义 USART 索引枚举 */
typedef enum {
    USART_NONE,        /* 无 USART */
    USART1_IDX,        /* USART1 索引 */
    USART2_IDX,        /* USART2 索引 */
		USART3_IDX,        /* USART3 索引 */
		USART6_IDX,        /* USART6 索引 */
} Current_USART_Indx;

extern UART_HandleTypeDef* Current_USART_Handle;        /* 当前某个 USART 的句柄 */
extern Current_USART_Indx Current_USART_Printf_Indx;    /* 当前某个 USART 的索引 */
void Set_Current_USART(Current_USART_Indx indx);        /* 函数声明，用于设置当前使用的 USART */
/****************************************************************************/

/* 串口一 */
#define UART1_LEN  64
/* 串口二 */
#define UART2_LEN  64
/* 串口三 */
#define UART3_LEN  64


#endif