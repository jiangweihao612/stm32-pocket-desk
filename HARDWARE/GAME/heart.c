/*=============================================================================
 *  心率/血氧监测 -- MAX30102（I2C：SDA=PE0、SCL=PE1、INT=PE6）
 *
 *  布局（320 x 240，横屏）
 *
 *   +--------------------------------------------------+
 *   | HEART MONITOR         IR 102345        < BACK    |  标题栏  0..23
 *   |  BPM                    SpO2                     |
 *   |   78                    97 %                     |  数字 25..60
 *   | 400+------------------------------------------+  |
 *   |    |        HR trend   : 0 .. 400 BPM         |  |  HR 图 68..117
 *   | 200|                                          |  |
 *   |   0+------------------------------------------+  |
 *   | 100+------------------------------------------+  |
 *   |    |        SpO2 trend : 0 .. 100 %           |  |  SpO2 图 136..185
 *   |  50|                                          |  |
 *   |   0+------------------------------------------+  |
 *   |               LIVE - HOLD STILL                  |  状态行  198
 *   |               I2C OK  ID 15  N 1234              |  诊断行  216
 *   +--------------------------------------------------+
 *
 *  MAXIM 算法需要 100 Hz 下 5 秒的采样才能给出结果，
 *  所以界面先填满 ir_buf[]，然后才开始计算。
 *  之后维持一个 5 秒的滚动窗口，每秒重算一次
 *  （丢掉最旧的 100 个采样，追加 100 个新采样）。
 *
 *  每算出一个结果，就在两条趋势图上各追加一个点，因此
 *  纵轴刻度就是真实数值：上图是 BPM（0..400），
 *  下图是百分比（0..100）。每秒一个点，宽 2 px。
 *
 *  退出：点右上角的 < BACK，或者按 K0（和三个游戏里的"返回"同键）。
 *===========================================================================*/

#include "heart.h"
#include "max30102.h"
#include "lcd.h"
#include "touch.h"
#include "key.h"
#include "delay.h"
#include "game.h"
#include <stdio.h>
#include <string.h>

/*-------------------- 颜色 --------------------*/
/* 和桌面同一套日落暖色：背景深咖，两块趋势图分别是
 * 深褐和深紫咖，波形一暖金一珊瑚，都在暖色系内区分。 */
#define C_BG        0x1881      /* 页面底色：深咖               */
#define C_HDR       0x30E3      /* 标题栏                       */
#define C_PANEL_HR  0x28E2      /* HR 图背景：深褐              */
#define C_PANEL_SP  0x20C4      /* SpO2 图背景：深紫咖          */
#define C_GRID      0x4984      /* 网格线：暖棕                 */
#define C_TRACE     0xF508      /* HR  波形：暖金               */
#define C_CYAN      0xEB07      /* SpO2 波形：珊瑚              */
#define C_WHITE     0xFF9B      /* 米白文字                     */
#define C_DIM       0xB4D0      /* 暖灰次要文字                 */
#define C_AMBER     0xECA7      /* 琥珀强调                     */
#define C_BTN       0x3923      /* 按钮：暖棕                   */

/*-------------------- 布局 --------------------*/
#define HDR_H       24
#define PX0         44          /* 左侧刻度栏，绘图区 x 起点       */
#define PW          268         /* PX0 + PW 结束于 x = 312              */

#define HR_Y0       68
#define HR_H        50          /* HR  面板：68 .. 117                  */
#define HR_MID      (HR_Y0 + HR_H / 2)

#define SP_Y0       136
#define SP_H        50          /* SpO2 面板：136 .. 185                */
#define SP_MID      (SP_Y0 + SP_H / 2)

#define HR_MAX      400         /* 轴范围：0 .. 400 BPM              */
#define SP_MAX      100         /* 轴范围：0 .. 100 %                */

#define TREND_W     2           /* 每采样点像素数，268/2 = 134 秒填满  */

#define BACK_X      250
#define BACK_Y      4
#define BACK_W      62
#define BACK_H      16

/* IR 电平达到此值即认为"手指已放在传感器上" */
#define FINGER_TH   20000

/*-------------------- 状态 ---------------------*/
static u16 sw = 320;
static u16 sh = 240;

