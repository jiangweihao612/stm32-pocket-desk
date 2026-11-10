#ifndef __EXPLORER_H
#define __EXPLORER_H
#include "sys.h"

/* SD 卡文件浏览器（启动器第 1 页的 FILES 应用）
 *
 *   列出当前目录下的全部文件和子目录，可以进目录、看文件信息，
 *   320x240 的 BMP 还能直接全屏看。
 *
 *   K0    返回主界面          K1 / K2   光标上移 / 下移
 *   WK_UP 打开                触摸点某一行 = 选中并打开
 *
 *   本函数会一直阻塞到按 K0 为止。
 */
void Explorer_Run(void);

#endif
