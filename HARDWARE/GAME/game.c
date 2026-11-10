/*=============================================================================
 *  Dino Run（Chrome 小恐龙风格的无限跑酷）
 *  目标平台：ALIENTEK 战舰 STM32F103ZET6 + ILI9341 TFT-LCD（FSMC）
 *  驱动：lcd.c / key.c / delay.c / touch.c / 24cxx.c（工程中已包含）
 *
 *  操作方式（各按键的有效电平见 key.h）
 *    游戏菜单：KEY0 = 回桌面，KEY1 = 设置，KEY2 = 开始，WK_UP = 关于
 *    关卡内  ：KEY0 = 回游戏菜单，KEY1 / KEY2 = 跳跃，WK_UP = 暂停
 *    触摸屏  ：跳跃 / SET / HOME / 左上角 BACK
 *
 *  注意：K0 在这个应用里始终是"返回上一级"（关卡里退回菜单，
 *  菜单里退回桌面）；K2 是"开始 / 跳跃"这个正向动作键。
 *  两者在菜单和关卡里的顺序含义正好相反，所以宏名要分开写：
 *  EV_BACK = K0，EV_START = K2。
 *
 *  难度
 *    左上角的 "SET" 按钮会打开设置页，
 *    EASY / HARD / HELL 决定仙人掌的移动速度。所选难度保存在
 *    AT24C02 EEPROM 中，掉电后仍然有效。
 *
 *  实现原理
 *    - 把 LCD 切换到横屏（320x240），每帧只重画需要更新的
 *      矩形区域，所以画面不会闪烁。
 *    - erase_rect() 负责恢复背景（地平线以上是白色天空，
 *      以下是棕色地面），这样精灵越过地平线之后
 *      两个色区依然正确。
 *    - 触摸屏是在横屏模式下校准的（见 Touch_Cal_Check），
 *      因此这里的 tp_dev.x/y 已经是屏幕坐标。
 *    - 本文件所有源码均为纯 ASCII，这样 Keil MDK（GBK）能正确显示
 *      注释。
 *===========================================================================*/

#include "game.h"
#include "menu.h"
#include "lcd.h"
#include "key.h"
#include "delay.h"
#include "touch.h"
#include "24cxx.h"

/*-------------------- 颜色 --------------------*/
/* 沿用桌面的日落暖色：天空用奶油白而不是纯白，
 * 恐龙和仙人掌也各压低一点色温，整体不发冷。      */
#define SKY_COLOR       0xFFBD      /* 奶油白 #FFF6E8          */
#define GROUND_COLOR    BROWN       /* 地面保持棕色            */
#define DINO_COLOR      0x20A2      /* 恐龙：深咖 #241410      */
#define CACTUS_COLOR    0x6C67      /* 仙人掌：橄榄绿          */
#define CLOUD_COLOR     0xDE15      /* 云：淡沙                */
#define PANEL_COLOR     0xDE14      /* 暂停/结束面板：暖沙     */
#define BTN_BG_COLOR    0xA285      /* 次按钮：暖棕            */
#define BTN_SEL_COLOR   0xE4C7      /* 选中：琥珀              */

/* 菜单按钮配色（和 ui.c 的游戏主页面一致） */
#define MENU_START_BG   0xE4C7
#define MENU_BTN_BG     0xA285
#define MENU_HOME_BG    0xC2A5
#define MENU_INK        0x20A2
#define MENU_TIP        0xFF9B

/*-------------------- 几何尺寸 -------------------*/
#define DINO_W          40
#define DINO_H          46
#define DINO_X          44
#define GROUND_H        54      /* 地面带的高度              */
#define MAX_OBS         3

/*-------------------- 物理参数 --------------------*/
#define JUMP_SPEED      (-12)   /* 每帧像素，负值 = 向上     */
#define GRAVITY_STEP    1       /* 每帧平方的像素 */
#define FRAME_MS        28      /* 约 31 fps */

/*-------------------- 状态 ---------------------*/
#define ST_READY        0
#define ST_RUN          1
#define ST_PAUSE        2
#define ST_OVER         3
#define ST_SET          4

/*-------------------- 事件 ---------------------*/
#define EV_KEY0         0x01    /* KEY0（PE4，接 GND，低电平有效） */
#define EV_KEY1         0x02    /* KEY1（PE3）                     */
#define EV_KEY2         0x04    /* KEY2（PE2）                     */
#define EV_WKUP         0x08    /* WK_UP（PA0，接 VCC，高电平有效） */
#define EV_TOUCH        0x10    /* 检测到一次新的触摸              */

/* 按键分工（整个 DINO RUN 应用里 K0 都表示"返回上一级"）：
 *   菜单：K0 回桌面、K1 设置、K2 开始、WK_UP 关于
 *   关卡：K0 回菜单、K1 / K2 跳跃、WK_UP 暂停
 * 所以 "开始" 只能认 K2，不能再用 K0 —— 否则和"返回"撞车。   */
