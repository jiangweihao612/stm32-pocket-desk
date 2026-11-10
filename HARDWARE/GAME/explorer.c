/*=============================================================================
 *  SD 卡文件浏览器（启动器第 1 页的 FILES 应用）
 *
 *   +--------------------------------------------------+
 *   | SD CARD   /                       12 ITEM        |  0..19 顶栏
 *   |--------------------------------------------------|
 *   |  [D] ..                                          |
 *   |  [D] PHOTOS                                      |  每行 22 px
 *   |  [F] PH0012.BMP                   225.0K         |  9 行 = 198 px
 *   |  [F] NOTE.TXT                       112B         |
 *   |  ...                                             |  20..219
 *   |--------------------------------------------------|
 *   | K0 EXIT   K1 UP   K2 DOWN   WK_UP OPEN           |  220..239 底栏
 *   +--------------------------------------------------+
 *
 *
 *  操作
 *    K0     返回主界面          K1 / K2   光标上移 / 下移
 *    WK_UP  打开                触摸      点哪一行就选中并打开哪一行
 *
 *  "打开"的含义
 *    目录    进入该目录（列表第一项 ".." 用于返回上一级，
 *            已经在根目录时不显示它）
 *    文件    320x240 的 24 位 BMP  ->  全屏显示这张图
 *            其它文件              ->  弹面板显示大小/属性/日期
 *
 *
 *  内存
 *    整个目录一次性读进一张表（上限 48 项），之后翻页、移动光标
 *    都只是改下标，不再碰 SD 卡 —— 在 SPI/SDIO 上反复 f_readdir 很慢，
 *    光标一动就重新遍历目录会明显卡顿。
 *
 *    长名只保留前 30 个字符：FatFs 开了 _USE_LFN，一个长名最多 255 字节，
 *    48 项全存下来要 12 KB，这里截断后整张表约 2.7 KB。
 *
 *
 *  两个必须注意的坑
 *
 *  1) 字库只有 ASCII
 *     font.h 里是 asc2_1206 / 1608 / 2412，只有 0x20~0x7E。
 *     GBK 的中文文件名直接送进 LCD_ShowString 会读越界乱码，
 *     所以扫描时把 >= 0x80 的字节一律换成 '?'。英文长名正常显示。
 *
 *  2) 拼路径要用 8.3 短名，不能用显示名
 *     显示名里的中文已经被换成 '?'，拿它去 f_open 会找不到文件。
 *     所以每一项同时存了 name[]（给人看）和 sname[]（给 FatFs 用）。
 *===========================================================================*/

#include "explorer.h"
#include "ui.h"
#include "lcd.h"
#include "delay.h"
#include "key.h"
#include "photo.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

/*-------------------- 布局 ----------------------------*/
#define X_BAR_H     20                  /* 顶栏 / 底栏高度              */
#define X_LIST_Y    20                  /* 列表第一行的 y               */
#define X_ROWS      9                   /* 一屏 9 行                    */
#define X_ROW_H     22                  /* 行高                         */
#define X_ROW_X     3
#define X_ROW_W     314

#define X_ICON_X    8                   /* 类型图标的左边界             */
#define X_NAME_X    26                  /* 文件名文字的左边界           */
#define X_SIZE_R    310                 /* 大小文字右对齐到这里         */

#define X_FOOT_Y    220                 /* 底栏顶部                     */

/*-------------------- 容量 ----------------------------*/
#define X_MAX_ENT   48                  /* 一个目录最多缓存多少项       */
#define X_NAME_MAX  30                  /* 显示名最多保留多少字符       */
#define X_PATH_MAX  64                  /* 目录路径缓冲                 */

#define X_LFN_BUF   256                 /* 长名工作缓冲（_MAX_LFN 255） */

