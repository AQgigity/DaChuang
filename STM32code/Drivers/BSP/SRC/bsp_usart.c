#include "bsp_usart.h"

/******************************************************************************************/
/* 加入以下代码, 支持printf函数, 而不用选择use MicroLIB */
/* 如printf("HIGH:%d us\r\n", temp);打印"HIGH:temp\n" */

#if 1
#if (__ARMCC_VERSION >= 6010050)           /* 使用AC6编译器时 */
__asm(".global __use_no_semihosting\n\t"); /* 声明不使用半主机模式 */
__asm(".global __ARM_use_no_argv \n\t");   /* AC6下需要声明main函数为无参数格式，否则部分例程可能出现半主机模式 */

#else
/* 使用AC5编译器时, 要在这里定义__FILE  不使用半主机模式 */
#pragma import(__use_no_semihosting)

struct __FILE
{
  int handle;
  /* Whatever you require here. If the only file you are using is */
  /* standard output using printf() for debugging, no file handling */
  /* is required. */
};

#endif

/* 不使用半主机模式，至少需要重定义_ttywrch\_sys_exit\_sys_command_string函数,以同时兼容AC6和AC5模式 */
int _ttywrch(int ch)
{
  ch = ch;
  return ch;
}

/* 定义_sys_exit()以避免使用半主机模式 */
void _sys_exit(int x)
{
  x = x;
}

char *_sys_command_string(char *cmd, int len)
{
  return NULL;
}

/************************** 以下是多串口 printf 重定向函数 **************************/
/* FILE 在 stdio.h 里面定义 */
FILE __stdout;

UART_HandleTypeDef* Current_USART_Handle = NULL;
Current_USART_Indx Current_USART_Printf_Indx = USART_NONE;

/* 
 * 简介：重定义 fputc 函数，用于将字符输出到当前设置的 USART
 * 参数：
 * ch - 要发送的字符
 * f  - 文件指针（在此实现中未使用）
 * 返回值：发送的字符（或 EOF 如果出错）
 */
int fputc(int ch, FILE *f)
{
  if(Current_USART_Handle == NULL){          /* 如果当前没有设置 USART 句柄，则返回 EOF 表示错误 */
    return EOF;
  }
  /* 根据当前设置的 USART 句柄，选择对应的 USART 外设发送字符 */
  if(Current_USART_Handle == &huart1){
    while ((USART1->ISR & 0X40) == 0);        /* 等待 USART1 发送完成，然后发送字符 */
    USART1->TDR = (uint8_t)ch;             		/* 将要发送的字符 ch 写入到 DR 寄存器 */
  }
  return ch;                            /* 返回发送的字符 */
}
/* 
 * 简介：设置当前使用的 USART
 * 参数：indx - 要设置的 USART 索引
 * 这个参数可以是：USARTx_IDX，其中 x 可以是 1~3
 * 使用举例：（必须要将其放在 printf 函数前面，指定其中一个串口）
 *    Set_Current_USART(USART1_IDX);
 *    printf("我是串口 1\r\n");
 */
void Set_Current_USART(Current_USART_Indx indx)
{
  switch(indx)
  {
    case USART1_IDX:
      Current_USART_Handle = &huart1;
      Current_USART_Printf_Indx = USART1_IDX;
      break;
    default:
      Current_USART_Handle = NULL;
      Current_USART_Printf_Indx = USART_NONE;
      break;
  }
}
#endif
/************************************************************************************/