#define EV_CONFIRM      (EV_KEY1 | EV_KEY2)
#define EV_PAUSE        EV_WKUP
#define EV_BACK         EV_KEY0
#define EV_START        EV_KEY2

/*-------------------- 难度 -----------------*/
#define DIFF_EASY       0
#define DIFF_HARD       1
#define DIFF_HELL       2

/* 简单  困难  地狱 */
static const u8  diff_min[3]  = {3,    4,    6};
static const u8  diff_max[3]  = {5,    7,    9};
static const u8  diff_step[3] = {15,   10,   6};
static char     *diff_name[3] = {"EASY", "HARD", "HELL"};

/*-------------------- UI 按钮布局 ---*/
#define NAV_BTN_X       4       /* 游戏内的"返回游戏菜单"按钮         */
#define NAV_BTN_Y       4
#define NAV_BTN_W       54
#define NAV_BTN_H       22

/* 游戏主页按钮（START / SETTINGS+INFO / HOME） */
#define GM_BTN_X        60
#define GM_BTN_W        200
#define GM_START_Y      52
#define GM_START_H      44
#define GM_SET_X        60      /* SETTINGS + INFO 共用一行      */
#define GM_SET_W        96
#define GM_SET_Y        106
#define GM_SET_H        36
#define GM_INFO_X       164
#define GM_INFO_W       96
#define GM_HOME_Y       152
#define GM_HOME_H       36

#define DIFF_BTN_X      40
#define DIFF_BTN_Y0     66
#define DIFF_BTN_H      34
#define DIFF_BTN_GAP    42
#define BACK_BTN_Y      200
#define BACK_BTN_H      30

/*-------------------- EEPROM 布局 --------------*/
#define EE_TOUCH_FLAG   60      /* 0x5A：已完成横屏校准             */
#define EE_DIFF         61      /* 保存的难度                       */
#define EE_BEST         62      /* 最高分，16 位大端序              */

typedef struct
{
    int x;          /* 包围盒左边界             */
    int y;          /* 包围盒上边界             */
    int w;
    int h;
    u8  used;
} Obstacle;

/*-------------------- 模块数据 ---------------*/
static u16 sw = 320;            /* 当前屏幕宽度          */
static u16 sh = 240;            /* 当前屏幕高度          */
static u16 ground_y = 186;      /* 天空 / 地面分界线的 y 坐标     */

static u16 rnd_seed = 0x3F21;
static u8  k0_last = 0;
static u8  k1_last = 0;
static u8  k2_last = 0;
static u8  ku_last = 0;
static u8  tp_last = 0;
static u16 tp_x = 0;
static u16 tp_y = 0;

static int g_dino_y;            /* 恐龙脚部的 y 坐标  */
static int g_dino_vy;
static u8  g_on_ground;
static u32 g_passed;            /* 已通过的障碍数 = 得分    */
static u32 g_best = 0;
static u32 g_frame;
static int g_speed;
static int g_dist;              /* 距离下一棵仙人掌生成还有多远          */
static u8  g_diff = DIFF_EASY;
static Obstacle g_obs[MAX_OBS];

/*-------------------- 函数声明 -----------------*/
static void game_fill(int x, int y, int w, int h, u16 color);
static void erase_rect(int x, int y, int w, int h);
static u8   in_rect(u16 x, u16 y, u16 rx, u16 ry, u16 rw, u16 rh);
static void draw_scene(void);
static void redraw_all(void);
static void draw_dino(int x, int y, u8 leg);
static void draw_cactus(int x, int y, int w, int h);
static void draw_back_button(void);
static u8   back_hit(u16 x, u16 y);
static void draw_game_menu(void);
static void show_score(u32 score);
static void show_center_str(int y, u8 size, char *s);
static void show_score_line(int y, char *label, u32 val);
static void draw_panel(int py, int ph);
static void show_pause(void);
static void show_over(void);
static void show_settings(void);
static u8   settings_touch(u16 x, u16 y);
static void reset_game(void);
static u8   input_scan(void);
static u16  rnd_next(void);
static void spawn_obstacle(void);
static u8   check_hit(void);
static void apply_speed(void);

/*=============================================================================
 *  EEPROM 辅助函数
 *===========================================================================*/

void Touch_Cal_Check(void)
{
    if (AT24CXX_ReadOneByte(EE_TOUCH_FLAG) == 0x5A) return;

    /* 游戏首次运行时：清掉可能是竖屏的校准值，
     * 让 TP_Init() 重新校准，这次是在横屏模式下 */
    AT24CXX_WriteOneByte(54, 0x00);            /* touch.c 使用的触摸校准标志     */
    AT24CXX_WriteOneByte(EE_TOUCH_FLAG, 0x5A);
}