/*-------------------- 颜色 ----------------------------*/
#define X_BAR       0x30E3              /* 深咖色条（和相机页一致）     */
#define X_BAR2      0x59A4              /* 面板标题条：暖棕             */
/* 列表用深色底。挑颜色时别凭直觉写 4 位十六进制：
 * RGB565 的绿分量是 6 位，值 4 就已经是 16 级亮度了，
 * 比如 0x0C63 看着"很暗"，实际 g=35，折算 8 位是 140 ——
 * 那是中绿色，不是深色。想压暗就得三个分量都小。 */
#define X_BG        0x1881              /* 列表底色：深咖 #1A100C       */
#define X_ROW_A     0x1881              /* 隔行条纹 A（与底色同）       */
#define X_ROW_B     0x20A1              /* 隔行条纹 B：稍亮的咖啡       */
#define X_SEL       0xA285              /* 光标行：陶土                 */
#define X_LINE      0x59A4              /* 1 像素分隔线                 */
#define X_PANEL     0x3923              /* 信息面板底色：暖棕           */
#define X_TXT       0xFF9B              /* 米白文字                     */
#define X_NAME      0xEEB7              /* 普通文件名：米色             */
#define X_DIM       0xB4D0              /* 暖灰次要文字                 */
#define X_DIR       0xF549              /* 文件夹图标：琥珀             */
#define X_FILE      0xCDC4              /* 文件图标：暖卡其             */
#define X_ERR       0xE225              /* 错误：暖红                   */

/*-------------------- 表项 ----------------------------*/
typedef struct
{
    char name[X_NAME_MAX + 1];          /* 显示名，非 ASCII 已换成 '?'  */
    char sname[13];                     /* 原始 8.3 短名，拼路径用       */
    u32  size;
    u16  fdate;                         /* FAT 日期 / 时间，信息面板用   */
    u16  ftime;
    u8   attr;
} XEnt;

static XEnt  x_ent[X_MAX_ENT];
static u16   x_cnt;                     /* 当前目录的项数               */
static u16   x_sel;                     /* 光标位置                     */
static u16   x_top;                     /* 列表第一行对应的项下标       */
static char  x_path[X_PATH_MAX];        /* 当前目录，"/" 或 "/PHOTOS"   */
static u8    x_err;                     /* 0 = 正常，1 = 卡/挂载失败，2 = 打不开目录 */
static u8    x_more;                    /* 1 = 目录项超过 X_MAX_ENT，只显示了一部分 */

/* _USE_LFN == 1（静态缓冲模式）要求调用者自己提供长名缓冲区，
 * 并把指针挂到 FILINFO 上，否则 f_readdir 只填 8.3 短名。 */
static char  x_lfn[X_LFN_BUF];

/*=============================================================================
 *  小工具
 *===========================================================================*/

static u16 x_len(char *s)
{
    u16 n = 0;
    while (s[n]) n++;
    return n;
}

/* 只保留可打印 ASCII。GBK 汉字的两个字节都在 0x80 以上，
 * 会各变成一个 '?'，长度看起来仍然对得上。 */
static void x_clean(char *dst, char *src)
{
    u16 i;

    for (i = 0; (i < X_NAME_MAX) && src[i]; i++)
    {
        u8 c = (u8)src[i];

        dst[i] = ((c < 0x20) || (c > 0x7E)) ? '?' : (char)c;
    }

    dst[i] = 0;
}

/* 路径太长时只显示尾部，前面加省略号，免得把顶栏挤爆 */
static char *x_tail(char *s, u8 keep)
{
    u16 n = x_len(s);

    return (n > keep) ? (s + n - keep) : s;
}

static u8 x_is_bmp(char *s)
{
    u16 n = x_len(s);

    if (n < 4) return 0;

    return ((s[n - 4] == '.') && (s[n - 3] == 'B') &&
            (s[n - 2] == 'M') && (s[n - 1] == 'P')) ? 1 : 0;
}

