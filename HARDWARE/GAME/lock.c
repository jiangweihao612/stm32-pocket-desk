/*=============================================================================
 *  密码锁屏（320 x 240 横屏）
 *
 *   +--------------------------------------------------+
 *   | 09:41:07        2026-11-02          signal  batt  |  状态栏  0..19
 *   |                                                  |
 *   |                ENTER PASSWORD                    |  标题      28
 *   |          [ ] [ ] [ ] [ ] [ ] [ ]                 |  6 位数字   44..66
 *   |              WRONG PASSWORD                      |  提示信息    68
 *   |    +------+ +------+ +------+                    |
 *   |    |  1   | |  2   | |  3   |                    |  键盘     82
 *   |    +------+ +------+ +------+                    |
 *   |    |  4   | |  5   | |  6   |                    |
 *   |    |  7   | |  8   | |  9   |  H                 |
 *   |    | DEL  | |  0   | |  OK  |  O  <- HOME        |         ..227
 *   |                                M  (K0 也可)      |
 *   +--------------------------------------------------+
 *
 *  密码是六个 ASCII 数字，存在 AT24C02 里，所以掉电
 *  也不会丢。EEPROM 的 40..54 放触摸校准数据，60..67 放游戏
 *  分数，因此密码放在 128（标志位）+ 129..134（各位数字）。
 *
 *  当 EEPROM 为空时（新芯片，或者读回来的内容
 *  不是数字），就使用出厂密码 123456，并在下次保存时写入。
 *===========================================================================*/

#include "lock.h"
#include "lcd.h"
#include "touch.h"
#include "delay.h"
#include "rtc.h"
#include "24cxx.h"
#include "key.h"
#include <stdio.h>
#include <string.h>

/*-------------------- EEPROM 映射 -----------------*/
#define EE_PWD_MAGIC    128
#define EE_PWD_BASE     129
#define PWD_LEN         6
/* 故意用 0xA6 代替 0xA5：第一版把键盘整体错了一位
 * （按"1"存成 '0'），所以它存下的密码现在都是错位的垃圾。用一个新
 * 标志位可以让那些数据块一次性作废，恢复出厂密码。                      */
#define PWD_MAGIC_VAL   0xA6

/*-------------------- 颜色 --------------------*/
/* 和桌面同一套日落暖色：背景深咖渐变，按键暖棕，确认用琥珀 */
#define L_BG0       0x30E4  /* 背景上端：深咖 #331E22        */
#define L_BG1       0x4944  /* 背景中段                      */
#define L_BG2       0x5984  /* 背景下端                      */
#define L_BAR       0x30E3  /* 标题栏：深咖啡                */
#define L_TEXT      0xFF9B  /* 主文字：米白                  */
#define L_DIM       0xB4D0  /* 次要文字：暖灰                */
#define L_KEY       0x3923  /* 数字键：暖棕                  */
#define L_KEY_HI    0xA285  /* 数字键按下：陶土              */
#define L_BACK      0xA285  /* 返回键：陶土                  */
#define L_OK        0x6CC5  /* 确认：橄榄绿                  */
#define L_DEL       0x8AC7  /* 删除：暗棕                    */
#define L_GREEN     0xE4C7  /* 进度 / 状态：琥珀             */
#define L_AMBER     0xECA7  /* 提示：琥珀                    */
#define L_ERR       0xE225  /* 错误：暖红                    */

/*-------------------- 布局 ---------------------*/
#define BAR_H       20

#define TITLE_Y     26
#define DOT_X       74
#define DOT_Y       44
#define DOT_W       22
#define DOT_H       22
#define DOT_GAP     30
#define MSG_Y       68

#define KP_X        52
#define KP_Y        82
#define KP_W        68
#define KP_H        32
#define KP_GX       74
#define KP_GY       38

/* 右侧留白正好 52 px(键盘到 x=268 结束),整条做成 HOME:
 * 44 x 138 的触摸区,手指按得中,也不用在标题旁边挤一个小按钮。 */
#define HOME_X      272
#define HOME_Y      86
#define HOME_W      44
#define HOME_H      138
#define HOME_PITCH  26

