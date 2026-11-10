#include "bmp180.h"
#include "delay.h"
#include <math.h>

/*******************************************************************************
 * BMP180 (GY-68 / MK000808) 驱动 - PB6 / PB7 上的软件 I2C
 *
 * 默认引脚映射 : SCL = PB7 , SDA = PB6
 * 备用引脚映射 : SCL = PB6 , SDA = PB7   (初始化时自动检测)
 *
 * 本文件只负责传感器，不涉及显示部分。屏幕
 * 相关的代码在 menu.c 中（启动器桌面上的传感器卡片）。
 ******************************************************************************/

typedef struct
{
    int16_t  ac1;
    int16_t  ac2;
    int16_t  ac3;
    uint16_t ac4;
    uint16_t ac5;
    uint16_t ac6;
    int16_t  b1;
    int16_t  b2;
    int16_t  mb;
    int16_t  mc;
    int16_t  md;
} bmp180_cal_t;

static bmp180_cal_t bmp180_cal;
static int32_t b5 = 0;                  /* 与温度相关的中间值，供气压计算使用 */
static float   press_filtered = 0.0f;   /* 滤波后的气压，单位 Pa */

/* 运行时引脚映射 */
static uint16_t scl_pin = GPIO_Pin_7;
static uint16_t sda_pin = GPIO_Pin_6;

/* 诊断信息，main 中可以直接看到 */
uint8_t BMP180_IdA         = 0xFF;
uint8_t BMP180_IdB         = 0x00;
uint8_t BMP180_PinSwapped  = 0;
uint8_t BMP180_BusLevel    = 0x03;
uint8_t BMP180_ScanCnt     = 0;
uint8_t BMP180_ScanList[8] = {0};
uint8_t BMP180_ScanSwapped = 0;

/* ------------------------------------------------------------------ */
/*                       软件 I2C 底层                                */
/* ------------------------------------------------------------------ */
#define SCL_H()     GPIO_SetBits(GPIOB, scl_pin)
#define SCL_L()     GPIO_ResetBits(GPIOB, scl_pin)
#define SDA_H()     GPIO_SetBits(GPIOB, sda_pin)
#define SDA_L()     GPIO_ResetBits(GPIOB, sda_pin)
#define SDA_READ()  ((GPIOB->IDR & sda_pin) ? 1 : 0)

static void iic_sda_out(void)
{
    GPIO_InitTypeDef gpio;

    gpio.GPIO_Pin   = sda_pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOB, &gpio);
}

static void iic_sda_in(void)
{
    GPIO_InitTypeDef gpio;

    gpio.GPIO_Pin   = sda_pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);
}

/* (重新)把两根线配置为推挽输出，空闲为高 */
static void iic_pins_apply(uint16_t scl, uint16_t sda)
{
    GPIO_InitTypeDef gpio;

    scl_pin = scl;
    sda_pin = sda;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    gpio.GPIO_Pin   = scl_pin | sda_pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOB, &gpio);

    SCL_H();
    SDA_H();
    delay_us(5);
}

void BMP180_IIC_Init(void)
{
    iic_pins_apply(GPIO_Pin_7, GPIO_Pin_6);     /* SCL=PB7, SDA=PB6 */
}

/* 两个引脚都设为上拉输入，读取空闲电平 */
static uint8_t iic_bus_level(void)
{
    GPIO_InitTypeDef gpio;
    uint8_t lv;

    gpio.GPIO_Pin   = GPIO_Pin_6 | GPIO_Pin_7;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);
    delay_us(200);

    lv  = (GPIOB->IDR & GPIO_Pin_7) ? 0x02 : 0x00;   /* 此处 SCL 是 PB7 */
    lv |= (GPIOB->IDR & GPIO_Pin_6) ? 0x01 : 0x00;   /* 此处 SDA 是 PB6 */

    iic_pins_apply(scl_pin, sda_pin);                /* 恢复为输出 */

    return lv;
}

/* 从机如果在传输中途被复位，可能会一直拉住 SDA 为低：
 * 松开 SDA 打 9 个时钟脉冲，然后发一个 STOP */
static void iic_stop(void);