/* 大小转成一行短文字，最宽 "<DIR>" / "4095.9M" 都是 7 个字符 */
static void x_size_str(char *out, u32 sz, u8 isdir)
{
    if (isdir)
    {
        strcpy(out, "<DIR>");
        return;
    }

    if (sz < 1024UL)
    {
        sprintf(out, "%uB", (unsigned)sz);
    }
    else if (sz < (1024UL * 1024UL))
    {
        sprintf(out, "%u.%uK",
                (unsigned)(sz >> 10),
                (unsigned)(((sz & 1023UL) * 10UL) >> 10));
    }
    else
    {
        sprintf(out, "%u.%uM",
                (unsigned)(sz >> 20),
                (unsigned)(((sz & 0xFFFFFUL) * 10UL) >> 20));
    }
}

/* 把当前目录和短名拼成绝对路径：根目录不重复加 '/' */
static void x_full(char *out, char *sname)
{
    if (strcmp(x_path, "/") == 0) sprintf(out, "/%s", sname);
    else                          sprintf(out, "%s/%s", x_path, sname);
}

/*=============================================================================
 *  扫描目录
 *===========================================================================*/

/* 目录排在文件前面，同类按名字升序。
 * ".."(0x2E) 的 ASCII 比数字和字母都小，所以自然会留在第 0 位。 */
static u8 x_less(XEnt *a, XEnt *b)
{
    u8 da = (a->attr & AM_DIR) ? 1 : 0;
    u8 db = (b->attr & AM_DIR) ? 1 : 0;

    if (da != db) return (u8)(da > db);

    return (strcmp(a->name, b->name) < 0) ? 1 : 0;
}

static void x_sort(void)
{
    u16  i;
    u16  j;
    XEnt t;

    for (i = 1; i < x_cnt; i++)
    {
        t = x_ent[i];
        j = i;

        while ((j > 0) && x_less(&t, &x_ent[j - 1]))
        {
            x_ent[j] = x_ent[j - 1];
            j--;
        }

        x_ent[j] = t;
    }
}

/* 0 = 成功，1 = 卡没挂上，2 = 目录打不开 */
static u8 x_scan(void)
{
    DIR     dir;
    FILINFO fi;
    u8      r;

    x_cnt = 0;
    x_sel = 0;
    x_top = 0;
    x_more = 0;

    /* 挂载统一由 photo.c 负责：FatFs 的 "0:" 同一时刻只认一个
     * FATFS 对象，这里再 f_mount 一次会把相册和相机的挂载顶掉。 */
    if (SD_Mount()) return 1;

    fi.lfname = x_lfn;
    fi.lfsize = X_LFN_BUF;

    r = (u8)f_opendir(&dir, x_path);

    if (r != FR_OK) return 2;

    /* 非根目录先手工放一个 ".."，不依赖 FatFs 返回的那一条
     * （不同版本的 _FS_RPATH 配置对 "." / ".." 的处理不一样） */
    if (strcmp(x_path, "/") != 0)
    {
        strcpy(x_ent[0].name,  "..");
        strcpy(x_ent[0].sname, "..");
        x_ent[0].size  = 0;
        x_ent[0].fdate = 0;
        x_ent[0].ftime = 0;
        x_ent[0].attr  = AM_DIR;
        x_cnt = 1;
    }

    while (x_cnt < X_MAX_ENT)
    {
        x_lfn[0] = 0;                       /* 该项没有长名时保持空 */

        if (f_readdir(&dir, &fi) != FR_OK) break;
        if (fi.fname[0] == 0) break;        /* 读完了 */

        if (strcmp(fi.fname, ".") == 0)  continue;
        if (strcmp(fi.fname, "..") == 0) continue;   /* 上面自己造了 */

        /* 长名优先；没有长名（纯 8.3）就用短名。
         * 短名统一是大写，长名保留原样，显示时不做大小写转换。 */
        x_clean(x_ent[x_cnt].name, x_lfn[0] ? x_lfn : fi.fname);

        strcpy(x_ent[x_cnt].sname, fi.fname);

        x_ent[x_cnt].size  = fi.fsize;
        x_ent[x_cnt].fdate = fi.fdate;
        x_ent[x_cnt].ftime = fi.ftime;
        x_ent[x_cnt].attr  = fi.fattrib;

        x_cnt++;
    }

    /* 表满了再往后看一眼：还有别的项就把顶栏的计数写成 "48+"，
     * 免得用户以为目录里真的只有这么几个文件 */
    if (x_cnt >= X_MAX_ENT)
    {
        while (f_readdir(&dir, &fi) == FR_OK)
        {
            if (fi.fname[0] == 0) break;
            if (strcmp(fi.fname, ".") == 0)  continue;
            if (strcmp(fi.fname, "..") == 0) continue;

            x_more = 1;
            break;
        }
    }

    f_closedir(&dir);

    x_sort();

    return 0;
}

