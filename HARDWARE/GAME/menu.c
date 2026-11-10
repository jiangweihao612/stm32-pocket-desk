/*=============================================================================
 *  手机风格桌面（启动器）—— 三页
 *
 *   第一页（状态）                      第二页（应用）
 *   +--------------------------+        +--------------------------+
 *   | 09:41:07  2026-11-02  .. |        | 09:41:07  2026-11-02  .. |
 *   | +----------------------+ |        |                          |
 *   | | TEMP | PRESS  | ALT  | |        | [dino] [plane] [snake]   |
 *   | | DHT11 strip          | |        | [heart]                  |
 *   | | MQ-2  strip          | |        |  DINO   PLANE  SNAKE     |
 *   | +----------------------+ |        |         HEART            |
 *   |        [settings]        |        |                          |
 *   |  SWIPE LEFT FOR APPS     |        |  SWIPE RIGHT FOR STATUS  |
 *   +--------------------------+        +--------------------------+
 *
 *  水平拖动超过 60 px 即切换页面。
 *  点击图标会播放一段简短的加载界面，
 *  随后打开该游戏的主页，或者打开 MAX30102 心率监测。
 *  第一页上的 SETTINGS 图标用于修改 6 位锁屏密码。
 *
 *  桌面上的按键：
 *      K0    = 回到登录界面（重新上锁，输对密码才回来）
 *      K1    = 下一页（与左滑等价，三页循环）
 *      K2    = 上一页（与右滑等价，三页循环）
 *      WK_UP = 直接打开 CAMERA
 *
 *  第三页（工具）放 DAC 波形输出：PA4(DAC1) / PA5(DAC2) 两路，
 *  波形有正弦 / 方波 / 三角 / 锯齿 / 噪声，频率 8 档（50 Hz ~ 10 kHz），
 *  输出界面用两个坐标系分别显示两路曲线，设置界面可改引脚 / 波形 / 频率。
 *===========================================================================*/

#include "menu.h" /* 引入菜单模块头文件 */
#include "game.h" /* 引入游戏公共头文件 */
#include "plane.h" /* 引入飞机大战头文件 */
#include "snake.h" /* 引入贪吃蛇头文件 */
#include "heart.h" /* 引入心率监测头文件 */
#include "lock.h" /* 引入锁屏模块头文件 */
#include "photo.h" /* 引入相册/照片模块头文件 */
#include "explorer.h" /* 引入文件浏览器头文件 */
#include "dacapp.h" /* 引入DAC波形应用头文件 */
#include "bmp180.h" /* 引入BMP180气压传感器头文件 */
#include "mq2.h" /* 引入MQ-2气体传感器头文件 */
#include "DHT11.h" /* 引入DHT11温湿度传感器头文件 */
#include "rtc.h" /* 引入RTC实时时钟头文件 */
#include "lcd.h" /* 引入LCD驱动头文件 */
#include "key.h" /* 引入按键驱动头文件 */
#include "delay.h" /* 引入延时函数头文件 */
#include "touch.h" /* 引入触摸屏驱动头文件 */
#include <stdio.h> /* 引入标准输入输出库 */
#include <string.h> /* 引入字符串处理库 */

/*-------------------- 颜色（RGB565） ------------*/
/* 主题：日落暖色。整体由冷青改成暖棕 / 陶土 / 琥珀，
 * 壁纸是一条从深咖到暖橙的垂直渐变，共 7 条色带。
 * 挑色时注意 RGB565 的绿分量是 6 位，填值时三个分量都要小
 * 才能真正压暗，否则很容易出现荧光绿那种刺眼效果。      */
#define C_WALL0         0x30E4  /* 壁纸渐变，顶部深咖 #331E22 ...    */
#define C_WALL1         0x4944
#define C_WALL2         0x5984
#define C_WALL3         0x71E5
#define C_WALL4         0x8A25
#define C_WALL5         0xA285
#define C_WALL6         0xBAC5  /* ... 底部暖橙 #B85A2E              */
#define C_BAR           0x30E3  /* 状态栏：深咖啡                    */
#define C_DOCK          0x20A1  /* 底部 dock 栏：更深的咖啡          */
#define C_TILE          0xFF9B  /* 浅色应用图标块：奶油              */
#define C_TILE_SH       0x8B4A  /* 图标块阴影边                      */
#define C_GRAY          0xCDB2  /* 沙色应用图标块 / 相册照片区       */
#define C_BLUE          0xC2A5  /* 陶土色应用图标块（原先是蓝色块）  */
#define C_INK           0x20A2  /* 深色图形：深咖啡                  */
#define C_GREEN         0xE4C7  /* 进度条 / 电量条：琥珀             */
#define C_TEXT          0xFF9B  /* 主文字：米白                      */
#define C_DIM           0xB4D0  /* 暗色圆点 / 次要文字：暖灰         */

/*-------------------- 传感器卡片颜色 ----------*/
#define C_CARD          0xFFBD  /* 奶油白卡片主体                    */
#define C_CARD_TOP      0xFFFF  /* 1 像素顶部高光                    */
#define C_CARD_EDGE     0xCD51  /* 1 像素边框：暖棕                  */
#define C_CARD_SH       0x28C2  /* 投影，深咖                        */
#define C_CARD_SPLIT    0xDE15  /* 列分隔线                          */
#define C_LABEL         0x6A46  /* 小号标签文字：暖棕                */
#define C_REF           0x9BCB  /* 更暗的标签文字                    */
#define C_VALUE         0x28C2  /* 读数数字：近黑棕                  */
#define C_AMBER         0xECA7  /* 颜色标记：温度                    */
#define C_ACC_BLUE      0x8B49  /* 颜色标记：气压（改暖棕）          */
#define C_ACC_GREEN     0x6C67  /* 颜色标记：海拔（橄榄绿）          */
#define C_OK            0x6CC5  /* "在线"圆点                        */
#define C_ERR           0xE225  /* "离线"圆点 / 危险                 */
#define C_GAS_TAB       0xECA7  /* 颜色标记：可燃气体                */
#define C_BAR_BG        0xEEB7  /* 等级条的空槽部分                  */
#define C_GAS_MID       0xF549  /* 等级条：报警                      */
#define C_CLIM_TAB      0xDC45  /* 颜色标记：温度/湿度               */

/*-------------------- 桌面布局 -------------*/
#define BAR_H           20 /* 状态栏高度 */
#define DOCK_Y          200 /* 底部dock栏起始Y坐标 */
#define ICON_SZ         48 /* 应用图标尺寸 */

#define APP_DINO_X      16 /* 恐龙图标X坐标 */
#define APP_PLANE_X     96 /* 飞机图标X坐标 */
#define APP_SNAKE_X     176 /* 贪吃蛇图标X坐标 */
#define APP_HEART_X     256 /* 心率图标X坐标 */

/* 第一页：上方是传感器卡片，下方是四个应用图标。
 * 320 px 宽放 4 个 48 px 图标，间隔 74：左边留 24，右边留 26。 */
#define P1_ICON_Y       136 /* 第一页图标Y坐标 */
#define P1_LABEL_Y      187     /* 位于壁纸色带 6 内 (170..199)       */
#define ICON_SET_X      24      /* SETTINGS / ALBUM / CAMERA / FILES 四列图标 */
#define ICON_ALB_X      98 /* 相册图标X坐标 */
#define ICON_CAM_X      172 /* 相机图标X坐标 */
#define ICON_FIL_X      246 /* 文件图标X坐标 */

/* 第二页：四个启动器图标 */
#define P2_ICON_Y       72 /* 第二页图标Y坐标 */
#define P2_LABEL_Y      122     /* 位于壁纸色带 4 内 (120..144)       */

/* 第三页：目前只有 DAC 波形输出一个图标，居中摆放 */
#define APP_DAC_X       136 /* DAC图标X坐标 */
#define P3_ICON_Y       76 /* 第三页图标Y坐标 */
#define P3_LABEL_Y      126 /* 第三页标签Y坐标 */

/*-------------------- 传感器卡片布局 ----------*/
/* 第一页把卡片放在顶部，紧贴状态栏下方。卡片分为三条：
 * BMP180 三列读数、DHT11 气候条和 MQ-2 气体条，
 * 最后还有一行状态信息。
 *
 *   +------------------------------------------------+
 *   | | TEMP      | PRESS        | ALT               |
 *   | |  23.7 C   |  963.6 hPa   |  96.2 m           |
 *   |------------------------------------------------|
 *   | |DHT11 26C 58%RH [######----]       COMFORT   |
 *   | |MQ-2 ADC 1842 1.48V 9ppm [###]      TRACE    |
 *   | o BMP180 ONLINE               REF 1013.25hPa  |
 *   +------------------------------------------------+
 */
#define CARD_X          8 /* 传感器卡片X坐标 */
#define CARD_Y          28 /* 传感器卡片Y坐标 */
#define CARD_H          99 /* 传感器卡片高度 */
#define CARD_COL1       12 /* 第一列X坐标 */
#define CARD_COL2       112 /* 第二列X坐标 */
#define CARD_COL3       212 /* 第三列X坐标 */
#define CARD_LBL_DY     6       /* 标题行，位于卡片内部             */
#define CARD_VAL_DY     20      /* 读数行                           */
#define CARD_RULE_DY    40      /* 各条上方的细线                   */
#define CARD_FTR_DY     78      /* 状态行                           */
#define CARD_VAL_DX     8       /* 读数与对应标题左对齐                         */
#define CARD_SPLIT1     104     /* 列分隔线                         */
#define CARD_SPLIT2     204 /* 第二列分隔线X坐标 */

