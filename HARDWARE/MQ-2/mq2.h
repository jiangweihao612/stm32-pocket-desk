#ifndef __MQ2_H
#define __MQ2_H
#include "stm32f10x.h"

/*=============================================================================
 *  MQ-2 可燃气体/烟雾传感器（模拟量 AO 输出）
 *
 *  接线：VCC -> 5V（模块加热丝要 5V 才能正常工作，3.3V 浓度读数会偏低）
 *        GND -> GND
 *        AO  -> PA6   (ADC1_IN6)   <- J3 扩展排针第 43 脚，板载无其他器件
 *        DO  -> 不用
 *
 *  注意：AO 输出最高接近模块供电电压，如果模块用 5V 供电，AO 分压后
 *        必须 <= 3.3V 再进 PA6，否则会打坏 ADC 引脚。模块自带电位器
 *        只能调 DO 的比较阈值，不影响 AO 幅值。
 *
 *  为什么从 PA1 挪走：PA1 在原理图上是"外壳兼做触摸按键"，板上挂了
 *  R25(5.1M) 上拉到 3.3V 并引到 J13 跳线。MQ-2 的 AO 接在同一根线上，
 *  除非拔掉 J13，否则读数会被这路触摸检测拉偏。PA6 板载完全空闲。
 *===========================================================================*/

/*-------------------- 引脚 / 通道 --------------------*/
#define MQ2_AO_GPIO_CLK     RCC_APB2Periph_GPIOA
#define MQ2_AO_GPIO_PORT    GPIOA
#define MQ2_AO_GPIO_PIN     GPIO_Pin_6
#define MQ2_ADC_CHANNEL     ADC_Channel_6   /* PA6 = ADC1_IN6 */

/*-------------------- 采样参数 ------------------------*/
#define MQ2_READ_TIMES      8       /* 每次测量采样次数（去极值后平均） */
#define MQ2_VREF_MV         3300    /* 参考电压 = VDDA = 3.3V          */

/*-------------------- 浓度分级阈值（mV） --------------*/
/* 按自己模块实测标定：上电预热 3~5 分钟后记录洁净空气的 AO 电压，
 * 阈值取它上面 2~3 倍作为 TRACE，再按需给 ALARM / DANGER           */
#define MQ2_TH_TRACE_MV     900
#define MQ2_TH_ALARM_MV     1600
#define MQ2_TH_DANGER_MV    2400

/* ppm 估算：ppm = MQ2_PPM_A * (R0 / Rs) ^ MQ2_PPM_B
 * 这三个常数沿用原驱动（R0=6.64 是按 5V 供电标定的）。换供电电压或换模块后
 * 一定要重新标定，否则 ppm 只能当相对趋势看：洁净空气里大约 5~20，吹口气
 * 或有可燃气时会明显变大。想用数据手册的 LPG 曲线可改成 A=574.25、B=-2.222
 * 并把 B 的符号一起改到公式里。                                     */
#define MQ2_R0              6.64f   /* 洁净空气中 Rs，kOhm    */
#define MQ2_PPM_A           11.5428f
#define MQ2_PPM_B           0.6549f

/*-------------------- 状态 ----------------------------*/
#define MQ2_ST_CLEAN        0
#define MQ2_ST_TRACE        1
#define MQ2_ST_ALARM        2
#define MQ2_ST_DANGER       3

typedef struct
{
    u16 adc;        /* 0 ~ 4095            */
    u16 mv;         /* 0 ~ 3300 mV         */
    u16 ppm;        /* 估算浓度，0 ~ 9999  */
    u8  level;      /* 0 ~ 100，相对浓度   */
    u8  status;     /* MQ2_ST_xxx          */
} MQ2_Data;

void        MQ2_Init(void);
void        MQ2_Read(MQ2_Data *out);      /* 一次采样，填满整个结构体 */
u16         MQ2_GetData(void);            /* 只取 ADC 平均值          */
const char *MQ2_StatusName(u8 status);    /* "CLEAN"/"TRACE"/...      */
float       MQ2_GetData_PPM(void);        /* ppm 估算（仅供参考）    */

#endif