/*=============================================================================
 *  绘制
 *===========================================================================*/

static void x_draw_bar(void)
{
    char buf[24];
    u16  n;

    UI_Fill(0, 0, ui_sw, X_BAR_H, X_BAR);

    UI_Text(6, 4, 12, (char *)"SD CARD", X_TXT, X_BAR);

    /* 当前路径，太长只显示尾部 */
    UI_Text(56, 4, 12, x_tail(x_path, 30), X_DIM, X_BAR);

    /* 右侧项数 */
    sprintf(buf, "%u%s ITEM", (unsigned)x_cnt, x_more ? "+" : "");
    n = (u16)(x_len(buf) * 6);
    UI_Text((int)(X_SIZE_R - n + 4), 4, 12, buf, X_DIM, X_BAR);
}

static void x_draw_foot(void)
{
    UI_Fill(0, X_FOOT_Y, ui_sw, ui_sh - X_FOOT_Y, X_BAR);
    UI_Fill(0, X_FOOT_Y, ui_sw, 1, X_LINE);

    UI_Center(X_FOOT_Y + 4, 12,
              (char *)"K0 EXIT  K1 UP  K2 DOWN  WK_UP OPEN",
              X_DIM, X_BAR);
}

/* 画一行（调用者保证 idx 落在可视范围内） */
static void x_draw_row(u16 idx)
{
    u16  y = (u16)(X_LIST_Y + (idx - x_top) * X_ROW_H);
    u8   isdir = (x_ent[idx].attr & AM_DIR) ? 1 : 0;
    u16  bg;
    char sz[10];

    bg = (idx == x_sel) ? X_SEL : (((idx & 1) == 0) ? X_ROW_A : X_ROW_B);

    UI_Fill(X_ROW_X, y, X_ROW_W, X_ROW_H, bg);

    /* 图标：目录是带小凸起的文件夹，文件是带几道横线的纸片 */
    if (isdir)
    {
        UI_Fill(X_ICON_X, y + 8, 12, 9, X_DIR);
        UI_Fill(X_ICON_X, y + 6,  5, 3, X_DIR);
    }
    else
    {
        UI_Fill(X_ICON_X + 1, y + 6, 9, 11, X_FILE);
        UI_Fill(X_ICON_X + 4, y + 9,  4, 1, bg);
        UI_Fill(X_ICON_X + 4, y + 12, 4, 1, bg);
        UI_Fill(X_ICON_X + 4, y + 15, 3, 1, bg);
    }

    UI_Text(X_NAME_X, y + 5, 12, x_ent[idx].name,
            (u8)((idx == x_sel) ? X_TXT : X_NAME), bg);

    x_size_str(sz, x_ent[idx].size, isdir);
    UI_Text((int)(X_SIZE_R - x_len(sz) * 6), y + 5, 12, sz,
            (u8)((idx == x_sel) ? X_TXT : X_DIM), bg);
}