static void load_diff(void)
{
    u8 d = AT24CXX_ReadOneByte(EE_DIFF);

    g_diff = (d <= DIFF_HELL) ? d : DIFF_EASY;
}

/* 最高分掉电不丢失（u16，大端序） */
static void load_best(void)
{
    g_best = ((u32)AT24CXX_ReadOneByte(EE_BEST) << 8)
           |  (u32)AT24CXX_ReadOneByte(EE_BEST + 1);

    if (g_best > 9999) g_best = 0;      /* 空白 EEPROM 读出来是 0xFFFF */
}

static void save_best(void)
{
    AT24CXX_WriteOneByte(EE_BEST,     (u8)(g_best >> 8));
    AT24CXX_WriteOneByte(EE_BEST + 1, (u8)(g_best & 0xFF));
}

/* --- 供启动器（menu.c）使用的辅助函数 --- */
u8 Dino_Get_Diff(void)
{
    load_diff();
    return g_diff;
}

u16 Dino_Get_Best(void)
{
    load_best();
    return (u16)g_best;
}

/* 阻塞到手指抬起，避免切页操作把同一次触摸
 * 又当成一次按钮按下 */
void Game_Wait_Touch_Release(void)
{
    u8 down = 1;

    while (down)
    {
        down = 0;

        if (tp_dev.scan != 0 && tp_dev.scan(0)) down = 1;

        delay_ms(10);
    }

    tp_last = 0;
}

/* 独立的设置页，按下 BACK 时返回调用者 */
void Game_Settings_Page(void)
{
    u8 ev;

    /* 等待所有按键松开：否则打开本页的那个按键
     * 会立刻又把它关掉 */
    while (KEY0_DOWN || KEY1_DOWN || KEY2_DOWN || WKUP_DOWN) delay_ms(10);

    k0_last = 0;
    k1_last = 0;
    k2_last = 0;
    ku_last = 0;

    Game_Wait_Touch_Release();
    show_settings();

    for (;;)
    {
        ev = input_scan();

        /* 任意一个硬件按键都能关掉设置页（4 个键全列出来，
         * 不能再用 EV_CONFIRM 拼，那样会漏掉 K0） */
        if (ev & (EV_KEY0 | EV_KEY1 | EV_KEY2 | EV_WKUP)) break;

        if (ev & EV_TOUCH)
        {
            if (settings_touch(tp_x, tp_y)) break;

            delay_ms(80);
            continue;
        }

        delay_ms(20);
    }

    Game_Wait_Touch_Release();
}

/*=============================================================================
 *  基础绘图辅助函数
 *===========================================================================*/

/* 带裁剪的实心矩形，(x,y) = 左上角 */
static void game_fill(int x, int y, int w, int h, u16 color)
{
    if (w <= 0 || h <= 0) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;

    if (x + w > (int)sw) w = (int)sw - x;
    if (y + h > (int)sh) h = (int)sh - y;
    if (w <= 0 || h <= 0) return;

    LCD_Fill((u16)x, (u16)y, (u16)(x + w - 1), (u16)(y + h - 1), color);
}

/* 通过恢复背景来擦除一个矩形：
 * ground_y 以上 -> 天空色，ground_y 以下 -> 地面色 */
static void erase_rect(int x, int y, int w, int h)
{
    int top = y;
    int bot = y + h;

    if (top < (int)ground_y)
    {
        int b = (bot < (int)ground_y) ? bot : (int)ground_y;
        game_fill(x, top, w, b - top, SKY_COLOR);
    }

    if (bot > (int)ground_y)
    {
        int t = (top > (int)ground_y) ? top : (int)ground_y;
        game_fill(x, t, w, bot - t, GROUND_COLOR);
    }
}

static u8 in_rect(u16 x, u16 y, u16 rx, u16 ry, u16 rw, u16 rh)
{
    return (x >= rx && x < (u16)(rx + rw) && y >= ry && y < (u16)(ry + rh)) ? 1 : 0;
}

/*=============================================================================
 *  精灵图
 *===========================================================================*/

/* 霸王龙，(x,y) = 40x46 包围盒的左上角
 * 方形大头带上下颚、细小的前肢、粗重的尾巴、两条跑动的腿
 * leg = 0 / 1：交替使用支撑腿 */
