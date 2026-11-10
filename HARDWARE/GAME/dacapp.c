/*****************************************************************************
 *  dacapp.c - DAC 波形输出 APP（输出界面 + 设置界面）
 *
 *  输出界面
 *  +--------------------------------------------------+
 *  | [SET]        DAC WAVEFORM              [HOME]    |  0..23
 *  | PA4 DAC1 SINE     x2 R>L   1K Hz         ON      |  26..38
 *  | +----------------------------------------------+ |
 *  | |           PA4 波形（2 个周期，滚动）          | |  40..103
 *  | +----------------------------------------------+ |
 *  | PA5 DAC2 SINE     x2 R>L   1K Hz        OFF      | 108..120
 *  | +----------------------------------------------+ |
 *  | |           PA5 波形（2 个周期，滚动）          | | 122..185
 *  | +----------------------------------------------+ |
 *  |      K1 PA4   K2 PA5   WK BOTH                   | 192
 *  |      K0 HOME   TAP SET FOR OPTIONS               | 210
 *  +--------------------------------------------------+
 *
 *  【多周期显示】
 *  曲线不是"一个周期 64 个点连折线"，而是逐像素算相位：
 *  每个 x 像素对应的相位 = 起点相位 + x * (周期数 * 64 * 256 / 宽度)，
 *  再按 Q8 小数部分在表里相邻两点之间做线性插值。这样无论显示
 *  1 个还是 5 个周期，横向都是 290 个采样点，密度永远够，
 *  不会出现"5 个周期只给 64 个点画成锯齿"的失真。
 *  陡峭段（方波跳变）用竖直线把相邻两列连起来，避免断线。
 *
 *  【滚动】
 *  每帧把相位偏移推 2 个像素对应的量，然后只擦掉框内可绘区
 *  （填回框底色）再重画曲线，边框不动。因为填的就是原来的
 *  底色，视觉上只有曲线在动，不会整块闪。
 *  方向：R>L = 相位递增（画面向左走，默认）；L>R = 相位递减。
 *
 *  【按通道分开】
 *  disp_cycle[2] / disp_dir[2] 下标 0 = PA4、1 = PA5，
 *  设置页里先选 EDIT PA4/PA5，再设 CYC（1~5）和 DIR，两路互不影响。
 *
 *  【跟的到底是不是真信号】
 *  曲线直接取驱动里那张 DMA 源表描点，屏幕画的就是引脚在输出的数据。
 *
 *  按键：
 *    K1     ->  PA4 开 / 关
 *    K2     ->  PA5 开 / 关
 *    WK_UP  ->  两路一起（都开就都关，否则都开）
 *    K0     ->  回主界面（退出时两路都停，设置保留）
 *
 *  设置界面
 *  +--------------------------------------------------+
 *  | [BACK]        DAC SETTINGS                       |  0..24
 *  | EDIT [PA4][PA5]                                  |  26..46
 *  | PIN  [PA4][PA5][BOTH][OFF]                       |  50..70
 *  | WAVE [SIN][SQR][TRI][SAW][NOI]                   |  74..94
 *  | FREQ [50 ][100][200 ][500 ]                      |  98..118
 *  |      [1K ][2K ][5K  ][10K ]                      | 121..141
 *  | CYC  [1][2][3][4][5]                             | 145..165
 *  | DIR  [R>L][L>R]                                  | 169..189
 *  |        PA4  SINE  1KHz  x2 R>L                   | 195
 *  | K0 BACK / TAP TO SELECT                          | 214
 *  +--------------------------------------------------+
 *****************************************************************************/
#include "dacapp.h"
#include "dac.h"
#include "ui.h"
#include "lcd.h"
#include "touch.h"
#include "key.h"
#include "delay.h"
#include <stdio.h>