/* 整块重画：先铺底色，这样项数变少时不会留下上一屏的残行 */
static void x_draw_list(void)
{
    u16 i;

    UI_Fill(0, X_LIST_Y, ui_sw, X_FOOT_Y - X_LIST_Y, X_BG);

    for (i = 0; i < X_ROWS; i++)
    {
        u16 k = (u16)(x_top + i);

        if (k < x_cnt) x_draw_row(k);
    }

    if (x_cnt == 0)
    {
        UI_Center(X_LIST_Y + 66, 12, (char *)"EMPTY FOLDER", X_DIM, X_BG);
    }
}

/* 光标移出可视区时滚动一屏 */
static void x_follow(void)
{
    if (x_sel < x_top)
    {
        x_top = x_sel;
    }
    else if (x_sel >= (u16)(x_top + X_ROWS))
    {
        x_top = (u16)(x_sel - X_ROWS + 1);
    }
}

/*=============================================================================
 *  信息面板
 *===========================================================================*/

static void x_info(XEnt *e)
{
    u16  px = 26;
    u16  py = 46;
    u16  pw = 268;
    u16  ph = 148;
    u8   ev;
    char buf[32];
    char *type;

    if (e->attr & AM_DIR)       type = (char *)"FOLDER";
    else if (x_is_bmp(e->sname)) type = (char *)"BMP IMAGE";
    else                        type = (char *)"FILE";

    /* 面板是模态的：先把整屏刷成底色再画面板。
     * 这个屏没有 alpha 混合，做不出"把列表压暗"的效果，
     * 而面板又不可能宽到盖住列表右侧的大小区（那里右对齐到 310），
     * 于是边缝会露出半个 "225.0K" 的残字。干脆全遮住。 */
    UI_Fill(0, 0, ui_sw, ui_sh, X_BG);
    UI_Fill(0, 0, ui_sw, X_BAR_H, X_BAR);
    UI_Text(6, 4, 12, (char *)"SD CARD", X_TXT, X_BAR);

    UI_Fill(px, py, pw, ph, X_PANEL);

    UI_Fill(px, py, pw, 1, X_LINE);
    UI_Fill(px, py + ph - 1, pw, 1, X_LINE);
    UI_Fill(px, py, 1, ph, X_LINE);
    UI_Fill(px + pw - 1, py, 1, ph, X_LINE);

    /* 标题条 */
    UI_Fill(px + 1, py + 1, pw - 2, 20, X_BAR2);
    UI_Text(px + 8, py + 5, 12, e->name, X_TXT, X_BAR2);

    /* 字段行，18 px 一行 */
    UI_Text(px + 12, py + 32, 12, (char *)"SIZE", X_DIM, X_PANEL);
    x_size_str(buf, e->size, (u8)((e->attr & AM_DIR) ? 1 : 0));
    UI_Text(px + 76, py + 32, 12, buf, X_TXT, X_PANEL);

    UI_Text(px + 12, py + 50, 12, (char *)"TYPE", X_DIM, X_PANEL);
    UI_Text(px + 76, py + 50, 12, type, X_TXT, X_PANEL);

    UI_Text(px + 12, py + 68, 12, (char *)"DATE", X_DIM, X_PANEL);
    sprintf(buf, "%04u-%02u-%02u",
            1980 + (unsigned)(e->fdate >> 9),
            (unsigned)((e->fdate >> 5) & 0x0F),
            (unsigned)(e->fdate & 0x1F));
    UI_Text(px + 76, py + 68, 12, buf, X_TXT, X_PANEL);

    UI_Text(px + 12, py + 86, 12, (char *)"TIME", X_DIM, X_PANEL);
    sprintf(buf, "%02u:%02u:%02u",
            (unsigned)(e->ftime >> 11),
            (unsigned)((e->ftime >> 5) & 0x3F),
            (unsigned)((e->ftime & 0x1F) * 2));
    UI_Text(px + 76, py + 86, 12, buf, X_TXT, X_PANEL);

    UI_Text(px + 12, py + 104, 12, (char *)"SHORT", X_DIM, X_PANEL);
    UI_Text(px + 76, py + 104, 12, e->sname, X_TXT, X_PANEL);

    /* 提示行 */
    UI_Fill(px + 1, py + ph - 20, pw - 2, 19, X_BAR);
    UI_Center(py + ph - 17, 12, (char *)"K0 / TAP  CLOSE", X_DIM, X_BAR);

    /* 等一个"关闭"动作。WK_UP 在这里也当关闭用，
     * 免得手指按在面板上时只能用 K0 退出。 */
    UI_Wait_Release();

    for (;;)
    {
        ev = UI_Scan();

        if (ev & (UI_K0 | UI_K2 | UI_WKUP | UI_TOUCH))
        {
            UI_Wait_Release();
            return;
        }

        delay_ms(20);
    }
}

