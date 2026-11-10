#ifndef __USART2_H
#define __USART2_H	
#include "sys.h" 
#include <stdio.h>

void USART2_Init(uint32_t bound);

// 向指定串口发送字符串（非格式化）
void UART_SendString(USART_TypeDef* USARTx, const char* str);

// 自定义 printf，可指定串口输出格式化字符串
void UART_Printf(USART_TypeDef* USARTx, const char* format, ...);

#endif
