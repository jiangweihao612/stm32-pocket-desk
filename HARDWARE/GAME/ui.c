/*=============================================================================
 *  ui.c - 小游戏共用的 UI 原语
 *
 *  所有游戏页面（主页面、HUD 按钮、提示面板）都在这里绘制，
 *  这样 PLANE WAR 和 SNAKE 的外观与操作才和 DINO RUN 完全一致：
 *
 *    主页面      K0 开始     K1 设置       WK_UP 说明   K2 主界面
 *                （PLANE WAR 用 k0_back=1 调换：K0 主界面，K2 开始）
 *                （每个按钮也可以直接点击）
 *    游戏中      左上角 BACK   ->  返回游戏主页面
 *                左上角 PAUSE ->  暂停 / 继续
 *
 *  触摸屏是在横屏模式下校准的（见 Touch_Cal_Check），
 *  因此 tp_dev.x/y 已经是屏幕坐标。
 *===========================================================================*/

#include "ui.h"
#include "lcd.h"
#include "key.h"
#include "touch.h"
#include "delay.h"

/* 游戏主页面按钮配色：沿用桌面的日落暖色，
 * 琥珀 = 开始 / 沙色 = 次级 / 陶土 = 回桌面。 */
#define UI_C_START   0xE4C7     /* 开始按钮：琥珀            */
#define UI_C_BTN     0xA285     /* SETTINGS / INFO：暖棕      */
#define UI_C_HOME    0xC2A5     /* HOME：陶土                 */
#define UI_C_INK     0x20A2     /* 按钮文字：深咖             */
#define UI_C_TIP     0xFF9B     /* 底部提示：米白             */

u16 ui_sw = 320;
u16 ui_sh = 240;
u16 ui_tx = 0;
u16 ui_ty = 0;

static u8 tp_last = 0;
static u8 k0_last = 0;
static u8 k1_last = 0;
static u8 k2_last = 0;
static u8 ku_last = 0;

static const char *diff_name[3] = {"EASY", "HARD", "HELL"};

/*-------------------- 基础 ---------------------*/

void UI_Init(void)
{
    ui_sw = lcddev.width;
    ui_sh = lcddev.height;

    tp_last = 0;
    k0_last = 0;
    k1_last = 0;
    k2_last = 0;
    ku_last = 0;
}

/* 裁剪后的实心矩形，(x,y) = 左上角 */
void UI_Fill(int x, int y, int w, int h, u16 color)
{
    if (w <= 0 || h <= 0) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;

    if (x + w > (int)ui_sw) w = (int)ui_sw - x;
    if (y + h > (int)ui_sh) h = (int)ui_sh - y;
    if (w <= 0 || h <= 0) return;

    LCD_Fill((u16)x, (u16)y, (u16)(x + w - 1), (u16)(y + h - 1), color);
}

u8 UI_Hit(u16 x, u16 y, u16 rx, u16 ry, u16 rw, u16 rh)
{
    return (x >= rx && x < (u16)(rx + rw)
         && y >= ry && y < (u16)(ry + rh)) ? 1 : 0;
}

void UI_Text(int x, int y, u8 size, char *s, u16 fg, u16 bg)
{
    POINT_COLOR = fg;
    BACK_COLOR  = bg;
    LCD_ShowString((u16)x, (u16)y, 320, (u8)(size + 4), size, (u8 *)s);
}

void UI_Center(int y, u8 size, char *s, u16 fg, u16 bg)
{
    u16 len = 0;
    u16 x;
    char *p = s;

    while (*p++) len++;

    x = (ui_sw > len * (size / 2)) ? (u16)((ui_sw - len * (size / 2)) / 2) : 0;
    UI_Text(x, y, size, s, fg, bg);
}

void UI_Btn(int x, int y, int w, int h, char *label, u16 bg, u16 fg, u8 size)
{
    u16 len = 0;
    char *p = label;
    u16 tx;
    u16 ty;

    while (*p++) len++;

    UI_Fill(x, y, w, h, bg);

    POINT_COLOR = BLACK;
    LCD_DrawRectangle((u16)x, (u16)y, (u16)(x + w - 1), (u16)(y + h - 1));

    tx = (u16)(x + (w - (int)len * (size / 2)) / 2);
    ty = (u16)(y + (h - (int)size) / 2);

    UI_Text(tx, ty, size, label, fg, bg);
}

void UI_Panel(int y, int h, u16 bg)
{
    UI_Fill(40, y, ui_sw - 80, h, bg);

    POINT_COLOR = BLACK;
    LCD_DrawRectangle(40, (u16)y, (u16)(ui_sw - 41), (u16)(y + h - 1));
}

/* HUD：左上角是 BACK，紧挨着右边是 PAUSE */
void UI_TopButtons(void)
{
    UI_Btn(4,  4, 54, 20, "BACK",  LGRAY, BLACK, 12);
    UI_Btn(62, 4, 54, 20, "PAUSE", LGRAY, BLACK, 12);
}

u8 UI_Back_Hit(void)
{
    return UI_Hit(ui_tx, ui_ty, 4, 4, 54, 20);
}

u8 UI_Pause_Hit(void)
{
    return UI_Hit(ui_tx, ui_ty, 62, 4, 54, 20);
}

/*-------------------- 输入 ---------------------*/

