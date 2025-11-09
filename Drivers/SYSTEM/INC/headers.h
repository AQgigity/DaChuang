#ifndef __HEADERS_H
#define __HEADERS_H
/* CMSIS-DSP库 */
#include "arm_math.h"
#include "arm_const_structs.h"
//#include "DSP/window_functions.h"
/* 主函数预处理指令 */
#include "main.h"

/* DSP库函数 */

/* 片上外设库函数 */

/* C语言库函数 */
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "stdbool.h"
#include "math.h"
#include "usart.h"   
#include "gpio.h"             // Component selection
#include "bsp_usart.h"


/*------------------------ SYS ------------------------*/

/*------------------------ DSP ------------------------*/
/* 个人函数 */

/*------------------------ BSP ------------------------*/
/* 外设 */

/* 协议/算法 */

/*------------------------ MODULE ------------------------*/
/* 模块启用管理 */

/* 模块 */

/*------------------------ 全局系数 ------------------------*/
#define pi		3.14159265358979323846f	/* 定义圆周率的近似值，方便计算正弦波等波形时使用 */
#define ZOOM	(3.3f / 65535.0f)					/* ADC模数转换缩放系数 */
#define IZOOM	(65535.0f / 3.3f)					/* ADC模数转换逆缩放系数 */


#endif