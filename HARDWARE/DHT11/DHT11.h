#ifndef __DHT11_H__
#define __DHT11_H__

#include "stm32f10x.h"
#include <stdint.h>

// DHT11 数据引脚 = PA15
// 注意1：PB0 在本工程是 LCD 背光控制脚（lcd.h: #define LCD_LED PBout(0)）。
//        读 DHT11 要先拉低数据线 18ms 发起始信号，若接 PB0，背光会跟着熄灭，
//        表现为主界面每隔 2 秒整屏闪一下，所以不能用 PB0。
// PORTB 已占用：PB0=背光 PB1/PB2=触摸 PB3/PB4=摄像头时钟 PB9=红外接收
//              PB6/PB7=BMP180 PB10/PB11=24C02
// 注意2：PA15 复位后是 JTAG 的 JTDI 脚，DHT11_Init() 里先调用
//        GPIO_Remap_SWJ_JTAGDisable 关掉 JTAG（SWD 的 PA13/PA14 保留，
//        下载和调试不受影响），再把它当普通 IO 用。
#define DHT11_GPIO_PORT     GPIOA
#define DHT11_GPIO_PIN      GPIO_Pin_15
#define DHT11_GPIO_CLK      RCC_APB2Periph_GPIOA   // 对应的时钟

// 函数声明（与 Nios II 版本完全一致）
void DHT11_Rst(void);
uint8_t DHT11_Check(void);
uint8_t DHT11_Read_Bit(void);
uint8_t DHT11_Read_Byte(void);
uint8_t DHT11_Read_Data(uint8_t *temp, uint8_t *humi);
uint8_t DHT11_Init(void);

#endif