/*-------------------- DHT11 条 -----------------*/
#define DHT_ROW_DY      44 /* DHT11行相对卡片Y偏移 */
#define DHT_TAB_DX      12 /* DHT11颜色标记X偏移 */
#define DHT_CAP_DX      20      /* "DHT11" 文本位置                 */
#define DHT_T_DX        56      /* "26C" 文本位置                   */
#define DHT_H_DX        82      /* "58%RH" 文本位置                 */
#define DHT_BAR_DX      118 /* 湿度条X偏移 */
#define DHT_BAR_W       100 /* 湿度条宽度 */
#define DHT_BAR_H       10 /* 湿度条高度 */
#define DHT_ST_DX       226     /* "DRY" / "COMFORT" / "HUMID" 文本位置 */

/*-------------------- MQ-2 条 ------------------*/
#define GAS_ROW_DY      60      /* 12 像素高的一行，偏移量相对卡片      */
#define GAS_TAB_DX      12 /* 气体颜色标记X偏移 */
#define GAS_CAP_DX      20      /* "MQ-2" 文本位置                  */
#define GAS_ADC_DX      50      /* "ADC 1842" 文本位置              */
#define GAS_VOL_DX      104     /* "1.48V" 文本位置                 */
#define GAS_PPM_DX      140     /* "1394ppm" 文本位置               */
#define GAS_BAR_DX      188 /* 气体等级条X偏移 */
#define GAS_BAR_W       62 /* 气体等级条宽度 */
#define GAS_BAR_H       10 /* 气体等级条高度 */
#define GAS_ST_DX       256     /* "CLEAN" / "DANGER" 文本位置      */

/* 20 ms 空闲循环每执行 N 遍做一次测量（约 35 ms） */
#define SENSOR_TICKS    50 /* 传感器刷新计数阈值 */

/*-------------------- 加载界面 --------------*/
#define PROG_X          60 /* 进度条X坐标 */
#define PROG_Y          150 /* 进度条Y坐标 */
#define PROG_W          200 /* 进度条宽度 */
#define PROG_H          18 /* 进度条高度 */
#define STAGE_Y         126 /* 加载阶段文字Y坐标 */
#define PCT_Y           176 /* 百分比文字Y坐标 */

/*-------------------- 霸王龙图形 ----------------*/
/* 与游戏中 draw_dino() 使用的矩形完全一致（40 x 46） */
static const u8 dino_rect[11][4] = /* 恐龙矩形块表 */
{
    { 0,  8,  5,  4}, { 4, 10,  9,  7}, /* 恐龙矩形块 */
    {11,  8, 16, 26}, /* 恐龙矩形块 */
    {21,  6,  6,  4}, {23,  0, 17, 14}, /* 恐龙矩形块 */
    {29, 13, 11,  5}, /* 恐龙矩形块 */
    {26, 20,  5,  3}, /* 恐龙矩形块 */
    {13, 30,  6, 12}, {13, 42,  9,  4}, /* 恐龙矩形块 */
    {21, 30,  6, 12}, {21, 42,  9,  4}, /* 恐龙矩形块 */
};
#define DINO_W          40 /* 恐龙图形宽度 */
#define DINO_H          46 /* 恐龙图形高度 */

/* plane.c 使用的飞机精灵图（24 x 26） */
static const u8 plane_rect[5][4] = /* 飞机矩形块表 */
{
    {10,  0,  4,  4}, { 9,  4,  6, 18}, { 1, 13, 22, 4}, /* 飞机矩形块 */
    { 5, 20, 14,  3}, {11,  7,  2,  4}, /* 飞机矩形块 */
};

/* 盘绕的蛇，48 x 48 图标区域 */
static const u8 snake_rect[12][4] = /* 蛇矩形块表 */
{
    {10, 10, 7, 7}, {18, 10, 7, 7}, {26, 10, 7, 7}, {34, 10, 7, 7}, /* 蛇矩形块 */
    {34, 18, 7, 7}, /* 蛇矩形块 */
    {26, 18, 7, 7}, {18, 18, 7, 7}, {10, 18, 7, 7}, /* 蛇矩形块 */
    {10, 26, 7, 7}, /* 蛇矩形块 */
    {18, 26, 7, 7}, {26, 26, 7, 7}, /* 蛇矩形块 */
    {28, 34, 9, 9}, /* 蛇矩形块 */
};

static u16 sw = 320; /* 屏幕宽度 */
static u16 sh = 240; /* 屏幕高度 */

static u8  tp_down = 0;         /* 手指当前是否按在屏上              */
static u16 tp_x = 0;            /* 最后位置 / 点击位置               */
static u16 tp_y = 0; /* 最后触摸Y坐标 */
static u16 tp_x0 = 0;           /* 手指按下时的位置                  */
static u16 tp_y0 = 0; /* 手指按下时Y坐标 */

/* 上一次 BMP180 读数，保留它是为了重绘卡片时不必重新测量
 * （每次游戏退出后桌面都会重画） */
static u8  sen_ready = 0;       /* 1：初始化时芯片有响应               */
static u8  sen_valid = 0;       /* 1：至少完成过一次测量               */
static float sen_temp = 0.0f; /* 上次温度读数 */
static float sen_pres = 0.0f; /* 上次气压读数 */
static float sen_alt  = 0.0f; /* 上次海拔读数 */

/* 上一次 MQ-2 读数（PA6 / ADC1_IN6） */
static MQ2_Data gas; /* MQ-2读数缓存 */
static u8 gas_valid = 0;        /* 1：AO 输出正常（不是 0 / 未饱和）      */

/* 上一次 DHT11 读数（PA15，单总线） */
static u8 dht_t = 0; /* DHT11温度缓存 */
static u8 dht_h = 0; /* DHT11湿度缓存 */
static u8 dht_valid = 0;        /* 1：上次读取通过了校验和          */
static u8 dht_fail = 0;         /* 连续失败次数                     */

/*----------------- 重绘记录 --------------------------------
 * 以前每次刷新都会重写整条内容，即使数字完全一样：
 * 每秒三个 10 字符字段加上两行传感器数据，
 * 每一次刷新都是一串 LCD 写操作。下面的缓存让某一条在
 * 格式化内容没变时可以被跳过，这样数字显示非常稳定，
 * 同时把空闲循环留给触摸扫描和时钟刷新。
 *
 * 只有在 draw_sensor_card() 从头重画整张卡片时（切页、退出应用），
 * card_full 才为 1：因为填充卡片主体会擦掉所有文字，
 * 所以此时必须把每个缓存都忽略一次。 */
static u8 card_full = 1; /* 整卡重绘标志 */

static char rd_last[3][12];     /* 每个读数列上次绘制的文本           */
static u8   rd_have[3] = { 0, 0, 0 }; /* 各读数列是否有缓存 */

static u8   dh_have = 0;        /* 上次送到 LCD 的 DHT11 行            */
static u8   dh_v = 0; /* DHT11行有效缓存 */
static u8   dh_t = 0; /* DHT11行温度缓存 */
static u8   dh_h = 0; /* DHT11行湿度缓存 */

static MQ2_Data gas_last;       /* 上次送到 LCD 的 MQ-2 行             */
static u8   gas_have = 0; /* 气体行是否有缓存 */
static u8   gas_lv = 0; /* 气体行有效缓存 */

/*=============================================================================
 *  小的绘图辅助函数
 *===========================================================================*/

static u8 menu_scan(void); /* 扫描菜单输入事件 */
static void draw_page1(void); /* 绘制第一页 */
static void draw_page2(void); /* 绘制第二页 */
static void draw_page3(void); /* 绘制第三页 */
static void draw_sensor_card(void); /* 绘制传感器卡片 */
static void sensor_refresh(void); /* 刷新BMP180读数 */
static void gas_init(void); /* 初始化MQ-2 */
static void gas_refresh(void); /* 刷新MQ-2读数 */
static void dht_init(void); /* 初始化DHT11 */
static void dht_refresh(void); /* 刷新DHT11读数 */
static void draw_dht_row(void); /* 绘制DHT11行 */

static void fill(int x, int y, int w, int h, u16 color) /* 带裁剪的矩形填充 */
{
    if (w <= 0 || h <= 0) return; /* 宽高非法直接返回 */

    if (x < 0) { w += x; x = 0; } /* 左侧越界裁剪 */
    if (y < 0) { h += y; y = 0; } /* 顶部越界裁剪 */
    if (w <= 0 || h <= 0) return; /* 裁剪后非法返回 */

    if (x + w > (int)sw) w = (int)sw - x; /* 右侧越界裁剪 */
    if (y + h > (int)sh) h = (int)sh - y; /* 底部越界裁剪 */
    if (w <= 0 || h <= 0) return; /* 裁剪后非法返回 */

    LCD_Fill((u16)x, (u16)y, (u16)(x + w - 1), (u16)(y + h - 1), color); /* 填充矩形 */
}

static u8 in_rect(u16 x, u16 y, u16 rx, u16 ry, u16 rw, u16 rh) /* 判断点是否在矩形内 */
{
    return (x >= rx && x < (u16)(rx + rw) && y >= ry && y < (u16)(ry + rh)) ? 1 : 0; /* 返回命中结果 */
}

static void text_at(int x, int y, u8 size, char *s, u16 fg, u16 bg) /* 在指定位置显示文字 */
{
    POINT_COLOR = fg; /* 设置文字前景色 */
    BACK_COLOR  = bg; /* 设置文字背景色 */
    LCD_ShowString((u16)x, (u16)y, 200, size + 4, size, (u8 *)s); /* 显示字符串 */
}

static void center_str(int y, u8 size, char *s, u16 fg, u16 bg) /* 居中显示字符串 */
{
    u16 len = 0; /* 字符串长度 */
    char *p = s; /* 遍历指针 */
    u16 x; /* 计算出的X坐标 */

    while (*p++) len++; /* 统计字符数 */

    x = (sw > len * (size / 2)) ? (u16)((sw - len * (size / 2)) / 2) : 0; /* 计算居中X坐标 */
    text_at(x, y, size, s, fg, bg); /* 显示居中文字 */
}