static void draw_dino(int x, int y, u8 leg)
{
    /* 尾巴（阶梯状，指向左上） */
    game_fill(x + 0,  y + 8,  5,  4,  DINO_COLOR);
    game_fill(x + 4,  y + 10, 9,  7,  DINO_COLOR);

    /* 身体 */
    game_fill(x + 11, y + 8,  16, 26, DINO_COLOR);

    /* 脖子 + 方形大头 */
    game_fill(x + 21, y + 6,  6,  4,  DINO_COLOR);
    game_fill(x + 23, y + 0,  17, 14, DINO_COLOR);

    /* 下颚 + 张开的嘴（头与下颚之间用天空色留出缝隙） */
    game_fill(x + 29, y + 13, 11, 5,  DINO_COLOR);
    game_fill(x + 29, y + 11, 11, 2,  SKY_COLOR);

    /* 眼睛 */
    game_fill(x + 33, y + 3,  4,  4,  SKY_COLOR);

    /* 霸王龙的小前肢 */
    game_fill(x + 26, y + 20, 5,  3,  DINO_COLOR);

    if (leg)
    {
        /* 左腿着地并带脚掌，右腿抬起 */
        game_fill(x + 13, y + 30, 6,  12, DINO_COLOR);
        game_fill(x + 11, y + 42, 9,  4,  DINO_COLOR);
        game_fill(x + 21, y + 30, 6,  5,  DINO_COLOR);
    }
    else
    {
        /* 右腿着地并带脚掌，左腿抬起 */
        game_fill(x + 13, y + 30, 6,  5,  DINO_COLOR);
        game_fill(x + 21, y + 30, 6,  12, DINO_COLOR);
        game_fill(x + 21, y + 42, 9,  4,  DINO_COLOR);
    }
}

/* 仙人掌，(x,y) = 左上角，w x h */
static void draw_cactus(int x, int y, int w, int h)
{
    int tw = 6;
    int tx = x + (w - tw) / 2;

    game_fill(tx, y, tw, h, CACTUS_COLOR);                              /* 主干       */

    game_fill(x, y + h / 3, 4, h / 3, CACTUS_COLOR);                    /* 左臂       */
    game_fill(x, y + h / 3 + h / 3 - 4, 6, 4, CACTUS_COLOR);            /* 左关节     */

    game_fill(x + w - 4, y + h / 4, 4, h / 4 + 2, CACTUS_COLOR);        /* 右臂       */
    game_fill(x + w - 6, y + h / 4 + h / 4 - 2, 6, 4, CACTUS_COLOR);    /* 右关节     */
}

/*=============================================================================
 *  场景
 *===========================================================================*/

static void draw_back_button(void)
{
    game_fill(NAV_BTN_X, NAV_BTN_Y, NAV_BTN_W, NAV_BTN_H, BTN_BG_COLOR);

    POINT_COLOR = BLACK;
    LCD_DrawRectangle(NAV_BTN_X, NAV_BTN_Y,
                      NAV_BTN_X + NAV_BTN_W - 1, NAV_BTN_Y + NAV_BTN_H - 1);

    BACK_COLOR = BTN_BG_COLOR;
    LCD_ShowString(NAV_BTN_X + 11, NAV_BTN_Y + 3, 44, 18, 16, (u8 *)"BACK");
}

/* 点击左上角 BACK 按钮时返回 1 */
static u8 back_hit(u16 x, u16 y)
{
    return in_rect(x, y, NAV_BTN_X, NAV_BTN_Y, NAV_BTN_W, NAV_BTN_H);
}

static void draw_scene(void)
{
    game_fill(0, 0, sw, sh, SKY_COLOR);
    game_fill(0, ground_y, sw, sh - ground_y, GROUND_COLOR);

    /* 静态云朵，放在最高跳跃高度之上 */
    game_fill(60,  40, 34, 10, CLOUD_COLOR);
    game_fill(70,  32, 18, 10, CLOUD_COLOR);
    game_fill(210, 56, 30, 10, CLOUD_COLOR);
    game_fill(220, 48, 14, 10, CLOUD_COLOR);

    draw_dino(DINO_X, g_dino_y - DINO_H, 0);
    draw_back_button();
}

static void redraw_all(void)
{
    u8 i;

    draw_scene();
    for (i = 0; i < MAX_OBS; i++)
    {
        if (g_obs[i].used)
            draw_cactus(g_obs[i].x, g_obs[i].y, g_obs[i].w, g_obs[i].h);
    }
    draw_dino(DINO_X, g_dino_y - DINO_H, 0);
    show_score(g_passed);
}

/*=============================================================================
 *  文字辅助函数
 *===========================================================================*/

static void show_score(u32 score)
{
    u16 x = sw - 112;

    POINT_COLOR = BLACK;
    BACK_COLOR = SKY_COLOR;

    game_fill(x, 4, 108, 20, SKY_COLOR);
    LCD_ShowString(x, 6, 60, 20, 16, (u8 *)"SCORE");
    LCD_ShowNum(x + 48, 6, score, 5, 16);
}

static void show_center_str(int y, u8 size, char *s)
{
    char *p = s;
    u16 len = 0;
    u16 x;

    while (*p++) len++;

    x = (sw > len * (size / 2)) ? (u16)((sw - len * (size / 2)) / 2) : 0;
    LCD_ShowString(x, y, len * (size / 2) + 4, size + 4, size, (u8 *)s);
}

