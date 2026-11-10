#ifndef __PHOTO_H
#define __PHOTO_H
#include "sys.h"

/* 启动器用的相机 + 相册应用（menu.c，第 1 页）
 *
 *   CAMERA ：全屏显示 OV7725 实时预览，按 SHUTTER 会把一张
 *            320x240 24 位 BMP 写入 SD 卡（SDIO + FatFs）。
 *   ALBUM  ：列出卡根目录下的 .BMP 文件，点一下即可
 *            全屏显示，并且可以在那里删除。
 *
 *  两个页面都会阻塞，直到按下各自的返回按钮（或 K0）为止。
 *  相册列表页：K0 回主界面，K1 下一页，K2 上一页。
 */
void Camera_Run(void);
void Album_Run(void);

/* 下面三个接口给文件浏览器(explorer.c)复用。
 *
 * 挂载必须共用:FatFs 的逻辑驱动器 "0:" 同一时刻只认一个 FATFS 对象,
 * 各模块各自 f_mount 会互相顶掉。所以 explorer 只调 SD_Mount(),
 * 不自己做 f_mount。                                      */
u8    SD_Mount(void);              /* 0 = 卡已就绪 */
char *SD_LastError(void);          /* 上次失败的说明,例如 "SD INIT ERR 27" */
u8    Photo_ShowBMP(char *name);   /* 0 = 成功,1 = 打不开,2 = 尺寸不符 */

#endif