/* 实心圆（中点算法，按水平线段填充） */
static void fill_circle(int cx, int cy, int r, u16 color) /* 绘制实心圆 */
{
    int x = r; /* 圆算法X初值 */
    int y = 0; /* 圆算法Y初值 */
    int err = 1 - r; /* 中点误差初值 */

    while (x >= y) /* 八分圆循环 */
    {
        fill(cx - x, cy + y, 2 * x + 1, 1, color); /* 填充一段水平线 */
        fill(cx - x, cy - y, 2 * x + 1, 1, color); /* 填充一段水平线 */
        fill(cx - y, cy + x, 2 * y + 1, 1, color); /* 填充一段水平线 */
        fill(cx - y, cy - x, 2 * y + 1, 1, color); /* 填充一段水平线 */

        y++; /* Y步进 */
        if (err < 0) /* 误差小于0 */
        {
            err += 2 * y + 1; /* 更新误差 */
        }
        else /* 误差不小于0 */
        {
            x--; /* X步进 */
            err += 2 * (y - x) + 1; /* 更新误差 */
        }
    }
}

static void draw_dino_glyph(int ox, int oy, u16 color) /* 绘制恐龙图形 */
{
    u8 i; /* 矩形块索引 */

    for (i = 0; i < sizeof(dino_rect) / sizeof(dino_rect[0]); i++) /* 遍历恐龙矩形块 */
    {
        fill(ox + dino_rect[i][0], oy + dino_rect[i][1], /* 计算矩形X/Y */
             dino_rect[i][2],     dino_rect[i][3],     color); /* 填充矩形块 */
    }
}

static void draw_plane_glyph(int ox, int oy, u16 color) /* 绘制飞机图形 */
{
    u8 i; /* 矩形块索引 */

    for (i = 0; i < sizeof(plane_rect) / sizeof(plane_rect[0]); i++) /* 遍历飞机矩形块 */
    {
        fill(ox + plane_rect[i][0], oy + plane_rect[i][1], /* 计算矩形X/Y */
             plane_rect[i][2],     plane_rect[i][3],     color); /* 填充矩形块 */
    }
}

static void draw_snake_glyph(int ox, int oy, u16 color) /* 绘制蛇图形 */
{
    u8 i; /* 矩形块索引 */

    for (i = 0; i < sizeof(snake_rect) / sizeof(snake_rect[0]); i++) /* 遍历蛇矩形块 */
    {
        fill(ox + snake_rect[i][0], oy + snake_rect[i][1], /* 计算矩形X/Y */
             snake_rect[i][2],     snake_rect[i][3],     color); /* 填充矩形块 */
    }
}

/* 心形：两个圆瓣 + 一个向下收窄的三角形（48 图标块内 34 x 30） */
static void draw_heart_glyph(int ox, int oy, u16 color) /* 绘制心形图形 */
{
    int r; /* 三角形行号 */
    int w; /* 当前行宽度 */

    fill_circle(ox + 15, oy + 14, 9, color); /* 画左圆瓣 */
    fill_circle(ox + 29, oy + 14, 9, color); /* 画右圆瓣 */

    for (r = 0; r < 20; r++) /* 逐行绘制下方三角形 */
    {
        w = 34 - r * 2; /* 计算当前行宽度 */
        if (w <= 0) break; /* 宽度非法则结束 */

        fill(ox + 22 - w / 2, oy + 18 + r, w, 1, color); /* 填充当前行 */
    }
}

/*=============================================================================
 *  状态栏 / 壁纸 / dock 栏
 *===========================================================================*/

/* RTC 中断（rtc.c）每秒刷新一次这个结构体，
 * 所以状态栏只需读取即可 */
static void draw_clock(void) /* 绘制状态栏时钟 */
{
    u8 h = calendar.hour; /* 当前小时 */
    u8 m = calendar.min; /* 当前分钟 */
    u8 s = calendar.sec; /* 当前秒 */
    char buf[10]; /* 时间字符串缓冲 */

    sprintf(buf, "%02d:%02d:%02d", h, m, s); /* 格式化时间 */

    fill(0, 0, 62, BAR_H, C_BAR);               /* 擦掉旧数字          */
    text_at(8, 4, 12, buf, C_TEXT, C_BAR); /* 显示时间文字 */
}

/* yyyy-mm-dd，居中显示在状态栏 */
static void draw_date(void) /* 绘制状态栏日期 */
{
    u16 y = calendar.w_year; /* 当前年份 */
    u8  m = calendar.w_month; /* 当前月份 */
    u8  d = calendar.w_date; /* 当前日期 */
    char buf[14]; /* 日期字符串缓冲 */

    sprintf(buf, "%04d-%02d-%02d", y, m, d); /* 格式化日期 */

    fill(120, 0, 80, BAR_H, C_BAR); /* 擦掉旧日期 */
    text_at(130, 4, 12, buf, C_DIM, C_BAR); /* 显示日期文字 */
}

static void draw_status_bar(void) /* 绘制状态栏 */
{
    u8 i; /* 信号格索引 */

    fill(0, 0, sw, BAR_H, C_BAR); /* 填充状态栏背景 */

    /* 时钟（左）和日期（中）都来自 RTC */
    draw_clock(); /* 绘制时钟 */
    draw_date(); /* 绘制日期 */

    /* 信号格 */
    for (i = 0; i < 4; i++) /* 画四格信号 */
    {
        fill(248 + i * 5, 17 - (i + 1) * 3, 3, (i + 1) * 3, C_TEXT); /* 画一格信号 */
    }

    /* 电池：外框、内芯、电量、电极头 */
    fill(276, 4, 30, 12, C_TEXT); /* 电池外框 */
    fill(278, 6, 26, 8, C_BAR); /* 电池内芯背景 */
    fill(278, 6, 19, 8, C_GREEN); /* 电池电量 */
    fill(306, 7, 3, 6, C_TEXT); /* 电池电极头 */
}

/* 七条水平色带组成一个廉价的垂直渐变。两个页面都使用它，
 * 所以切换页面时壁纸不需要重画。 */
static void draw_wallpaper(void) /* 绘制渐变壁纸 */
{
    static const u16 band[7] = /* 七条壁纸色带 */
    {
        C_WALL0, C_WALL1, C_WALL2, C_WALL3, C_WALL4, C_WALL5, C_WALL6 /* 色带颜色 */
    };
    u8 i; /* 色带索引 */
    u16 bh = (u16)((DOCK_Y - BAR_H) / 7); /* 每条色带高度 */

    for (i = 0; i < 7; i++) /* 逐条绘制色带 */
    {
        u16 y0 = BAR_H + i * bh; /* 当前色带起始Y */
        u16 y1 = (i == 6) ? (u16)(DOCK_Y - 1) : (u16)(y0 + bh - 1); /* 当前色带结束Y */

        fill(0, y0, sw, y1 - y0 + 1, band[i]); /* 填充当前色带 */
    }
}

/* pg = 0（状态页）/ 1（应用页）/ 2（工具页）：提示文字和页码点
 * 会跟随当前显示的页面 */
static void draw_dock(u8 pg) /* 绘制底部dock栏 */
{
    u8 i; /* 页码点索引 */
    char *tip; /* 提示文字 */

    fill(0, DOCK_Y, sw, sh - DOCK_Y, C_DOCK); /* 填充dock背景 */
    fill(0, DOCK_Y, sw, 1, C_WALL5); /* 绘制dock顶部亮线 */

    if (pg == 0)      tip = (char *)"SWIPE LEFT FOR APPS"; /* 第一页提示 */
    else if (pg == 1) tip = (char *)"SWIPE FOR TOOLS"; /* 第二页提示 */
    else              tip = (char *)"SWIPE RIGHT FOR STATUS"; /* 第三页提示 */

    center_str(208, 12, tip, C_DIM, C_DOCK); /* 居中显示提示 */

    /* 页码点：3 页居中 */
    for (i = 0; i < 3; i++) /* 绘制三个页码点 */
    {
        fill_circle(148 + i * 12, 228, 3, (i == pg) ? C_TEXT : C_DIM); /* 当前页高亮 */
    }
}

/*=============================================================================
 *  BMP180 卡片：温度 / 气压 / 海拔
 *===========================================================================*/

/* 保留一位小数的数值，不使用浮点 printf */
static void fmt_1dec(char *out, float v) /* 格式化一位小数 */
{
    int32_t v10 = (int32_t)(v * 10.0f + ((v >= 0.0f) ? 0.5f : -0.5f)); /* 四舍五入到十分位 */
    int32_t ip  = v10 / 10; /* 整数部分 */
    int32_t fp  = v10 % 10; /* 小数部分 */

    if (fp < 0) fp = -fp; /* 小数部分取正 */

    if ((ip == 0) && (v10 < 0)) sprintf(out, "-0.%d", (int)fp); /* 处理负零情况 */
    else                        sprintf(out, "%d.%d", (int)ip, (int)fp); /* 正常格式化 */
}

/* 一行读数："<值> <单位>"，与标题左对齐，
 * 用空格补足到固定的 10 字符宽度（80 px），
 * 这样上一次更长的数字也会被一并擦掉 */
static void show_reading(u16 x, float v, char *unit, u8 valid) /* 显示一列读数 */
{
    char body[8]; /* 数值文本 */
    char line[20]; /* 数值加单位 */
    char field[20]; /* 固定宽度字段 */
    u8 idx = (u8)((x - CARD_COL1) / 100); /* 计算列索引 */

    if (valid) fmt_1dec(body, v); /* 有效则格式化数值 */
    else       sprintf(body, "--.-"); /* 无效则显示占位符 */

    sprintf(line, "%s %s", body, unit); /* 拼接数值和单位 */
    sprintf(field, "%-10s", line); /* 补齐固定宽度 */

    /* 与上次数字相同：不动这些像素 */
    if ((idx < 3) && !card_full && rd_have[idx] && (strcmp(rd_last[idx], field) == 0)) /* 判断缓存是否命中 */
    {
        return; /* 相同则跳过重绘 */
    }

    if (idx < 3) /* 更新缓存 */
    {
        strcpy(rd_last[idx], field); /* 保存本次文本 */
        rd_have[idx] = 1; /* 标记缓存有效 */
    }

    text_at(x + CARD_VAL_DX, CARD_Y + CARD_VAL_DY, 16, field, C_VALUE, C_CARD); /* 显示读数 */
}

