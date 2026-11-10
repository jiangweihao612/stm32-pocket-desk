/*****************************************************************************
 *  dac.c - 双通道 DAC 波形发生器
 *
 *  PA4(DAC_OUT1) -> DMA1_Channel3 <- TIM6_TRGO
 *  PA5(DAC_OUT2) -> DMA1_Channel4 <- TIM7_TRGO
 *
 *  为什么用 DMA 而不是在循环里写 DHR：
 *  定时器触发 + DMA 循环搬运是纯硬件链路，CPU 去画屏幕、读触摸
 *  都不会让输出抖一下。软件写的话一进 LCD_Fill 输出就断了。
 *
 *  为什么两路用两个定时器：
 *  DAC 的两个通道只有一个数据寄存器阵列，触发源也是各自独立的。
 *  TIM6 只做 DAC1 的触发（它本来就是 DAC 专用基本定时器），
 *  TIM7 同理给 DAC2。两个定时器同频启动，相位差在几十个时钟内，
 *  示波器双通道看基本重合。
 *****************************************************************************/
#include "dac.h"

/*-------------------- 表 --------------------------*/

/* 64 点正弦，中心 2048，幅度 ±2000（留出上下各 ~48 LSB 余量，
 * 避免 DAC 输出缓冲在轨附近非线性导致的削顶） */
static const u16 sine_tab[DAC_TAB_LEN] =
{
    2048, 2244, 2438, 2629, 2813, 2991, 3159, 3317,
    3462, 3594, 3711, 3812, 3896, 3962, 4010, 4038,
    4048, 4038, 4010, 3962, 3896, 3812, 3711, 3594,
    3462, 3317, 3159, 2991, 2813, 2629, 2438, 2244,
    2048, 1852, 1658, 1467, 1283, 1105,  937,  779,
     634,  502,  385,  284,  200,  134,   86,   58,
      48,   58,   86,  134,  200,  284,  385,  502,
     634,  779,  937, 1105, 1283, 1467, 1658, 1852
};

/* 频率档：符号频率、定时器自动重装值、界面显示
 * ARR = 72 MHz / (64 * f) - 1，PSC 固定 0（TIM6/7 挂在 APB1，
 * 但定时器时钟是 APB1 的两倍，即 72 MHz） */
static const u32 freq_hz[DAC_FREQ_LEVELS] =
{
    50, 100, 200, 500, 1000, 2000, 5000, 10000
};

static const u16 freq_arr[DAC_FREQ_LEVELS] =
{
    22499, 11249, 5624, 2249, 1124, 562, 224, 112
};

static const char *freq_name[DAC_FREQ_LEVELS] =
{
    "50", "100", "200", "500", "1K", "2K", "5K", "10K"
};

static const char *wave_name[DW_COUNT] =
{
    "SINE", "SQUARE", "TRI", "SAW", "NOISE"
};

/*-------------------- 状态 ------------------------*/

static u16 wave_tab[DAC_TAB_LEN];       /* 两路共用的 DMA 源缓冲     */
static u8  cur_wave  = DW_SINE;
static u8  cur_freq  = 4;               /* 默认 1 kHz                 */
static u8  ch1_on    = 0;
static u8  ch2_on    = 0;
static u8  dac_ready = 0;
static u16 lfsr      = 0xACE1;

/*-------------------- 波形表生成 ------------------*/

/* 16 位 Galois LFSR，抽头 0xB400 就是标准的 x^16+x^14+x^13+x^11+1，
 * 取低 12 位当 DAC 数据，看起来是均匀的白噪声 */
static u16 next_noise(void)
{
    if (lfsr & 1) lfsr = (u16)((lfsr >> 1) ^ 0xB400u);
    else          lfsr = (u16)(lfsr >> 1);

    return (u16)(lfsr & 0x0FFF);
}