/*-------------------- 配色（沿用桌面的日落暖色） ----*/
#define DA_BG       0x20A1      /* 页面底：深咖           */
#define DA_BAR      0x30E3      /* 标题栏                 */
#define DA_BOX      0x3923      /* 坐标框底：比页面亮一点 */
#define DA_GRID     0x59A4      /* 网格与边框             */
#define DA_WAVE1    0xF508      /* PA4 曲线：亮橙         */
#define DA_WAVE2    0x07FF      /* PA5 曲线：青，和橙色区分明显 */
#define DA_TEXT     0xFF9B      /* 主文字：米白           */
#define DA_DIM      0xB4D0      /* 次要文字：暖灰         */
#define DA_ON       0x6CC5      /* ON：橄榄绿             */
#define DA_OFF      0xE225      /* OFF：砖红              */
#define DA_BTN      0xA285      /* 未选中按钮             */
#define DA_SEL      0xE4C7      /* 选中按钮：琥珀         */
#define DA_INK      0x20A2      /* 选中按钮上的深色字     */

/*-------------------- 输出界面布局 ----------------*/
#define SET_X       4
#define SET_Y       3
#define SET_W       54
#define SET_H       20

#define HOME_X      262
#define HOME_Y      3
#define HOME_W      54
#define HOME_H      20

#define BOX_X       12
#define BOX_W       296
#define BOX_H       64

#define LBL1_Y      26
#define BOX1_Y      40
#define LBL2_Y      108
#define BOX2_Y      122

#define TRACE_PAD   3           /* 曲线离框内边的留白     */

/* 框内可绘区（绝对坐标） */
#define TRX_X       (BOX_X + TRACE_PAD)
#define TR_W        (BOX_W - TRACE_PAD * 2)
#define TR_H        (BOX_H - TRACE_PAD * 2)

#define TIP1_Y      192
#define TIP2_Y      210

/*-------------------- 设置界面布局 ----------------*/
#define ST_BACK_X   4
#define ST_BACK_Y   3
#define ST_BACK_W   54
#define ST_BACK_H   20

#define ST_EDIT_Y   26
#define ST_EDIT_H   20
#define ST_EDIT_W   54
#define ST_EDIT_X0  44
#define ST_EDIT_DX  60

#define ST_PIN_Y    50
#define ST_PIN_H    20
#define ST_PIN_W    60
#define ST_PIN_X0   44
#define ST_PIN_DX   64

#define ST_WAVE_Y   74
#define ST_WAVE_H   20
#define ST_WAVE_W   48
#define ST_WAVE_X0  44
#define ST_WAVE_DX  52

#define ST_FREQ_Y1  98
#define ST_FREQ_Y2  121
#define ST_FREQ_H   20
#define ST_FREQ_W   60
#define ST_FREQ_X0  44
#define ST_FREQ_DX  64

#define ST_CYC_Y    145
#define ST_CYC_H    20
#define ST_CYC_W    40
#define ST_CYC_X0   44
#define ST_CYC_DX   44
#define ST_CYC_MAX  5

#define ST_DIR_Y    169
#define ST_DIR_H    20
#define ST_DIR_W    100
#define ST_DIR_X0   44
#define ST_DIR_DX   108

#define ST_SUM_Y    195
#define ST_TIP_Y    214

static const char *st_edit_lbl[2] = { "PA4", "PA5" };
static const char *st_pin_lbl[4]  = { "PA4", "PA5", "BOTH", "OFF" };
static const char *st_wave_lbl[5] = { "SIN", "SQR", "TRI", "SAW", "NOI" };
static const char *st_cyc_lbl[5]  = { "1", "2", "3", "4", "5" };
static const char *st_dir_lbl[2]  = { "R>L", "L>R" };

/* 按钮从左到右是 PA4/PA5/BOTH/OFF，和 DOUT_xxx 的编号顺序不一致
 * （DOUT_NONE=0、DOUT_CH1=1、DOUT_CH2=2、DOUT_BOTH=3），
 * 所以按下去要经过这张表换算，不能直接把下标丢给 DAC_SetOutput。 */
static const u8 st_pin_sel[4] = { DOUT_CH1, DOUT_CH2, DOUT_BOTH, DOUT_NONE };

