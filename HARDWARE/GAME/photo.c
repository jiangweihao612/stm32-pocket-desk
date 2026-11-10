/*=============================================================================
 *  相机 + 相册（启动器第 1 页的应用）
 *
 *  相机
 *   +----------------------------------------------+
 *   |                                              |
 *   |            OV7725 实时预览                   |  0..195
 *   |              （320 x 240 帧）                |
 *   |                                              |
 *   |  +------+   +----------+   已存 PH0007       |  196..239
 *   |  | BACK |   | SHUTTER  |                     |
 *   +----------------------------------------------+
 *
 *  相册
 *   +----------------------------------------------+
 *   | ALBUM      12 PHOTOS                [ BACK ] |
 *   |  PH0012.BMP                                  |
 *   |  PH0011.BMP                                  |  每页 5 行
 *   |  ...                                         |
 *   | [ <PREV ]            1/3          [ NEXT> ]  |
 *   +----------------------------------------------+
 *
 *  列表页按键：K0 回主界面   K1 下一页   K2 上一页
 *
 *  点一下某一行就会全屏打开这张照片，完全不带任何按钮条：
 *      左滑 / 右滑  ->  下一张 / 上一张
 *      上滑 / 下滑  ->  第一张 / 最后一张
 *      K0 返回   K1 下一张   K2 上一张   WK_UP 删除（提示 1 秒）
 *
 *  存储：经 SDIO 访问 SD 卡，FatFs 根目录，320x240 24 位 BMP。
 *  图片是在预览之后从 LCD GRAM 回读的，所以存下来的
 *  正是屏幕上取景看到的内容（不会有旋转错位）。
 *===========================================================================*/

#include "photo.h"
#include "ui.h"
#include "lcd.h"
#include "delay.h"
#include "touch.h"
#include "key.h"
#include "ov7725.h"
#include "sdio_sdcard.h"
#include "ff.h"
#include "beep.h"
#include <stdio.h>
#include <string.h>

/*-------------------- 帧几何参数 --------------------*/
#define IMG_W       320
#define IMG_H       240
#define ROW_BYTES   (IMG_W * 3)         /* 960，已经是 4 字节对齐 */

/*-------------------- 蜂鸣器 -------------------------*/
/* 拍照和删除都响这么长时间。60ms 听起来是干脆的"滴"一声;
 * 觉得太短可以改到 100,太长 40。                           */
#define PH_BEEP_MS  60

/*-------------------- 颜色 ---------------------------*/
/* 和桌面同一套日落暖色：标题/底栏深咖，按钮暖沙，
 * 提示文字米白，出错用暖红。                        */
#define P_BAR       0x30E3              /* 深咖色条（标题/底栏） */
#define P_TXT       0xFF9B              /* 米白文字             */
#define P_DIM       0xB4D0              /* 暖灰次要文字         */
#define P_BTN       0xDE14              /* 按钮：暖沙           */
#define P_OK        0x6CC5              /* 成功：橄榄绿         */
#define P_ERR       0xE225              /* 失败：暖红           */
#define P_ROW1      0x3923              /* 列表奇数行           */
#define P_ROW2      0x4944              /* 列表偶数行           */
#define P_SEL       0xA285              /* 选中行：陶土         */

/*-------------------- 相机底栏布局 -----------------*/
#define CBAR_Y      196
#define CBAR_H      44
#define CB_BACK_X   6
#define CB_BACK_Y   202
#define CB_BACK_W   52
#define CB_BACK_H   22
#define CB_SHT_X    116
#define CB_SHT_Y    198
#define CB_SHT_W    84
#define CB_SHT_H    30
#define CB_ST_X     208
#define CB_ST_Y     202

/*-------------------- 相册布局 ----------------------*/
#define AL_ROWS     5
#define AL_ROW_X    8
#define AL_ROW_W    304
#define AL_ROW_Y0   32
#define AL_ROW_H    32
#define AL_ROW_DY   36
#define AL_BACK_X   258
#define AL_BACK_Y   4
#define AL_BACK_W   56
#define AL_BACK_H   20

#define AL_PREV_X   8
#define AL_PREV_Y   212
#define AL_PREV_W   64
#define AL_PREV_H   24
#define AL_NEXT_X   248
#define AL_NEXT_Y   212
#define AL_NEXT_W   64
#define AL_NEXT_H   24

/*-------------------- 看图手势 -------------------*/
#define VW_SWIPE    36                  /* 判定为滑动的最小位移(px) */

#define MAX_PHOTOS  48

/*-------------------- 状态 -----------------------------*/
static FATFS pfs;
static FIL   pfil;
static DIR   pdir;

static u8 sd_tried = 0;
static u8 sd_ok = 0;

/* 最后一次挂载失败的原因，例如 "SD INIT ERR 1" / "MOUNT ERR 3"，
 * 相册页和相机底栏都会把它显示出来，方便判断是卡的问题还是接线问题 */
static char sd_err[24] = "";

