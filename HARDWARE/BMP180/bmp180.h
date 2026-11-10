#ifndef __BMP180_H
#define __BMP180_H

#include "sys.h"

/*******************************************************************************
 * BMP180 (GY-68 / MK000808) 气压 / 温度 / 高度驱动
 * 软件 I2C，位操作实现，引脚映射可在运行时切换。
 *
 * 本工程使用的接线：
 *      VCC -> 3.3V      SCL -> PB7
 *      GND -> GND       SDA -> PB6
 *
 * 驱动先试这种映射，再试反过来的一种，因此
 * SCL/SDA 接反的情况会被自动检测并纠正。
 *
 * 本模块只负责传感器。显示这三个数值的桌面传感器卡片
 * 位于 menu.c 中（draw_sensor_card / sensor_refresh）。
 ******************************************************************************/

/* ---------------- 芯片定义 ------------------- */
#define BMP180_ADDR             0xEE        /* 0x77 << 1 : 写地址             */
#define BMP180_CHIP_ID_REG      0xD0
#define BMP180_CHIP_ID          0x55        /* BMP180 的 ID                    */
#define BMP280_CHIP_ID          0x58        /* BMP280 的 ID，同一个寄存器     */
#define BMP180_CTRL_MEAS_REG    0xF4
#define BMP180_DATA_REG         0xF6
#define BMP180_CALIB_REG        0xAA        /* 11 个 16 位校准值              */
#define BMP180_CMD_TEMP         0x2E
#define BMP180_CMD_PRESS        (0x34)

/* 过采样设置: 0 -> 1 倍, 1 -> 2 倍, 2 -> 4 倍, 3 -> 8 倍
 * 每提高一级，单次测量的转换时间约增加 6 ms */
#define BMP180_OSS              3

/* 海平面气压，用于高度计算 (Pa) */
#define BMP180_SEA_LEVEL_PA     101325.0f

/* 气压软件低通滤波的权重 (0 ~ 1) */
#define BMP180_FILTER           0.2f

/* ---------------- 诊断信息 ----------------------- */
extern uint8_t BMP180_IdA;          /* SCL=PB7, SDA=PB6 时读到的芯片 ID      */
extern uint8_t BMP180_IdB;          /* SCL=PB6, SDA=PB7 时读到的芯片 ID      */
extern uint8_t BMP180_PinSwapped;   /* 1: 接线接反了，已在运行时纠正         */
extern uint8_t BMP180_BusLevel;     /* bit1 = SCL 空闲电平，bit0 = SDA       */
extern uint8_t BMP180_ScanCnt;      /* 总线扫描时有应答的器件数              */
extern uint8_t BMP180_ScanList[8];  /* 它们的 7 位地址                       */
extern uint8_t BMP180_ScanSwapped;  /* 1: 反接引脚时找到了器件               */

/* ---------------- 驱动接口 -------------------------- */
void    BMP180_IIC_Init(void);
uint8_t BMP180_ReadID(void);
uint8_t BMP180_Init(void);          /* 1 = 找到芯片，校准值已载入          */
void    BMP180_GetData(float *temperature, float *pressure, float *altitude);
int32_t BMP180_ReadPressurePa(void);
uint8_t BMP180_Probe(uint8_t addr7);
void    BMP180_ScanBus(void);

#endif