/*-------------------- 按通道分开的显示参数 --------*/
/* 下标 0 = PA4，1 = PA5。周期数和滚动方向两路独立。 */
static u8  disp_cycle[2] = { 2, 2 };    /* 显示几个周期：1..ST_CYC_MAX */
static u8  disp_dir[2]   = { 0, 0 };    /* 0 = 右->左（默认）, 1 = 左->右 */
static u32 disp_ph[2]    = { 0, 0 };    /* 当前相位偏移，Q8 定点        */

static u8  edit_ch       = 0;           /* 设置页正在编辑哪一路          */

static const char *cyc_name(u8 n)
{
    /* C89：static 变量必须声明在函数最前面 */
    static char s[3];

    if (n < 1) n = 1;
    if (n > ST_CYC_MAX) n = ST_CYC_MAX;

    /* "x1" .. "x5" */
    s[0] = 'x';
    s[1] = (char)('0' + n);
    s[2] = 0;

    return s;
}

/*=============================================================================
 *  输出界面
 *===========================================================================*/

/* 框内中线虚线。终点是 306，循环变量必须是 int：
 * 以前写成 u8 会在 254 溢出回 4，永远到不了终点 = 死循环。 */
static void draw_midline(int by)
{
    int i;

    for (i = BOX_X + 2; i < BOX_X + BOX_W - 2; i += 6)
        UI_Fill(i, by + BOX_H / 2, 3, 1, DA_GRID);
}

/* 关断时画一条贴中线的暗色虚线，表示 0 V 平线。
 * 刻意不用波形色，否则看着像还在输出。 */
static void draw_off_line(int by)
{
    u8 i;

    for (i = 0; i < 8; i++)
        UI_Fill(TRX_X + (TR_W * i) / 8, by + BOX_H / 2, TR_W / 16, 1, DA_GRID);
}

/* 逐像素相位采样画曲线（ch：0 = PA4，1 = PA5） */
static void draw_trace(u8 ch)
{
    const u16 *tab = DAC_Table();
    int  by    = (ch == 0) ? BOX1_Y : BOX2_Y;
    int  y     = by + TRACE_PAD;
    int  h     = TR_H;
    u16  color = (ch == 0) ? DA_WAVE1 : DA_WAVE2;
    u8   n     = disp_cycle[ch];
    u32  total;
    u32  ph;
    int  px;
    int  cur;
    int  prev = 0;
    u8   first = 1;

    if (n < 1) n = 1;
    if (n > ST_CYC_MAX) n = ST_CYC_MAX;

    /* 一屏要扫过的总相位 = 周期数 x 每周期点数，Q8 定点 */
    total = (u32)n * (u32)DAC_TAB_LEN * 256u;

    ph = disp_ph[ch] % total;

    POINT_COLOR = color;

    for (px = 0; px < TR_W; px++)
    {
        /* 每列现算相位，不用累加步长：
         * 累加的话 step = total/(TR_W-1) 是整除截断的，
         * 5 个周期时尾巴会差大半个采样点，右边缘接不上。
         * 现算还能保证 px = TR_W-1 时精确等于 ph + total，
         * 取模回 ph，首尾严丝合缝。
         * px*total 最大 289 x 81920，u32 够用。 */
        u32 p = ph + ((u32)px * total) / (u32)(TR_W - 1);
        u16 ip;
        u16 f;
        u16 i0, i1;
        u32 v;

        if (p >= total) p %= total;

        ip = (u16)(p >> 8);             /* 落在表里第几个点   */
        f  = (u16)(p & 0xFF);           /* Q8 小数部分        */

        i0 = (u16)(ip % DAC_TAB_LEN);
        i1 = (u16)((i0 + 1) % DAC_TAB_LEN);

        /* 线性插值：相邻两点按小数部分加权，正弦才不会一格一格的 */
        v = (((u32)tab[i0] * (256u - f)) + ((u32)tab[i1] * f)) >> 8;

        cur = y + h - 1 - (int)((v * (u32)(h - 1)) / 4095u);

        if (first)
        {
            UI_Fill(TRX_X + px, cur, 1, 1, color);
            first = 0;
        }
        else if (cur >= prev)
        {
            /* 和上一列之间用竖线填满，方波跳变处才不会断 */
            UI_Fill(TRX_X + px, prev, 1, cur - prev + 1, color);
        }
        else
        {
            UI_Fill(TRX_X + px, cur, 1, prev - cur + 1, color);
        }

        prev = cur;
    }
}