static void iic_bus_recover(void)
{
    uint8_t i;

    iic_sda_out();
    SDA_H();
    for (i = 0; i < 9; i++)
    {
        SCL_H();
        delay_us(4);
        SCL_L();
        delay_us(4);
    }
    iic_stop();
}

static void iic_start(void)
{
    iic_sda_out();
    SDA_H();
    SCL_H();
    delay_us(4);
    SDA_L();
    delay_us(4);
    SCL_L();
}

static void iic_stop(void)
{
    iic_sda_out();
    SCL_L();
    SDA_L();
    delay_us(4);
    SCL_H();
    SDA_H();
    delay_us(4);
}

/* 0: 收到应答，1: 无应答（超时） */
static uint8_t iic_wait_ack(void)
{
    uint8_t err = 0;

    iic_sda_in();
    SDA_H();                    /* 释放该线，由上拉拉高 */
    delay_us(2);
    SCL_H();
    delay_us(2);
    while (SDA_READ())
    {
        err++;
        if (err > 200)
        {
            SCL_L();
            return 1;
        }
    }
    SCL_L();
    return 0;
}

static void iic_ack(void)
{
    SCL_L();
    iic_sda_out();
    SDA_L();
    delay_us(2);
    SCL_H();
    delay_us(2);
    SCL_L();
}

static void iic_nack(void)
{
    SCL_L();
    iic_sda_out();
    SDA_H();
    delay_us(2);
    SCL_H();
    delay_us(2);
    SCL_L();
}

static void iic_send_byte(uint8_t dat)
{
    uint8_t i;

    iic_sda_out();
    SCL_L();
    for (i = 0; i < 8; i++)
    {
        if (dat & 0x80) SDA_H();
        else            SDA_L();
        dat <<= 1;
        delay_us(3);
        SCL_H();
        delay_us(3);
        SCL_L();
        delay_us(3);
    }
}

static uint8_t iic_read_byte(uint8_t ack)
{
    uint8_t i, dat = 0;

    iic_sda_in();
    for (i = 0; i < 8; i++)
    {
        SCL_L();
        delay_us(3);
        SCL_H();
        dat <<= 1;
        if (SDA_READ()) dat++;
        delay_us(2);
    }
    if (ack) iic_ack();
    else     iic_nack();

    return dat;
}

/* ------------------------------------------------------------------ */
/*                        寄存器访问                                   */
/* ------------------------------------------------------------------ */
static void bmp180_write_reg(uint8_t reg, uint8_t val)
{
    iic_start();
    iic_send_byte(BMP180_ADDR);
    iic_wait_ack();
    iic_send_byte(reg);
    iic_wait_ack();
    iic_send_byte(val);
    iic_wait_ack();
    iic_stop();
}

static void bmp180_read_regs(uint8_t reg, uint8_t len, uint8_t *buf)
{
    uint8_t i;

    iic_start();
    iic_send_byte(BMP180_ADDR);
    iic_wait_ack();
    iic_send_byte(reg);
    iic_wait_ack();

    iic_start();
    iic_send_byte(BMP180_ADDR | 0x01);
    iic_wait_ack();
    for (i = 0; i < len; i++)
    {
        if (i == len - 1) buf[i] = iic_read_byte(0);
        else              buf[i] = iic_read_byte(1);
    }
    iic_stop();
}

static uint8_t bmp180_read_reg(uint8_t reg)
{
    uint8_t val;

    bmp180_read_regs(reg, 1, &val);
    return val;
}

/* ------------------------------------------------------------------ */
/*                        总线诊断                                     */
/* ------------------------------------------------------------------ */
/* 返回 1: 地址 addr7（7 位）上的器件有应答 */
uint8_t BMP180_Probe(uint8_t addr7)
{
    uint8_t ack;

    iic_start();
    iic_send_byte((uint8_t)(addr7 << 1));
    ack = iic_wait_ack();
    iic_stop();

    return (ack == 0) ? 1 : 0;
}

void BMP180_ScanBus(void)
{
    uint8_t addr;

    BMP180_ScanCnt = 0;
    for (addr = 0x08; addr < 0x78; addr++)
    {
        if (BMP180_Probe(addr))
        {
            if (BMP180_ScanCnt < 8) BMP180_ScanList[BMP180_ScanCnt] = addr;
            BMP180_ScanCnt++;
        }
    }
    if (BMP180_ScanCnt > 8) BMP180_ScanCnt = 8;
}