/*-------------------- 状态 ----------------------*/
static u16 sw = 320;
static u16 sh = 240;

static char pwd[PWD_LEN + 1] = "123456";

static u8  tp_last = 0;
static u16 tx = 0;
static u16 ty = 0;

static const char *kp_label[12] =
{
    "1", "2", "3",
    "4", "5", "6",
    "7", "8", "9",
    "DELETE", "0", "CONFIRM"
};

/*=============================================================================
 *  绘图工具函数
 *===========================================================================*/

static void lfill(int x, int y, int w, int h, u16 c)
{
    if (w <= 0 || h <= 0) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;

    if (x + w > (int)sw) w = (int)sw - x;
    if (y + h > (int)sh) h = (int)sh - y;
    if (w <= 0 || h <= 0) return;

    LCD_Fill((u16)x, (u16)y, (u16)(x + w - 1), (u16)(y + h - 1), c);
}

static void ltext(int x, int y, u8 size, char *s, u16 fg, u16 bg)
{
    POINT_COLOR = fg;
    BACK_COLOR  = bg;
    LCD_ShowString((u16)x, (u16)y, 250, size + 4, size, (u8 *)s);
}

static void lcenter(int y, u8 size, char *s, u16 fg, u16 bg)
{
    u16 n = 0;
    char *p = s;

    while (*p++) n++;

    ltext((int)((sw - n * (size / 2)) / 2), y, size, s, fg, bg);
}

static u8 in_rect(u16 x, u16 y, u16 rx, u16 ry, u16 rw, u16 rh)
{
    return (x >= rx && x < (u16)(rx + rw) && y >= ry && y < (u16)(ry + rh)) ? 1 : 0;
}

/*=============================================================================
 *  状态栏 -- 与桌面各页上的完全一致，这样锁屏
 *  交接到启动器时时钟不会跳动
 *===========================================================================*/

static void l_clock(void)
{
    char buf[10];

    sprintf(buf, "%02d:%02d:%02d", calendar.hour, calendar.min, calendar.sec);

    lfill(0, 0, 62, BAR_H, L_BAR);
    ltext(8, 4, 12, buf, L_TEXT, L_BAR);
}

static void l_date(void)
{
    char buf[14];

    sprintf(buf, "%04d-%02d-%02d", calendar.w_year, calendar.w_month,
            calendar.w_date);

    lfill(120, 0, 80, BAR_H, L_BAR);
    ltext(130, 4, 12, buf, L_DIM, L_BAR);
}

static void l_bar(void)
{
    u8 i;

    lfill(0, 0, sw, BAR_H, L_BAR);

    l_clock();
    l_date();

    for (i = 0; i < 4; i++)
    {
        lfill(248 + i * 5, 17 - (i + 1) * 3, 3, (i + 1) * 3, L_TEXT);
    }

    lfill(276, 4, 30, 12, L_TEXT);
    lfill(278, 6, 26, 8, L_BAR);
    lfill(278, 6, 19, 8, L_GREEN);
    lfill(306, 7, 3, 6, L_TEXT);
}

/*=============================================================================
 *  背景 / 固定部分
 *===========================================================================*/

/* 色带边界特意与键盘对齐（y = 82 .. 227）：
 * 如果色带分界落在键位网格中间，两个键之间的每条缝隙
 * 都会显出略微不同的颜色，键盘看起来就像打了补丁。                      */
static void lock_bg(void)
{
    lfill(0, 0, sw, sh, L_BG1);
    lfill(0, BAR_H, sw, KP_Y - BAR_H, L_BG0);        /* 20 .. 81  */
    lfill(0, KP_Y, sw, sh - KP_Y, L_BG2);            /* 82 .. 239 */
}

static void lock_title(char *s)
{
    lfill(0, TITLE_Y, sw, 18, L_BG0);
    lcenter(TITLE_Y, 16, s, L_TEXT, L_BG0);
}

static void lock_msg(char *s, u16 color)
{
    lfill(0, MSG_Y, sw, 14, L_BG0);
    lcenter(MSG_Y, 12, s, color, L_BG0);
}