/* 清掉框内曲线区，只填回底色（边框和网格线在外面，不受影响） */
static void clear_trace(u8 ch)
{
    int by = (ch == 0) ? BOX1_Y : BOX2_Y;

    UI_Fill(TRX_X, by + TRACE_PAD, TR_W, TR_H, DA_BOX);
    draw_midline(by);
}

/* 一个通道：标签行 + 坐标框 + 曲线 */
static void draw_ch(u8 ch)
{
    int by = (ch == 0) ? BOX1_Y : BOX2_Y;
    int ly = (ch == 0) ? LBL1_Y : LBL2_Y;
    u8  on = (ch == 0) ? DAC_Ch1IsOn() : DAC_Ch2IsOn();
    u16 wc = (ch == 0) ? DA_WAVE1 : DA_WAVE2;

    /* 标签行 */
    UI_Fill(BOX_X, ly, BOX_W, 14, DA_BG);
    UI_Text(BOX_X + 2, ly + 1, 12, (char *)((ch == 0) ? "PA4" : "PA5"), wc, DA_BG);
    UI_Text(BOX_X + 30, ly + 1, 12, (char *)((ch == 0) ? "DAC1" : "DAC2"), DA_DIM, DA_BG);
    UI_Text(BOX_X + 80, ly + 1, 12, (char *)DAC_WaveName(DAC_GetWave()), DA_TEXT, DA_BG);

    /* 本路自己的显示参数：几个周期 + 往哪边滚 */
    UI_Text(BOX_X + 124, ly + 1, 12, (char *)cyc_name(disp_cycle[ch]), DA_DIM, DA_BG);
    UI_Text(BOX_X + 148, ly + 1, 12, (char *)st_dir_lbl[disp_dir[ch] & 1], DA_DIM, DA_BG);

    UI_Text(BOX_X + 178, ly + 1, 12, (char *)DAC_FreqName(DAC_GetFreq()), DA_TEXT, DA_BG);
    UI_Text(BOX_X + 202, ly + 1, 12, (char *)"Hz", DA_DIM, DA_BG);
    UI_Text(BOX_X + 252, ly + 1, 12, (char *)(on ? "ON" : "OFF"),
            on ? DA_ON : DA_OFF, DA_BG);

    /* 坐标框 */
    UI_Fill(BOX_X, by, BOX_W, BOX_H, DA_BOX);

    POINT_COLOR = DA_GRID;
    LCD_DrawRectangle((u16)BOX_X, (u16)by,
                      (u16)(BOX_X + BOX_W - 1), (u16)(by + BOX_H - 1));

    draw_midline(by);

    if (on) draw_trace(ch);
    else    draw_off_line(by);
}

static void draw_title(void)
{
    UI_Fill(0, 0, ui_sw, 24, DA_BAR);
    UI_Center(6, 12, (char *)"DAC WAVEFORM", DA_TEXT, DA_BAR);

    UI_Btn(SET_X,  SET_Y,  SET_W,  SET_H,  "SET",  DA_BTN, DA_TEXT, 12);
    UI_Btn(HOME_X, HOME_Y, HOME_W, HOME_H, "HOME", DA_BTN, DA_TEXT, 12);
}

static void draw_main(void)
{
    UI_Fill(0, 0, ui_sw, ui_sh, DA_BG);

    draw_title();
    draw_ch(0);
    draw_ch(1);

    UI_Center(TIP1_Y, 12, (char *)"K1 PA4   K2 PA5   WK BOTH", DA_DIM, DA_BG);
    UI_Center(TIP2_Y, 12, (char *)"K0 HOME   TAP SET FOR OPTIONS", DA_DIM, DA_BG);
}