static void build_table(u8 w)
{
    u8  i;
    u16 half = DAC_TAB_LEN / 2;

    switch (w)
    {
    case DW_SQUARE:
        for (i = 0; i < DAC_TAB_LEN; i++)
            wave_tab[i] = (i < half) ? 3900 : 200;
        break;

    case DW_TRI:
        for (i = 0; i < half; i++)
        {
            wave_tab[i]           = (u16)(200 + (u32)i * 3700 / (half - 1));
            wave_tab[DAC_TAB_LEN - 1 - i] = wave_tab[i];
        }
        break;

    case DW_SAW:
        for (i = 0; i < DAC_TAB_LEN; i++)
            wave_tab[i] = (u16)(200 + (u32)i * 3700 / (DAC_TAB_LEN - 1));
        break;

    case DW_NOISE:
        for (i = 0; i < DAC_TAB_LEN; i++)
            wave_tab[i] = next_noise();
        break;

    default:                            /* DW_SINE */
        for (i = 0; i < DAC_TAB_LEN; i++)
            wave_tab[i] = sine_tab[i];
        break;
    }
}

/*-------------------- 初始化 ----------------------*/

void DAC_WaveInit(void)
{
    GPIO_InitTypeDef       gpio;
    DAC_InitTypeDef        dac;
    DMA_InitTypeDef        dma;
    TIM_TimeBaseInitTypeDef tb;

    if (dac_ready) return;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC |
                           RCC_APB1Periph_TIM6 |
                           RCC_APB1Periph_TIM7, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* PA4/PA5 必须配成模拟输入，否则 DAC 引脚不接管输出 */
    gpio.GPIO_Pin   = GPIO_Pin_4 | GPIO_Pin_5;
    gpio.GPIO_Mode  = GPIO_Mode_AIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    build_table(cur_wave);

    /* 通道 1 -> TIM6，通道 2 -> TIM7
     *
     * 这里不用库里的 DAC_DeInit()：V3.5 标准库的它只做了
     * RCC_APB1PeriphResetCmd(RCC_APB1Periph_DAC, ENABLE)，
     * 并没有再清 0，调用完 DAC 会一直停在复位态，
     * 后面所有 DAC_Init / DAC_Cmd 写进去的寄存器全部被忽略，
     * 表现就是"程序没死但引脚一点波形都没有"。
     * 手动置位再清零才真正把外设释放出来。 */
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_DAC, ENABLE);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_DAC, DISABLE);

    dac.DAC_WaveGeneration            = DAC_WaveGeneration_None;
    dac.DAC_LFSRUnmask_TriangleAmplitude = DAC_LFSRUnmask_Bit0;
    dac.DAC_OutputBuffer              = DAC_OutputBuffer_Enable;

    dac.DAC_Trigger = DAC_Trigger_T6_TRGO;
    DAC_Init(DAC_Channel_1, &dac);

    dac.DAC_Trigger = DAC_Trigger_T7_TRGO;
    DAC_Init(DAC_Channel_2, &dac);

    DAC_SetChannel1Data(DAC_Align_12b_R, 0);
    DAC_SetChannel2Data(DAC_Align_12b_R, 0);

    /* DMA1 通道 3 服务 DAC1，通道 4 服务 DAC2；
     * SDIO 用的是 DMA2_Channel4，两者不冲突 */
    DMA_DeInit(DMA1_Channel3);
    DMA_DeInit(DMA1_Channel4);

    dma.DMA_PeripheralBaseAddr = (u32)&DAC->DHR12R1;
    dma.DMA_MemoryBaseAddr     = (u32)wave_tab;
    dma.DMA_DIR                = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize         = DAC_TAB_LEN;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode               = DMA_Mode_Circular;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &dma);

    dma.DMA_PeripheralBaseAddr = (u32)&DAC->DHR12R2;
    DMA_Init(DMA1_Channel4, &dma);

    /* 顺序按参考手册来：先使能 DAC 通道，再挂 DMA 请求 */
    DAC_Cmd(DAC_Channel_1, ENABLE);
    DAC_Cmd(DAC_Channel_2, ENABLE);

    DAC_DMACmd(DAC_Channel_1, ENABLE);
    DAC_DMACmd(DAC_Channel_2, ENABLE);

    /* TIM6 / TIM7：PSC=0，ARR 决定采样率，更新事件送 TRGO */
    tb.TIM_Prescaler         = 0;
    tb.TIM_Period            = freq_arr[cur_freq];
    tb.TIM_ClockDivision     = TIM_CKD_DIV1;
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_RepetitionCounter = 0;

    TIM_TimeBaseInit(TIM6, &tb);
    TIM_TimeBaseInit(TIM7, &tb);

    TIM_SelectOutputTrigger(TIM6, TIM_TRGOSource_Update);
    TIM_SelectOutputTrigger(TIM7, TIM_TRGOSource_Update);

    ch1_on    = 0;
    ch2_on    = 0;
    dac_ready = 1;
}