/*=============================================================================
 *  打开当前项
 *===========================================================================*/

static void x_push(char *sname)
{
    u16 n = x_len(x_path);
    u16 m = x_len(sname);
    u8  slash = (u8)((n > 1) ? 1 : 0);

    if ((u16)(n + slash + m) >= X_PATH_MAX) return;

    if (slash) x_path[n++] = '/';

    strcpy(x_path + n, sname);
}

static void x_pop(void)
{
    u16 n = x_len(x_path);

    while (n > 1)
    {
        n--;
        if (x_path[n] == '/') break;
    }

    if (n < 1) n = 1;

    x_path[n] = 0;
}

/* 取路径的最后一段："/A/PHOTOS" -> "PHOTOS" */
static void x_last_seg(char *out, char *path)
{
    u16 n = x_len(path);
    u16 i = n;

    while ((i > 0) && (path[i - 1] != '/')) i--;

    strcpy(out, path + i);
}

/* 返回 1 表示目录结构变了（调用者要整屏重画） */
static u8 x_open(void)
{
    XEnt *e;
    char  full[X_PATH_MAX + 16];
    char  seg[X_PATH_MAX];
    u16   i;
    u8    ev;

    if (x_cnt == 0) return 0;

    e = &x_ent[x_sel];

    /* ".."：退到上级，并把光标停在刚离开的那个目录上，
     * 否则每次退出来都要重新从第一行找起 */
    if (strcmp(e->name, "..") == 0)
    {
        x_last_seg(seg, x_path);

        x_pop();
        x_err = x_scan();

        for (i = 0; i < x_cnt; i++)
        {
            if (strcmp(x_ent[i].sname, seg) == 0)
            {
                x_sel = i;
                x_follow();
                break;
            }
        }

        return 1;
    }

    /* 子目录：进去。进不去（名字过长 / 没权限）就退回原目录，
     * 并且把光标放回刚才那一项，不要让它跳回第一行。 */
    if (e->attr & AM_DIR)
    {
        u16 keep_sel = x_sel;
        u16 keep_top = x_top;

        x_push(e->sname);

        if (x_scan() != 0)
        {
            x_pop();
            x_err = x_scan();

            x_sel = (u16)((keep_sel < x_cnt) ? keep_sel : 0);
            x_top = (u16)((keep_top + X_ROWS <= x_cnt) ? keep_top : 0);
        }

        return 1;
    }

    /* 文件：BMP 先试着当图片铺开（bmp_show 内部会校验
     * 必须正好 320x240 24 位，不符合就返回非 0，退回信息面板） */
    if (x_is_bmp(e->sname))
    {
        x_full(full, e->sname);

        if (Photo_ShowBMP(full) == 0)
        {
            /* 图片占满整屏，只在左下角留一个小标签 */
            UI_Fill(0, ui_sh - 16, 62, 16, 0x0000);
            UI_Text(6, ui_sh - 13, 12, (char *)"K0 BACK", 0xFFFF, 0x0000);

            UI_Wait_Release();

            for (;;)
            {
                ev = UI_Scan();

                if (ev & (UI_K0 | UI_K2 | UI_TOUCH)) break;

                delay_ms(20);
            }

            UI_Wait_Release();          /* 先松手，免得回到列表又被当成点击 */
            return 1;                   /* 图已经把屏幕盖掉了，要重画 */
        }
    }

    x_info(e);

    return 1;                           /* 面板也要擦掉 */
}