/* 每帧推一次相位并重绘正在输出的那几路 */
static void anim_step(void)
{
    u8 ch;

    for (ch = 0; ch < 2; ch++)
    {
        u8  on = (ch == 0) ? DAC_Ch1IsOn() : DAC_Ch2IsOn();
        u8  n  = disp_cycle[ch];
        u32 total;
        u32 d;

        if (!on) continue;

        if (n < 1) n = 1;
        if (n > ST_CYC_MAX) n = ST_CYC_MAX;

        total = (u32)n * (u32)DAC_TAB_LEN * 256u;

        /* 每帧走 2 个像素对应的相位，帧间隔 20 ms -> 约 70 px/s */
        d = (total * 2u) / (u32)TR_W;
        if (d == 0) d = 1;

        if (disp_dir[ch] == 0)          /* 右 -> 左：相位递增 */
        {
            disp_ph[ch] = (disp_ph[ch] + d) % total;
        }
        else                            /* 左 -> 右：相位递减 */
        {
            disp_ph[ch] = (disp_ph[ch] + total - d) % total;
        }
    }

    for (ch = 0; ch < 2; ch++)
    {
        u8 on = (ch == 0) ? DAC_Ch1IsOn() : DAC_Ch2IsOn();

        if (!on) continue;

        clear_trace(ch);
        draw_trace(ch);
    }
}

/*=============================================================================
 *  设置界面
 *===========================================================================*/

static void draw_edit_row(void)
{
    u8 i;
    u8 hl;

    for (i = 0; i < 2; i++)
    {
        hl = (edit_ch == i) ? 1 : 0;

        UI_Btn(ST_EDIT_X0 + i * ST_EDIT_DX, ST_EDIT_Y, ST_EDIT_W, ST_EDIT_H,
               (char *)st_edit_lbl[i],
               hl ? DA_SEL : DA_BTN,
               hl ? DA_INK : DA_TEXT, 12);
    }
}

static void draw_pin_row(void)
{
    u8 i;
    u8 sel = DAC_GetOutput();
    u8 hl;

    for (i = 0; i < 4; i++)
    {
        hl = (st_pin_sel[i] == sel) ? 1 : 0;

        UI_Btn(ST_PIN_X0 + i * ST_PIN_DX, ST_PIN_Y, ST_PIN_W, ST_PIN_H,
               (char *)st_pin_lbl[i],
               hl ? DA_SEL : DA_BTN,
               hl ? DA_INK : DA_TEXT, 12);
    }
}

static void draw_wave_row(void)
{
    u8 i;
    u8 sel = DAC_GetWave();
    u8 hl;

    for (i = 0; i < DW_COUNT; i++)
    {
        hl = (i == sel) ? 1 : 0;

        UI_Btn(ST_WAVE_X0 + i * ST_WAVE_DX, ST_WAVE_Y, ST_WAVE_W, ST_WAVE_H,
               (char *)st_wave_lbl[i],
               hl ? DA_SEL : DA_BTN,
               hl ? DA_INK : DA_TEXT, 12);
    }
}

static void draw_freq_row(void)
{
    u8 i;
    u8 sel = DAC_GetFreq();
    u8 hl;

    for (i = 0; i < DAC_FREQ_LEVELS; i++)
    {
        u16 bx = (u16)(ST_FREQ_X0 + (i % 4) * ST_FREQ_DX);
        u16 by = (u16)((i < 4) ? ST_FREQ_Y1 : ST_FREQ_Y2);

        hl = (i == sel) ? 1 : 0;

        UI_Btn(bx, by, ST_FREQ_W, ST_FREQ_H,
               (char *)DAC_FreqName(i),
               hl ? DA_SEL : DA_BTN,
               hl ? DA_INK : DA_TEXT, 12);
    }
}

/* 周期数：只反映当前 edit_ch 那一路的值 */
static void draw_cyc_row(void)
{
    u8 i;
    u8 hl;

    for (i = 0; i < ST_CYC_MAX; i++)
    {
        hl = (disp_cycle[edit_ch] == (u8)(i + 1)) ? 1 : 0;

        UI_Btn(ST_CYC_X0 + i * ST_CYC_DX, ST_CYC_Y, ST_CYC_W, ST_CYC_H,
               (char *)st_cyc_lbl[i],
               hl ? DA_SEL : DA_BTN,
               hl ? DA_INK : DA_TEXT, 12);
    }
}

