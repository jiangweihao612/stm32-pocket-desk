#ifndef __GAME_H
#define __GAME_H
#include "sys.h"

/* Dino Run —— ILI9341 TFT-LCD 板上的无限跑酷小游戏
 *
 *  手机桌面（menu.c）
 *      -> 点击 DINO RUN 应用  : 加载进度
 *      -> Game_Menu()           : START / SETTINGS / HOME（触摸 + 按键）
 *      -> Dino_Game()           : 实际的奔跑流程
 *
 *  按键
 *    KEY0（PE4，接 GND）：START（游戏菜单）/ 跳跃
 *    KEY1（PE3，接 GND）：SETTINGS（游戏菜单）/ 跳跃
 *    KEY2（PE2，接 GND）：返回上一级（游戏 -> 游戏菜单 -> 桌面）
 *    WK_UP（PA0，接 VCC）：暂停 / 继续，也可开始
 */
void Dino_Game(void);           /* 奔跑流程，按 BACK / KEY2 时返回     */
void Game_Menu(void);           /* 游戏主页，选择 HOME 时返回          */

/* 必须在 AT24CXX_Init() 之后、tp_dev.init() 之前调用：
 * 首次运行时会清除旧的（竖屏）触摸校准标志，
 * 让 TP_Init() 自动执行一次横屏校准。 */
void Touch_Cal_Check(void);

/* --- 供启动器（menu.c）使用的共享辅助函数 -------------------------- */
u8   Dino_Get_Diff(void);       /* 保存在 EEPROM 中的难度 0/1/2           */
u16  Dino_Get_Best(void);       /* 保存在 EEPROM 中的最高分               */
void Game_Settings_Page(void);  /* 独立的难度设置页                       */
void Game_Wait_Touch_Release(void); /* 阻塞直到手指抬起                   */

#endif