/* ------------------------------------------------------------------ */
/*                             接口                                     */
/* ------------------------------------------------------------------ */
uint8_t BMP180_ReadID(void)
{
    return bmp180_read_reg(BMP180_CHIP_ID_REG);
}

static void bmp180_read_calibration(void)
{
    uint8_t buf[22];

    bmp180_read_regs(BMP180_CALIB_REG, 22, buf);

    bmp180_cal.ac1 = (int16_t)(((uint16_t)buf[0]  << 8) | buf[1]);
    bmp180_cal.ac2 = (int16_t)(((uint16_t)buf[2]  << 8) | buf[3]);
    bmp180_cal.ac3 = (int16_t)(((uint16_t)buf[4]  << 8) | buf[5]);
    bmp180_cal.ac4 = (uint16_t)(((uint16_t)buf[6]  << 8) | buf[7]);
    bmp180_cal.ac5 = (uint16_t)(((uint16_t)buf[8]  << 8) | buf[9]);
    bmp180_cal.ac6 = (uint16_t)(((uint16_t)buf[10] << 8) | buf[11]);
    bmp180_cal.b1  = (int16_t)(((uint16_t)buf[12] << 8) | buf[13]);
    bmp180_cal.b2  = (int16_t)(((uint16_t)buf[14] << 8) | buf[15]);
    bmp180_cal.mb  = (int16_t)(((uint16_t)buf[16] << 8) | buf[17]);
    bmp180_cal.mc  = (int16_t)(((uint16_t)buf[18] << 8) | buf[19]);
    bmp180_cal.md  = (int16_t)(((uint16_t)buf[20] << 8) | buf[21]);

    press_filtered = 0.0f;
    b5 = 0;
}

/*
 * 初始化流程
 * 1. 先试 SCL = PB7  SDA = PB6   (按文档接线)
 * 2. 再试 SCL = PB6  SDA = PB7   (接线反了)
 * 3. 都不行 -> 扫描整条总线用于诊断，返回 0
 */
uint8_t BMP180_Init(void)
{
    BMP180_PinSwapped = 0;
    BMP180_ScanCnt    = 0;
    BMP180_ScanSwapped = 0;
    BMP180_IdB        = 0x00;

    /* ---- 1. 按文档接线 ---- */
    iic_pins_apply(GPIO_Pin_7, GPIO_Pin_6);
    delay_ms(20);
    iic_bus_recover();
    BMP180_BusLevel = iic_bus_level();
    BMP180_IdA = BMP180_ReadID();

    if (BMP180_IdA == BMP180_CHIP_ID)
    {
        bmp180_read_calibration();
        return 1;
    }

    /* ---- 2. 反接 ---- */
    iic_pins_apply(GPIO_Pin_6, GPIO_Pin_7);
    delay_ms(5);
    BMP180_IdB = BMP180_ReadID();

    if (BMP180_IdB == BMP180_CHIP_ID)
    {
        BMP180_PinSwapped = 1;
        bmp180_read_calibration();
        return 1;                       /* 保留可用的引脚映射 */
    }

    /* ---- 3. 两种都不行，收集诊断信息 ---- */
    iic_pins_apply(GPIO_Pin_7, GPIO_Pin_6);
    delay_ms(5);
    BMP180_ScanBus();

    if (BMP180_ScanCnt == 0)
    {
        iic_pins_apply(GPIO_Pin_6, GPIO_Pin_7);
        delay_ms(5);
        BMP180_ScanBus();
        BMP180_ScanSwapped = (BMP180_ScanCnt != 0) ? 1 : 0;
    }

    return 0;
}

/* 读取未补偿的温度 */
static int32_t bmp180_read_ut(void)
{
    uint8_t buf[2];

    bmp180_write_reg(BMP180_CTRL_MEAS_REG, BMP180_CMD_TEMP);
    delay_ms(5);                                        /* 最长 4.5ms */
    bmp180_read_regs(BMP180_DATA_REG, 2, buf);

    return ((int32_t)buf[0] << 8) | buf[1];
}