static void draw_dir_row(void)
{
    u8 i;
    u8 hl;

    for (i = 0; i < 2; i++)
    {
        hl = (disp_dir[edit_ch] == i) ? 1 : 0;

        UI_Btn(ST_DIR_X0 + i * ST_DIR_DX, ST_DIR_Y, ST_DIR_W, ST_DIR_H,
               (char *)st_dir_lbl[i],
               hl ? DA_SEL : DA_BTN,
               hl ? DA_INK : DA_TEXT, 12);
    }
}

static void draw_summary(void)
{
    char buf[32];
    const char *p;

    switch (DAC_GetOutput())
    {
    case DOUT_CH1:  p = "PA4";     break;
    case DOUT_CH2:  p = "PA5";     break;
    case DOUT_BOTH: p = "PA4+PA5"; break;
    default:        p = "NONE";    break;
    }

    sprintf(buf, "%s %s %s Hz x%u %s", p,
            DAC_WaveName(DAC_GetWave()), DAC_FreqName(DAC_GetFreq()),
            (unsigned)disp_cycle[edit_ch],
            st_dir_lbl[disp_dir[edit_ch] & 1]);

    UI_Fill(0, ST_SUM_Y, ui_sw, 14, DA_BG);
    UI_Center(ST_SUM_Y + 1, 12, buf, DA_TEXT, DA_BG);
}

static void draw_settings(void)
{
    UI_Fill(0, 0, ui_sw, ui_sh, DA_BG);

    UI_Fill(0, 0, ui_sw, 24, DA_BAR);
    UI_Center(6, 12, (char *)"DAC SETTINGS", DA_TEXT, DA_BAR);
    UI_Btn(ST_BACK_X, ST_BACK_Y, ST_BACK_W, ST_BACK_H, "BACK", DA_BTN, DA_TEXT, 12);

    /* 行标签 */
    UI_Text(6, ST_EDIT_Y + 5,  12, (char *)"EDIT", DA_DIM, DA_BG);
    UI_Text(6, ST_PIN_Y + 5,   12, (char *)"PIN",  DA_DIM, DA_BG);
    UI_Text(6, ST_WAVE_Y + 5,  12, (char *)"WAVE", DA_DIM, DA_BG);
    UI_Text(6, ST_FREQ_Y1 + 5, 12, (char *)"FREQ", DA_DIM, DA_BG);
    UI_Text(6, ST_CYC_Y + 5,   12, (char *)"CYC",  DA_DIM, DA_BG);
    UI_Text(6, ST_DIR_Y + 5,   12, (char *)"DIR",  DA_DIM, DA_BG);

    draw_edit_row();
    draw_pin_row();
    draw_wave_row();
    draw_freq_row();
    draw_cyc_row();
    draw_dir_row();
    draw_summary();

    UI_Center(ST_TIP_Y, 12, (char *)"K0 BACK   TAP A BOX TO SELECT", DA_DIM, DA_BG);
}

