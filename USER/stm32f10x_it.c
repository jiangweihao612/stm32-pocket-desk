/**
  ******************************************************************************
  * @file    GPIO/IOToggle/stm32f10x_it.c 
  * @author  MCD Application Team
  * @version V3.5.0
  * @date    08-April-2011
  * @brief   主中断服务程序。
  *                             本文件提供所有异常处理程序和外设
  *                             中断服务程序的模板。
  ******************************************************************************
  * @attention
  *
  * THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
  * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
  * TIME. AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY
  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
  * FROM THE CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * <h2><center>&copy; COPYRIGHT 2011 STMicroelectronics</center></h2>
  ******************************************************************************
  */

/* 头文件 ------------------------------------------------------------------*/
#include "stm32f10x_it.h" 


 
void NMI_Handler(void)
{
}
 
void HardFault_Handler(void)
{
  /* 以前这里只是一个空死循环：程序一旦 HardFault，屏幕就停在最后一帧，
   * 看起来像"卡在加载 100%"。现在直接把故障打到屏上，一眼能分清是
   * 真的卡死还是跑飞了（红底白字 = HardFault）。                     */
  extern void LCD_Clear(unsigned short Color);
  extern void LCD_ShowString(unsigned short x, unsigned short y,
                             unsigned short width, unsigned short height,
                             unsigned char size, unsigned char *pstr);
  LCD_Clear(0xF800);
  LCD_ShowString(10, 10, 300, 24, 16, (unsigned char *)"HARD FAULT");
  LCD_ShowString(10, 40, 300, 24, 16, (unsigned char *)"camera / album app");
  LCD_ShowString(10, 70, 300, 24, 16, (unsigned char *)"reboot by reset");

  while (1)
  {
  }
}
 
void MemManage_Handler(void)
{
    /* 发生内存管理 (MemManage) 异常时进入死循环 */
  while (1)
  {
  }
}

 
void BusFault_Handler(void)
{
    /* 发生总线故障 (BusFault) 异常时进入死循环 */
  while (1)
  {
  }
}
 
void UsageFault_Handler(void)
{
    /* 发生用法故障 (UsageFault) 异常时进入死循环 */
  while (1)
  {
  }
}
 
void SVC_Handler(void)
{
}
 
void DebugMon_Handler(void)
{
}
 
void PendSV_Handler(void)
{
}
 
void SysTick_Handler(void)
{
}

/******************************************************************************/
/*                 STM32F10x 外设中断处理程序                                 */
/*  在这里添加所使用外设 (PPP) 的中断处理函数，可用的外设中断                */
/*  处理函数名请参考启动文件                                                  */
/*  (startup_stm32f10x_xx.s)。                                                */
/******************************************************************************/
