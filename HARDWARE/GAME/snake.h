#ifndef __SNAKE_H
#define __SNAKE_H
#include "sys.h"

/* SNAKE - 20 x 12 格棋盘上的经典贪吃蛇
 *
 *   贪吃蛇桌面图标 -> 加载 -> Snake_Menu() -> Snake_Game()
 *
 *   游戏菜单 : K0 开始 / K1 设置 / WK_UP 说明 / K2 主界面
 *   游戏中   : K0 左转（逆时针），K1 右转，
 *              WK_UP 暂停，K2 或左上角 BACK 按钮返回
 *              点击棋盘左半 / 右半区域让蛇转向
 *   每吃一个苹果 +1 分，蛇越长速度越快
 */
void Snake_Menu(void);          /* 游戏主页面，按 HOME 返回 */
void Snake_Game(void);          /* 游戏过程，返回到游戏菜单 */

#endif
