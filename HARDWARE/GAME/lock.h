#ifndef __LOCK_H
#define __LOCK_H
#include "sys.h"

/* 桌面用的密码锁屏。
 *
 *   Lock_Init()            从 AT24C02 中读出 6 位密码
 *   Lock_Run()             显示键盘，阻塞直到密码匹配
 *   Lock_Settings_Page()   修改密码（输入两遍，保存到 EEPROM）
 *
 * 密码存放在 EEPROM 的 128/129..134，远离触摸
 * 校准区（40..54）和游戏分数区（60..67）。          */
void Lock_Init(void);
void Lock_Run(void);
void Lock_Settings_Page(void);

#endif
