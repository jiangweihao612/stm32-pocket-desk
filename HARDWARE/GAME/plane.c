/*=============================================================================
 *  PLANE WAR - 纵向卷轴射击
 *  目标平台 : ALIENTEK WarShip STM32F103ZET6 + ILI9341 TFT-LCD (FSMC)
 *
 *  操作说明
 *    KEY2 / KEY1 (PE2 / PE3, 接 GND, 低电平有效) : 左移 / 右移
 *    KEY0        (PE4)                          : 返回游戏菜单
 *    WK_UP       (PA0, 接 VCC, 高电平有效)       : 暂停 / 继续
 *    触摸                                       : 点击任意位置操纵飞机，
 *                                                 以及 BACK / PAUSE 按钮
 *
 *  注意：K0 在整个应用里都是"返回上一级"（菜单里回桌面、关卡里回菜单），
 *  K2 是"开始 / 左移"这类正向动作键，和 DINO RUN 的约定一致。
 *
 *  机枪自动开火，玩家只负责操纵方向。三条命，
 *  每击落一架敌机得一分，最高分保存在 AT24C02 EEPROM 中。
 *  所有图形都用裁剪矩形绘制，不需要位图。
 *===========================================================================*/

#include "plane.h"
#include "game.h"
#include "menu.h"
#include "ui.h"
#include "lcd.h"
#include "key.h"
#include "delay.h"
#include "touch.h"
#include "24cxx.h"

/*-------------------- 参数调整 -------------------*/
#define EE_BEST_PLANE   64      /* u16 大端存放，占用 64/65            */
#define FRAME_MS        18      /* 约 55 帧/秒                         */

#define PW              24      /* 玩家飞机尺寸                        */
#define PH              26
#define PY              198     /* 玩家飞机顶部 y 坐标                 */
#define FIELD_TOP       30      /* 位于 HUD 条下方                     */

#define MAX_SHOT        6
#define SHOT_SPEED      9
#define FIRE_GAP        9       /* 相邻两发子弹间隔的帧数              */

#define MAX_FOE         4
#define FW              20      /* 敌机尺寸                            */
#define FH              16

#define MAX_STAR        32

/*-------------------- 颜色 ---------------------*/
#define C_BG            BLACK
#define C_STAR          WHITE
#define C_STAR2         LGRAY
#define C_BODY          WHITE
#define C_COCK          BLUE
#define C_ENEMY         RED
#define C_DOME          CYAN
#define C_SHOT          YELLOW
#define C_LIFE          RED

/*-------------------- 难度 --------------------*/
/*                 简单 困难 地狱 */
static const u8 foe_speed_tab[3] = {2, 3, 4};
static const u8 foe_gap_tab[3]   = {46, 34, 26};

/*-------------------- 模块数据 ---------------*/
typedef struct
{
    int x;
    int y;
    u8  used;
} Shot;

typedef struct
{
    int x;
    int y;
    u8  used;
    int dir;            /* -1 / +1，左右小幅摆动 */
} Foe;

static Shot shots[MAX_SHOT];
static Foe  foes[MAX_FOE];

static int  plx = 148;          /* 玩家左边界 */
static int  ptarget = -1;       /* 触摸目标，-1 = 无 */

static u8   lives;
static u8   invuln;
static u8   fire_cnt;
static u8   spawn_cnt;
static u8   spawn_gap;
static u8   speed_now;

static u16  score;
static u16  best;

static u16  rnd_state = 0x1F3D;

static u16  star_x[MAX_STAR];
static u16  star_y[MAX_STAR];
static u8   star_c[MAX_STAR];

/* 爆炸闪光，下一帧会被擦掉 */
static int  flash_x;
static int  flash_y;
static u8   flash_t;

/*-------------------- 随机数 ---------------------*/
static u16 rnd(void)
{
    rnd_state = (u16)(rnd_state * 1103 + 12345);

    return (u16)(rnd_state >> 4);
}

/*-------------------- EEPROM --------------------*/
static u16 load_best(void)
{
    u16 v = (u16)(((u16)AT24CXX_ReadOneByte(EE_BEST_PLANE) << 8)
                  | (u16)AT24CXX_ReadOneByte(EE_BEST_PLANE + 1));

    return (v > 9999) ? 0 : v;
}

