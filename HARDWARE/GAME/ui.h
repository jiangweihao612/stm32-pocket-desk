#ifndef __UI_H
#define __UI_H
#include "sys.h"

/* 小游戏共用的 UI 原语（plane.c / snake.c）。
 * 让所有应用页面保持一致：相同的按钮、相同的按键绑定、
 * 相同的面板风格。所有坐标都是横屏 (320x240) 像素。       */

/* UI_Scan() 返回的事件位 */
#define UI_K0       0x01
#define UI_K1       0x02
#define UI_K2       0x04
#define UI_WKUP     0x08
#define UI_TOUCH    0x10

/* UI_HomePage() 的返回值 */
#define UI_MENU_START   0
#define UI_MENU_SET     1
#define UI_MENU_INFO    2
#define UI_MENU_HOME    3

/*-------------------- 游戏主页面的按钮 ----*/
#define HP_BTN_X        60
#define HP_BTN_W        200
#define HP_START_Y      52
#define HP_START_H      44
#define HP_SET_X        60
#define HP_SET_W        96
#define HP_ROW2_Y       106
#define HP_ROW2_H       36
#define HP_INFO_X       164
#define HP_INFO_W       96
#define HP_HOME_Y       152
#define HP_HOME_H       36

extern u16 ui_sw;               /* 屏幕宽度 (横屏 320) */
extern u16 ui_sh;               /* 屏幕高度 (横屏 240) */
extern u16 ui_tx;               /* 最近一次触摸 x        */
extern u16 ui_ty;               /* 最近一次触摸 y        */

void UI_Init(void);             /* 读取当前 LCD 尺寸     */

void UI_Fill(int x, int y, int w, int h, u16 color);
u8   UI_Hit(u16 x, u16 y, u16 rx, u16 ry, u16 rw, u16 rh);
void UI_Text(int x, int y, u8 size, char *s, u16 fg, u16 bg);
void UI_Center(int y, u8 size, char *s, u16 fg, u16 bg);
void UI_Btn(int x, int y, int w, int h, char *label, u16 bg, u16 fg, u8 size);
void UI_Panel(int y, int h, u16 bg);    /* 居中的提示面板     */
void UI_TopButtons(void);               /* HUD 中的 BACK + PAUSE   */
u8   UI_Back_Hit(void);                 /* 点击了 BACK 时为 1    */
u8   UI_Pause_Hit(void);                /* 点击了 PAUSE 时为 1   */

u8   UI_Scan(void);             /* 边沿触发的按键 / 触摸事件 */
void UI_Wait_Release(void);     /* 等待手指和按键松开 */

/* 游戏主页面：标题、BEST/MODE 行、START / SETTINGS / INFO / HOME、
 * 按键提示，以及可选的游戏图案装饰回调。
 * k0_back = 0：K0 开始、K2 回桌面（SNAKE 用）
 * k0_back = 1：K0 回桌面、K2 开始（PLANE WAR 用，和关卡内一致）
 * 阻塞直到选中某个按钮，返回 UI_MENU_xxx。                */
u8   UI_HomePage(char *title, u16 best, u8 diff, u16 sky, u16 ground,
                 u16 fg, void (*deco)(void), u8 k0_back);

#endif
