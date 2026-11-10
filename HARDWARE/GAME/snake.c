/*=============================================================================
 *  SNAKE - 经典网格贪吃蛇
 *  目标平台 : ALIENTEK WarShip STM32F103ZET6 + ILI9341 TFT-LCD (FSMC)
 *
 *  操作说明
 *    KEY2 (PE2, 接 GND, 低电平有效) : 左转  (逆时针)
 *    KEY1 (PE3, 接 GND, 低电平有效) : 右转  (顺时针)
 *    KEY0 (PE4, 接 GND, 低电平有效) : 返回游戏菜单 / 主界面
 *    WK_UP (PA0, 接 VCC, 高电平)    : 暂停 / 继续
 *    触摸                           : 点击棋盘左半 / 右半区域
 *                                     转向，以及 BACK / PAUSE 按钮
 *
 *  采用相对转向（转向而不是绝对方向），是因为
 *  只有四个按键，而 K0 必须保持“返回上一级”
 *  （菜单里回桌面、关卡里回菜单，与另外两个游戏一致）。
 *  棋盘为 20 x 12 格，每格 16 x 16 像素，因此不需要缩放：
 *  20 * 16 = 320，12 * 16 = 192。
 *===========================================================================*/

#include "snake.h"
#include "game.h"
#include "menu.h"
#include "ui.h"
#include "lcd.h"
#include "key.h"
#include "delay.h"
#include "touch.h"
#include "24cxx.h"

/*-------------------- 棋盘 ---------------------*/
#define COLS            20
#define CELL            16
#define ROWS            12
#define FY              28      /* 棋盘顶部，位于 HUD 下方      */

#define SMAX            130     /* 能放进 RAM 的最长蛇身   */

/*-------------------- 颜色 ---------------------*/
#define C_BG            BLACK
#define C_FIELD         0x0861  /* 深蓝灰色棋盘                 */
#define C_LINE          LGRAY
#define C_BODY          GREEN
#define C_HEAD          YELLOW
#define C_FOOD          RED
#define C_HUD           BLACK

/*-------------------- EEPROM -------------------*/
#define EE_BEST_SNAKE   66      /* u16 大端存放，占用 66/67             */

/*-------------------- 模块数据 ---------------*/
static u8  sx[SMAX];
static u8  sy[SMAX];
static u8  slen;

static u8  sdir;                /* 0 右, 1 下, 2 左, 3 上 */
static u8  turn_used;
static u8  fx;
static u8  fy;

static u16 score;
static u16 best;
static u16 step_ms;

static u16 rnd_state = 0x2B71;

/* 每种难度的每步时间                      简单 困难 地狱 */
static const u16 step_tab[3] = {170, 130, 100};

static const s8 dx[4] = {1, 0, -1, 0};
static const s8 dy[4] = {0, 1, 0, -1};

/*-------------------- 随机数 -------------------*/
static u16 rnd(void)
{
    rnd_state = (u16)(rnd_state * 1103 + 12345);

    return (u16)(rnd_state >> 4);
}

/*-------------------- EEPROM -------------------*/
static u16 load_best(void)
{
    u16 v = (u16)(((u16)AT24CXX_ReadOneByte(EE_BEST_SNAKE) << 8)
                  | (u16)AT24CXX_ReadOneByte(EE_BEST_SNAKE + 1));

    return (v > 9999) ? 0 : v;
}

static void save_best(void)
{
    AT24CXX_WriteOneByte(EE_BEST_SNAKE,     (u8)(best >> 8));
    AT24CXX_WriteOneByte(EE_BEST_SNAKE + 1, (u8)(best & 0xFF));
}

/*-------------------- 绘制 ---------------------*/
static void paint(u8 cx, u8 cy, u16 color)
{
    UI_Fill((int)cx * CELL + 1, FY + (int)cy * CELL + 1,
            CELL - 2, CELL - 2, color);
}

