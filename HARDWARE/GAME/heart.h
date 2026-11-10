#ifndef __HEART_H
#define __HEART_H

/* MAX30102 心率/血氧页。
 * 一直运行到按下 "< BACK" 按钮（或点击标题栏任意位置）为止；
 * 之后由调用者（menu.c）重绘桌面。                      */
void Heart_Run(void);

#endif