static void save_best(void)
{
    AT24CXX_WriteOneByte(EE_BEST_PLANE,     (u8)(best >> 8));
    AT24CXX_WriteOneByte(EE_BEST_PLANE + 1, (u8)(best & 0xFF));
}

/*-------------------- 图形 -------------------*/

/* 玩家飞机，机头朝上，(x,y) = 24x26 包围盒左上角 */
static void draw_plane(int x, int y, u16 body, u16 cock)
{
    UI_Fill(x + 10, y,      4,  4,  body);      /* 机头     */
    UI_Fill(x + 9,  y + 4,  6,  18, body);      /* 机身     */
    UI_Fill(x + 1,  y + 13, 22, 4,  body);      /* 机翼     */
    UI_Fill(x + 5,  y + 20, 14, 3,  body);      /* 尾翼     */
    UI_Fill(x + 11, y + 7,  2,  4,  cock);      /* 座舱     */
}

/* 敌机，机头朝下，(x,y) = 20x16 包围盒左上角 */
static void draw_foe(int x, int y)
{
    UI_Fill(x + 2,  y + 5,  16, 6, C_ENEMY);    /* 机身       */
    UI_Fill(x + 7,  y + 1,  6,  5, C_DOME);     /* 圆顶       */
    UI_Fill(x + 1,  y + 11, 4,  4, C_ENEMY);    /* 左腿       */
    UI_Fill(x + 15, y + 11, 4,  4, C_ENEMY);    /* 右腿       */
}

static void draw_shot(int x, int y)
{
    UI_Fill(x, y, 3, 8, C_SHOT);
}

/*-------------------- HUD ------------------------*/
static void draw_hud(void)
{
    u8 i;

    UI_Fill(126, 0, (int)ui_sw - 126, FIELD_TOP, C_BG);

    UI_Text(128, 6, 12, (char *)"SCORE", C_BODY, C_BG);
    POINT_COLOR = C_BODY;
    BACK_COLOR  = C_BG;
    LCD_ShowNum(168, 6, score, 5, 12);

    UI_Text(216, 20, 12, (char *)"LIFE", C_BODY, C_BG);
    for (i = 0; i < lives; i++)
    {
        UI_Fill(246 + i * 11, 20, 8, 9, C_LIFE);
    }
}

/*-------------------- 星空 -------------------*/
static void stars_init(void)
{
    u8 i;

    for (i = 0; i < MAX_STAR; i++)
    {
        star_x[i] = (u16)(rnd() % ui_sw);
        star_y[i] = (u16)(FIELD_TOP + rnd() % (ui_sh - FIELD_TOP));
        star_c[i] = (u8)(rnd() & 1);
    }
}

static void stars_draw(void)
{
    u8 i;

    for (i = 0; i < MAX_STAR; i++)
    {
        UI_Fill(star_x[i], star_y[i], 2, 2, star_c[i] ? C_STAR : C_STAR2);
    }
}

static void stars_step(void)
{
    u8 i;

    for (i = 0; i < MAX_STAR; i++)
    {
        UI_Fill(star_x[i], star_y[i], 2, 2, C_BG);

        star_y[i] = (u16)(star_y[i] + 1 + (star_c[i] ? 1 : 0));

        if (star_y[i] > ui_sh - 3)
        {
            star_y[i] = FIELD_TOP;
            star_x[i] = (u16)(rnd() % ui_sw);
        }

        UI_Fill(star_x[i], star_y[i], 2, 2, star_c[i] ? C_STAR : C_STAR2);
    }
}

/*-------------------- 场景 -------------------*/
static void draw_scene(void)
{
    u8 i;

    UI_Fill(0, 0, ui_sw, ui_sh, C_BG);

    UI_TopButtons();
    draw_hud();

    stars_init();
    stars_draw();

    for (i = 0; i < MAX_FOE; i++)
    {
        if (foes[i].used) draw_foe(foes[i].x, foes[i].y);
    }

    for (i = 0; i < MAX_SHOT; i++)
    {
        if (shots[i].used) draw_shot(shots[i].x, shots[i].y);
    }

    draw_plane(plx, PY, C_BODY, C_COCK);
}