static void show_score_line(int y, char *label, u32 val)
{
    u16 lx;

    lx = (u16)((sw - 11 * 8) / 2);      /* "XXXXX 00000" -> 11 个字符 */
    LCD_ShowString(lx, y, 60, 20, 16, (u8 *)label);
    LCD_ShowNum(lx + 6 * 8, y, val, 5, 16);
}

static void draw_panel(int py, int ph)
{
    int px = ((int)sw - 260) / 2;

    if (px < 4) px = 4;

    game_fill(px, py, 260, ph, PANEL_COLOR);
    POINT_COLOR = BLACK;
    LCD_DrawRectangle((u16)px, (u16)py, (u16)(px + 259), (u16)(py + ph - 1));

    BACK_COLOR = PANEL_COLOR;
    POINT_COLOR = BLACK;
}

/*=============================================================================
 *  界面
 *===========================================================================*/

/* 游戏主页：按钮（START / SETTINGS / INFO / HOME）可以点击，
 * 每个按钮也对应一个按键（K2 / K1 / WK_UP / K0） */
static void draw_game_menu(void)
{
    u16 lx;

    /* 天空 + 地面 */
    game_fill(0, 0, sw, sh, SKY_COLOR);
    game_fill(0, 200, sw, sh - 200, GROUND_COLOR);

    /* 标题 */
    POINT_COLOR = BLACK;
    BACK_COLOR  = SKY_COLOR;
    show_center_str(10, 24, "DINO RUN");

    /* "BEST 00000   MODE EASY" 显示位置 */
    lx = (u16)((sw - 132) / 2);
    LCD_ShowString(lx, 40, 30, 16, 12, (u8 *)"BEST");
    LCD_ShowNum(lx + 30, 40, g_best, 5, 12);
    LCD_ShowString(lx + 66, 40, 30, 16, 12, (u8 *)"MODE");
    LCD_ShowString(lx + 102, 40, 30, 16, 12, (u8 *)diff_name[g_diff]);

    /* START —— 琥珀大按钮 */
    game_fill(GM_BTN_X, GM_START_Y, GM_BTN_W, GM_START_H, MENU_START_BG);
    POINT_COLOR = MENU_INK;
    LCD_DrawRectangle(GM_BTN_X, GM_START_Y,
                      GM_BTN_X + GM_BTN_W - 1, GM_START_Y + GM_START_H - 1);
    POINT_COLOR = MENU_INK;
    BACK_COLOR  = MENU_START_BG;
    LCD_ShowString((u16)((sw - 60) / 2), GM_START_Y + 10, 64, 28, 24, (u8 *)"START");

    /* SETTINGS（第二行左半边） */
    game_fill(GM_SET_X, GM_SET_Y, GM_SET_W, GM_SET_H, MENU_BTN_BG);
    POINT_COLOR = MENU_TIP;
    LCD_DrawRectangle(GM_SET_X, GM_SET_Y,
                      GM_SET_X + GM_SET_W - 1, GM_SET_Y + GM_SET_H - 1);
    BACK_COLOR = MENU_BTN_BG;
    LCD_ShowString(GM_SET_X + 16, GM_SET_Y + 10, 68, 20, 16, (u8 *)"SETTINGS");

    /* INFO（第二行右半边，蓝色，与 INFO 应用一致） */
    game_fill(GM_INFO_X, GM_SET_Y, GM_INFO_W, GM_SET_H, MENU_BTN_BG);
    POINT_COLOR = MENU_TIP;
    LCD_DrawRectangle(GM_INFO_X, GM_SET_Y,
                      GM_INFO_X + GM_INFO_W - 1, GM_SET_Y + GM_SET_H - 1);
    POINT_COLOR = MENU_TIP;
    BACK_COLOR  = MENU_BTN_BG;
    LCD_ShowString(GM_INFO_X + 32, GM_SET_Y + 10, 40, 20, 16, (u8 *)"INFO");

    /* HOME（返回手机桌面） */
    game_fill(GM_BTN_X, GM_HOME_Y, GM_BTN_W, GM_HOME_H, MENU_HOME_BG);
    POINT_COLOR = MENU_TIP;
    LCD_DrawRectangle(GM_BTN_X, GM_HOME_Y,
                      GM_BTN_X + GM_BTN_W - 1, GM_HOME_Y + GM_HOME_H - 1);
    POINT_COLOR = MENU_TIP;
    BACK_COLOR  = MENU_HOME_BG;
    LCD_ShowString((u16)((sw - 32) / 2), GM_HOME_Y + 10, 36, 20, 16, (u8 *)"HOME");

    /* 装饰：左边等待的恐龙，右边一棵仙人掌 */
    draw_dino(8, 200 - DINO_H, 0);
    draw_cactus(292, 200 - 42, 16, 42);

    /* 按键提示 */
    POINT_COLOR = WHITE;
    BACK_COLOR  = GROUND_COLOR;
    LCD_ShowString((u16)((sw - 180) / 2), 212, 190, 16, 12,
                   (u8 *)"K0 HOME K1 SET K2 START W INFO");
}