static void draw_hud(void)
{
    /* 停在棋盘边框上方一个像素（边框线位于 y = FY-1），
     * 否则这次填充会在游戏过程中把顶边擦掉     */
    UI_Fill(118, 0, (int)ui_sw - 118, FY - 2, C_HUD);

    UI_Text(120, 5, 12, (char *)"SCORE", WHITE, C_HUD);
    POINT_COLOR = WHITE;
    BACK_COLOR  = C_HUD;
    LCD_ShowNum(160, 5, score, 4, 12);

    UI_Text(220, 5, 12, (char *)"BEST", WHITE, C_HUD);
    LCD_ShowNum(258, 5, best, 4, 12);
}

static void draw_board(void)
{
    u8 i;

    UI_Fill(0, FY, ui_sw, ROWS * CELL, C_FIELD);

    POINT_COLOR = C_LINE;
    LCD_DrawRectangle(0, (u16)(FY - 1), (u16)(ui_sw - 1), (u16)(FY + ROWS * CELL - 1));

    for (i = 0; i < slen; i++)
    {
        paint(sx[i], sy[i], (i == 0) ? C_HEAD : C_BODY);
    }

    paint(fx, fy, C_FOOD);

    /* 棋盘下方的按键提示 */
    UI_Text(34, 224, 12, (char *)"K2 TURN L   K1 TURN R   TAP: TURN",
            WHITE, C_BG);
}

static void draw_scene(void)
{
    UI_Fill(0, 0, ui_sw, ui_sh, C_BG);

    UI_TopButtons();
    draw_hud();
    draw_board();
}

/*-------------------- 逻辑 ---------------------*/
static void place_food(void)
{
    u8 i;
    u8 clash;

    for (;;)
    {
        fx = (u8)(rnd() % COLS);
        fy = (u8)(rnd() % ROWS);

        clash = 0;

        for (i = 0; i < slen; i++)
        {
            if (sx[i] == fx && sy[i] == fy) { clash = 1; break; }
        }

        if (!clash) return;
    }
}

static void reset_run(void)
{
    u8 d = Dino_Get_Diff();

    slen = 3;
    sdir = 0;
    turn_used = 0;

    sx[0] = 6;  sy[0] = 6;
    sx[1] = 5;  sy[1] = 6;
    sx[2] = 4;  sy[2] = 6;

    score   = 0;
    step_ms = step_tab[d];

    place_food();
}

static void turn(s8 d)
{
    /* 每个格子步长只允许转向一次：同一步内第二次点击会让
     * 蛇直接反向撞进自己的脖子                            */
    if (turn_used) return;

    turn_used = 1;
    sdir = (u8)((sdir + 4 + d) & 3);
}

/* 蛇死亡时返回 1 */
static u8 step_snake(void)
{
    int nx = (int)sx[0] + dx[sdir];
    int ny = (int)sy[0] + dy[sdir];
    u8  ox;
    u8  oy;
    u8  grow = 0;
    u8  i;

    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) return 1;

    for (i = 0; i < slen; i++)
    {
        if (sx[i] == (u8)nx && sy[i] == (u8)ny) return 1;
    }

    ox = sx[slen - 1];
    oy = sy[slen - 1];

    if (nx == fx && ny == fy) grow = 1;

    if (grow && slen < SMAX) slen++;

    for (i = (u8)(slen - 1); i > 0; i--)
    {
        sx[i] = sx[i - 1];
        sy[i] = sy[i - 1];
    }

    sx[0] = (u8)nx;
    sy[0] = (u8)ny;

    if (!grow) paint(ox, oy, C_FIELD);

    paint(sx[1], sy[1], C_BODY);
    paint(sx[0], sy[0], C_HEAD);

    if (grow)
    {
        score++;
        if (score > best) best = score;

        draw_hud();

        place_food();
        paint(fx, fy, C_FOOD);

        if ((score % 3) == 0 && step_ms > 70) step_ms -= 4;
    }

    return 0;
}