static u8  cam_white = 0;   /* 上一帧里接近纯白的像素百分比 */
static u16 cam_p0    = 0;   /* 上一帧左上角第一个像素       */
static u8 cam_tried = 0;
static u8 cam_ok = 0;

static char pname[MAX_PHOTOS][13];
static u16  pcount = 0;

/* 最后写入的文件名，显示在相机底栏 */
static char last_saved[13] = "";

/*=============================================================================
 *  FATFS / SD 卡
 *===========================================================================*/

/* 分步提示。SD 卡初始化、摄像头探测都是阻塞的，万一卡住，屏幕上会停在
 * 最后一句提示上，这样能一眼看出卡在哪一步，而不是只留一个 100% 的加载条。*/
static void ph_stage(char *line)
{
    UI_Fill(0, 0, ui_sw, ui_sh, P_BAR);
    UI_Center(80, 16, (char *)"PLEASE WAIT", P_DIM, P_BAR);
    UI_Center(112, 12, line, P_TXT, P_BAR);
}

/* 对 5 fps 的看图应用来说轮询模式足够了，还能避免 DMA 调度上的麻烦。
 *
 * SD_Init 和 f_mount 都是阻塞的，卡有问题时原来会一直重试（画面就停在
 * 加载页不动）。现在分两步做，失败时把返回码直接印在屏幕上：
 *   SD 错误码 : 1 命令响应超时 / 2 CRC 错 / 3 数据超时 / 5 接收上溢
 *   FRESULT   : 3 卡无响应 / 12 卷无工作区 / 13 没有文件系统
 * 这样一眼就能看出卡在哪一步、什么原因。                              */
u8 SD_Mount(void)
{
    u8   r;
    char buf[24];

    if (sd_tried) return sd_ok ? 0 : 1;

    sd_tried = 1;

    SD_SetDeviceMode(SD_POLLING_MODE);

    /* 成功时不再往屏上打进度条:Camera 页一进来就要出画面,
     * 那两行动静会先把整屏刷黑再等 1 秒。失败时才显示错误码。*/
    r = (u8)SD_Init();

    if (r)
    {
        sprintf(buf, "SD INIT ERR %u", (unsigned)r);
        strcpy(sd_err, buf);
        ph_stage(buf);
        delay_ms(1500);
        return 1;
    }

    r = (u8)f_mount(&pfs, "0:", 1);

    if (r != FR_OK)
    {
        sprintf(buf, "MOUNT ERR %u", (unsigned)r);
        strcpy(sd_err, buf);
        ph_stage(buf);
        delay_ms(1500);
        return 1;
    }

    sd_err[0] = 0;
    sd_ok = 1;

    return 0;
}

static u8 up_char(const char *p)
{
    char c = *p;

    if (c >= 'a' && c <= 'z') c = (char)(c - 32);

    return (u8)c;
}

/* ".BMP"，不区分大小写，卡上的 8.3 文件名本来就是大写 */
static u8 is_bmp(const char *s)
{
    u8 len = (u8)strlen(s);

    if (len < 5) return 0;
    if (s[len - 4] != '.') return 0;
    if (up_char(&s[len - 3]) != 'B') return 0;
    if (up_char(&s[len - 2]) != 'M') return 0;
    if (up_char(&s[len - 1]) != 'P') return 0;

    return 1;
}

/* PH0001.BMP ..：卡上还不存在的第一个名字 */
static void next_name(char *out)
{
    FILINFO fi;
    u16 n;

    fi.lfname = 0;
    fi.lfsize = 0;

    for (n = 1; n <= 9999; n++)
    {
        sprintf(out, "PH%04u.BMP", n);

        if (f_stat(out, &fi) != FR_OK) return;
    }

    strcpy(out, "PH9999.BMP");
}

/* 最新的排前面，这样新拍的照片会出现在相册最上面 */
static void sort_desc(u16 n)
{
    u16 i;
    u16 j;
    char t[13];

    for (i = 0; i + 1 < n; i++)
    {
        for (j = 0; j + 1 < n - i; j++)
        {
            if (strcmp(pname[j], pname[j + 1]) < 0)
            {
                strcpy(t, pname[j]);
                strcpy(pname[j], pname[j + 1]);
                strcpy(pname[j + 1], t);
            }
        }
    }
}

static u16 album_scan(void)
{
    FILINFO fi;
    u16 n = 0;

    pcount = 0;

    if (SD_Mount()) return 0;

    fi.lfname = 0;
    fi.lfsize = 0;

    if (f_opendir(&pdir, "/") != FR_OK) return 0;

    while (n < MAX_PHOTOS)
    {
        if (f_readdir(&pdir, &fi) != FR_OK) break;
        if (fi.fname[0] == 0) break;
        if (fi.fattrib & AM_DIR) continue;
        if (!is_bmp(fi.fname)) continue;

        strcpy(pname[n], fi.fname);
        n++;
    }

    f_closedir(&pdir);

    sort_desc(n);

    pcount = n;

    return n;
}

/*=============================================================================
 *  BMP 辅助函数
 *===========================================================================*/

