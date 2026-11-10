#ifndef __MENU_H
#define __MENU_H
#include "sys.h"

/* 启动器 / 主屏幕（"手机桌面"），共三页。
 * 点图标（或左右滑）翻页并打开对应 APP；桌面上还支持按键：
 *   K0 = 回到登录界面   K1 = 下一页   K2 = 上一页
 *   WK_UP = 直接打开相机 */
void Main_Menu(void);

/* INFO 页面，由游戏主页打开；按任意键 / 点击任意处返回 */
void Dino_Info_Page(void);

#endif
