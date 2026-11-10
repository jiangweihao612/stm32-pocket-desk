/*****************************************************************************
 *  dac.h - 双通道 DAC 波形发生器
 *
 *  硬件：
 *    PA4 = DAC_OUT1   ->  DMA1_Channel3  <-  TIM6_TRGO
 *    PA5 = DAC_OUT2   ->  DMA1_Channel4  <-  TIM7_TRGO
 *
 *  两路共用同一张 64 点波形表，DMA 循环搬运，定时器决定更新率。
 *  整条链路是纯硬件的，CPU 只在切换波形/频率时动一下，
 *  界面重绘再慢也不会影响输出频率。
 *
 *  频率上限来自 STM32F103 DAC 的 1 MSPS 转换能力：
 *  每周期固定 64 点，所以输出频率 = 采样率 / 64。
 *  最高档 10 kHz 对应 640 kSPS，仍在 DAC 规格内；
 *  再高就只能减点数，波形会明显失真，所以没有往上放。
 *  最低档 50 Hz 对应 3.2 kSPS。
 *
 *  注意：PA4/PA5 在这块板子上是扩展排针引出的空闲脚，
 *  MAX30102 已经从这两脚挪到 PE0/PE1，互不影响。
 *****************************************************************************/
#ifndef __DAC_H
#define __DAC_H

#include "stm32f10x.h"

/*-------------------- 波形种类 --------------------*/
#define DW_SINE         0       /* 正弦   */
#define DW_SQUARE       1       /* 方波   */
#define DW_TRI          2       /* 三角波 */
#define DW_SAW          3       /* 锯齿波 */
#define DW_NOISE        4       /* 噪声   */
#define DW_COUNT        5

/*-------------------- 输出通道 --------------------*/
#define DOUT_NONE       0       /* 两路都不输出       */
#define DOUT_CH1        1       /* 只输出 PA4         */
#define DOUT_CH2        2       /* 只输出 PA5         */
#define DOUT_BOTH       3       /* 两路同时输出       */

#define DAC_TAB_LEN     64      /* 每周期采样点数，改它要同步改 ARR 表 */
#define DAC_FREQ_LEVELS 8       /* 频率档数                          */

void DAC_WaveInit(void);                /* 上电初始化，默认两路都关   */
void DAC_StopAll(void);                 /* 停止两路输出（输出 0 V）   */

/*-------------------- 波形 / 频率 ----------------*/
void        DAC_SetWave(u8 w);          /* DW_xxx                    */
u8          DAC_GetWave(void);
void        DAC_SetFreq(u8 lv);         /* 0 .. DAC_FREQ_LEVELS-1    */
u8          DAC_GetFreq(void);
u32         DAC_GetFreqHz(void);        /* 当前档位的实际频率（Hz）   */
const u16  *DAC_Table(void);            /* 当前波形表，给界面描点用   */
const char *DAC_WaveName(u8 w);         /* "SINE" / "SQR" / ...       */
const char *DAC_FreqName(u8 lv);        /* "50" / "1K" / "10K" ...    */

/*-------------------- 通道开关 --------------------*/
void DAC_SetOutput(u8 sel);             /* DOUT_xxx，两路一起设      */
u8   DAC_GetOutput(void);               /* 反推当前属于哪一档         */
void DAC_Ch1On(u8 on);
void DAC_Ch2On(u8 on);
void DAC_ToggleBoth(void);              /* 都开就都关，否则都开       */
u8   DAC_Ch1IsOn(void);
u8   DAC_Ch2IsOn(void);

#endif
