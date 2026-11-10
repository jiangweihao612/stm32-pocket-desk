#ifndef __PLANE_H
#define __PLANE_H
#include "sys.h"

/* PLANE WAR - 纵向射击
 *
 *   飞机大战桌面图标 -> 加载 -> Plane_Menu() -> Plane_Game()
 *
 *   游戏菜单 : K0 开始 / K1 设置 / WK_UP 说明 / K2 主界面
 *   游戏中   : K0 / K1 按住左右移动，WK_UP 暂停，
 *              K2 或左上角 BACK 按钮返回游戏菜单
 *              点击屏幕，飞机朝点击位置移动
 *   机枪自动开火，3 条命，每击落一架敌机 +1 分
 */
void Plane_Menu(void);          /* 游戏主页面，按 HOME 返回 */
void Plane_Game(void);          /* 游戏过程，返回到游戏菜单 */

#endif