/*-------------------- 通道开关 --------------------*/

void DAC_Ch1On(u8 on)
{
    if (!dac_ready) return;

    if (on)
    {
        DAC_SetChannel1Data(DAC_Align_12b_R, 0);
        DMA_SetCurrDataCounter(DMA1_Channel3, DAC_TAB_LEN);
        DMA_Cmd(DMA1_Channel3, ENABLE);
        TIM_Cmd(TIM6, ENABLE);
        ch1_on = 1;
    }
    else
    {
        TIM_Cmd(TIM6, DISABLE);
        DMA_Cmd(DMA1_Channel3, DISABLE);
        DAC_SetChannel1Data(DAC_Align_12b_R, 0);   /* 拉回 0 V */
        ch1_on = 0;
    }
}

void DAC_Ch2On(u8 on)
{
    if (!dac_ready) return;

    if (on)
    {
        DAC_SetChannel2Data(DAC_Align_12b_R, 0);
        DMA_SetCurrDataCounter(DMA1_Channel4, DAC_TAB_LEN);
        DMA_Cmd(DMA1_Channel4, ENABLE);
        TIM_Cmd(TIM7, ENABLE);
        ch2_on = 1;
    }
    else
    {
        TIM_Cmd(TIM7, DISABLE);
        DMA_Cmd(DMA1_Channel4, DISABLE);
        DAC_SetChannel2Data(DAC_Align_12b_R, 0);
        ch2_on = 0;
    }
}

void DAC_StopAll(void)
{
    DAC_Ch1On(0);
    DAC_Ch2On(0);
}

void DAC_ToggleBoth(void)
{
    /* 两路都开着 -> 全关；其余情况 -> 全开 */
    u8 on = (ch1_on && ch2_on) ? 0 : 1;

    DAC_Ch1On(on);
    DAC_Ch2On(on);
}

u8 DAC_Ch1IsOn(void) { return ch1_on; }
u8 DAC_Ch2IsOn(void) { return ch2_on; }

void DAC_SetOutput(u8 sel)
{
    switch (sel)
    {
    case DOUT_CH1:  DAC_Ch1On(1); DAC_Ch2On(0); break;
    case DOUT_CH2:  DAC_Ch1On(0); DAC_Ch2On(1); break;
    case DOUT_BOTH: DAC_Ch1On(1); DAC_Ch2On(1); break;
    default:        DAC_StopAll();              break;
    }
}

u8 DAC_GetOutput(void)
{
    if (ch1_on && ch2_on) return DOUT_BOTH;
    if (ch1_on)           return DOUT_CH1;
    if (ch2_on)           return DOUT_CH2;

    return DOUT_NONE;
}

/*-------------------- 波形 / 频率 ----------------*/

void DAC_SetWave(u8 w)
{
    u8 a, b;

    if (w >= DW_COUNT) return;

    /* 改表期间先停一下，否则 DMA 可能正读到一半，
     * 输出会突然跳一个台阶 */
    a = ch1_on;
    b = ch2_on;
    DAC_Ch1On(0);
    DAC_Ch2On(0);

    cur_wave = w;
    build_table(w);

    if (a) DAC_Ch1On(1);
    if (b) DAC_Ch2On(1);
}

u8 DAC_GetWave(void) { return cur_wave; }

void DAC_SetFreq(u8 lv)
{
    if (lv >= DAC_FREQ_LEVELS) return;

    cur_freq = lv;

    TIM_SetAutoreload(TIM6, freq_arr[lv]);
    TIM_SetAutoreload(TIM7, freq_arr[lv]);

    /* 新 ARR 可能比当前计数值小，不清零的话要等一圈才对齐 */
    TIM6->CNT = 0;
    TIM7->CNT = 0;
}

u8 DAC_GetFreq(void)      { return cur_freq; }
u32 DAC_GetFreqHz(void)   { return freq_hz[cur_freq]; }
const u16 *DAC_Table(void) { return wave_tab; }

const char *DAC_WaveName(u8 w)
{
    if (w >= DW_COUNT) w = 0;
    return wave_name[w];
}

const char *DAC_FreqName(u8 lv)
{
    if (lv >= DAC_FREQ_LEVELS) lv = 0;
    return freq_name[lv];
}