/*-------------------- 逻辑 --------------------*/
static void reset_run(void)
{
    u8 i;
    u8 d = Dino_Get_Diff();

    for (i = 0; i < MAX_SHOT; i++) shots[i].used = 0;
    for (i = 0; i < MAX_FOE; i++)  foes[i].used  = 0;

    spawn_gap = foe_gap_tab[d];
    speed_now = foe_speed_tab[d];

    score    = 0;
    lives    = 3;
    invuln   = 0;
    fire_cnt = 0;
    spawn_cnt = 0;
    plx      = (int)(ui_sw - PW) / 2;
    ptarget  = -1;
}

static void spawn_foe(void)
{
    u8 i;

    for (i = 0; i < MAX_FOE; i++)
    {
        if (!foes[i].used)
        {
            foes[i].used = 1;
            foes[i].x    = (int)(rnd() % (ui_sw - FW - 4)) + 2;
            foes[i].y    = FIELD_TOP;
            foes[i].dir  = (rnd() & 1) ? 1 : -1;
            return;
        }
    }
}

static void fire(void)
{
    u8 i;

    for (i = 0; i < MAX_SHOT; i++)
    {
        if (!shots[i].used)
        {
            shots[i].used = 1;
            shots[i].x    = plx + PW / 2 - 1;
            shots[i].y    = PY - 8;
            return;
        }
    }
}

/* 子弹 / 敌机碰撞检测，命中返回 1 */
static u8 hit_test(int x0, int y0, int w0, int h0,
                   int x1, int y1, int w1, int h1)
{
    return (x0 < x1 + w1 && x0 + w0 > x1
         && y0 < y1 + h1 && y0 + h0 > y1) ? 1 : 0;
}

static void update(void)
{
    u8 i;
    u8 j;

    /* ---- 擦除上一帧 ---- */
    if (flash_t > 0)
    {
        UI_Fill(flash_x, flash_y, FW, FH, C_BG);
        flash_t--;
    }

    for (i = 0; i < MAX_SHOT; i++)
        if (shots[i].used) UI_Fill(shots[i].x, shots[i].y, 3, 8, C_BG);

    for (i = 0; i < MAX_FOE; i++)
        if (foes[i].used) UI_Fill(foes[i].x - 1, foes[i].y - 1, FW + 2, FH + 2, C_BG);

    UI_Fill(plx - 1, PY - 1, PW + 2, PH + 2, C_BG);

    /* ---- 操纵 ---- */
    /* KEY2 左移、KEY1 右移：K0 已经让给"返回"，不能再兼做方向键，
     * 否则按住左移会立刻退出本局 */
    if (KEY2_DOWN) { plx -= 4; ptarget = -1; }
    if (KEY1_DOWN) { plx += 4; ptarget = -1; }

    if (ptarget >= 0)
    {
        if (plx < ptarget - 4)      plx += 6;
        else if (plx > ptarget + 4) plx -= 6;
    }

    if (plx < 2) plx = 2;
    if (plx > (int)ui_sw - PW - 2) plx = (int)ui_sw - PW - 2;

    /* ---- 子弹 ---- */
    for (i = 0; i < MAX_SHOT; i++)
    {
        if (!shots[i].used) continue;

        shots[i].y -= SHOT_SPEED;

        if (shots[i].y < FIELD_TOP) shots[i].used = 0;
    }

    /* ---- 敌机 ---- */
    for (i = 0; i < MAX_FOE; i++)
    {
        if (!foes[i].used) continue;

        foes[i].y += speed_now;

        if (foes[i].y > (int)ui_sh) { foes[i].used = 0; continue; }

        /* 隔帧做一次轻微左右摆动 */
        if ((rnd() & 3) == 0)
        {
            foes[i].x += (foes[i].dir > 0) ? 1 : -1;

            if (foes[i].x < 2) foes[i].dir = 1;
            if (foes[i].x > (int)ui_sw - FW - 2) foes[i].dir = -1;
        }
    }

    /* ---- 开火 ---- */
    if (++fire_cnt >= FIRE_GAP)
    {
        fire_cnt = 0;
        fire();
    }

    /* ---- 生成敌机 ---- */
    if (++spawn_cnt >= spawn_gap)
    {
        spawn_cnt = 0;
        spawn_foe();
    }

    /* ---- 子弹与敌机 ---- */
    for (i = 0; i < MAX_SHOT; i++)
    {
        if (!shots[i].used) continue;

        for (j = 0; j < MAX_FOE; j++)
        {
            if (!foes[j].used) continue;

            if (hit_test(shots[i].x, shots[i].y, 3, 8,
                         foes[j].x, foes[j].y, FW, FH))
            {
                shots[i].used = 0;
                foes[j].used  = 0;

                /* 在敌机位置留下小闪光，下一帧清除 */
                flash_x = foes[j].x;
                flash_y = foes[j].y;
                flash_t = 2;

                UI_Fill(flash_x + 4, flash_y + 4, 12, 8, C_SHOT);
                UI_Fill(flash_x + 7, flash_y + 2, 6, 12, C_SHOT);

                score++;
                if (score > best) best = score;
                draw_hud();
                break;
            }
        }
    }

    /* ---- 敌机与玩家 ---- */
    if (invuln == 0)
    {
        for (i = 0; i < MAX_FOE; i++)
        {
            if (!foes[i].used) continue;

            if (hit_test(plx + 5, PY + 4, PW - 10, PH - 8,
                         foes[i].x, foes[i].y, FW, FH))
            {
                foes[i].used = 0;
                lives--;
                invuln = 80;
                draw_hud();
                break;
            }
        }
    }

    /* ---- 重绘 ---- */
    stars_step();

    for (i = 0; i < MAX_FOE; i++)
        if (foes[i].used) draw_foe(foes[i].x, foes[i].y);

    for (i = 0; i < MAX_SHOT; i++)
        if (shots[i].used) draw_shot(shots[i].x, shots[i].y);

    if (invuln > 0)
    {
        invuln--;
        if ((invuln & 4) == 0) draw_plane(plx, PY, C_BODY, C_COCK);
    }
    else
    {
        draw_plane(plx, PY, C_BODY, C_COCK);
    }
}