/*-------------------- DHT11 气候条 ---------*/

static u16 dht_color(void) /* 根据湿度选择颜色 */
{
    if (dht_h < 35) return C_AMBER;     /* 太干燥   */
    if (dht_h > 65) return C_ACC_BLUE;  /* 太潮湿   */
    return C_ACC_GREEN; /* 舒适 */
}

static char *dht_word(void) /* 根据湿度返回状态文字 */
{
    if (dht_h < 35) return (char *)"DRY"; /* 干燥 */
    if (dht_h > 65) return (char *)"HUMID"; /* 潮湿 */
    return (char *)"COMFORT"; /* 舒适 */
}

/* 温度、湿度、湿度条、舒适度文字 */
static void draw_dht_row(void) /* 绘制DHT11行 */
{
    char buf[24]; /* 文本缓冲 */
    u16  x  = CARD_X; /* 卡片X坐标 */
    u16  y  = (u16)(CARD_Y + DHT_ROW_DY); /* 当前行Y坐标 */
    int  bx = (int)x + DHT_BAR_DX; /* 湿度条X坐标 */
    int  by = (int)y + 3; /* 湿度条Y坐标 */
    u16  fw; /* 湿度条填充宽度 */
    u16  col; /* 状态颜色 */

    /* 与上次绘制相比没有任何变化：整条跳过 */
    if (!card_full && dh_have && (dh_v == dht_valid) && /* 检查缓存 */
        (dh_t == dht_t) && (dh_h == dht_h)) /* 检查温湿度是否变化 */
    {
        return; /* 无变化则跳过 */
    }

    dh_have = 1; /* 标记缓存有效 */
    dh_v = dht_valid; /* 缓存有效标志 */
    dh_t = dht_t; /* 缓存温度 */
    dh_h = dht_h; /* 缓存湿度 */

    fill(x + DHT_TAB_DX, y + 1, 3, 10, C_CLIM_TAB); /* 绘制颜色标记 */
    text_at(x + DHT_CAP_DX, y, 12, (char *)"DHT11", C_LABEL, C_CARD); /* 显示DHT11标签 */

    if (!dht_valid) /* 数据无效 */
    {
        text_at(x + DHT_T_DX, y, 12, (char *)"--C",   C_LABEL, C_CARD); /* 显示温度占位符 */
        text_at(x + DHT_H_DX, y, 12, (char *)"--%RH", C_LABEL, C_CARD); /* 显示湿度占位符 */
        fill(bx, by, DHT_BAR_W, DHT_BAR_H, C_BAR_BG); /* 绘制空湿度条 */
        text_at(x + DHT_ST_DX, y, 12, (char *)"NO DATA", C_REF, C_CARD); /* 显示无数据 */
        return; /* 结束绘制 */
    }

    /* DHT11 的分辨率只有 1 C / 1 %RH，所以这里不显示小数 */
    sprintf(buf, "%2uC", dht_t); /* 格式化温度 */
    text_at(x + DHT_T_DX, y, 12, buf, C_VALUE, C_CARD); /* 显示温度 */

    sprintf(buf, "%2u%%RH", dht_h); /* 格式化湿度 */
    text_at(x + DHT_H_DX, y, 12, buf, C_VALUE, C_CARD); /* 显示湿度 */

    fill(bx, by, DHT_BAR_W, DHT_BAR_H, C_BAR_BG); /* 绘制湿度条空槽 */

    fw = (u16)((u32)DHT_BAR_W * dht_h / 100); /* 计算湿度条宽度 */
    if ((fw == 0) && (dht_h > 0)) fw = 1; /* 非零湿度至少显示1像素 */

    col = dht_color(); /* 获取状态颜色 */
    if (fw) fill(bx, by, fw, DHT_BAR_H, col); /* 填充湿度条 */

    text_at(x + DHT_ST_DX, y, 12, dht_word(), col, C_CARD); /* 显示舒适度文字 */
}

/* 某个 MQ-2 等级对应的等级条颜色 / 状态文字颜色 */
static u16 gas_color(u8 st) /* 根据气体状态返回颜色 */
{
    if (st == MQ2_ST_TRACE)  return C_AMBER; /* 微量 */
    if (st == MQ2_ST_ALARM)  return C_GAS_MID; /* 报警 */
    if (st == MQ2_ST_DANGER) return C_ERR; /* 危险 */
    return C_ACC_GREEN; /* 清洁 */
}

/* 气体条：标签、ADC、电压、等级条、状态文字 */
static void draw_gas_row(void) /* 绘制MQ-2行 */
{
    char buf[24]; /* 文本缓冲 */
    u16  x  = CARD_X; /* 卡片X坐标 */
    u16  y  = (u16)(CARD_Y + GAS_ROW_DY); /* 当前行Y坐标 */
    int  bx = (int)x + GAS_BAR_DX; /* 等级条X坐标 */
    int  by = (int)y + 3; /* 等级条Y坐标 */
    u16  fw; /* 等级条填充宽度 */
    u16  col; /* 状态颜色 */

    /* 如果每个字段的结果都相同就跳过这一条 */
    if (!card_full && gas_have && (gas_lv == gas_valid) && /* 检查缓存 */
        (gas.adc == gas_last.adc) && (gas.mv == gas_last.mv) && /* 检查ADC和电压 */
        (gas.ppm == gas_last.ppm) && (gas.level == gas_last.level) && /* 检查ppm和等级 */
        (gas.status == gas_last.status)) /* 检查状态 */
    {
        return; /* 无变化则跳过 */
    }

    gas_last = gas; /* 更新缓存 */
    gas_have = 1; /* 标记缓存有效 */
    gas_lv = gas_valid; /* 缓存有效标志 */

    /* 颜色标记 + 标签 */
    fill(x + GAS_TAB_DX, y + 1, 3, 10, C_GAS_TAB); /* 绘制颜色标记 */
    text_at(x + GAS_CAP_DX, y, 12, (char *)"MQ-2", C_LABEL, C_CARD); /* 显示MQ-2标签 */

    if (!gas_valid) /* 数据无效 */
    {
        text_at(x + GAS_ADC_DX, y, 12, (char *)"ADC ----", C_LABEL, C_CARD); /* 显示ADC占位符 */
        text_at(x + GAS_VOL_DX, y, 12, (char *)"--.--V",   C_LABEL, C_CARD); /* 显示电压占位符 */
        text_at(x + GAS_PPM_DX, y, 12, (char *)"----ppm",  C_LABEL, C_CARD); /* 显示ppm占位符 */
        fill(bx, by, GAS_BAR_W, GAS_BAR_H, C_BAR_BG); /* 绘制空等级条 */
        text_at(x + GAS_ST_DX, y, 12, (char *)"NO SIG", C_REF, C_CARD); /* 显示无信号 */
        return; /* 结束绘制 */
    }

    sprintf(buf, "ADC %4u", gas.adc); /* 格式化ADC值 */
    text_at(x + GAS_ADC_DX, y, 12, buf, C_VALUE, C_CARD); /* 显示ADC值 */

    /* 用 "%u.%02u" 代替 "%f"：本平台上不使用浮点 printf */
    sprintf(buf, "%u.%02uV", gas.mv / 1000, (gas.mv % 1000) / 10); /* 格式化电压 */
    text_at(x + GAS_VOL_DX, y, 12, buf, C_LABEL, C_CARD); /* 显示电压 */

    /* ppm 估算值，驱动里已限制到 9999，所以 4 位数字总能放下 */
    sprintf(buf, "%4uppm", gas.ppm); /* 格式化ppm值 */
    text_at(x + GAS_PPM_DX, y, 12, buf, C_VALUE, C_CARD); /* 显示ppm值 */

    /* 等级条：先画空槽，这样数值变小时不会留下残影 */
    fill(bx, by, GAS_BAR_W, GAS_BAR_H, C_BAR_BG); /* 绘制等级条空槽 */

    fw = (u16)((u32)GAS_BAR_W * gas.level / 100); /* 计算等级条宽度 */
    if ((fw == 0) && (gas.level > 0)) fw = 1; /* 非零等级至少显示1像素 */

    col = gas_color(gas.status); /* 获取状态颜色 */
    if (fw) fill(bx, by, fw, GAS_BAR_H, col); /* 填充等级条 */

    text_at(x + GAS_ST_DX, y, 12, (char *)MQ2_StatusName(gas.status), /* 显示状态文字 */
            col, C_CARD); /* 使用状态颜色 */
}

/* 整张卡片：边框、标题、状态；读数最后绘制，
 * 这样数值字符串就覆盖在已完成的背景之上 */