/*=============================================================================
 *  错误页
 *===========================================================================*/

static void x_error_page(void)
{
    char *msg;

    UI_Fill(0, 0, ui_sw, ui_sh, X_BG);

    UI_Fill(0, 0, ui_sw, X_BAR_H, X_BAR);
    UI_Text(6, 4, 12, (char *)"SD CARD", X_TXT, X_BAR);

    UI_Center(74, 16, (char *)"CANNOT OPEN", X_ERR, X_BG);

    if (x_err == 1)
    {
        msg = SD_LastError();
        UI_Center(102, 12, (msg[0] ? msg : (char *)"NO SD CARD"), X_DIM, X_BG);
    }
    else
    {
        UI_Center(102, 12, (char *)"DIRECTORY ERROR", X_DIM, X_BG);
    }

    /* 路径最长可以有 63 个字符 = 378 px，超过屏宽的一半会画到屏幕外，
     * 所以只显示尾部 40 个字符（240 px） */
    UI_Center(126, 12, x_tail(x_path, 40), X_DIM, X_BG);

    UI_Center(200, 12, (char *)"K0 / TAP  EXIT", X_DIM, X_BG);
}

/*=============================================================================
 *  主入口
 *===========================================================================*/

void Explorer_Run(void)
{
    u8  ev;
    u8  dirty;

    UI_Init();

    strcpy(x_path, "/");
    x_sel = 0;
    x_top = 0;

    x_err = x_scan();

    if (x_err)
    {
        x_error_page();

        UI_Wait_Release();

        for (;;)
        {
            ev = UI_Scan();

            if (ev & (UI_K0 | UI_TOUCH)) break;

            delay_ms(20);
        }

        UI_Wait_Release();              /* 回主界面前等松手，理由同一览页   */
        return;
    }

    dirty = 2;                          /* 2 = 连顶栏和底栏一起画 */

    for (;;)
    {
        if (dirty == 2)
        {
            x_draw_bar();
            x_draw_list();
            x_draw_foot();
            dirty = 0;
        }
        else if (dirty == 1)
        {
            x_draw_list();
            dirty = 0;
        }

        ev = UI_Scan();

        /* K0 退出：必须等按键松开再 return。
         * 手指从按下到松手大约 80~150 ms，如果这时直接返回，
         * 主界面的 menu_scan 里 k0_last 还停在上次的值（0），
         * 会把"还按着"误判成一次新的按下，于是刚回到桌面就
         * 立刻启动恐龙游戏。UI_Wait_Release() 会一直等到全部
         * 按键和触摸都松开，顺便把 UI 模块的边沿标志清零。 */
        if (ev & UI_K0)
        {
            UI_Wait_Release();
            return;
        }

        if (ev & UI_K1)
        {
            if (x_sel > 0)
            {
                x_sel--;
                x_follow();
                dirty = 1;
            }
        }

        if (ev & UI_K2)
        {
            if ((u16)(x_sel + 1) < x_cnt)
            {
                x_sel++;
                x_follow();
                dirty = 1;
            }
        }

        if (ev & UI_WKUP)
        {
            if (x_open()) dirty = 2;
        }

        if (ev & UI_TOUCH)
        {
            /* 只有点在列表区里才算选中某一行；
             * 点顶栏和底栏什么都不做，免得误触 */
            if ((ui_ty >= X_LIST_Y) && (ui_ty < X_FOOT_Y))
            {
                u16 idx = (u16)(x_top + (ui_ty - X_LIST_Y) / X_ROW_H);

                if (idx < x_cnt)
                {
                    x_sel = idx;
                    x_follow();

                    if (x_open()) dirty = 2;
                    else          dirty = 1;
                }
            }
        }

        delay_ms(20);
    }
}