/* 读取未补偿的气压 */
static int32_t bmp180_read_up(void)
{
    uint8_t  buf[3];
    int32_t  up;

    bmp180_write_reg(BMP180_CTRL_MEAS_REG, BMP180_CMD_PRESS + (BMP180_OSS << 6));
    delay_ms(2 + (3 << BMP180_OSS));                    /* 4.5 / 7.5 / 13.5 / 25.5 ms */

    bmp180_read_regs(BMP180_DATA_REG, 3, buf);
    up = ((int32_t)buf[0] << 16) | ((int32_t)buf[1] << 8) | buf[2];

    return up >> (8 - BMP180_OSS);
}

/* 返回温度，单位 0.1 摄氏度，同时刷新 b5 */
static int32_t bmp180_calc_temp(int32_t ut)
{
    int32_t x1, x2;

    x1 = ((ut - (int32_t)bmp180_cal.ac6) * (int32_t)bmp180_cal.ac5) >> 15;
    x2 = (((int32_t)bmp180_cal.mc) << 11) / (x1 + (int32_t)bmp180_cal.md);
    b5 = x1 + x2;

    return (b5 + 8) >> 4;
}

/* 返回气压，单位 Pa，调用前 b5 必须有效 */
static int32_t bmp180_calc_press(int32_t up)
{
    int32_t  x1, x2, x3, b3, b6, p;
    uint32_t b4, b7;

    b6 = b5 - 4000;

    x1 = ((int32_t)bmp180_cal.b2 * ((b6 * b6) >> 12)) >> 11;
    x2 = ((int32_t)bmp180_cal.ac2 * b6) >> 11;
    x3 = x1 + x2;
    b3 = ((((int32_t)bmp180_cal.ac1 * 4 + x3) << BMP180_OSS) + 2) >> 2;

    x1 = ((int32_t)bmp180_cal.ac3 * b6) >> 13;
    x2 = ((int32_t)bmp180_cal.b1 * ((b6 * b6) >> 12)) >> 16;
    x3 = ((x1 + x2) + 2) >> 2;
    b4 = ((uint32_t)bmp180_cal.ac4 * (uint32_t)(x3 + 32768)) >> 15;

    b7 = ((uint32_t)(up - b3)) * (uint32_t)(50000UL >> BMP180_OSS);
    if (b7 < 0x80000000UL)
        p = (int32_t)((b7 << 1) / b4);
    else
        p = (int32_t)((b7 / b4) << 1);

    x1 = (p >> 8) * (p >> 8);
    x1 = (x1 * 3038) >> 16;
    x2 = (-7357 * p) >> 16;
    p  = p + ((x1 + x2 + 3791) >> 4);

    return p;
}

/* 气压 (Pa) -> 高度 (m) */
static float bmp180_press_to_alt(float pa)
{
    float ratio;

    if (pa <= 0.0f) return 0.0f;

    ratio = pa / BMP180_SEA_LEVEL_PA;
    return 44330.0f * (1.0f - (float)pow((double)ratio, 0.190295));
}

int32_t BMP180_ReadPressurePa(void)
{
    int32_t ut, up;

    ut = bmp180_read_ut();
    (void)bmp180_calc_temp(ut);                         /* 先刷新 b5 */
    up = bmp180_read_up();

    return bmp180_calc_press(up);
}

/*
 * 一次完整测量。
 * 温度 : 摄氏度
 * 气压 : hPa
 * 高度 : m（相对于海平面气压 1013.25 hPa）
 *
 * BMP180_OSS == 3 时阻塞时间约 35 ms，因此调用者
 * 不要在紧凑的循环里调用它。
 */
void BMP180_GetData(float *temperature, float *pressure, float *altitude)
{
    int32_t ut, up, t10, p_pa;
    float   p;

    ut  = bmp180_read_ut();
    t10 = bmp180_calc_temp(ut);
    up  = bmp180_read_up();
    p_pa = bmp180_calc_press(up);

    p = (float)p_pa;
    if (press_filtered <= 0.0f)
        press_filtered = p;
    else
        press_filtered = press_filtered * (1.0f - BMP180_FILTER) + p * BMP180_FILTER;

    if (temperature) *temperature = (float)t10 / 10.0f;
    if (pressure)    *pressure    = press_filtered / 100.0f;
    if (altitude)    *altitude    = bmp180_press_to_alt(press_filtered);
}
