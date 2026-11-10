#include "mq2.h"
#include "adcx.h"
#include "delay.h"
#include <math.h>

/*-----------------------------------------------------------------------------
 * PA6 配置为模拟输入，并初始化 ADC1（12 位、单次转换、软件触发）
 *----------------------------------------------------------------------------*/
void MQ2_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(MQ2_AO_GPIO_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin  = MQ2_AO_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;   /* 模拟输入 */
    GPIO_Init(MQ2_AO_GPIO_PORT, &GPIO_InitStructure);

    ADCx_Init();                                    /* ADC1 + 校准 */
}

/* 单次转换，约 14us */
static u16 mq2_adc_once(void)
{
    return ADC_GetValue(MQ2_ADC_CHANNEL, ADC_SampleTime_55Cycles5);
}

/*-----------------------------------------------------------------------------
 * 连续采样 MQ2_READ_TIMES 次，去掉最大最小值再平均：
 * MQ-2 加热丝是脉冲供电，电源上会有尖峰，去极值能明显压住跳动
 * 耗时约 (转换 + 2ms) * 8 ≈ 20ms
 *----------------------------------------------------------------------------*/
u16 MQ2_GetData(void)
{
    u32 sum = 0;
    u16 v, vmax = 0, vmin = 0xFFFF;
    u8  i;

    for (i = 0; i < MQ2_READ_TIMES; i++)
    {
        v = mq2_adc_once();

        sum += v;
        if (v > vmax) vmax = v;
        if (v < vmin) vmin = v;

        delay_ms(2);
    }

    sum -= (u32)(vmax + vmin);                      /* 去掉一个最高一个最低 */
    sum /= (MQ2_READ_TIMES - 2);

    return (u16)sum;
}

/*-----------------------------------------------------------------------------
 * ppm 估算：Rs = (Vc - Vo) / (Vo * 0.5)，ppm = (11.5428 * R0 / Rs) ^ 0.6549
 * 曲线来自数据手册，只能当量级参考，不同模块/预热时间差别很大
 *----------------------------------------------------------------------------*/
static u16 mq2_ppm_from_adc(u16 adc)
{
    float vc  = (float)MQ2_VREF_MV / 1000.0f;
    float vol = (float)adc * vc / 4095.0f;
    float rs, ppm;

    if (vol < 0.01f) return 0;              /* 基本没信号，别算出个天量 */
    if (vol >= vc)   return 9999;           /* 满量程，ppm 已经没意义   */

    rs = (vc - vol) / (vol * 0.5f);
    if (rs <= 0.001f) return 9999;

    ppm = (float)pow((double)(MQ2_PPM_A * MQ2_R0 / rs), (double)MQ2_PPM_B);

    if (ppm > 9999.0f) ppm = 9999.0f;

    return (u16)ppm;
}

/* 一次采样把界面上要用的量都算好，避免重复转换 */
void MQ2_Read(MQ2_Data *out)
{
    u16 adc = MQ2_GetData();

    out->adc   = adc;
    out->mv    = (u16)((u32)adc * MQ2_VREF_MV / 4095);
    out->ppm   = mq2_ppm_from_adc(adc);
    out->level = (u8)((u32)adc * 100 / 4095);

    if      (out->mv < MQ2_TH_TRACE_MV)  out->status = MQ2_ST_CLEAN;
    else if (out->mv < MQ2_TH_ALARM_MV)  out->status = MQ2_ST_TRACE;
    else if (out->mv < MQ2_TH_DANGER_MV) out->status = MQ2_ST_ALARM;
    else                                 out->status = MQ2_ST_DANGER;
}

const char *MQ2_StatusName(u8 status)
{
    switch (status)
    {
    case MQ2_ST_TRACE:  return "TRACE";
    case MQ2_ST_ALARM:  return "ALARM";
    case MQ2_ST_DANGER: return "DANGER";
    default:            return "CLEAN";
    }
}

/* 单独要 ppm 时用这个（会重新采一次样） */
float MQ2_GetData_PPM(void)
{
    return (float)mq2_ppm_from_adc(MQ2_GetData());
}