static void show_pause(void)
{
    draw_panel(66, 100);
    show_center_str(84,  16, "PAUSED");
    show_center_str(112, 12, "TAP / K1 K2 : RESUME");
    show_center_str(134, 12, "K0 : MAIN MENU");
}

static void show_over(void)
{
    draw_panel(44, 150);
    show_center_str(58,  16, "GAME OVER");
    show_score_line(84,  "SCORE", g_passed);
    show_score_line(106, "BEST ", g_best);
    show_center_str(132, 16, "TAP / K1 K2 : RETRY");
    show_center_str(154, 12, "K0 / BACK : MAIN MENU");
}

static void show_settings(void)
{
    u8 i;
    u16 y;
    u16 bg;
    u16 bw = (u16)(sw - 80);

    game_fill(0, 0, sw, sh, WHITE);

    POINT_COLOR = BLACK;
    BACK_COLOR = WHITE;

    show_center_str(12, 16, "SETTINGS");
    show_center_str(34, 12, "SELECT DIFFICULTY");

    for (i = 0; i < 3; i++)
    {
        y  = DIFF_BTN_Y0 + i * DIFF_BTN_GAP;
        bg = (i == g_diff) ? BTN_SEL_COLOR : BTN_BG_COLOR;

        game_fill(DIFF_BTN_X, y, bw, DIFF_BTN_H, bg);
        POINT_COLOR = BLACK;
        LCD_DrawRectangle(DIFF_BTN_X, y,
                          (u16)(DIFF_BTN_X + bw - 1), (u16)(y + DIFF_BTN_H - 1));

        BACK_COLOR = bg;
        show_center_str(y + 9, 16, diff_name[i]);
    }

    game_fill(DIFF_BTN_X, BACK_BTN_Y, bw, BACK_BTN_H, BTN_BG_COLOR);
    POINT_COLOR = BLACK;
    LCD_DrawRectangle(DIFF_BTN_X, BACK_BTN_Y,
                      (u16)(DIFF_BTN_X + bw - 1), (u16)(BACK_BTN_Y + BACK_BTN_H - 1));
    BACK_COLOR = BTN_BG_COLOR;
    show_center_str(BACK_BTN_Y + 7, 16, "BACK");
}

/* 命中 BACK 按钮时返回 1 */
static u8 settings_touch(u16 x, u16 y)
{
    u8 i;
    u16 by;
    u16 bw = (u16)(sw - 80);

    for (i = 0; i < 3; i++)
    {
        by = DIFF_BTN_Y0 + i * DIFF_BTN_GAP;

        if (in_rect(x, y, DIFF_BTN_X, by, bw, DIFF_BTN_H))
        {
            if (g_diff != i)
            {
                g_diff = i;
                AT24CXX_WriteOneByte(EE_DIFF, g_diff);
                apply_speed();
                show_settings();        /* 重绘以移动高亮                */
            }
            return 0;
        }
    }

    if (in_rect(x, y, DIFF_BTN_X, BACK_BTN_Y, bw, BACK_BTN_H)) return 1;

    return 0;
}

/*=============================================================================
 *  游戏逻辑
 *===========================================================================*/

static void apply_speed(void)
{
    int s = (int)diff_min[g_diff] + (int)(g_passed / diff_step[g_diff]);

    if (s > (int)diff_max[g_diff]) s = (int)diff_max[g_diff];

    g_speed = s;
}

static void reset_game(void)
{
    u8 i;

    g_dino_y    = (int)ground_y;
    g_dino_vy   = 0;
    g_on_ground = 1;
    g_passed    = 0;
    g_frame     = 0;
    g_dist      = 150;

    for (i = 0; i < MAX_OBS; i++) g_obs[i].used = 0;

    apply_speed();

    draw_scene();
    show_score(0);
}

/* 键盘 + 触摸，每帧调用一次，返回边沿触发的事件 */
static u8 input_scan(void)
{
    u8 ev = 0;
    u8 k0 = KEY0_DOWN ? 1 : 0;
    u8 k1 = KEY1_DOWN ? 1 : 0;
    u8 k2 = KEY2_DOWN ? 1 : 0;
    u8 ku = WKUP_DOWN ? 1 : 0;

    /* ---- 按键，边沿触发 ---- */
    if (k0 && !k0_last) ev |= EV_KEY0;
    if (k1 && !k1_last) ev |= EV_KEY1;
    if (k2 && !k2_last) ev |= EV_KEY2;
    if (ku && !ku_last) ev |= EV_WKUP;

    k0_last = k0;
    k1_last = k1;
    k2_last = k2;
    ku_last = ku;

    /* ---- 触摸屏 ---- */
    if (tp_dev.scan != 0)
    {
        if (tp_dev.scan(0))
        {
            if (!tp_last
                && tp_dev.x[0] < sw && tp_dev.y[0] < sh)
            {
                tp_x = tp_dev.x[0];
                tp_y = tp_dev.y[0];
                ev |= EV_TOUCH;
            }
            tp_last = 1;
        }
        else
        {
            tp_last = 0;
        }
    }

    return ev;
}