static uint32_t ir_buf[FS * 5];         /* 500 个采样 = 5 秒  */
static uint32_t red_buf[FS * 5];
static uint32_t pend_ir[FS];            /* 下一秒的数据，暂存 */
static uint32_t pend_red[FS];
static u16 buf_n = 0;
static u16 pend_n = 0;
static u8  calc_pending = 0;

static int32_t hr_val = 0;
static int32_t spo2_val = 0;
static u8      hr_ok = 0;
static u8      spo2_ok = 0;

/* 趋势图光标；-1 表示"没有上一个点"，即抬笔 */
static u16 plot_x   = PX0;
static int hr_prev  = -1;
static int sp_prev  = -1;

static char last_msg[40] = "";

/* 诊断用：FIFO 实际送来了多少个采样，以及
 * 看到的最大 IR 值（芯片正常且手指在位时约 100k）        */
static u32 sample_cnt = 0;
static u32 ir_max     = 0;

/*=============================================================================
 *  绘图小工具（menu.c 里有自己的一份私有副本）
 *===========================================================================*/

static void hfill(int x, int y, int w, int h, u16 c)
{
    if (w <= 0 || h <= 0) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;

    if (x + w > (int)sw) w = (int)sw - x;
    if (y + h > (int)sh) h = (int)sh - y;
    if (w <= 0 || h <= 0) return;

    LCD_Fill((u16)x, (u16)y, (u16)(x + w - 1), (u16)(y + h - 1), c);
}

static void htext(int x, int y, u8 size, char *s, u16 fg, u16 bg)
{
    POINT_COLOR = fg;
    BACK_COLOR  = bg;
    LCD_ShowString((u16)x, (u16)y, 250, size + 4, size, (u8 *)s);
}

static void hcenter(int x0, int x1, int y, u8 size, char *s, u16 fg, u16 bg)
{
    u16 n = 0;
    char *p = s;

    while (*p++) n++;

    htext(x0 + (int)((x1 - x0 - n * (size / 2)) / 2), y, size, s, fg, bg);
}

static u8 in_rect(u16 x, u16 y, u16 rx, u16 ry, u16 rw, u16 rh)
{
    return (x >= rx && x < (u16)(rx + rw) && y >= ry && y < (u16)(ry + rh)) ? 1 : 0;
}

/*=============================================================================
 *  数值 -> 像素映射
 *===========================================================================*/

/* 波形与面板边框保持 2 px 间距，否则 0 BPM 或
 * 100 % 会正好压在边框线上，看起来像多出一道边   */
static int hr_to_y(int32_t v)
{
    if (v < 0)   v = 0;
    if (v > HR_MAX) v = HR_MAX;

    return (HR_Y0 + HR_H - 2) - (int)((v * (HR_H - 4)) / HR_MAX);
}

static int sp_to_y(int32_t v)
{
    if (v < 0)   v = 0;
    if (v > SP_MAX) v = SP_MAX;

    return (SP_Y0 + SP_H - 2) - (int)((v * (SP_H - 4)) / SP_MAX);
}

/*=============================================================================
 *  页面的静态部分
 *===========================================================================*/

static void heart_header(void)
{
    hfill(0, 0, sw, HDR_H, C_HDR);
    htext(8, 6, 12, (char *)"HEART MONITOR", C_WHITE, C_HDR);

    hfill(BACK_X, BACK_Y, BACK_W, BACK_H, C_BTN);
    htext(BACK_X + 6, 6, 12, (char *)"K0 <BACK", C_WHITE, C_BTN);
}

/* 刻度栏里的一个数字，右对齐以便各位数字对齐 */
static void axis_label(int y, int v, u16 color)
{
    char b[8];
    int  len;
    int  xr = PX0 - 8;

    sprintf(b, "%d", v);
    len = (int)strlen(b);

    hfill(0, y - 5, xr, 12, C_BG);
    htext(xr - 6 * len, y - 5, 12, b, color, C_BG);
}