static void draw_sensor_card(void) /* 绘制整张传感器卡片 */
{
    static const u16 tab[3] = { C_AMBER, C_ACC_BLUE, C_ACC_GREEN }; /* 三列颜色标记 */
    static char *cap[3]     = { "TEMP",  "PRESS",    "ALT" }; /* 三列标题 */
    u16 col[3]; /* 三列X坐标 */
    u16 x = CARD_X; /* 卡片X坐标 */
    u16 y = CARD_Y; /* 卡片Y坐标 */
    u16 w = (u16)(sw - 2 * CARD_X); /* 卡片宽度 */
    u8  i; /* 列索引 */

    col[0] = CARD_COL1; /* 第一列X坐标 */
    col[1] = CARD_COL2; /* 第二列X坐标 */
    col[2] = CARD_COL3; /* 第三列X坐标 */

    card_full = 1;                  /* 填充卡片主体会擦掉下方所有文字     */

    /* 先画投影，再画主体 */
    fill(x + 3, y + 4, w, CARD_H, C_CARD_SH); /* 绘制卡片投影 */
    fill(x, y, w, CARD_H, C_CARD); /* 绘制卡片主体 */

    /* 1 像素边框 */
    fill(x, y, w, 1, C_CARD_TOP); /* 顶部高光 */
    fill(x, y + CARD_H - 1, w, 1, C_CARD_EDGE); /* 底部边框 */
    fill(x, y, 1, CARD_H, C_CARD_EDGE); /* 左边框 */
    fill(x + w - 1, y, 1, CARD_H, C_CARD_EDGE); /* 右边框 */

    /* 列分隔线，止于细线之上 */
    fill(CARD_SPLIT1, y + 6, 1, 32, C_CARD_SPLIT); /* 第一列分隔线 */
    fill(CARD_SPLIT2, y + 6, 1, 32, C_CARD_SPLIT); /* 第二列分隔线 */

    /* BMP180 三列与气体条之间的细线 */
    fill(x + 12, y + CARD_RULE_DY, w - 24, 1, C_CARD_SPLIT); /* 绘制横向细线 */

    for (i = 0; i < 3; i++) /* 绘制三列标题 */
    {
        fill(col[i], y + 8, 3, 10, tab[i]); /* 绘制列颜色标记 */
        text_at(col[i] + 8, y + CARD_LBL_DY, 12, cap[i], C_LABEL, C_CARD); /* 显示列标题 */
    }

    /* 状态行：在线 / 离线圆点 + 文字，以及海拔参考值 */
    fill(x + 12, y + CARD_FTR_DY + 5, 4, 4, sen_ready ? C_OK : C_ERR); /* 绘制在线状态圆点 */
    text_at(x + 20, y + CARD_FTR_DY, 12, /* 显示BMP180状态文字 */
            (char *)(sen_ready ? "BMP180 ONLINE" : "BMP180 OFFLINE"), /* 状态字符串 */
            C_LABEL, C_CARD); /* 使用标签颜色 */
    text_at(x + w - 96, y + CARD_FTR_DY, 12, /* 显示参考气压 */
            (char *)"REF 1013.25hPa", C_REF, C_CARD); /* 参考值文字 */

    draw_dht_row(); /* 绘制DHT11行 */
    draw_gas_row(); /* 绘制MQ-2行 */

    show_reading(col[0], sen_temp, "C",   sen_valid); /* 显示温度读数 */
    show_reading(col[1], sen_pres, "hPa", sen_valid); /* 显示气压读数 */
    show_reading(col[2], sen_alt,  "m",   sen_valid); /* 显示海拔读数 */

    card_full = 0;                  /* 恢复为增量更新              */
}

/* 开机时探测一次芯片并取第一组读数，
 * 这样桌面第一次绘制时卡片就已经有数据了 */
static void sensor_init(void) /* 初始化BMP180并读取首次数据 */
{
    sen_ready = (BMP180_Init() != 0) ? 1 : 0; /* 初始化BMP180 */
    sen_valid = 0; /* 初始无有效数据 */

    if (sen_ready) /* 芯片有响应 */
    {
        BMP180_GetData(&sen_temp, &sen_pres, &sen_alt); /* 读取温压海拔 */
        sen_valid = 1; /* 标记数据有效 */
    }
}

/* 测量一次并重画三列读数（约 35 ms） */
static void sensor_refresh(void) /* 刷新BMP180读数 */
{
    if (!sen_ready) return; /* 未就绪直接返回 */

    BMP180_GetData(&sen_temp, &sen_pres, &sen_alt); /* 读取最新数据 */
    sen_valid = 1; /* 标记数据有效 */

    show_reading(CARD_COL1, sen_temp, "C",   1); /* 刷新温度显示 */
    show_reading(CARD_COL2, sen_pres, "hPa", 1); /* 刷新气压显示 */
    show_reading(CARD_COL3, sen_alt,  "m",   1); /* 刷新海拔显示 */
}

/*=============================================================================
 *  PA6（ADC1_IN6）上的 MQ-2 气体传感器
 *===========================================================================*/

/* PA6 是普通模拟输入，所以"检测"只能看信号：
 * 未供电的坏模块会让引脚停在 0，
 * 短接到 VCC 的模块会把引脚拉到 4095。介于两者之间都算正常。 */
static void gas_init(void) /* 初始化MQ-2 */
{
    gas.adc    = 0; /* ADC初值 */
    gas.mv     = 0; /* 电压初值 */
    gas.ppm    = 0; /* ppm初值 */
    gas.level  = 0; /* 等级初值 */
    gas.status = MQ2_ST_CLEAN; /* 状态初值 */
    gas_valid  = 0; /* 数据无效 */

    MQ2_Init();                     /* PA6 模拟输入 + ADC1 + 校准      */
    gas_refresh();                  /* 先取一次样，卡片不会是空白             */
}

/* 采集一次（8 个样本，约 20 ms）并只重画这一条 */
static void gas_refresh(void) /* 刷新MQ-2读数 */
{
    MQ2_Read(&gas); /* 读取MQ-2数据 */

    gas_valid = ((gas.adc > 8) && (gas.adc < 4092)) ? 1 : 0; /* 判断信号是否正常 */

    draw_gas_row(); /* 重绘气体行 */
}

/*=============================================================================
 *  PA15 上的 DHT11 温度 / 湿度（自定义单总线协议）
 *===========================================================================*/

static void dht_init(void) /* 初始化DHT11 */
{
    dht_t = 0; /* 温度初值 */
    dht_h = 0; /* 湿度初值 */
    dht_valid = 0; /* 数据无效 */
    dht_fail  = 0; /* 失败次数清零 */

    DHT11_Init();               /* 无器件应答时返回 1             */
}

/* 读取一次（约 22 ms，器件无应答时最长 40 ms）并重绘。
 * DHT11 大约每秒才更新一次，调用方每 2 s 调用一次本函数，
 * 校验和出错时只是把上一次的值继续留在屏幕上。 */
static void dht_refresh(void) /* 刷新DHT11读数 */
{
    u8 t = 0; /* 临时温度 */
    u8 h = 0; /* 临时湿度 */

    if (DHT11_Read_Data(&t, &h) == 0) /* 读取成功 */
    {
        dht_t = t; /* 更新温度 */
        dht_h = h; /* 更新湿度 */
        dht_valid = 1; /* 标记有效 */
        dht_fail  = 0; /* 清除失败计数 */
    }
    else /* 读取失败 */
    {
        if (dht_fail < 3) dht_fail++; /* 累计失败次数 */
        if (dht_fail >= 3) dht_valid = 0;   /* 连续失败 3 次：放弃        */
    }

    draw_dht_row(); /* 重绘DHT11行 */
}

/*=============================================================================
 *  应用图标
 *===========================================================================*/

static void draw_tile(int x, int y, u16 color) /* 绘制应用图标底板 */
{
    fill(x, y, ICON_SZ, ICON_SZ, color); /* 填充图标块 */
    fill(x, y + ICON_SZ - 1, ICON_SZ, 1, C_TILE_SH); /* 绘制底部阴影边 */
    fill(x + ICON_SZ - 1, y, 1, ICON_SZ, C_TILE_SH); /* 绘制右侧阴影边 */
}

static void draw_app_dino(int x, int y) /* 绘制恐龙应用图标 */
{
    draw_tile(x, y, C_TILE); /* 绘制图标底板 */
    draw_dino_glyph(x + 4, y + 1, C_INK); /* 绘制恐龙图形 */
}

static void draw_app_plane(int x, int y) /* 绘制飞机应用图标 */
{
    draw_tile(x, y, C_BLUE); /* 绘制图标底板 */
    draw_plane_glyph(x + 12, y + 11, C_TEXT); /* 绘制飞机图形 */
}

static void draw_app_snake(int x, int y) /* 绘制贪吃蛇应用图标 */
{
    draw_tile(x, y, C_TILE); /* 绘制图标底板 */
    /* 蛇用橄榄绿而不是 C_GREEN：C_GREEN 现在是为深色底准备的琥珀，
     * 摆在奶油色图标块上会糊成一片。                        */
    draw_snake_glyph(x, y, 0x6C67); /* 绘制蛇图形 */
}

static void draw_app_heart(int x, int y) /* 绘制心率应用图标 */
{
    draw_tile(x, y, C_TILE);            /* 奶油底，心形用暖红       */
    draw_heart_glyph(x + 1, y + 5, 0xC985); /* 绘制心形图形 */
}

/* 齿轮：圆环周围八个齿（48 x 48 图标区域） */
static const short gear_tooth[8][2] = /* 齿轮齿偏移表 */
{
    {  0, -17}, { 12, -12}, { 17,   0}, { 12,  12}, /* 齿轮齿偏移 */
    {  0,  17}, {-12,  12}, {-17,   0}, {-12, -12}, /* 齿轮齿偏移 */
};

static void draw_app_settings(int x, int y) /* 绘制设置应用图标 */
{
    int cx = x + ICON_SZ / 2; /* 图标中心X */
    int cy = y + ICON_SZ / 2; /* 图标中心Y */
    u8  i; /* 齿索引 */

    draw_tile(x, y, C_GRAY); /* 绘制图标底板 */

    for (i = 0; i < 8; i++) /* 绘制八个齿 */
    {
        fill(cx + gear_tooth[i][0] - 3, cy + gear_tooth[i][1] - 3, 7, 7, C_INK); /* 绘制一个齿 */
    }

    fill_circle(cx, cy, 13, C_INK); /* 绘制齿轮外圆 */
    fill_circle(cx, cy, 6, C_GRAY);         /* 中心镂空      */
}