static u16 rnd_next(void)
{
    rnd_seed = (u16)((u32)rnd_seed * 31421u + 6927u);
    return rnd_seed;
}

static void spawn_obstacle(void)
{
    u8 i;
    u16 r = rnd_next();

    for (i = 0; i < MAX_OBS; i++)
    {
        if (g_obs[i].used) continue;

        if (r & 1) { g_obs[i].w = 14; g_obs[i].h = 26; }    /* 小仙人掌     */
        else       { g_obs[i].w = 16; g_obs[i].h = 42; }    /* 高仙人掌     */

        g_obs[i].x = (int)sw;
        g_obs[i].y = (int)ground_y - g_obs[i].h;
        g_obs[i].used = 1;
        return;
    }
}

/* AABB 相交检测，两侧略微收缩，让手感更公平 */
static u8 check_hit(void)
{
    u8 i;
    int dx0 = DINO_X + 7;
    int dx1 = DINO_X + DINO_W - 4;
    int dy0 = g_dino_y - DINO_H + 5;
    int dy1 = g_dino_y - 2;

    for (i = 0; i < MAX_OBS; i++)
    {
        int ox0, ox1, oy0, oy1;

        if (!g_obs[i].used) continue;

        ox0 = g_obs[i].x + 2;
        ox1 = g_obs[i].x + g_obs[i].w - 2;
        oy0 = g_obs[i].y + 2;
        oy1 = g_obs[i].y + g_obs[i].h;

        if (dx0 < ox1 && dx1 > ox0 && dy0 < oy1 && dy1 > oy0) return 1;
    }

    return 0;
}

/*=============================================================================
 *  游戏主页：START / SETTINGS / HOME
 *  触摸按钮 + KEY0 / KEY1 / KEY2，选择 HOME 时返回
 *===========================================================================*/

void Game_Menu(void)
{
    u8 ev;

    sw = lcddev.width;
    sh = lcddev.height;
    ground_y = sh - GROUND_H;

    load_diff();
    load_best();

    draw_game_menu();

    /* 先把菜单画出来，再等按键松开：
     * 现在 K0 是"回桌面"，而桌面上的 K0 又是"打开 DINO RUN"。
     * 如果玩家一直按着 K0 进来，第一次 input_scan 就会把这个
     * 刚出现的菜单当成"按了一次返回"，一闪又回到桌面。 */
    while (KEY0_DOWN || KEY1_DOWN || KEY2_DOWN || WKUP_DOWN) delay_ms(10);

    for (;;)
    {
        ev = input_scan();

        /* ---- HOME：返回手机桌面 ---- */
        if (ev & EV_BACK) return;

        if ((ev & EV_TOUCH)
            && in_rect(tp_x, tp_y, GM_BTN_X, GM_HOME_Y, GM_BTN_W, GM_HOME_H))
            return;

        /* ---- 设置（SETTINGS） ---- */
        if ((ev & EV_KEY1)
            || ((ev & EV_TOUCH)
                && in_rect(tp_x, tp_y, GM_BTN_X, GM_SET_Y, GM_BTN_W, GM_SET_H)))
        {
            Game_Settings_Page();

            load_diff();
            load_best();
            draw_game_menu();
            delay_ms(120);
            continue;
        }

        /* ---- 关于（INFO） ---- */
        if ((ev & EV_WKUP)
            || ((ev & EV_TOUCH)
                && in_rect(tp_x, tp_y, GM_INFO_X, GM_SET_Y, GM_INFO_W, GM_SET_H)))
        {
            Game_Wait_Touch_Release();
            Dino_Info_Page();

            Game_Wait_Touch_Release();
            draw_game_menu();
            delay_ms(120);
            continue;
        }

        /* ---- 开始（START） ---- */
        if ((ev & EV_START)
            || ((ev & EV_TOUCH)
                && in_rect(tp_x, tp_y, GM_BTN_X, GM_START_Y, GM_BTN_W, GM_START_H)))
        {
            Game_Wait_Touch_Release();
            Dino_Game();                    /* 按 BACK / KEY2 时返回    */

            load_best();
            draw_game_menu();
            delay_ms(120);
            continue;
        }

        delay_ms(20);
    }
}

/*=============================================================================
 *  实际的奔跑流程
 *===========================================================================*/