/* 六个方块，前 n 个填充。err 非零时把它们画成红色而不是白色 */
static void lock_dots(u8 n, u8 err)
{
    u8 i;

    lfill(0, DOT_Y - 2, sw, DOT_H + 4, L_BG0);

    for (i = 0; i < PWD_LEN; i++)
    {
        int x = DOT_X + i * DOT_GAP;
        u16 body = (i < n) ? (err ? L_ERR : L_TEXT) : L_BG1;
        u16 edge = err ? L_ERR : ((i < n) ? L_TEXT : L_DIM);

        lfill(x, DOT_Y, DOT_W, DOT_H, body);

        lfill(x, DOT_Y, DOT_W, 1, edge);
        lfill(x, DOT_Y + DOT_H - 1, DOT_W, 1, edge);
        lfill(x, DOT_Y, 1, DOT_H, edge);
        lfill(x + DOT_W - 1, DOT_Y, 1, DOT_H, edge);
    }
}

/* 竖排 HOME:4 个字母各自一行,16 px 字高、26 px 行距,
 * 整块 44 x 138 都是可点区域(见 HOME_X/HOME_Y/HOME_W/HOME_H)。*/
static void lock_home_btn(void)
{
    static const char *t = "HOME";
    char one[2];
    u8 i;

    one[1] = 0;

    lfill(HOME_X, HOME_Y, HOME_W, HOME_H, L_BACK);
    lfill(HOME_X, HOME_Y, HOME_W, 1, L_TEXT);        /* 1 px 顶部高光 */

    for (i = 0; i < 4; i++)
    {
        one[0] = t[i];
        ltext(HOME_X + (HOME_W - 8) / 2,
              HOME_Y + 22 + i * HOME_PITCH, 16, one, L_TEXT, L_BACK);
    }
}

static void lock_keypad(void)
{
    u8 i;

    for (i = 0; i < 12; i++)
    {
        int c = i % 3;
        int r = i / 3;
        int x = KP_X + c * KP_GX;
        int y = KP_Y + r * KP_GY;
        u16 bg = (i == 9) ? L_DEL : ((i == 11) ? L_OK : L_KEY);
        u16 fg = L_TEXT;

        lfill(x, y, KP_W, KP_H, bg);
        lfill(x, y, KP_W, 1, L_KEY_HI);          /* 1 px 顶部高光 */

        if (i < 9)                               /* 数字键 */
        {
            ltext(x + (KP_W - 12) / 2, y + (KP_H - 24) / 2, 24,
                  (char *)kp_label[i], fg, bg);
        }
        else                                     /* DELETE / CONFIRM 键 */
        {
            int len = (int)strlen(kp_label[i]);

            ltext(x + (KP_W - len * 6) / 2, y + (KP_H - 12) / 2, 12,
                  (char *)kp_label[i], fg, bg);
        }
    }
}

/*=============================================================================
 *  输入
 *===========================================================================*/

/* 每个按下动作只产生一个事件，这样按住键不会连续重复 */
static u8 lock_touch(void)
{
    u8 ev = 0;

    if (tp_dev.scan != 0)
    {
        if (tp_dev.scan(0))
        {
            if (!tp_last && tp_dev.x[0] < sw && tp_dev.y[0] < sh)
            {
                tx = tp_dev.x[0];
                ty = tp_dev.y[0];
                ev = 1;
            }
            tp_last = 1;
        }
        else
        {
            tp_last = 0;
        }
    }

    return ev;
}

/* 键位网格索引，排布与 kp_label[] 完全一致：
 *
 *      0  1  2        "1" "2" "3"
 *      3  4  5        "4" "5" "6"
 *      6  7  8        "7" "8" "9"
 *      9 10 11        DEL "0" OK
 *
 *  所以 0..8 -> '1'..'9'，9 -> DELETE，10 -> '0'，11 -> CONFIRM。
 *  -1 = 没有按键。                                                */
#define KEY_DEL     9
#define KEY_ZERO    10
#define KEY_OK      11