/* 返回：0 = 继续留在设置页，1 = 退出 */
static u8 settings_click(void)
{
    u8 i;

    /* 左上角 BACK */
    if (UI_Hit(ui_tx, ui_ty, ST_BACK_X, ST_BACK_Y, ST_BACK_W, ST_BACK_H))
        return 1;

    /* EDIT：切换正在编辑的通道，下面 CYC/DIR 跟着刷新 */
    for (i = 0; i < 2; i++)
    {
        if (UI_Hit(ui_tx, ui_ty,
                   (u16)(ST_EDIT_X0 + i * ST_EDIT_DX), ST_EDIT_Y, ST_EDIT_W, ST_EDIT_H))
        {
            edit_ch = i;
            draw_edit_row();
            draw_cyc_row();
            draw_dir_row();
            draw_summary();
            return 0;
        }
    }

    for (i = 0; i < 4; i++)
    {
        if (UI_Hit(ui_tx, ui_ty,
                   (u16)(ST_PIN_X0 + i * ST_PIN_DX), ST_PIN_Y, ST_PIN_W, ST_PIN_H))
        {
            DAC_SetOutput(st_pin_sel[i]);
            draw_pin_row();
            draw_summary();
            return 0;
        }
    }

    for (i = 0; i < DW_COUNT; i++)
    {
        if (UI_Hit(ui_tx, ui_ty,
                   (u16)(ST_WAVE_X0 + i * ST_WAVE_DX), ST_WAVE_Y, ST_WAVE_W, ST_WAVE_H))
        {
            DAC_SetWave(i);
            draw_wave_row();
            draw_summary();
            return 0;
        }
    }

    for (i = 0; i < DAC_FREQ_LEVELS; i++)
    {
        if (UI_Hit(ui_tx, ui_ty,
                   (u16)(ST_FREQ_X0 + (i % 4) * ST_FREQ_DX),
                   (u16)((i < 4) ? ST_FREQ_Y1 : ST_FREQ_Y2),
                   ST_FREQ_W, ST_FREQ_H))
        {
            DAC_SetFreq(i);
            draw_freq_row();
            draw_summary();
            return 0;
        }
    }

    /* CYC：只改当前 edit_ch 那一路 */
    for (i = 0; i < ST_CYC_MAX; i++)
    {
        if (UI_Hit(ui_tx, ui_ty,
                   (u16)(ST_CYC_X0 + i * ST_CYC_DX), ST_CYC_Y, ST_CYC_W, ST_CYC_H))
        {
            disp_cycle[edit_ch] = (u8)(i + 1);
            draw_cyc_row();
            draw_summary();
            return 0;
        }
    }

    /* DIR：只改当前 edit_ch 那一路 */
    for (i = 0; i < 2; i++)
    {
        if (UI_Hit(ui_tx, ui_ty,
                   (u16)(ST_DIR_X0 + i * ST_DIR_DX), ST_DIR_Y, ST_DIR_W, ST_DIR_H))
        {
            disp_dir[edit_ch] = i;
            draw_dir_row();
            draw_summary();
            return 0;
        }
    }

    return 0;
}

static void dac_settings(void)
{
    u8 ev;

    draw_settings();
    UI_Wait_Release();

    for (;;)
    {
        ev = UI_Scan();

        if (ev & UI_K0) break;                  /* 返回上一页 */

        if (ev & UI_TOUCH)
        {
            if (settings_click()) break;
        }

        delay_ms(20);
    }

    UI_Wait_Release();
}

/*=============================================================================
 *  入口
 *===========================================================================*/

void DacApp_Run(void)
{
    u8 ev;

    DAC_WaveInit();                 /* 内部有防重入标志，调多次只初始化一次 */
    UI_Init();
    UI_Wait_Release();

    draw_main();

    for (;;)
    {
        ev = UI_Scan();

        if (ev & UI_K0) break;                  /* 回主界面 */

        if (ev & UI_K1)
        {
            DAC_Ch1On((u8)(DAC_Ch1IsOn() ? 0 : 1));
            draw_ch(0);
        }

        if (ev & UI_K2)
        {
            DAC_Ch2On((u8)(DAC_Ch2IsOn() ? 0 : 1));
            draw_ch(1);
        }

        if (ev & UI_WKUP)
        {
            DAC_ToggleBoth();
            draw_ch(0);
            draw_ch(1);
        }

        if (ev & UI_TOUCH)
        {
            if (UI_Hit(ui_tx, ui_ty, SET_X, SET_Y, SET_W, SET_H))
            {
                UI_Wait_Release();
                dac_settings();
                draw_main();
                continue;
            }
            else if (UI_Hit(ui_tx, ui_ty, HOME_X, HOME_Y, HOME_W, HOME_H))
            {
                break;
            }
        }

        anim_step();
        delay_ms(20);
    }

    /* 退出 APP 就把两路停掉：主界面上没有输出状态的提示，
     * 引脚一直在动反而容易让人以为坏了。设置本身保留，
     * 下次进来还是上次选的波形和频率。 */
    DAC_StopAll();
    UI_Wait_Release();
}
