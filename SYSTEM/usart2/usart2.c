#include "sys.h"
#include "usart2.h"	
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/**
 * @brief USART2 初始化（STM32F103，PA2 TX，PA3 RX）
 * @param bound 波特率
 */
void USART2_Init(uint32_t bound)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // 1. 使能时钟：USART2 在 APB1 总线上，GPIOA 在 APB2 总线上
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // 2. 配置 GPIO 引脚
    // TX PA2 (复用推挽输出)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // RX PA3 (浮空输入)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 3. 配置 NVIC 中断
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 4. 配置串口参数
    USART_InitStructure.USART_BaudRate = bound;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStructure);

    // 5. 开启接收中断并使能串口
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART2, ENABLE);
}

/**
 * @brief USART2 中断服务函数：接收字节
 * 注意：如果你后续要接 ESP-01S 接收长数据，这里需要改成环形缓冲区或DMA
 */
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) == SET)
    {
        uint8_t rxdata = USART_ReceiveData(USART2);  // 读取一个字节
		
        // 读DR寄存器会自动清除RXNE标志，这里手动清除也无妨
		USART_ClearITPendingBit(USART2, USART_IT_RXNE);
        
        // TODO: 在这里添加你的数据处理逻辑
        // 比如：RingBuffer_Write(rxdata);
    }
}

// 向指定串口发送字符串
void UART_SendString(USART_TypeDef* USARTx, const char* str)
{
    while (*str) {
        // 等待发送数据寄存器空 (TXE)
        while ((USARTx->SR & 0x40) == 0); 
        USARTx->DR = *str++;
    }
    // 等待最后一个字节发送完成 (TC)
    while ((USARTx->SR & 0x40) == 0);
}

// 自定义 printf，可指定串口，比如 UART_Printf(USART2, "Temp: %d\r\n", 25);
void UART_Printf(USART_TypeDef* USARTx, const char* format, ...)
{
    char buffer[256];   // 缓冲区足够大即可
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    UART_SendString(USARTx, buffer);
}