static int lock_key(int x, int y)
{
    int c;
    int r;

    if (x < KP_X || y < KP_Y) return -1;

    c = (x - KP_X) / KP_GX;
    r = (y - KP_Y) / KP_GY;

    if (c > 2 || r > 3) return -1;

    if (((x - KP_X) % KP_GX) >= KP_W) return -1;    /* 落在两个键之间 */
    if (((y - KP_Y) % KP_GY) >= KP_H) return -1;

    return r * 3 + c;
}

/*=============================================================================
 *  EEPROM 读写
 *===========================================================================*/

static void lock_load(void)
{
    char tmp[PWD_LEN];
    u8   i;

    strcpy(pwd, "123456");

    if (AT24CXX_ReadOneByte(EE_PWD_MAGIC) != PWD_MAGIC_VAL) return;

    /* 先读到临时缓冲区：半有效的数据块不能把
     * 出厂密码覆盖掉一半                                                  */
    for (i = 0; i < PWD_LEN; i++)
    {
        tmp[i] = (char)AT24CXX_ReadOneByte(EE_PWD_BASE + i);

        if (tmp[i] < '0' || tmp[i] > '9') return;   /* 数据损坏：保留默认值 */
    }

    for (i = 0; i < PWD_LEN; i++) pwd[i] = tmp[i];

    pwd[PWD_LEN] = 0;
}

static void lock_save(char *p)
{
    u8 i;

    for (i = 0; i < PWD_LEN; i++)
    {
        AT24CXX_WriteOneByte(EE_PWD_BASE + i, (u8)p[i]);
    }

    /* 标志位最后写：半个密码会被检测出来并回落到默认值 */
    AT24CXX_WriteOneByte(EE_PWD_MAGIC, PWD_MAGIC_VAL);
}

/*=============================================================================
 *  对外入口函数
 *===========================================================================*/

void Lock_Init(void)
{
    lock_load();
}

void Lock_Run(void)
{
    char cur[PWD_LEN];
    u8   n = 0;
    u8   last_sec;
    u8   last_day;
    int  k;

    sw = lcddev.width;
    sh = lcddev.height;

    lock_load();

    lock_bg();
    l_bar();
    lock_title((char *)"ENTER PASSWORD");
    lock_dots(0, 0);
    lock_msg((char *)"ENTER 6-DIGIT CODE", L_DIM);
    lock_keypad();

    last_sec = calendar.sec;
    last_day = calendar.w_date;
    tp_last = 0;

    for (;;)
    {
        if (lock_touch())
        {
            k = lock_key((int)tx, (int)ty);

            if (k >= 0 && k <= 8)                   /* "1" .. "9" */
            {
                if (n < PWD_LEN)
                {
                    cur[n++] = (char)('1' + k);
                    lock_dots(n, 0);
                    lock_msg((char *)"", L_DIM);
                }
            }
            else if (k == KEY_ZERO)                 /* "0" */
            {
                if (n < PWD_LEN)
                {
                    cur[n++] = '0';
                    lock_dots(n, 0);
                    lock_msg((char *)"", L_DIM);
                }
            }
            else if (k == KEY_DEL)                  /* 删除键 */
            {
                if (n > 0) n--;
                lock_dots(n, 0);
                lock_msg((char *)"", L_DIM);
            }
            else if (k == KEY_OK)                   /* 确认键 */
            {
                if (n < PWD_LEN)
                {
                    lock_msg((char *)"ENTER 6 DIGITS", L_AMBER);
                }
                else if (memcmp(cur, pwd, PWD_LEN) == 0)
                {
                    lock_msg((char *)"ACCESS GRANTED", L_GREEN);
                    delay_ms(450);
                    return;
                }
                else
                {
                    lock_msg((char *)"WRONG PASSWORD", L_ERR);
                    lock_dots(PWD_LEN, 1);          /* 闪成红色 */

                    delay_ms(700);

                    n = 0;
                    lock_dots(0, 0);
                    lock_msg((char *)"TRY AGAIN", L_DIM);
                }
            }
        }

        if (calendar.sec != last_sec)
        {
            last_sec = calendar.sec;
            l_clock();

            if (calendar.w_date != last_day)
            {
                last_day = calendar.w_date;
                l_date();
            }
        }

        delay_ms(20);
    }
}