/* 相册：带边框的图片，里面有太阳和山（48 x 48 图标区域） */
static void draw_album_glyph(int ox, int oy, u16 color) /* 绘制相册图形 */
{
    int r; /* 山行号 */
    int w; /* 当前行宽度 */

    fill(ox + 7,  oy + 11, 34, 26, color);          /* 边框           */
    fill(ox + 9,  oy + 13, 30, 22, C_GRAY);         /* 照片区域       */

    fill_circle(ox + 17, oy + 20, 3, C_AMBER);      /* 太阳           */

    /* 山：一个贴住照片下边缘的三角形 */
    for (r = 0; r < 12; r++) /* 逐行绘制山 */
    {
        w = 4 + r * 2; /* 计算当前行宽度 */
        if (w > 24) w = 24; /* 限制最大宽度 */

        fill(ox + 24 - w / 2, oy + 22 + r, w, 1, color); /* 填充当前行 */
    }
}

static void draw_album_app(int x, int y) /* 绘制相册应用图标 */
{
    draw_tile(x, y, C_TILE); /* 绘制图标底板 */
    draw_album_glyph(x, y, C_INK); /* 绘制相册图形 */
}

/* 相机：机身、镜圈和一个小的取景器凸起 */
static void draw_camera_glyph(int ox, int oy, u16 color) /* 绘制相机图形 */
{
    fill(ox + 17, oy + 9,  14,  5, color);          /* 闪光灯凸起     */
    fill(ox + 7,  oy + 13, 34, 23, color);          /* 机身           */

    fill_circle(ox + 24, oy + 25, 9, C_TILE);       /* 镜圈           */
    fill_circle(ox + 24, oy + 25, 5, color);        /* 镜头           */
    fill_circle(ox + 24, oy + 25, 2, C_TILE);       /* 高光           */

    fill(ox + 10, oy + 16, 4, 3, C_TILE);           /* LED 指示灯     */
}

static void draw_camera_app(int x, int y) /* 绘制相机应用图标 */
{
    draw_tile(x, y, C_BLUE); /* 绘制图标底板 */
    draw_camera_glyph(x, y, C_INK); /* 绘制相机图形 */
}

/* 文件夹：一个标签页加一块主体，主体上沿留一道亮线当作盖子分界
 * （48 x 48 图标区域）。亮线用固定的 C_TILE 而不是传进来的颜色，
 * 这样在桌面的橙色块上和加载页的黑底上都能看出层次。       */
static void draw_folder_glyph(int ox, int oy, u16 color) /* 绘制文件夹图形 */
{
    fill(ox + 8, oy + 12, 12, 6,  color);       /* 上面凸起的标签页 */
    fill(ox + 8, oy + 17, 32, 18, color);       /* 文件夹主体       */
    fill(ox + 8, oy + 17, 32, 1,  C_TILE);      /* 盖子分界高光     */
}

static void draw_files_app(int x, int y) /* 绘制文件应用图标 */
{
    draw_tile(x, y, C_AMBER); /* 绘制图标底板 */
    draw_folder_glyph(x, y, C_INK); /* 绘制文件夹图形 */
}

/* 波形：48 x 48 图标块里画一条正弦，32 列逐点填 */
static const u8 wave_icon_y[32] = /* 波形图标Y采样表 */
{
     24, 21, 18, 14, 13, 11, 10,  8, /* 波形采样Y值 */
      8,  8, 10, 11, 13, 14, 18, 21, /* 波形采样Y值 */
     24, 27, 30, 34, 35, 37, 38, 40, /* 波形采样Y值 */
     40, 40, 38, 37, 35, 34, 30, 27 /* 波形采样Y值 */
};

static void draw_wave_glyph(int ox, int oy, u16 color) /* 绘制波形图形 */
{
    u8 i; /* 列索引 */

    for (i = 0; i < 32; i++) /* 逐列绘制波形 */
        fill(ox + 8 + i, oy + wave_icon_y[i], 1, 2, color); /* 绘制一个波形点 */
}

static void draw_dac_app(int x, int y) /* 绘制DAC应用图标 */
{
    draw_tile(x, y, C_GRAY); /* 绘制图标底板 */
    draw_wave_glyph(x, y, C_INK); /* 绘制波形图形 */
}

/*=============================================================================
 *  两个桌面页
 *===========================================================================*/

static void draw_page1(void) /* 绘制第一页 */
{
    draw_wallpaper(); /* 绘制壁纸 */
    draw_status_bar(); /* 绘制状态栏 */

    draw_sensor_card(); /* 绘制传感器卡片 */

    draw_app_settings(ICON_SET_X, P1_ICON_Y); /* 绘制设置图标 */
    text_at(ICON_SET_X, P1_LABEL_Y, 12, (char *)"SETTINGS", C_TEXT, C_WALL6); /* 显示设置标签 */

    draw_album_app(ICON_ALB_X, P1_ICON_Y); /* 绘制相册图标 */
    text_at(ICON_ALB_X + 9, P1_LABEL_Y, 12, (char *)"ALBUM", C_TEXT, C_WALL6); /* 显示相册标签 */

    draw_camera_app(ICON_CAM_X, P1_ICON_Y); /* 绘制相机图标 */
    text_at(ICON_CAM_X + 6, P1_LABEL_Y, 12, (char *)"CAMERA", C_TEXT, C_WALL6); /* 显示相机标签 */

    draw_files_app(ICON_FIL_X, P1_ICON_Y); /* 绘制文件图标 */
    text_at(ICON_FIL_X + 9, P1_LABEL_Y, 12, (char *)"FILES", C_TEXT, C_WALL6); /* 显示文件标签 */

    draw_dock(0); /* 绘制第一页dock */
}

static void draw_page2(void) /* 绘制第二页 */
{
    draw_wallpaper(); /* 绘制壁纸 */
    draw_status_bar(); /* 绘制状态栏 */

    draw_app_dino(APP_DINO_X, P2_ICON_Y); /* 绘制恐龙图标 */
    draw_app_plane(APP_PLANE_X, P2_ICON_Y); /* 绘制飞机图标 */
    draw_app_snake(APP_SNAKE_X, P2_ICON_Y); /* 绘制贪吃蛇图标 */
    draw_app_heart(APP_HEART_X, P2_ICON_Y); /* 绘制心率图标 */

    /* 标签就贴在图标块下方，位于壁纸色带 4 内 */
    text_at(APP_DINO_X, P2_LABEL_Y, 12, (char *)"DINO RUN", C_TEXT, C_WALL4); /* 显示恐龙标签 */
    text_at(APP_PLANE_X + 9, P2_LABEL_Y, 12, (char *)"PLANE", C_TEXT, C_WALL4); /* 显示飞机标签 */
    text_at(APP_SNAKE_X + 9, P2_LABEL_Y, 12, (char *)"SNAKE", C_TEXT, C_WALL4); /* 显示贪吃蛇标签 */
    text_at(APP_HEART_X + 9, P2_LABEL_Y, 12, (char *)"HEART", C_TEXT, C_WALL4); /* 显示心率标签 */

    draw_dock(1); /* 绘制第二页dock */
}

/* 第三页：DAC 波形输出。以后再加 APP 就往这一页排。 */
static void draw_page3(void) /* 绘制第三页 */
{
    draw_wallpaper(); /* 绘制壁纸 */
    draw_status_bar(); /* 绘制状态栏 */

    draw_dac_app(APP_DAC_X, P3_ICON_Y); /* 绘制DAC图标 */
    text_at(APP_DAC_X + 15, P3_LABEL_Y, 12, (char *)"DAC", C_TEXT, C_WALL4); /* 显示DAC标签 */

    draw_dock(2); /* 绘制第三页dock */
}

/*=============================================================================
 *  加载界面
 *===========================================================================*/

static const char *stage_name(u8 pct) /* 根据进度返回加载阶段名称 */
{
    if (pct < 25) return "LOADING SPRITES"; /* 加载精灵 */
    if (pct < 50) return "BUILDING WORLD"; /* 构建世界 */
    if (pct < 75) return "READING SAVE DATA"; /* 读取存档 */
    return "STARTING GAME"; /* 启动游戏 */
}

static void draw_progress(u8 pct, u8 *last_w) /* 绘制加载进度 */
{
    u16 w = (u16)((u32)(PROG_W - 4) * pct / 100); /* 计算当前填充宽度 */
    char buf[5]; /* 百分比文本缓冲 */

    if (w > *last_w) /* 只在进度增加时填充 */
    {
        fill(PROG_X + 2 + *last_w, PROG_Y + 2, w - *last_w, PROG_H - 4, C_GREEN); /* 填充新增进度 */
        *last_w = (u8)w; /* 更新上次宽度 */
    }

    buf[0] = (char)('0' + pct / 100); /* 百位 */
    buf[1] = (char)('0' + (pct / 10) % 10); /* 十位 */
    buf[2] = (char)('0' + pct % 10); /* 个位 */
    buf[3] = '%'; /* 百分号 */
    buf[4] = 0; /* 字符串结束符 */

    text_at((u16)(sw / 2 - 18), PCT_Y, 16, buf, C_TEXT, C_DOCK); /* 显示百分比 */

    center_str(STAGE_Y, 16, (char *)stage_name(pct), C_GRAY, C_DOCK); /* 显示阶段名称 */
}

static void show_loading(void (*glyph)(int, int, u16), char *title) /* 显示加载界面框架 */
{
    int ox = (int)sw / 2 - 24; /* 图标X坐标 */

    fill(0, 0, sw, sh, C_DOCK);          /* 深咖啡底，和桌面同族 */

    glyph(ox, 24, C_TEXT); /* 绘制应用图标 */
    center_str(80, 24, title, C_TEXT, C_DOCK); /* 显示应用标题 */
    center_str(108, 12, "by STM32F103", C_GRAY, C_DOCK); /* 显示副标题 */

    POINT_COLOR = C_TEXT; /* 设置边框颜色 */
    LCD_DrawRectangle(PROG_X, PROG_Y, PROG_X + PROG_W - 1, PROG_Y + PROG_H - 1); /* 绘制进度条外框 */
}