static void put16(u8 *p, u16 v)
{
    p[0] = (u8)(v & 0xFF);
    p[1] = (u8)(v >> 8);
}

static void put32(u8 *p, u32 v)
{
    p[0] = (u8)(v & 0xFF);
    p[1] = (u8)((v >> 8) & 0xFF);
    p[2] = (u8)((v >> 16) & 0xFF);
    p[3] = (u8)((v >> 24) & 0xFF);
}

static u32 get32(const u8 *p)
{
    return ((u32)p[0]) | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* RGB565 -> 三个字节，按 B、G、R 顺序写入（BMP 顺序） */
static void pix_to_bgr(u16 c, u8 *o)
{
    u16 r = (u16)((c >> 11) & 0x1F);
    u16 g = (u16)((c >> 5) & 0x3F);
    u16 b = (u16)(c & 0x1F);

    o[0] = (u8)((b << 3) | (b >> 2));
    o[1] = (u8)((g << 2) | (g >> 4));
    o[2] = (u8)((r << 3) | (r >> 2));
}

/*=============================================================================
 *  OV7725 帧采集
 *===========================================================================*/

/* 等待 VSYNC 的一个上升沿；返回 1 = 超时 */
static u8 vsync_rising(void)
{
    u32 t = 0;

    while (OV7725_VSYNC)                    /* 等待低电平阶段   */
    {
        if (++t > 800000) return 1;
    }

    t = 0;

    while (!OV7725_VSYNC)                   /* 等待上升沿 */
    {
        if (++t > 800000) return 1;
    }

    return 0;
}

/* 复位 FIFO 读指针并打一拍时钟（AL422B 时序） */
static void fifo_read_reset(void)
{
    OV7725_RRST = 0;
    OV7725_RCK_L;
    OV7725_RCK_H;
    OV7725_RCK_L;
    OV7725_RRST = 1;
    OV7725_RCK_H;
}

/* 从 FIFO 取一个像素：两个字节，8 位总线 */
#define CAM_PIXEL(c)                              \
    do {                                          \
        OV7725_RCK_L;                             \
        (c)  = (u16)((GPIOC->IDR & 0xFF) << 8);   \
        OV7725_RCK_H;                             \
        OV7725_RCK_L;                             \
        (c) |= (u16)(GPIOC->IDR & 0xFF);          \
        OV7725_RCK_H;                             \
    } while (0)

static u8 cam_init(void)
{
    if (cam_tried) return cam_ok ? 0 : 1;

    cam_tried = 1;

    if (OV7725_Init() == 0)
    {
        /* 初始化表里 COM7=0x06 是 VGA,这里明确切到 QVGA 320x240,
         * 否则传感器按 640x480 出图,FIFO 一行 640 个像素,而屏幕
         * 一行只写 320 个 —— 画面会被横切成两半、上下错位。      */
        OV7725_Window_Set(IMG_W, IMG_H, 0);

        cam_ok = 1;
    }

    return cam_ok ? 0 : 1;
}

/* 把一整帧抓进 FIFO：在一帧开始时复位写指针，
 * 使能写入，然后在下一帧开始时停止                                       */
static u8 cam_grab(void)
{
    if (vsync_rising()) return 1;

    OV7725_WRST = 0;
    OV7725_WRST = 1;
    OV7725_WREN = 1;                        /* 让 FIFO 开始填充   */

    if (vsync_rising()) return 1;

    OV7725_WREN = 0;                        /* 一帧已抓完       */

    return 0;
}

/* 一帧直接刷到屏幕上。
 *
 * 用 U2D_L2R 是因为传感器在模块上旋转了 90 度。
 *
 * 返回 0:画面里确实有内容;1:整屏几乎全白,说明 D0~D7 没有被 FIFO 驱动
 * (OE 没拉低 / 数据线没接 / 模块没供电)。同时把白色像素占比记在
 * cam_white 里,诊断页直接显示出来,不用再靠猜。                     */
static u8 cam_frame(void)
{
    u16 i;
    u16 j;
    u16 c;
    u16 first = 0;
    u32 white = 0;
    u32 diff  = 0;

    /* 必须用横屏默认方向!
     * U2D_L2R 在【竖屏】下是 320 列 x 240 行,正好配 320/行的写入;
     * 但本工程是【横屏】,同一个方向经 LCD_Scan_Dir 换算后变成
     * 240 列 x 320 行 —— 每写 240 个像素就换行,而循环还是 320 个,
     * 于是整幅图被斜着切开(现场看到的就是这个)。
     * 横屏下 L2R_U2D 才是 320 列 x 240 行,与摄像头帧完全对齐。  */
    LCD_Scan_Dir(DFT_SCAN_DIR);

    /* AL422B 的 /OE 低电平才把 D0~D7 推上总线。原厂例程从不碰它,
     * 是因为模块上 OE 直接接地;杜邦线引到 PG15 就必须自己拉低。 */
    OV7725_CS = 0;

    fifo_read_reset();

    /* 扫描方向一改,GRAM 地址计数器就乱了,必须先把光标归零,
     * 否则整帧会从上次画图停下的地方开始写(画面错位/回卷)。 */
    LCD_SetCursor(0, 0);
    LCD_WriteRAM_Prepare();

    for (i = 0; i < IMG_H; i++)
    {
        for (j = 0; j < IMG_W; j++)
        {
            CAM_PIXEL(c);

            if ((i == 0) && (j == 0)) first = c;

            diff |= (u32)(c ^ first);

            /* 三个分量都接近满量程 = 白。全白就是总线上的上拉电平 */
            if ((((c >> 11) & 0x1F) >= 30) &&
                (((c >> 5)  & 0x3F) >= 60) &&
                ((c & 0x1F) >= 30)) white++;

            LCD->LCD_RAM = c;
        }
    }

    /* 读完把输出关掉,否则 FIFO 会一直占着 PC0~PC7 */
    OV7725_CS = 1;

    /* LCD_Scan_Dir() 在旋转时会交换 lcddev.width/height，所以要手动把
     * 横屏的宽高和全屏窗口恢复回去。顺序
     * 很重要：先让 Scan_Dir() 完成交换，再重写窗口。                       */
    LCD_Scan_Dir(DFT_SCAN_DIR);

    lcddev.width  = IMG_W;
    lcddev.height = IMG_H;

    LCD_Set_Window(0, 0, IMG_W, IMG_H);

    cam_p0    = first;
    cam_white = (u8)((white * 100u) / ((u32)IMG_W * IMG_H));

    return ((cam_white >= 95) || (diff == 0)) ? 1 : 0;
}

/*=============================================================================
 *  把此刻屏幕上显示的画面保存下来
 *===========================================================================*/

/* 0 = 成功，1 = 无卡，2 = 创建失败，3 = 写错误 */
static u8 bmp_save(char *name)
{
    u8  hdr[54];
    static u8 row[ROW_BYTES];
    u16 x;
    u16 y;
    u16 c;
    UINT bw;
    FRESULT r;

    if (SD_Mount()) return 1;

    r = f_open(&pfil, name, FA_CREATE_ALWAYS | FA_WRITE);
    if (r != FR_OK) return 2;

    memset(hdr, 0, sizeof(hdr));

    hdr[0] = 'B';
    hdr[1] = 'M';
    put32(&hdr[2],  54u + (u32)ROW_BYTES * IMG_H);   /* 文件大小      */
    put32(&hdr[10], 54);                             /* 像素数据偏移   */
    put32(&hdr[14], 40);                             /* 信息头大小    */
    put32(&hdr[18], IMG_W);
    put32(&hdr[22], (u32)(0 - (long)IMG_H));         /* 自上而下的行  */
    put16(&hdr[26], 1);                              /* 色彩平面数         */
    put16(&hdr[28], 24);                             /* 每像素位数 */
    put32(&hdr[30], 0);                              /* BI_RGB 压缩方式         */
    put32(&hdr[34], (u32)ROW_BYTES * IMG_H);
    put32(&hdr[38], 2835);                           /* 72 dpi         */
    put32(&hdr[42], 2835);

    r = f_write(&pfil, hdr, 54, &bw);

    for (y = 0; (y < IMG_H) && (r == FR_OK); y++)
    {
        for (x = 0; x < IMG_W; x++)
        {
            c = LCD_ReadPoint(x, y);
            pix_to_bgr(c, &row[x * 3]);
        }

        r = f_write(&pfil, row, ROW_BYTES, &bw);
    }

    f_close(&pfil);

    return (r == FR_OK) ? 0 : 3;
}

/*=============================================================================
 *  在屏幕上显示一个 BMP 文件
 *===========================================================================*/

/* 0 = 成功，1 = 打开失败，2 = 不是 320x240 的 24 位 BMP */
static u8 bmp_show(char *name, u16 y0, u16 rows)
{
    u8  hdr[54];
    static u8 row[ROW_BYTES];
    long w;
    long h;
    long li;
    u16 bpp;
    u16 x;
    u16 y;
    u16 c;
    u16 b;
    u16 g;
    u16 r;
    UINT br;
    u32 stride;
    int  topdown;
    int  seek;
    FRESULT fr;

    if (SD_Mount()) return 1;

    fr = f_open(&pfil, name, FA_READ);
    if (fr != FR_OK) return 1;

    fr = f_read(&pfil, hdr, 54, &br);

    if ((fr != FR_OK) || (br != 54) || (hdr[0] != 'B') || (hdr[1] != 'M'))
    {
        f_close(&pfil);
        return 2;
    }

    w    = (long)get32(&hdr[18]);
    h    = (long)get32(&hdr[22]);
    bpp  = (u16)(get32(&hdr[28]) & 0xFFFF);

    topdown = (h < 0) ? 1 : 0;
    if (h < 0) h = -h;

    if ((w != IMG_W) || (h != IMG_H) || (bpp != 24))
    {
        f_close(&pfil);
        return 2;
    }

    stride = (u32)(((w * 3) + 3) & ~3L);

    LCD_Scan_Dir(DFT_SCAN_DIR);

    lcddev.width  = IMG_W;              /* Scan_Dir() 可能已经把宽高交换了 */
    lcddev.height = IMG_H;

    LCD_Set_Window(0, y0, IMG_W, rows);
    LCD_WriteRAM_Prepare();

    seek = 1;

    for (y = 0; y < rows; y++)
    {
        /* 自上而下的文件顺序写入，自下而上的则反向读取 */
        if (topdown) li = (long)y0 + (long)y;
        else         li = h - 1 - (long)y0 - (long)y;

        /* top-down 是递增的,第一次 seek 之后指针自然跟在下一行,
         * 不必每行都做一次 f_lseek —— 240 次定位会把整屏画得很慢,
         * 慢到能看见"从上往下一行行刷"的过程。 */
        if (seek || !topdown)
        {
            f_lseek(&pfil, 54u + (u32)li * stride);
            seek = 0;
        }

        fr = f_read(&pfil, row, stride, &br);

        if ((fr != FR_OK) || (br != stride)) break;

        for (x = 0; x < IMG_W; x++)
        {
            b = row[x * 3 + 0];
            g = row[x * 3 + 1];
            r = row[x * 3 + 2];

            c = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            LCD->LCD_RAM = c;
        }
    }

    f_close(&pfil);

    LCD_Set_Window(0, 0, IMG_W, IMG_H);

    return 0;
}

/*=============================================================================
 *  给别的模块用的两个出口
 *===========================================================================*/

/* FatFs 的 f_mount 是按逻辑驱动器 "0:" 注册的,同一时刻只能有一个
 * FATFS 对象指向它。文件浏览器如果自己再 f_mount 一次,就会把这里的
 * pfs 顶掉,之后相册再打开文件会返回 FR_NOT_ENABLED。
 * 所以挂载动作统一留在本文件里,别的模块只调用这个函数。 */
char *SD_LastError(void)
{
    return sd_err;
}

/* 把任意 320x240 的 24 位 BMP 全屏铺开(文件浏览器用)。
 * 0 = 成功,1 = 打不开,2 = 尺寸/位深不匹配 */
u8 Photo_ShowBMP(char *name)
{
    return bmp_show(name, 0, IMG_H);
}

/*=============================================================================
 *  相机页
 *===========================================================================*/

/* 拍照结果只在画面中央压一条,1 秒后消失 —— 不占画面,
 * 也不会把底栏那一块从预览里挖掉 */
static void cam_toast(char *txt, u8 ok)
{
    UI_Fill(24, 100, 272, 34, P_BAR);
    UI_Fill(24, 100, 272, 2, (u16)(ok ? P_OK : P_ERR));

    UI_Center(110, 16, txt, P_TXT, P_BAR);
}

static void cam_fail_page(char *line)
{
    u8 ev;

    UI_Fill(0, 0, ui_sw, ui_sh, P_BAR);
    UI_Center(70, 24, (char *)"CAMERA", P_TXT, P_BAR);
    UI_Center(110, 12, line, P_ERR, P_BAR);
    UI_Center(140, 12, (char *)"CHECK THE MODULE WIRING", P_DIM, P_BAR);
    UI_Center(170, 12, (char *)"PRESS ANY KEY TO GO BACK", P_DIM, P_BAR);

    UI_Wait_Release();

    for (;;)
    {
        ev = UI_Scan();

        if (ev) break;

        delay_ms(20);
    }

    UI_Wait_Release();
}

/* 相机没出图时的提示页:把"缺哪一路信号"直接写在屏幕上,
 * 比停在纯白画面里让人猜要清楚得多 */
static void cam_diag(char *l1, char *l2)
{
    char buf[28];

    UI_Fill(0, 0, ui_sw, ui_sh, P_BAR);

    UI_Center(44, 24, (char *)"CAMERA", P_TXT, P_BAR);
    UI_Center(80, 16, l1, P_ERR, P_BAR);
    UI_Center(104, 12, l2, P_TXT, P_BAR);

    /* 实测值比任何猜测都直接:WHITE 100% 就是总线全被上拉,
     * 也就是 FIFO 一个字节都没送出来 */
    sprintf(buf, "WHITE %u%%  P0=%04X", (unsigned)cam_white, (unsigned)cam_p0);
    UI_Center(130, 12, buf, P_DIM, P_BAR);

    UI_Center(156, 12, (char *)"KEY0 / KEY2 TO GO BACK", P_DIM, P_BAR);
}

static void cam_diag_draw(u8 st)
{
    if (st == 1)
    {
        cam_diag((char *)"NO VSYNC SIGNAL",
                 (char *)"CHECK PA8 (VSYNC) WIRE");
    }
    else
    {
        cam_diag((char *)"NO VIDEO DATA",
                 (char *)"CHECK PC0-PC7 / OE / WREN / RCK");
    }
}

void Camera_Run(void)
{
    u8  ev;
    u8  r;
    u8  st = 0;                  /* 0 有图 / 1 无 VSYNC / 2 无数据 */
    u8  ns;
    u16 tick = 0;                /* 每 30 轮重试一次抓帧,约 0.6s */
    u16 k;
    char msg[24];
    char name[13];

    UI_Init();

    sd_tried  = 0;              /* 每次都允许重新挂载  */
    cam_tried = 0;              /* 同时重新探测传感器 */

    if (cam_init())
    {
        cam_fail_page((char *)"NO OV7725 FOUND");
        return;
    }

    /* 先把卡挂上。等第一次按快门才去初始化 SDIO 的话,那一秒里
     * ph_stage() 会把刚抓好的画面从 GRAM 上盖掉,存出来就是提示页。*/
    if (SD_Mount() == 0)  strcpy(msg, "READY");
    else                  strcpy(msg, sd_err[0] ? sd_err : "NO SD CARD");

    /* 整屏 320x240 全部留给预览,不再有底栏 */
    LCD_Scan_Dir(DFT_SCAN_DIR);

    lcddev.width  = IMG_W;
    lcddev.height = IMG_H;

    LCD_Set_Window(0, 0, IMG_W, IMG_H);

    for (;;)
    {
        /* 0 = 有图,1 = 没有 VSYNC,2 = 没有数据 */
        ns = 0;

        if (cam_grab())
        {
            ns = 1;
        }
        else if ((st == 0) || (tick == 0))
        {
            /* 出过图就一直刷。没出图时每 tick 归零才重试一次,
             * 否则 cam_frame() 每轮都把屏刷白,诊断页留不住。     */
            ns = cam_frame();
            if (ns) ns = 2;
        }
        else
        {
            ns = st;
        }

        if (ns != st)
        {
            st = ns;

            if (st != 0) cam_diag_draw(st);

            UI_Wait_Release();
        }
        else if ((st != 0) && (tick == 0))
        {
            cam_diag_draw(st);          /* 重试那一帧刷白了,补回提示 */
        }

        ev = UI_Scan();

        /* KEY0(或 KEY2)返回主界面,任何状态下都有效 */
        if (ev & (UI_K0 | UI_K2)) break;

        /* 有画面时:WK_UP 或 触摸屏幕任意位置 = 拍照 */
        if ((st == 0) && (ev & (UI_WKUP | UI_TOUCH)))
        {
            /* 快门音放在存盘之前:写一张 225KB 的 BMP 在 1 位总线上要
             * 一秒多,等存完再响就没手感了。这 60ms 里没有任何东西去写
             * GRAM,所以回读出来的还是按下那一刻的画面,不受影响。   */
            BEEP_Beep(PH_BEEP_MS);

            next_name(name);

            /* 先存再提示 —— bmp_save() 是从 GRAM 回读的,
             * 提示先画上去就会被一起拍进照片里                */
            r = bmp_save(name);

            if (r == 0)
            {
                strcpy(last_saved, name);
                sprintf(msg, "SAVED %s", name);
            }
            else if (r == 1) strcpy(msg, "NO SD CARD");
            else if (r == 2) strcpy(msg, "CREATE FAIL");
            else             strcpy(msg, "WRITE ERROR");

            cam_toast(msg, (u8)(r == 0));

            /* 提示留 1 秒。这段时间不刷新预览,否则下一帧立刻盖掉它;
             * 但 KEY0/KEY2 仍然有效,按了就返回。                 */
            for (k = 0; k < 100; k++)
            {
                if (KEY0_DOWN || KEY2_DOWN) break;

                delay_ms(10);
            }

            if (KEY0_DOWN || KEY2_DOWN) break;

            UI_Wait_Release();
        }

        if (++tick >= 30) tick = 0;

        delay_ms(10);
    }

    UI_Wait_Release();
}

/*=============================================================================
 *  相册：列表页
 *===========================================================================*/

static void album_head(u16 total)
{
    char buf[24];

    UI_Fill(0, 0, ui_sw, 28, P_BAR);

    UI_Text(8, 6, 16, (char *)"ALBUM", P_TXT, P_BAR);

    sprintf(buf, "%u PHOTOS", total);
    UI_Text(108, 9, 12, buf, P_DIM, P_BAR);

    UI_Btn(AL_BACK_X, AL_BACK_Y, AL_BACK_W, AL_BACK_H, "BACK", P_BTN, BLACK, 12);
}

static void album_page(u16 page, u16 total)
{
    u16 i;
    u16 idx;
    u16 pages;
    char buf[24];

    pages = (u16)((total + AL_ROWS - 1) / AL_ROWS);

    UI_Fill(0, 28, ui_sw, ui_sh - 28, P_BAR);

    album_head(total);

    if (total == 0)
    {
        UI_Center(100, 12, (char *)"NO PHOTOS ON THE CARD", P_DIM, P_BAR);
        UI_Center(120, 12, (char *)"OPEN CAMERA AND TAKE ONE", P_DIM, P_BAR);
    }

    for (i = 0; i < AL_ROWS; i++)
    {
        idx = (u16)(page * AL_ROWS + i);
        if (idx >= total) break;

        UI_Fill(AL_ROW_X, AL_ROW_Y0 + i * AL_ROW_DY, AL_ROW_W, AL_ROW_H,
                (i & 1) ? P_ROW2 : P_ROW1);

        UI_Text(AL_ROW_X + 8, AL_ROW_Y0 + i * AL_ROW_DY + 10, 12,
                pname[idx], P_TXT, (i & 1) ? P_ROW2 : P_ROW1);
    }

    /* 底部导航 */
    UI_Fill(0, AL_PREV_Y, ui_sw, ui_sh - AL_PREV_Y, P_BAR);

    UI_Btn(AL_PREV_X, AL_PREV_Y, AL_PREV_W, AL_PREV_H, "<", P_BTN, BLACK, 12);
    UI_Btn(AL_NEXT_X, AL_NEXT_Y, AL_NEXT_W, AL_NEXT_H, ">", P_BTN, BLACK, 12);

    if (pages)
    {
        sprintf(buf, "%u/%u", page + 1, pages);
        UI_Center(AL_PREV_Y + 5, 12, buf, P_TXT, P_BAR);
    }
}

/*=============================================================================
 *  相册：全屏看图
 *===========================================================================*/

/* 0 = 显示成功,否则画一页提示(文件损坏 / 不是 320x240 24bit) */
static void vw_show(u16 idx)
{
    if (bmp_show(pname[idx], 0, IMG_H) == 0) return;

    UI_Fill(0, 0, ui_sw, ui_sh, P_BAR);

    UI_Center(88,  12, (char *)"CANNOT SHOW THIS FILE", P_ERR, P_BAR);
    UI_Center(110, 12, pname[idx], P_DIM, P_BAR);
    UI_Center(150, 12, (char *)"K0 BACK   K1/K2 SWITCH   WK_UP DELETE",
              P_DIM, P_BAR);
}

/* 删除结果提示:压在画面中央,由调用者负责重画照片把它擦掉 */
static void vw_toast(char *txt, u8 ok)
{
    UI_Fill(28, 96, 264, 34, P_BAR);
    UI_Fill(28, 96, 264, 2, (u16)(ok ? P_OK : P_ERR));

    UI_Center(106, 16, txt, P_TXT, P_BAR);
}

/* 0 = 停留(不会返回) 1 = 删掉了一张(调用者要重新扫描目录)
 * 2 = K0 回主界面(调用者直接退出相册)
 *
 * 整屏只放照片,没有任何按钮条。上一版的底栏本身就是"一闪一闪"的元凶:
 * `bar` 标志在画完之后没有清零,于是每 20ms 就把那条 40px 区域重刷一遍,
 * 看上去就是底部一直在闪。
 *
 *   触摸  左滑 = 下一张   右滑 = 上一张
 *         上滑 = 第一张   下滑 = 最后一张
 *   按键  K0 = 回主界面   K1 = 下一张   K2 = 上一张   WK_UP = 删除 */
static u8 album_view(u16 *pidx)
{
    u16 idx = *pidx;
    u16 tx, ty;
    u16 x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    u16 n;
    u8  k0, k1, k2, ku;
    u8  k0l = 0, k1l = 0, k2l = 0, kul = 0;
    u8  down = 0;
    u8  act;
    int dx, dy;
    int adx, ady;

    UI_Wait_Release();

    vw_show(idx);

    for (;;)
    {
        act = 0;

        /* ---- 按键，边沿触发 ---- */
        k0 = KEY0_DOWN ? 1 : 0;
        k1 = KEY1_DOWN ? 1 : 0;
        k2 = KEY2_DOWN ? 1 : 0;
        ku = WKUP_DOWN ? 1 : 0;

        if      (k0 && !k0l) act = 6;       /* 返回          */
        else if (ku && !kul) act = 5;       /* 删除        */
        else if (k1 && !k1l) act = 1;       /* 下一张          */
        else if (k2 && !k2l) act = 2;       /* 上一张      */

        k0l = k0;
        k1l = k1;
        k2l = k2;
        kul = ku;

        /* ---- touch: 记下按下点,松手时判定手势 ---- */
        if ((act == 0) && (tp_dev.scan != 0) && tp_dev.scan(0))
        {
            tx = tp_dev.x[0];
            ty = tp_dev.y[0];

            if (!down)                      /* 刚按下,记起点 */
            {
                down = 1;
                x0   = tx;
                y0   = ty;
            }

            x1 = tx;
            y1 = ty;
        }
        else if (down)                      /* 刚松手,算位移 */
        {
            down = 0;

            dx = (int)x1 - (int)x0;
            dy = (int)y1 - (int)y0;

            adx = (dx < 0) ? -dx : dx;
            ady = (dy < 0) ? -dy : dy;

            if (adx >= ady)
            {
                if      (dx <= -VW_SWIPE) act = 1;      /* 左滑:下一张   */
                else if (dx >=  VW_SWIPE) act = 2;      /* 右滑:上一张   */
            }
            else
            {
                if      (dy <= -VW_SWIPE) act = 3;      /* 上滑:第一张   */
                else if (dy >=  VW_SWIPE) act = 4;      /* 下滑:最后一张 */
            }
        }

        if (act == 0)
        {
            delay_ms(10);
            continue;
        }

        if (act == 6)                       /* K0:直接回主界面 */
        {
            *pidx = idx;
            UI_Wait_Release();
            return 2;
        }

        if (act == 5)                       /* WK_UP:删除当前照片 */
        {
            if (f_unlink(pname[idx]) == FR_OK)
            {
                BEEP_Beep(PH_BEEP_MS);      /* 删除成功才响,失败不响 */

                vw_toast((char *)"DELETED", 1);
                delay_ms(900);              /* 提示留 1 秒 */

                *pidx = idx;
                return 1;
            }

            vw_toast((char *)"DELETE FAILED", 0);
            delay_ms(900);

            vw_show(idx);                   /* 擦掉提示,照片重画 */
            continue;
        }

        n = idx;

        if ((act == 1) && ((u16)(idx + 1) < pcount)) n = (u16)(idx + 1);
        if ((act == 2) && (idx > 0))                 n = (u16)(idx - 1);
        if (act == 3)                                n = 0;
        if (act == 4)                                n = (u16)(pcount - 1);

        if (n != idx)                       /* 到头了就不重画,免得白闪 */
        {
            idx = n;
            vw_show(idx);
        }
    }
}

/*=============================================================================
 *  相册：入口
 *===========================================================================*/

void Album_Run(void)
{
    u8  ev;
    u8  r;
    u16 page = 0;
    u16 i;
    u16 idx;
    u16 total;
    u16 pages;

    UI_Init();

    sd_tried = 0;               /* 每次都允许重新挂载 */

    ph_stage((char *)"CHECKING SD CARD");

    if (SD_Mount())
    {
        UI_Fill(0, 0, ui_sw, ui_sh, P_BAR);
        UI_Center(80, 24, (char *)"ALBUM", P_TXT, P_BAR);
        UI_Center(114, 12, (char *)"NO SD CARD", P_ERR, P_BAR);
        UI_Center(138, 12, sd_err[0] ? sd_err : (char *)"INSERT A CARD AND RETRY", P_DIM, P_BAR);
        UI_Center(158, 12, (char *)"CHECK WIRING / USE FAT32", P_DIM, P_BAR);
        UI_Btn(AL_BACK_X, AL_BACK_Y + 10, AL_BACK_W, AL_BACK_H, "BACK", P_BTN, BLACK, 12);

        UI_Wait_Release();

        for (;;)
        {
            ev = UI_Scan();

            if (ev & (UI_K0 | UI_K2)) return;   /* K0 / K2 都能退出 */
            if (ev & UI_TOUCH) return;

            delay_ms(20);
        }
    }

    ph_stage((char *)"READING ROOT DIRECTORY");

    total = album_scan();

    for (;;)
    {
        pages = (u16)((total + AL_ROWS - 1) / AL_ROWS);
        if (pages == 0) pages = 1;
        if (page >= pages) page = (u16)(pages - 1);

        album_page(page, total);

        UI_Wait_Release();

        for (;;)
        {
            ev = UI_Scan();

            if (ev & UI_K0) return;             /* K0：回主界面 */

            if ((ev & UI_K1) && ((u16)(page + 1) < pages))
            {
                page++;                         /* K1：下一页   */
                break;                          /* 回外层重绘   */
            }

            if ((ev & UI_K2) && (page > 0))
            {
                page--;                         /* K2：上一页   */
                break;
            }

            if (ev & UI_TOUCH)
            {
                if (UI_Hit(ui_tx, ui_ty, AL_BACK_X, AL_BACK_Y, AL_BACK_W, AL_BACK_H))
                {
                    return;
                }

                if (UI_Hit(ui_tx, ui_ty, AL_PREV_X, AL_PREV_Y, AL_PREV_W, AL_PREV_H))
                {
                    if (page > 0) { page--; break; }
                }

                if (UI_Hit(ui_tx, ui_ty, AL_NEXT_X, AL_NEXT_Y, AL_NEXT_W, AL_NEXT_H))
                {
                    if ((u16)(page + 1) < pages) { page++; break; }
                }

                for (i = 0; i < AL_ROWS; i++)
                {
                    if (!UI_Hit(ui_tx, ui_ty, AL_ROW_X,
                                (u16)(AL_ROW_Y0 + i * AL_ROW_DY),
                                AL_ROW_W, AL_ROW_H))
                    {
                        continue;
                    }

                    if ((u16)(page * AL_ROWS + i) >= total) break;

                    idx = (u16)(page * AL_ROWS + i);

                    r = album_view(&idx);

                    if (r == 2) return;         /* K0:回主界面 */
                    if (r == 1) total = album_scan();

                    break;
                }

                break;                  /* 重绘列表 */
            }

            delay_ms(20);
        }
    }
}