void Lock_Settings_Page(void)
{
    char n1[PWD_LEN];
    char n2[PWD_LEN];
    u8   c1 = 0;
    u8   c2 = 0;
    u8   stage = 0;                 /* 0: 输入新密码，1: 再输一遍 */
    u8   last_sec;
    u8   last_day;
    int  k;

    sw = lcddev.width;
    sh = lcddev.height;

    lock_load();

    lock_bg();
    l_bar();
    lock_home_btn();
    lock_title((char *)"NEW PASSWORD");
    lock_dots(0, 0);
    lock_msg((char *)"ENTER 6 NEW DIGITS", L_DIM);
    lock_keypad();

    last_sec = calendar.sec;
    last_day = calendar.w_date;
    tp_last = 0;

    for (;;)
    {
        /* K0:直接回主界面。等松手再走,否则回到主界面时这根线还按着,
         * 菜单那边会立刻收到一次按下事件。                       */
        if (KEY0_DOWN)
        {
            while (KEY0_DOWN) delay_ms(10);
            return;
        }

        if (lock_touch())
        {
            /* 右侧整条 HOME 都是返回区 */
            if (in_rect(tx, ty, HOME_X, HOME_Y, HOME_W, HOME_H)) return;

            k = lock_key((int)tx, (int)ty);

            if (k >= 0 && k <= 8)                   /* "1" .. "9" */
            {
                if (stage == 0)
                {
                    if (c1 < PWD_LEN) n1[c1++] = (char)('1' + k);
                    lock_dots(c1, 0);
                }
                else
                {
                    if (c2 < PWD_LEN) n2[c2++] = (char)('1' + k);
                    lock_dots(c2, 0);
                }

                lock_msg((char *)"", L_DIM);
            }
            else if (k == KEY_ZERO)                 /* "0" */
            {
                if (stage == 0)
                {
                    if (c1 < PWD_LEN) n1[c1++] = '0';
                    lock_dots(c1, 0);
                }
                else
                {
                    if (c2 < PWD_LEN) n2[c2++] = '0';
                    lock_dots(c2, 0);
                }

                lock_msg((char *)"", L_DIM);
            }
            else if (k == KEY_DEL)                  /* 删除键 */
            {
                if (stage == 0) { if (c1 > 0) c1--; lock_dots(c1, 0); }
                else            { if (c2 > 0) c2--; lock_dots(c2, 0); }

                lock_msg((char *)"", L_DIM);
            }
            else if (k == KEY_OK)                   /* 确认键 */
            {
                if (stage == 0)
                {
                    if (c1 < PWD_LEN)
                    {
                        lock_msg((char *)"ENTER 6 DIGITS", L_AMBER);
                    }
                    else
                    {
                        stage = 1;
                        c2 = 0;
                        lock_title((char *)"CONFIRM NEW CODE");
                        lock_dots(0, 0);
                        lock_msg((char *)"TYPE IT AGAIN", L_DIM);
                    }
                }
                else
                {
                    if (c2 < PWD_LEN)
                    {
                        lock_msg((char *)"ENTER 6 DIGITS", L_AMBER);
                    }
                    else if (memcmp(n1, n2, PWD_LEN) == 0)
                    {
                        lock_save(n1);
                        memcpy(pwd, n1, PWD_LEN);
                        pwd[PWD_LEN] = 0;

                        lock_msg((char *)"PASSWORD SAVED", L_GREEN);
                        delay_ms(800);
                        return;
                    }
                    else
                    {
                        lock_msg((char *)"NOT MATCH - START OVER", L_ERR);
                        lock_dots(PWD_LEN, 1);

                        delay_ms(800);

                        stage = 0;
                        c1 = 0;
                        c2 = 0;
                        lock_title((char *)"NEW PASSWORD");
                        lock_dots(0, 0);
                        lock_msg((char *)"ENTER 6 NEW DIGITS", L_DIM);
                    }
                }
            }
        }

        if (calendar.sec != last_sec)
        {
            last_sec = calendar.sec;
            l_clock();

            if (calendar.w_date != last_day)
            {
                last_day = calendar.w_date;
                l_date();
            }
        }

        delay_ms(20);
    }
}
