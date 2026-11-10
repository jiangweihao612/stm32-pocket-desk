#include "dht11.h"
#include "delay.h"          // 提供 delay_us, delay_ms

// 设置引脚为输出模式（推挽输出）
static void DHT11_SetOutput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = DHT11_GPIO_PIN;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;   // 推挽输出
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStruct);
}

// 设置引脚为输入模式（上拉输入）
static void DHT11_SetInput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = DHT11_GPIO_PIN;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;      // 上拉输入（内部上拉）
    GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStruct);
}

// 拉高数据线
static void DHT11_High(void)
{
    GPIO_SetBits(DHT11_GPIO_PORT, DHT11_GPIO_PIN);
}

// 拉低数据线
static void DHT11_Low(void)
{
    GPIO_ResetBits(DHT11_GPIO_PORT, DHT11_GPIO_PIN);
}

// 读取引脚电平
static uint8_t DHT11_ReadPin(void)
{
    return GPIO_ReadInputDataBit(DHT11_GPIO_PORT, DHT11_GPIO_PIN);
}

// =========== 以下是移植自 Nios II 的原始函数 ===========

// 复位DHT11（主机发送起始信号）
void DHT11_Rst(void)
{
    DHT11_SetOutput();              // 设置为输出
    DHT11_High();                   // 初始拉高
    delay_us(25);                   // 保持25us高电平

    DHT11_Low();                    // 拉低至少18ms
    delay_ms(18);

    DHT11_High();                   // 拉高20~40us
    delay_us(30);
}

// 等待DHT11的回应
// 返回1: 未检测到DHT11的存在；返回0: 存在
uint8_t DHT11_Check(void)
{
    uint8_t retry = 0;
    DHT11_SetInput();               // 切换为输入

    // 等待DHT11拉低（响应信号），最多等待100us
    while (DHT11_ReadPin() && retry < 100)
    {
        retry++;
        delay_us(1);
    }
    if (retry >= 100) return 1;     // 超时，不存在

    retry = 0;
    // 等待DHT11拉高（准备发送数据），最多等待100us
    while (!DHT11_ReadPin() && retry < 100)
    {
        retry++;
        delay_us(1);
    }
    if (retry >= 100) return 1;     // 超时，不存在

    return 0;                       // 正常响应
}

// 从DHT11读取一个位
// 返回值：1/0
uint8_t DHT11_Read_Bit(void)
{
    uint8_t retry = 0;
    DHT11_SetInput();

    // 等待变为低电平
    while (DHT11_ReadPin() && retry < 100)
    {
        retry++;
        delay_us(1);
    }
    retry = 0;
    // 等待变为高电平
    while (!DHT11_ReadPin() && retry < 100)
    {
        retry++;
        delay_us(1);
    }

    delay_us(40);   // 延时40us后采样
    if (DHT11_ReadPin())
        return 1;
    else
        return 0;
}

// 从DHT11读取一个字节
// 返回值：读到的数据
uint8_t DHT11_Read_Byte(void)
{
    uint8_t i, dat = 0;
    for (i = 0; i < 8; i++)
    {
        dat <<= 1;
        dat |= DHT11_Read_Bit();
    }
    return dat;
}

// 从DHT11读取一次数据
// temp: 温度值(范围:0~50°)
// humi: 湿度值(范围:20%~90%)
// 返回值：0,正常; 1,读取失败
uint8_t DHT11_Read_Data(uint8_t *temp, uint8_t *humi)
{
    uint8_t buf[5];
    uint8_t i;

    DHT11_Rst();
    if (DHT11_Check() == 0)
    {
        for (i = 0; i < 5; i++)   // 读取40位数据
        {
            buf[i] = DHT11_Read_Byte();
        }
        // 校验和
        if ((buf[0] + buf[1] + buf[2] + buf[3]) == buf[4])
        {
            *humi = buf[0];
            *temp = buf[2];
        }
        else
        {
            return 1;   // 校验失败
        }
    }
    else
    {
        return 1;       // 响应失败
    }
    return 0;           // 成功
}

// 初始化DHT11的IO口，同时检测DHT11是否存在
// 返回1: 不存在；返回0: 存在
uint8_t DHT11_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    // 使能GPIO时钟
    RCC_APB2PeriphClockCmd(DHT11_GPIO_CLK, ENABLE);

    // 数据线在 PA15 上，而 PA15 复位后是 JTAG 的 JTDI。
    // 不关 JTAG 的话，GPIO_Init 配好了也不会被当普通 IO 用，读到的永远是高电平。
    // 只关 JTAG，SWD（PA13/PA14）保留，下载和调试都不受影响。
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    // 空闲状态：上拉输入（DHT11 数据线是开漏，靠上拉保持高电平）
    GPIO_InitStruct.GPIO_Pin   = DHT11_GPIO_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStruct);

    DHT11_Rst();                // 复位DHT11
    return DHT11_Check();       // 等待DHT11的回应
}