static void hr_panel(void)
{
    hfill(PX0, HR_Y0, PW, HR_H, C_PANEL_HR);
    hfill(PX0, HR_MID, PW, 1, C_GRID);

    POINT_COLOR = C_GRID;
    LCD_DrawRectangle(PX0 - 1, HR_Y0 - 1, PX0 + PW, HR_Y0 + HR_H);

    hfill(PX0 - 6, HR_Y0,  6, 1, C_DIM);
    hfill(PX0 - 6, HR_MID, 6, 1, C_DIM);
    hfill(PX0 - 6, HR_Y0 + HR_H, 6, 1, C_DIM);

    axis_label(HR_Y0,              HR_MAX,     C_DIM);
    axis_label(HR_MID,             HR_MAX / 2, C_WHITE);
    axis_label(HR_Y0 + HR_H,       0,          C_DIM);
}

static void sp_panel(void)
{
    hfill(PX0, SP_Y0, PW, SP_H, C_PANEL_SP);
    hfill(PX0, SP_MID, PW, 1, C_GRID);

    POINT_COLOR = C_GRID;
    LCD_DrawRectangle(PX0 - 1, SP_Y0 - 1, PX0 + PW, SP_Y0 + SP_H);

    hfill(PX0 - 6, SP_Y0,  6, 1, C_DIM);
    hfill(PX0 - 6, SP_MID, 6, 1, C_DIM);
    hfill(PX0 - 6, SP_Y0 + SP_H, 6, 1, C_DIM);

    axis_label(SP_Y0,              SP_MAX,     C_DIM);
    axis_label(SP_MID,             SP_MAX / 2, C_WHITE);
    axis_label(SP_Y0 + SP_H,       0,          C_DIM);
}

static void heart_frame(void)
{
    hr_panel();
    sp_panel();
}

/* 3 位大号数字，左对齐于固定区域，这样旧值会被擦干净 */
static void heart_draw_bpm(void)
{
    char b[8];

    if (hr_ok) sprintf(b, "%3d", (int)hr_val);
    else       sprintf(b, "---");

    hfill(12, 36, 62, 26, C_BG);
    htext(12, 36, 24, b, C_WHITE, C_BG);
}

static void heart_draw_spo2(void)
{
    char b[8];

    if (spo2_ok) sprintf(b, "%3d", (int)spo2_val);
    else         sprintf(b, "---");

    hfill(168, 36, 52, 26, C_BG);
    htext(168, 36, 24, b, C_CYAN, C_BG);
    htext(208, 44, 16, (char *)"%", C_CYAN, C_BG);
}

/* 只有文字真正变化时才重绘状态行（避免闪烁） */
static void heart_status(char *s, u16 color)
{
    if (strcmp(s, last_msg) == 0) return;

    strcpy(last_msg, s);
    hfill(0, 198, sw, 16, C_BG);
    hcenter(0, (int)sw, 198, 12, s, color, C_BG);
}

/*=============================================================================
 *  趋势图：每个计算结果占一列
 *===========================================================================*/

/* 竖直线段，用于把上一个点连到新点 */
static void vseg(int x, int y0, int y1, u16 c)
{
    int t;

    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    if (y0 < 0) y0 = 0;

    hfill(x, y0, 1, y1 - y0 + 1, c);
}

static void trend_reset(void)
{
    hfill(PX0, HR_Y0, PW, HR_H, C_PANEL_HR);
    hfill(PX0, HR_MID, PW, 1, C_GRID);
    hfill(PX0, SP_Y0, PW, SP_H, C_PANEL_SP);
    hfill(PX0, SP_MID, PW, 1, C_GRID);

    plot_x = PX0;
    hr_prev = -1;
    sp_prev = -1;
}

/* 把当前读数追加到两条曲线上；每秒调用一次，因为
 * 这也是 MAXIM 算法能产出结果的速率     */