/*-------------------- 暂停 ---------------------*/
static void pause_run(void)
{
    u8 ev;

    UI_Panel(88, 68, LGRAY);
    UI_Center(100, 16, (char *)"PAUSED", BLACK, LGRAY);
    UI_Center(126, 12, (char *)"TAP OR PRESS ANY KEY", BLACK, LGRAY);

    for (;;)
    {
        ev = UI_Scan();

        if (ev != 0) break;

        delay_ms(20);
    }

    UI_Wait_Release();
    draw_scene();
}

/*-------------------- 游戏结束 ----------------*/
/* 返回 1 回到游戏菜单，返回 0 再玩一局 */
static u8 show_over(void)
{
    u8 ev;

    if (score >= best) save_best();

    UI_Panel(50, 140, LGRAY);
    UI_Center(62, 16, (char *)"GAME OVER", BLACK, LGRAY);

    UI_Text(96, 92, 12, (char *)"SCORE", BLACK, LGRAY);
    POINT_COLOR = BLACK;
    BACK_COLOR  = LGRAY;
    LCD_ShowNum(150, 92, score, 4, 12);

    UI_Text(96, 114, 12, (char *)"BEST ", BLACK, LGRAY);
    LCD_ShowNum(150, 114, best, 4, 12);

    UI_Center(138, 12, (char *)"TAP OR K2: RETRY", BLACK, LGRAY);
    UI_Center(158, 12, (char *)"K0 / BACK: MENU", BLACK, LGRAY);

    for (;;)
    {
        ev = UI_Scan();

        if (ev & UI_K0) return 1;

        if (ev & UI_K2) return 0;

        if (ev & UI_TOUCH)
        {
            if (UI_Back_Hit()) return 1;

            return 0;
        }

        delay_ms(20);
    }
}

/*-------------------- 游戏菜单 ----------------*/
static void deco_snake(void)
{
    u8 i;

    for (i = 0; i < 4; i++)
    {
        UI_Fill(28 + i * 15, 202, 13, 13, (i == 0) ? YELLOW : GREEN);
    }

    UI_Fill(272, 202, 13, 13, RED);
}

void Snake_Menu(void)
{
    u8 act;

    UI_Init();

    for (;;)
    {
        best = load_best();

        /* 最后一个参数 1 = K0 回桌面 / K2 开始，与关卡内一致 */
        act = UI_HomePage((char *)"SNAKE", best, Dino_Get_Diff(),
                          WHITE, BROWN, BLACK, deco_snake, 1);

        switch (act)
        {
        case UI_MENU_START:
            UI_Wait_Release();
            Snake_Game();
            break;

        case UI_MENU_SET:
            Game_Settings_Page();
            break;

        case UI_MENU_INFO:
            Dino_Info_Page();
            break;

        default:
            return;
        }
    }
}

/*-------------------- 游戏过程 ----------------*/
void Snake_Game(void)
{
    u8  ev;
    u16 waited;

    UI_Init();
    best = load_best();

    for (;;)
    {
        reset_run();
        draw_scene();

        for (;;)
        {
            waited = 0;
            turn_used = 0;

                        /* 等待下一个格子步长，同时轮询按键和触摸 */
            while (waited < step_ms)
            {
                ev = UI_Scan();

                if (ev & UI_K0) return;

                if (ev & UI_TOUCH)
                {
                    if (UI_Back_Hit())  return;
                    if (UI_Pause_Hit()) { pause_run(); continue; }

                    turn((ui_tx < ui_sw / 2) ? (s8)-1 : (s8)1);
                }

                if (ev & UI_K2) turn(-1);      /* 原 KEY0，K0 让给返回 */
                if (ev & UI_K1) turn(1);

                if (ev & UI_WKUP) { pause_run(); continue; }

                delay_ms(5);
                waited = (u16)(waited + 5);
            }

            if (step_snake()) break;
        }

        if (show_over()) return;
    }
}