/*-------------------- 暂停 -------------------*/
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

    UI_Panel(46, 148, LGRAY);
    UI_Center(58, 16, (char *)"GAME OVER", BLACK, LGRAY);

    UI_Text(96, 86, 12, (char *)"SCORE", BLACK, LGRAY);
    POINT_COLOR = BLACK;
    BACK_COLOR  = LGRAY;
    LCD_ShowNum(150, 86, score, 5, 12);

    UI_Text(96, 108, 12, (char *)"BEST ", BLACK, LGRAY);
    LCD_ShowNum(150, 108, best, 5, 12);

    UI_Center(132, 12, (char *)"TAP OR K2: RETRY", BLACK, LGRAY);
    UI_Center(150, 12, (char *)"K0 / BACK: MENU", BLACK, LGRAY);

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
static void deco_plane(void)
{
    draw_plane(24, 152, WHITE, BLUE);
    draw_foe(278, 170);
}

void Plane_Menu(void)
{
    u8 act;

    UI_Init();

    for (;;)
    {
        best = load_best();

        /* 最后一个参数 1 = K0 回桌面 / K2 开始，
         * 与关卡内 "K0 返回、K2 左移" 保持一致 */
        act = UI_HomePage((char *)"PLANE WAR", best, Dino_Get_Diff(),
                          BLACK, BLACK, WHITE, deco_plane, 1);

        switch (act)
        {
        case UI_MENU_START:
            UI_Wait_Release();
            Plane_Game();
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
void Plane_Game(void)
{
    u8 ev;

    UI_Init();
    best = load_best();

    for (;;)
    {
        reset_run();
        draw_scene();

        for (;;)
        {
            ev = UI_Scan();

            /* 退出本局 */
            if (ev & UI_K0) return;

            if (ev & UI_TOUCH)
            {
                if (UI_Back_Hit())  return;
                if (UI_Pause_Hit()) { pause_run(); continue; }

                /* 操纵目标：让飞机中心对准手指位置 */
                ptarget = (int)ui_tx - PW / 2;
            }

            if (ev & UI_WKUP) { pause_run(); continue; }

            update();

            if (lives == 0)
            {
                if (show_over()) return;

                break;                      /* 再玩一局 */
            }

            delay_ms(FRAME_MS);
        }
    }
}