static void trend_push(void)
{
    int yh = -1;
    int ys = -1;
    int x;

    if (hr_ok)   yh = hr_to_y(hr_val);
    if (spo2_ok) ys = sp_to_y(spo2_val);

    /* 先擦掉即将写入的两列，再恢复中线 */
    for (x = plot_x; x < plot_x + TREND_W && x < PX0 + PW; x++)
    {
        hfill(x, HR_Y0, 1, HR_H, C_PANEL_HR);
        hfill(x, HR_MID, 1, 1, C_GRID);
        hfill(x, SP_Y0, 1, SP_H, C_PANEL_SP);
        hfill(x, SP_MID, 1, 1, C_GRID);
    }

    if (yh >= 0)
    {
        if (hr_prev >= 0) vseg(plot_x, hr_prev, yh, C_TRACE);
        else              hfill(plot_x, yh, TREND_W, 1, C_TRACE);

        if (plot_x + 1 < PX0 + PW) hfill(plot_x + 1, yh, 1, 1, C_TRACE);
        hr_prev = yh;
    }
    else hr_prev = -1;                  /* 信号丢失时抬笔 */

    if (ys >= 0)
    {
        if (sp_prev >= 0) vseg(plot_x, sp_prev, ys, C_CYAN);
        else              hfill(plot_x, ys, TREND_W, 1, C_CYAN);

        if (plot_x + 1 < PX0 + PW) hfill(plot_x + 1, ys, 1, 1, C_CYAN);
        sp_prev = ys;
    }
    else sp_prev = -1;

    plot_x += TREND_W;

    if (plot_x + TREND_W > PX0 + PW)
    {
        plot_x  = PX0;
        hr_prev = -1;                   /* 回卷处不要连线 */
        sp_prev = -1;
    }
}

/*=============================================================================
 *  采样
 *===========================================================================*/

/* 芯片 FIFO 中待取的采样数（0..31） */
static u8 heart_pending(void)
{
    u8 wr = max30102_Bus_Read(REG_FIFO_WR_PTR);
    u8 rd = max30102_Bus_Read(REG_FIFO_RD_PTR);

    return (u8)((wr - rd) & 0x1F);
}

static void heart_buf_reset(void)
{
    buf_n = 0;
    pend_n = 0;
    calc_pending = 0;

    trend_reset();
}

/* 向窗口追加一个采样；到该运行算法的时刻就置 calc_pending
 * （前 5 秒之后，之后每秒一次）               */
static void heart_push(uint32_t ir, uint32_t red)
{
    sample_cnt++;
    if (ir > ir_max) ir_max = ir;

    if (buf_n < FS * 5)
    {
        ir_buf[buf_n]  = ir;
        red_buf[buf_n] = red;
        buf_n++;

        if (buf_n == FS * 5)
        {
            pend_n = 0;
            calc_pending = 1;
        }
        return;
    }

    pend_ir[pend_n]  = ir;
    pend_red[pend_n] = red;
    pend_n++;

    if (pend_n >= FS)
    {
        /* 丢掉最旧的一秒，追加新的一秒 */
        memmove(ir_buf,  ir_buf  + FS, (FS * 4) * sizeof(uint32_t));
        memmove(red_buf, red_buf + FS, (FS * 4) * sizeof(uint32_t));
        memcpy(ir_buf  + FS * 4, pend_ir,  FS * sizeof(uint32_t));
        memcpy(red_buf + FS * 4, pend_red, FS * sizeof(uint32_t));

        pend_n = 0;
        calc_pending = 1;
    }
}

static void heart_compute(void)
{
    int32_t spo2 = 0;
    int32_t hr   = 0;
    int8_t  s_ok = 0;
    int8_t  h_ok = 0;

    maxim_heart_rate_and_oxygen_saturation(ir_buf, FS * 5, red_buf,
                                           &spo2, &s_ok, &hr, &h_ok);

    hr_ok   = (h_ok != 0) ? 1 : 0;
    spo2_ok = (s_ok != 0) ? 1 : 0;

    /* 算法在窗口刚填满时偶尔会报出无意义的值；
     * 超出轴范围的数值都不可信，
     * 所以保留上一次的有效值，而不是在图上画出尖刺           */
    if (hr_ok   && (hr   < 20 || hr   > HR_MAX)) hr_ok   = 0;
    if (spo2_ok && (spo2 < 50 || spo2 > SP_MAX)) spo2_ok = 0;

    if (hr_ok)   hr_val   = hr;
    if (spo2_ok) spo2_val = spo2;

    heart_draw_bpm();
    heart_draw_spo2();
    trend_push();
}

/*=============================================================================
 *  启动故障报告
 *===========================================================================*/

/* 芯片为什么没应答 -- 否则这四种情况在屏幕上一模一样；
 * 所以这里直接写清楚该检查什么                       */
