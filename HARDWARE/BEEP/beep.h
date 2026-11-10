#ifndef __BEEP_H
#define __BEEP_H

#include "stm32f10x.h"

/*--------------------------------------------------------------------------------
 * 蜂鸣器接在 PB8，推挽输出。
 *
 * 板载的是【有源蜂鸣器】：给一个直流电平就响，不需要方波。
 * 正点原子/德飞莱这类板子上，PB8 经三极管驱动，高电平 = 响。
 *
 * 如果你的模块是低电平触发的（或者换成了无源蜂鸣器），
 * 只改下面两个宏就行，别的代码一行都不用动：
 *   BEEP_ACTIVE_HIGH  1 = 高电平响（默认）   0 = 低电平响
 *   BEEP_PASSIVE      1 = 无源蜂鸣器，用方波驱动
 *--------------------------------------------------------------------------------*/
#define BEEP_ACTIVE_HIGH    1
#define BEEP_PASSIVE        0

#if BEEP_ACTIVE_HIGH
  #define BEEP_ON           GPIO_SetBits(GPIOB, GPIO_Pin_8)     /* 响   */
  #define BEEP_OFF          GPIO_ResetBits(GPIOB, GPIO_Pin_8)   /* 不响 */
#else
  #define BEEP_ON           GPIO_ResetBits(GPIOB, GPIO_Pin_8)   /* 响   */
  #define BEEP_OFF          GPIO_SetBits(GPIOB, GPIO_Pin_8)     /* 不响 */
#endif

/* 翻转一次电平，调试时可以用 */
#define BEEP_REV  GPIO_WriteBit(GPIOB, GPIO_Pin_8, \
                  (BitAction)(1 - (GPIO_ReadOutputDataBit(GPIOB, GPIO_Pin_8))))

/* 初始化 PB8 为推挽输出，并让蜂鸣器保持安静 */
void BEEP_Init(void);

/* 响一声：持续 ms 毫秒后自动关闭。阻塞式，别在中断里调用。 */
void BEEP_Beep(u16 ms);

#endif