static void loading_run(void (*glyph)(int, int, u16), char *title) /* 运行加载动画 */
{
    u8 pct; /* 当前进度 */
    u8 last_w = 0; /* 上次进度宽度 */

    show_loading(glyph, title); /* 显示加载界面 */

    for (pct = 0; pct <= 100; pct += 2) /* 进度从0到100 */
    {
        draw_progress(pct, &last_w); /* 绘制当前进度 */
        delay_ms(18); /* 延时制造动画效果 */
    }

    delay_ms(200); /* 加载完成后短暂停留 */
}

/*=============================================================================
 *  关于页面（由游戏主页的 INFO 按钮打开）
 *===========================================================================*/

void Dino_Info_Page(void) /* 恐龙游戏信息页 */
{
    u8 ev; /* 输入事件 */

    fill(0, 0, sw, sh, C_WALL1); /* 填充背景 */
    center_str(16, 24, "INFO", C_TEXT, C_WALL1); /* 显示标题 */

    text_at(16, 50, 12, (char *)"3 GAMES  v1.1", C_TEXT, C_WALL1); /* 显示版本信息 */
    text_at(16, 68, 12, (char *)"STM32F103ZET6 + ILI9341", C_GRAY, C_WALL1); /* 显示硬件信息 */
    text_at(16, 94, 12, (char *)"DINO : K0/K1 JUMP", C_TEXT, C_WALL1); /* 显示恐龙操作 */
    text_at(16, 112, 12, (char *)"PLANE: K0/K1 MOVE", C_TEXT, C_WALL1); /* 显示飞机操作 */
    text_at(16, 130, 12, (char *)"SNAKE: K0/K1 TURN", C_TEXT, C_WALL1); /* 显示贪吃蛇操作 */
    text_at(16, 148, 12, (char *)"K2 BACK   WK_UP PAUSE", C_TEXT, C_WALL1); /* 显示返回和暂停键 */
    text_at(16, 166, 12, (char *)"TOUCH: PLAY / BACK", C_GRAY, C_WALL1); /* 显示触摸操作 */
    center_str(196, 12, "TAP OR PRESS ANY KEY", C_GRAY, C_WALL1); /* 显示退出提示 */

    Game_Wait_Touch_Release(); /* 等待触摸释放 */

    for (;;) /* 等待退出事件 */
    {
        ev = menu_scan(); /* 扫描输入事件 */

        if (ev) return; /* 有事件则返回 */

        delay_ms(20); /* 空闲延时 */
    }
}

/*=============================================================================
 *  输入
 *===========================================================================*/

/* 事件位。只有在手指抬起后才会上报点击，因为同一个手势
 * 也可能是一次翻页滑动：
 *   按下 -> 移动 -> 在远处抬起 = 滑动
 *   按下 -> 原地抬起 = 点击（位置取按下点） */
#define EV_K0       0x01 /* K0按键事件 */
#define EV_K1       0x02 /* K1按键事件 */
#define EV_K2       0x04 /* K2按键事件 */
#define EV_WKUP     0x08 /* WK_UP按键事件 */
#define EV_TAP      0x10 /* 点击事件 */
#define EV_SWIPE_L  0x20 /* 左滑事件 */
#define EV_SWIPE_R  0x40 /* 右滑事件 */

#define SWIPE_DX    60          /* 判定为滑动所需的水平位移                 */
#define SWIPE_DY    50          /* ... 前提是保持大致水平                   */
#define TAP_SLOP    12          /* 抖动仍视为点击的最大幅度                 */

/* 按键上一次的电平。放在文件作用域而不是 menu_scan() 里面，
 * 是为了让阻塞界面返回后能一次性把它们同步回去（见 menu_key_sync）。*/
static u8 k0_last = 0; /* K0上次电平 */
static u8 k1_last = 0; /* K1上次电平 */
static u8 k2_last = 0; /* K2上次电平 */
static u8 ku_last = 0; /* WK_UP上次电平 */

static u8 menu_scan(void) /* 扫描菜单输入 */
{
    u8 ev = 0; /* 事件集合 */
    u8 k0 = KEY0_DOWN ? 1 : 0; /* 读取K0当前电平 */
    u8 k1 = KEY1_DOWN ? 1 : 0; /* 读取K1当前电平 */
    u8 k2 = KEY2_DOWN ? 1 : 0; /* 读取K2当前电平 */
    u8 ku = WKUP_DOWN ? 1 : 0; /* 读取WK_UP当前电平 */

    if (k0 && !k0_last) ev |= EV_K0; /* 检测K0上升沿 */
    if (k1 && !k1_last) ev |= EV_K1; /* 检测K1上升沿 */
    if (k2 && !k2_last) ev |= EV_K2; /* 检测K2上升沿 */
    if (ku && !ku_last) ev |= EV_WKUP; /* 检测WK_UP上升沿 */

    k0_last = k0; /* 更新K0上次电平 */
    k1_last = k1; /* 更新K1上次电平 */
    k2_last = k2; /* 更新K2上次电平 */
    ku_last = ku; /* 更新WK_UP上次电平 */

    if (tp_dev.scan != 0) /* 触摸驱动有效 */
    {
        if (tp_dev.scan(0)) /* 扫描到触摸 */
        {
            u16 x = tp_dev.x[0]; /* 触摸X坐标 */
            u16 y = tp_dev.y[0]; /* 触摸Y坐标 */

            if (x < sw && y < sh) /* 坐标在屏幕内 */
            {
                if (!tp_down) /* 首次按下 */
                {
                    tp_down = 1; /* 标记按下 */
                    tp_x0 = x; /* 记录按下X */
                    tp_y0 = y; /* 记录按下Y */
                }

                tp_x = x; /* 更新最后X */
                tp_y = y; /* 更新最后Y */
            }
        }
        else if (tp_down) /* 触摸抬起 */
        {
            int dx = (int)tp_x - (int)tp_x0; /* 计算水平位移 */
            int dy = (int)tp_y - (int)tp_y0; /* 计算垂直位移 */

            tp_down = 0; /* 清除按下标志 */

            if (dy <= SWIPE_DY && dy >= -SWIPE_DY) /* 垂直位移足够小 */
            {
                if (dx <= -SWIPE_DX)      ev |= EV_SWIPE_L; /* 判定左滑 */
                else if (dx >= SWIPE_DX)  ev |= EV_SWIPE_R; /* 判定右滑 */
            }

            if ((ev & (EV_SWIPE_L | EV_SWIPE_R)) == 0) /* 不是滑动 */
            {
                if (dx <= TAP_SLOP && dx >= -TAP_SLOP && /* 水平抖动小 */
                    dy <= TAP_SLOP && dy >= -TAP_SLOP) /* 垂直抖动小 */
                {
                    tp_x = tp_x0;       /* 上报按下点的位置           */
                    tp_y = tp_y0; /* 上报按下点Y坐标 */
                    ev |= EV_TAP; /* 判定为点击 */
                }
            }
        }
    }
    else /* 触摸驱动无效 */
    {
        tp_down = 0; /* 清除按下标志 */
    }

    return ev; /* 返回事件集合 */
}

/* 把"上次电平"同步成当前电平，并且不产生任何事件。
 *
 * 阻塞界面（Lock_Run / 各游戏）运行期间 menu_scan() 一次都不会被调用，
 * 那四个 last 变量就停在进入界面之前的值上。如果触发进入的那一下
 * 恰好是 K0，k0_last 会一直是 1；等回到桌面再按 K0，就会因为
 * "1 -> 1 没有上升沿"而彻底失灵。所以在阻塞界面返回后必须调一次。*/
static void menu_key_sync(void) /* 同步按键上次电平 */
{
    k0_last = KEY0_DOWN ? 1 : 0; /* 同步K0 */
    k1_last = KEY1_DOWN ? 1 : 0; /* 同步K1 */
    k2_last = KEY2_DOWN ? 1 : 0; /* 同步K2 */
    ku_last = WKUP_DOWN ? 1 : 0; /* 同步WK_UP */
}

/*=============================================================================
 *  主入口
 *===========================================================================*/