static char *fault_text(u8 f)
{
    switch (f)
    {
    case 1:  return (char *)"I2C BUS LOW - CHECK VCC/GND";
    case 2:  return (char *)"NO ACK - CHECK SDA/SCL/PULLUP";
    case 3:  return (char *)"WRONG CHIP ID";
    case 4:  return (char *)"WRITE FAIL - CHECK SDA";
    default: return (char *)"OK";
    }
}

/* 底部一行小字：I2C 状态、器件 ID、已收到的采样数。
 * 这是区分"i2c 坏了"和"芯片空转"最快的方法。        */
static void heart_diag(u8 fault, u8 part)
{
    char b[44];

    sprintf(b, "I2C %s  ID %02X  N %lu", fault ? "FAIL" : "OK ",
            (unsigned)part, (unsigned long)sample_cnt);

    hfill(0, 216, sw, 14, C_BG);
    hcenter(0, (int)sw, 216, 12, b, fault ? C_AMBER : C_DIM, C_BG);
}

/*=============================================================================
 *  页面入口
 *===========================================================================*/

void Heart_Run(void)
{
    uint32_t ir = 0;
    uint32_t red = 0;
    u8  n;
    u8  part;
    u8  fault = 0;
    u8  tick = 0;
    u16 diag_tick = 0;
    u16 secs = 0;
    u8  k0_last = 0;            /* K0 的边沿标志：按住只算一次          */
    char msg[40];

    sw = lcddev.width;
    sh = lcddev.height;

    hfill(0, 0, sw, sh, C_BG);

    heart_header();
    htext(12, 25, 12, (char *)"BPM", C_DIM, C_BG);
    htext(168, 25, 12, (char *)"SpO2", C_DIM, C_BG);
    heart_frame();
    hcenter(0, (int)sw, 198, 12, (char *)"CHECKING SENSOR", C_DIM, C_BG);

    hr_ok = 0;
    spo2_ok = 0;
    sample_cnt = 0;
    ir_max = 0;
    last_msg[0] = 0;
    heart_draw_bpm();
    heart_draw_spo2();

    /* 先让芯片起来，然后等复位稳定后再做
     * 下面的配置写入（驱动本身不会等待）      */
    MAX30102_Init();
    delay_ms(60);

    /*---- 探测：三次读就能定位问题在哪 ----*/
    part = max30102_Bus_Read(REG_PART_ID);

    if (part == 0x00)      fault = 1;   /* 总线被拉低，没有供电      */
    else if (part == 0xFF) fault = 2;   /* 无人应答：接线/上拉 */
    else if (part != 0x15) fault = 3;   /* 有应答，但不是 MAX30102  */

    if (fault == 0)
    {
        max30102_Bus_Write(REG_FIFO_WR_PTR, 0x00);
        max30102_Bus_Write(REG_OVF_COUNTER, 0x00);
        max30102_Bus_Write(REG_FIFO_RD_PTR, 0x00);
        max30102_Bus_Write(REG_FIFO_CONFIG, 0x1F);  /* 不做平均、溢出回卷、A_FULL 15 */
        max30102_Bus_Write(REG_MODE_CONFIG, 0x03);  /* SpO2 模式：红光 + 红外         */
        max30102_Bus_Write(REG_SPO2_CONFIG, 0x27);  /* 4096 nA, 100 Hz, 411 us     */
        max30102_Bus_Write(REG_LED1_PA, 0x24);      /* ~7 mA                       */
        max30102_Bus_Write(REG_LED2_PA, 0x24);

        /* 回读 LED 电流：同时也证明写入通路是好的 */
        if (max30102_Bus_Read(REG_LED1_PA) != 0x24) fault = 4;
    }

    heart_buf_reset();
    heart_diag(fault, part);

    if (fault) heart_status(fault_text(fault), C_AMBER);
    else       heart_status((char *)"PLACE FINGER ON SENSOR", C_AMBER);

    Game_Wait_Touch_Release();

    for (;;)
    {
        /* K0 返回主界面。本页没有用 UI 模块（触摸是直接轮询 tp_dev），
         * 所以这里自己做边沿检测：按住不放只触发一次，
         * 并且返回前等松手 —— 桌面上的 K0 是"打开恐龙游戏"，
         * 带着按键回桌面会被菜单当成一次新的按下。 */
        if (KEY0_DOWN)
        {
            if (!k0_last)
            {
                k0_last = 1;

                while (KEY0_DOWN) delay_ms(10);
                Game_Wait_Touch_Release();
                return;
            }
        }
        else
        {
            k0_last = 0;
        }

        /* 触摸：只有标题栏的返回按钮能离开本页 */
        if (++tick >= 8)
        {
            tick = 0;

            if (tp_dev.scan != 0 && tp_dev.scan(0))
            {
                if (in_rect(tp_dev.x[0], tp_dev.y[0], BACK_X, BACK_Y, BACK_W, BACK_H))
                {
                    Game_Wait_Touch_Release();
                    return;
                }
            }
        }

        /* 排空 FIFO
         * 像参考例程那样用 max30102_FIFO_ReadBytes() -- 那条
         * 路径有正确的重复起始信号；maxim_max30102_read_fifo()
         * 这层封装走的是 MAX30102_IIC_ReadBytes()，原本
         * 漏掉了它，于是永远返回 0xFF。                      */
        if (!fault)
        {
            u8 t[6];

            n = heart_pending();
            while (n--)
            {
                max30102_FIFO_ReadBytes(REG_FIFO_DATA, t);

                red = ((u32)(t[0] & 0x03) << 16) | ((u32)t[1] << 8) | t[2];
                ir  = ((u32)(t[3] & 0x03) << 16) | ((u32)t[4] << 8) | t[5];

                heart_push(ir, red);
            }
        }

        if (calc_pending)
        {
            calc_pending = 0;
            heart_compute();
        }

        /* 标题栏实时显示 IR 值，便于确认手指是否在位 */
        if (ir != 0 && (tick & 0x03) == 0)
        {
            sprintf(msg, "IR %6lu", (unsigned long)ir);
            hfill(132, 6, 110, 14, C_HDR);
            htext(132, 6, 12, msg, C_DIM, C_HDR);
        }

        /* 每秒一次：刷新诊断行和计数器 */
        if (++diag_tick >= 250)
        {
            diag_tick = 0;
            if (secs < 60000) secs++;
            heart_diag(fault, part);

            /* 同样的数字也送到 USART1（PA9，115200 8N1）便于串口查看：
             * wr/rd 一直停在同一个值说明芯片没有在采样  */
            printf("MAX30102 part=0x%02X wr=%u rd=%u n=%lu ir=%lu imax=%lu\r\n",
                   (unsigned)part,
                   (unsigned)max30102_Bus_Read(REG_FIFO_WR_PTR),
                   (unsigned)max30102_Bus_Read(REG_FIFO_RD_PTR),
                   (unsigned long)sample_cnt,
                   (unsigned long)ir, (unsigned long)ir_max);
        }

        /* 状态行 */
        if (fault)
        {
            heart_status(fault_text(fault), C_AMBER);
        }
        else if (ir < FINGER_TH)
        {
            if (buf_n || pend_n)
            {
                heart_buf_reset();
                hr_ok = 0;
                spo2_ok = 0;
                heart_draw_bpm();
                heart_draw_spo2();
            }

            /* 芯片有应答但一直不产采样 -> 模式写入
             * 没生效，或者传感器还在关断状态            */
            if (secs >= 3 && sample_cnt == 0)
                heart_status((char *)"NO FIFO DATA - CHIP IN SHUTDOWN", C_AMBER);
            /* 有采样但 LED 不亮 -> VLED 没供电      */
            else if (sample_cnt >= 200 && ir_max < 1000)
                heart_status((char *)"IR TOO LOW - CHECK VLED", C_AMBER);
            else
                heart_status((char *)"PLACE FINGER ON SENSOR", C_AMBER);
        }
        else if (buf_n < FS * 5)
        {
            sprintf(msg, "MEASURING %d%% - HOLD STILL",
                    (int)((u32)buf_n * 100 / (FS * 5)));
            heart_status(msg, C_CYAN);
        }
        else if (hr_ok || spo2_ok)
        {
            heart_status((char *)"LIVE - HOLD STILL", C_TRACE);
        }
        else
        {
            heart_status((char *)"POOR SIGNAL - KEEP STILL", C_AMBER);
        }

        delay_ms(4);
    }
}