/* 边沿触发：每个按键一位，另外新按下手指时给出 UI_TOUCH */
u8 UI_Scan(void)
{
    u8 ev = 0;
    u8 k0 = KEY0_DOWN ? 1 : 0;
    u8 k1 = KEY1_DOWN ? 1 : 0;
    u8 k2 = KEY2_DOWN ? 1 : 0;
    u8 ku = WKUP_DOWN ? 1 : 0;

    if (k0 && !k0_last) ev |= UI_K0;
    if (k1 && !k1_last) ev |= UI_K1;
    if (k2 && !k2_last) ev |= UI_K2;
    if (ku && !ku_last) ev |= UI_WKUP;

    k0_last = k0;
    k1_last = k1;
    k2_last = k2;
    ku_last = ku;

    if (tp_dev.scan != 0)
    {
        if (tp_dev.scan(0))
        {
            if (!tp_last && tp_dev.x[0] < ui_sw && tp_dev.y[0] < ui_sh)
            {
                ui_tx = tp_dev.x[0];
                ui_ty = tp_dev.y[0];
                ev |= UI_TOUCH;
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

void UI_Wait_Release(void)
{
    for (;;)
    {
        if (KEY0_DOWN || KEY1_DOWN || KEY2_DOWN || WKUP_DOWN) { delay_ms(10); continue; }
        if (tp_dev.scan != 0 && tp_dev.scan(0))               { delay_ms(10); continue; }

        break;
    }

    k0_last = 0;
    k1_last = 0;
    k2_last = 0;
    ku_last = 0;
    tp_last = 0;
}

/*-------------------- 游戏主页面 --------------*/

/* k0_back = 1 时把 K0 / K2 的含义对调：K0 = 主界面，K2 = 开始。
 * 按钮和触摸永远不变，只有底部那行按键提示跟着换。 */
static void draw_home(char *title, u16 best, u8 diff,
                      u16 sky, u16 ground, u16 fg, void (*deco)(void),
                      u8 k0_back)
{
    u16 lx;

    UI_Fill(0, 0, ui_sw, ui_sh, sky);
    if (ground != sky) UI_Fill(0, 200, ui_sw, (int)ui_sh - 200, ground);

    UI_Center(10, 24, title, fg, sky);

    lx = (u16)((ui_sw - 132) / 2);
    UI_Text(lx, 40, 12, (char *)"BEST", fg, sky);
    POINT_COLOR = fg;
    BACK_COLOR  = sky;
    LCD_ShowNum(lx + 30, 40, best, 5, 12);
    UI_Text(lx + 66, 40, 12, (char *)"MODE", fg, sky);
    UI_Text(lx + 102, 40, 12, (char *)diff_name[diff], fg, sky);

    UI_Btn(HP_BTN_X, HP_START_Y, HP_BTN_W, HP_START_H, "START",    UI_C_START, UI_C_INK, 24);
    UI_Btn(HP_SET_X,  HP_ROW2_Y,  HP_SET_W,  HP_ROW2_H,  "SETTINGS", UI_C_BTN,   UI_C_TIP, 16);
    UI_Btn(HP_INFO_X, HP_ROW2_Y,  HP_INFO_W, HP_ROW2_H,  "INFO",     UI_C_BTN,   UI_C_TIP, 16);
    UI_Btn(HP_BTN_X,  HP_HOME_Y,  HP_BTN_W,  HP_HOME_H,  "HOME",     UI_C_HOME,  UI_C_TIP, 16);

    if (deco != 0) deco();

    /* 两条提示都是 30 个字符 = 180 px，换哪条都不会超出屏幕 */
    UI_Center(212, 12, k0_back ? (char *)"K0 HOME K1 SET K2 START W INFO"
                               : (char *)"K0 START K1 SET K2 HOME W INFO",
              UI_C_TIP, ground);
}

u8 UI_HomePage(char *title, u16 best, u8 diff,
               u16 sky, u16 ground, u16 fg, void (*deco)(void),
               u8 k0_back)
{
    u8 ev;

    draw_home(title, best, diff, sky, ground, fg, deco, k0_back);

    for (;;)
    {
        ev = UI_Scan();

        if (k0_back)
        {
            /* PLANE WAR：K0 回桌面，K2 开始（和关卡内的 K0 = 返回对齐） */
            if (ev & UI_K0)   return UI_MENU_HOME;
            if (ev & UI_K2)   return UI_MENU_START;
        }
        else
        {
            if (ev & UI_K0)   return UI_MENU_START;
            if (ev & UI_K2)   return UI_MENU_HOME;
        }

        if (ev & UI_K1)   return UI_MENU_SET;
        if (ev & UI_WKUP) return UI_MENU_INFO;

        if (ev & UI_TOUCH)
        {
            if (UI_Hit(ui_tx, ui_ty, HP_BTN_X, HP_START_Y, HP_BTN_W, HP_START_H))
                return UI_MENU_START;

            if (UI_Hit(ui_tx, ui_ty, HP_SET_X, HP_ROW2_Y, HP_SET_W, HP_ROW2_H))
                return UI_MENU_SET;

            if (UI_Hit(ui_tx, ui_ty, HP_INFO_X, HP_ROW2_Y, HP_INFO_W, HP_ROW2_H))
                return UI_MENU_INFO;

            if (UI_Hit(ui_tx, ui_ty, HP_BTN_X, HP_HOME_Y, HP_BTN_W, HP_HOME_H))
                return UI_MENU_HOME;
        }

        delay_ms(20);
    }
}