void Dino_Game(void)
{
    u8 state = ST_RUN;
    u8 ev;
    u8 i;
    u8 leg;
    u8 jump;
    int old_top;

    LCD_Display_Dir(1);             /* 横屏：ILI9341 为 320 x 240       */
    sw = lcddev.width;
    sh = lcddev.height;
    ground_y = sh - GROUND_H;

    POINT_COLOR = BLACK;
    BACK_COLOR  = SKY_COLOR;

    /* 先等所有按键松开，再把边沿标志清零。
     * 现在 K0 在关卡里是"返回"：玩家从菜单用 K0 开始游戏时，
     * 到这一步 K0 往往还按着，清零后第一次 input_scan 会立刻
     * 产生一个 EV_KEY0 (= EV_BACK)，新开的一局直接被踢出来。 */
    while (KEY0_DOWN || KEY1_DOWN || KEY2_DOWN || WKUP_DOWN) delay_ms(10);

    k0_last = 0;
    k1_last = 0;
    k2_last = 0;
    ku_last = 0;
    tp_last = 0;

    load_diff();
    load_best();

    reset_game();
    delay_ms(250);

    for (;;)
    {
        ev = input_scan();

        switch (state)
        {
        /*---------------------------------------------------------------*/
        case ST_RUN:
            /* 左上角 BACK 按钮 / KEY0 立即退出本局 */
            if (ev & EV_BACK) return;

            /* 暂停 */
            if (ev & EV_PAUSE)
            {
                show_pause();
                state = ST_PAUSE;
                delay_ms(200);
                break;
            }

            jump = 0;

            if (ev & EV_CONFIRM) jump = 1;

            if (ev & EV_TOUCH)
            {
                if (back_hit(tp_x, tp_y)) return;  /* 返回游戏菜单      */

                jump = 1;                       /* 点击其他任意位置 = 跳跃  */
            }

            if (jump && g_on_ground)
            {
                g_dino_vy   = JUMP_SPEED;
                g_on_ground = 0;
            }

            /* ---- 恐龙 ---- */
            old_top = g_dino_y - DINO_H;

            if (!g_on_ground)
            {
                g_dino_vy += GRAVITY_STEP;
                g_dino_y  += g_dino_vy;

                if (g_dino_y >= (int)ground_y)
                {
                    g_dino_y    = (int)ground_y;
                    g_dino_vy   = 0;
                    g_on_ground = 1;
                }
            }

            erase_rect(DINO_X, old_top, DINO_W, DINO_H);

            leg = g_on_ground ? (u8)((g_frame >> 2) & 1) : 0;
            draw_dino(DINO_X, g_dino_y - DINO_H, leg);

            /* ---- 障碍物 ---- */
            for (i = 0; i < MAX_OBS; i++)
            {
                if (!g_obs[i].used) continue;

                old_top = g_obs[i].x;
                g_obs[i].x -= g_speed;

                erase_rect(old_top, g_obs[i].y, g_obs[i].w + g_speed, g_obs[i].h);

                if (g_obs[i].x + g_obs[i].w < 0)
                {
                    g_obs[i].used = 0;
                    g_passed++;             /* 每通过一棵仙人掌 +1 分     */
                    show_score(g_passed);
                    continue;
                }

                draw_cactus(g_obs[i].x, g_obs[i].y, g_obs[i].w, g_obs[i].h);
            }

            /* ---- 生成 ---- */
            g_dist -= g_speed;
            if (g_dist <= 0)
            {
                spawn_obstacle();

                /* 间隔是按帧而不是按像素计算的：无论速度多少，
                 * 两棵仙人掌之间总是相隔 30-54 帧，
                 * 比一次完整的跳跃（24 帧）还长，
                 * 所以赛道始终是公平的 */
                g_dist = g_speed * (30 + (int)(rnd_next() % 25));
            }

            /* ---- 碰撞检测 ---- */
            if (check_hit())
            {
                if (g_passed > g_best)
                {
                    g_best = g_passed;
                    save_best();            /* 最高分保存在 EEPROM 中    */
                }

                redraw_all();
                show_over();

                state = ST_OVER;
                delay_ms(300);
                break;
            }

            /* ---- 难度 / 速度 ---- */
            g_frame++;
            apply_speed();

            delay_ms(FRAME_MS);
            break;

        /*---------------------------------------------------------------*/
        case ST_PAUSE:
            if ((ev & EV_BACK) || ((ev & EV_TOUCH) && back_hit(tp_x, tp_y))) return;

            if (ev)
            {
                redraw_all();
                state = ST_RUN;
                delay_ms(200);
            }
            else
            {
                delay_ms(20);
            }
            break;

        /*---------------------------------------------------------------*/
        case ST_OVER:
            /* K0 退出，K1 / K2 / 触摸都开始新的一局 */
            if ((ev & EV_BACK) || ((ev & EV_TOUCH) && back_hit(tp_x, tp_y))) return;

            if (ev)
            {
                reset_game();
                state = ST_RUN;
                delay_ms(250);
            }
            else
            {
                delay_ms(20);
            }
            break;

        /*---------------------------------------------------------------*/
        default:
            return;
        }
    }
}