void Main_Menu(void) /* 桌面主入口 */
{
    u8  ev; /* 输入事件 */
    u8  app; /* 待启动应用编号 */
    u8  page = 0;                   /* 0 = 状态页, 1 = 应用页, 2 = 工具页 */
    u8  redraw = 1; /* 需要重绘标志 */
    u16 sen_tick = 0; /* 传感器刷新计数 */
    u8  dht_div  = 0; /* DHT11分频计数 */
    u8  clk_sec  = calendar.sec;    /* 空闲循环上次绘制的秒               */
    u8  clk_day  = calendar.w_date; /* 上次绘制的日期 */

    sw = lcddev.width; /* 获取屏幕宽度 */
    sh = lcddev.height; /* 获取屏幕高度 */

    tp_down = 0; /* 清除触摸按下标志 */

    /* 在第一次绘制之前探测 BMP180 并取第一次 MQ-2 采样，
     * 这样卡片一开始就显示真实数字而不是 "--" */
    sensor_init(); /* 初始化BMP180 */
    gas_init(); /* 初始化MQ-2 */
    dht_init(); /* 初始化DHT11 */
    dht_refresh(); /* 首次读取DHT11 */

    /* 开机启动画面 */
    fill(0, 0, sw, sh, BLACK); /* 填充黑底 */
    center_str(92, 24, "WELCOME", C_TEXT, BLACK); /* 显示欢迎语 */
    center_str(126, 12, "STM32F103ZET6", C_GRAY, BLACK); /* 显示芯片型号 */
    delay_ms(700); /* 停留700ms */

    /* 密码锁：只有 6 位密码正确后才会返回 */
    Lock_Init(); /* 初始化密码锁 */
    Lock_Run(); /* 运行密码锁 */

    for (;;) /* 桌面主循环 */
    {
        if (redraw) /* 需要重绘 */
        {
            Game_Wait_Touch_Release(); /* 等待触摸释放 */
            tp_down = 0; /* 清除触摸按下标志 */

            if (page == 0)      draw_page1(); /* 绘制第一页 */
            else if (page == 1) draw_page2(); /* 绘制第二页 */
            else                draw_page3(); /* 绘制第三页 */

            redraw   = 0; /* 清除重绘标志 */
            sen_tick = 0; /* 重置传感器计数 */
        }

        app = 0xFF; /* 默认无应用启动 */

        /*---------------- 空闲桌面 ----------------*/
        for (;;) /* 空闲桌面循环 */
        {
            ev = menu_scan(); /* 扫描输入 */

            /* K0：回到登录界面。Lock_Run() 会阻塞到自己画完界面
             * 并等到密码正确，所以这里先把按键和手指放掉，
             * 否则"还按着"的那一下会被登录页或重绘后的桌面吃掉。 */
            if (ev & EV_K0) /* 检测K0事件 */
            {
                while (KEY0_DOWN || KEY1_DOWN || KEY2_DOWN || WKUP_DOWN) delay_ms(10); /* 等待按键释放 */
                Game_Wait_Touch_Release(); /* 等待触摸释放 */
                menu_key_sync();            /* 别让这次的 K0 把下次吃掉 */

                Lock_Run(); /* 进入锁屏 */

                menu_key_sync(); /* 同步按键状态 */
                redraw = 1; /* 请求重绘 */
                break; /* 退出空闲循环 */
            }

            /* K1 / K2：主界面翻页（三页循环，和左右滑等价） */
            if (ev & EV_K1) /* 检测K1事件 */
            {
                page = (u8)((page + 1) % 3);        /* 下一页 */
                redraw = 1; /* 请求重绘 */
                break; /* 退出空闲循环 */
            }

            if (ev & EV_K2) /* 检测K2事件 */
            {
                page = (u8)((page + 2) % 3);        /* 上一页 */
                redraw = 1; /* 请求重绘 */
                break; /* 退出空闲循环 */
            }

            /* WK_UP：直接进相机 */
            if (ev & EV_WKUP) { app = 5; break; } /* 启动相机应用 */

            /* 三页循环：0 -> 1 -> 2 -> 0，两个方向都能滑 */
            if (ev & EV_SWIPE_L) /* 检测左滑 */
            {
                page = (u8)((page + 1) % 3); /* 切换到下一页 */
                redraw = 1; /* 请求重绘 */
                break; /* 退出空闲循环 */
            }

            if (ev & EV_SWIPE_R) /* 检测右滑 */
            {
                page = (u8)((page + 2) % 3); /* 切换到上一页 */
                redraw = 1; /* 请求重绘 */
                break; /* 退出空闲循环 */
            }

            if (ev & EV_TAP) /* 检测点击 */
            {
                if (page == 0) /* 第一页点击 */
                {
                    if (in_rect(tp_x, tp_y, ICON_SET_X, P1_ICON_Y, ICON_SZ, ICON_SZ)) /* 点击设置图标 */
                    {
                        Game_Wait_Touch_Release(); /* 等待触摸释放 */
                        Lock_Settings_Page();       /* 修改密码         */
                        redraw = 1; /* 请求重绘 */
                        break; /* 退出空闲循环 */
                    }

                    if (in_rect(tp_x, tp_y, ICON_ALB_X, P1_ICON_Y, ICON_SZ, ICON_SZ)) /* 点击相册图标 */
                    { app = 4; break; }             /* 相册             */

                    if (in_rect(tp_x, tp_y, ICON_CAM_X, P1_ICON_Y, ICON_SZ, ICON_SZ)) /* 点击相机图标 */
                    { app = 5; break; }             /* 相机             */

                    if (in_rect(tp_x, tp_y, ICON_FIL_X, P1_ICON_Y, ICON_SZ, ICON_SZ)) /* 点击文件图标 */
                    { app = 6; break; }             /* 文件浏览器       */
                }
                else if (page == 1) /* 第二页点击 */
                {
                    if (in_rect(tp_x, tp_y, APP_DINO_X,  P2_ICON_Y, ICON_SZ, ICON_SZ)) /* 点击恐龙图标 */
                    { app = 0; break; } /* 启动恐龙游戏 */

                    if (in_rect(tp_x, tp_y, APP_PLANE_X, P2_ICON_Y, ICON_SZ, ICON_SZ)) /* 点击飞机图标 */
                    { app = 1; break; } /* 启动飞机游戏 */

                    if (in_rect(tp_x, tp_y, APP_SNAKE_X, P2_ICON_Y, ICON_SZ, ICON_SZ)) /* 点击贪吃蛇图标 */
                    { app = 2; break; } /* 启动贪吃蛇游戏 */

                    if (in_rect(tp_x, tp_y, APP_HEART_X, P2_ICON_Y, ICON_SZ, ICON_SZ)) /* 点击心率图标 */
                    { app = 3; break; } /* 启动心率监测 */
                }
                else /* 第三页点击 */
                {
                    if (in_rect(tp_x, tp_y, APP_DAC_X, P3_ICON_Y, ICON_SZ, ICON_SZ)) /* 点击DAC图标 */
                    { app = 7; break; }             /* DAC 波形输出     */
                }
            }

            /* 先刷新时钟：读取传感器会阻塞约 35 ms（BMP180）
             * 到约 40 ms（DHT11），所以数字必须在那之前刷新，
             * 否则这一秒看起来会卡顿 */
            if (calendar.sec != clk_sec) /* 秒发生变化 */
            {
                clk_sec = calendar.sec; /* 更新上次秒 */
                draw_clock(); /* 刷新时钟 */

                if (calendar.w_date != clk_day)     /* 日期已翻页       */
                {
                    clk_day = calendar.w_date; /* 更新上次日期 */
                    draw_date(); /* 刷新日期 */
                }
            }

            /* 传感器只在第一页可见；每秒测量一次
             * （BMP180 约 35 ms，MQ-2 约 20 ms，两者都只在这里运行）。
             * DHT11 较慢，每 2 s 一次足够。 */
            if (page == 0) /* 第一页才刷新传感器 */
            {
                sen_tick++; /* 增加传感器计数 */
                if (sen_tick >= SENSOR_TICKS) /* 到达刷新阈值 */
                {
                    sen_tick = 0; /* 清零计数 */
                    sensor_refresh(); /* 刷新BMP180 */
                    gas_refresh(); /* 刷新MQ-2 */

                    if (++dht_div >= 2) /* DHT11每两次刷新一次 */
                    {
                        dht_div = 0; /* 清零分频 */
                        dht_refresh(); /* 刷新DHT11 */
                    }
                }
            }

            delay_ms(20); /* 空闲延时20ms */
        }

        if (app == 0xFF) continue;      /* 切页 / 设置：重绘               */

        /*---------------- 应用启动 ----------------*/
        Game_Wait_Touch_Release(); /* 等待触摸释放 */

        switch (app) /* 根据应用编号启动应用 */
        {
        case 1: /* 飞机大战 */
            loading_run(draw_plane_glyph, "PLANE WAR"); /* 显示加载动画 */
            Plane_Menu(); /* 进入飞机大战菜单 */
            break; /* 结束分支 */

        case 2: /* 贪吃蛇 */
            loading_run(draw_snake_glyph, "SNAKE"); /* 显示加载动画 */
            Snake_Menu(); /* 进入贪吃蛇菜单 */
            break; /* 结束分支 */

        case 3: /* 心率监测 */
            loading_run(draw_heart_glyph, "HEART MON"); /* 显示加载动画 */
            Heart_Run();                /* 按下 BACK 按钮时返回       */
            break; /* 结束分支 */

        case 4: /* 相册 */
            loading_run(draw_album_glyph, "ALBUM"); /* 显示加载动画 */
            Album_Run();                /* SD 卡照片                  */
            break; /* 结束分支 */

        case 5: /* 相机 */
            loading_run(draw_camera_glyph, "CAMERA"); /* 显示加载动画 */
            Camera_Run();               /* OV7725 实时预览            */
            page = 0;                   /* 相机图标在第一页，回来停在第一页 */
            break; /* 结束分支 */

        case 6: /* 文件浏览器 */
            loading_run(draw_folder_glyph, "FILE MANAGER"); /* 显示加载动画 */
            Explorer_Run();             /* SD 卡文件浏览器：K0 返回   */
            page = 0;                   /* 一律回到第一页（FILES 就在这一页） */
            break; /* 结束分支 */

        case 7: /* DAC波形输出 */
            loading_run(draw_wave_glyph, "DAC WAVE"); /* 显示加载动画 */
            DacApp_Run();               /* K0 / HOME 返回             */
            page = 2;                   /* 回来还停在第三页           */
            break; /* 结束分支 */

        default: /* 恐龙游戏 */
            loading_run(draw_dino_glyph, "DINO RUN"); /* 显示加载动画 */
            Game_Menu();                /* 选择 HOME 时返回            */
            break; /* 结束分支 */
        }

        /* 应用返回后必须等所有按键松开，再重绘桌面。
         * 应用运行期间 menu_scan() 根本不会被调用，四个 last 变量
         * 停在进入应用之前的值；万一返回时某个键还按着，空闲桌面
         * 就会把"还按着"当成一次新按下（以前的表现是刚从相册回来
         * 就立刻又跳进某个游戏）。这里等松手之后再把状态同步一次。 */
        while (KEY0_DOWN || KEY1_DOWN || KEY2_DOWN || WKUP_DOWN) delay_ms(10); /* 等待所有按键释放 */

        Game_Wait_Touch_Release(); /* 等待触摸释放 */
        menu_key_sync(); /* 同步按键状态 */
        redraw = 1; /* 请求重绘桌面 */
    }
}
