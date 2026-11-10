/*--------------------------------------------------------------------------------
 * 文件名称：beep.c
 * 文件功能：蜂鸣器（PB8）的初始化与"响一声"接口
 * 说    明：PB8 在本工程里没有别的用途，可以放心独占
 *--------------------------------------------------------------------------------*/
#include "beep.h"
#include "delay.h"

/*--------------------------------------------------------------------------------
 * 函数名称：BEEP_Init
 * 函数功能：把 PB8 配成推挽输出，并让蜂鸣器一开始就处于不响的状态
 * 入口参数：无
 * 返回参数：无
 *--------------------------------------------------------------------------------*/
void BEEP_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    /* 打开 GPIOB 时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_8;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;

    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* 复位后 ODR 默认是 0，但对低电平触发的那种模块来说就是"上电就响"，
     * 所以这里明确关一次，保证一上电是安静的。 */
    BEEP_OFF;
}

/*--------------------------------------------------------------------------------
 * 函数名称：BEEP_Beep
 * 函数功能：响一声，持续 ms 毫秒后自动关闭
 * 入口参数：ms —— 持续时间，单位毫秒
 * 返回参数：无
 * 说    明：阻塞式（用的是 delay_ms/delay_us），不要在中断里调用
 *--------------------------------------------------------------------------------*/
void BEEP_Beep(u16 ms)
{
#if BEEP_PASSIVE
    /* 无源蜂鸣器：必须用方波驱动才响。半周期 185us，约 2.7kHz */
    u32 cnt;

    cnt = (u32)ms * 1000u / 370u;

    while (cnt--)
    {
        BEEP_ON;
        delay_us(185);
        BEEP_OFF;
        delay_us(185);
    }
#else
    /* 有源蜂鸣器：给电平就行 */
    if (ms == 0) return;

    BEEP_ON;
    delay_ms(ms);
    BEEP_OFF;
#endif
}
