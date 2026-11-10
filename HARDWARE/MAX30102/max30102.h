#ifndef __MAX30102_H
#define __MAX30102_H
#include "stm32f10x.h"
#include "stdbool.h"
#include "sys.h"




/* 引脚接法
 *   SDA -> PE0
 *   SCL -> PE1
 *   INT -> PE6   本工程轮询 FIFO，INT 可以不接
 *   SCL/SDA 需 4.7k 上拉到 3.3V
 *
 * 三根线全在 PE 口，而且都在 J3 扩展排针上：PE1/PE0 是 pin1/pin2，
 * PE6 是 pin8，接插件走线最省事。
 *
 * PE0/PE1 在 F103ZET6 上是 FSMC_NBL0/NBL1，本板 LCD 走 FSMC 但没用到
 * 字节使能这两根，所以完全空闲；PE6 板载也只引到排针。
 *
 * 为什么不用 PA4/PA5：它们本身不冲突（板上 SPI1 没接器件），但换到 PE 口
 * 可以把 MAX30102 三根线收敛到同一个排针，也把 PA4/PA5/PA7 留给以后
 * 可能的硬件 SPI1（不过 PA6 已给 MQ-2 的 ADC，SPI1 实际组不起来）。
 *
 * 为什么把 INT 从 PB9 挪到 PE6：PB9 在原理图上是板载红外接收头 IR1308
 * 的输出脚，MAX30102 的 INT 也是输出，两个输出顶在同一根线上会互相灌电流。
 */
//==============================================MAX30102硬件接口==================================================
#define		MAX30102_IIC_CLK				RCC_APB2Periph_GPIOE
#define		MAX30102_IIC_PORT				GPIOE
#define		MAX30102_IIC_SCL_PIN		GPIO_Pin_1
#define		MAX30102_IIC_SDA_PIN		GPIO_Pin_0

#define 	MAX30102_IIC_SCL				PEout(1)
#define 	MAX30102_IIC_SDA				PEout(0)
#define 	MAX30102_READ_SDA   		PEin(0)  //输入SDA 

#define		MAX30102_INT_CLK				RCC_APB2Periph_GPIOE
#define		MAX30102_INT_PORT				GPIOE
#define		MAX30102_INT_PIN		    GPIO_Pin_6
#define		MAX30102_INT            PEin(6)

//=============================================================================================================

//////////////////////////////////////////////////////////////////////////////// 	  

#define I2C_WR	0		/* 写控制bit */
#define I2C_RD	1		/* 读控制bit */

#define I2C_WRITE_ADDR 0xAE
#define I2C_READ_ADDR 0xAF


#define true 1
#define false 0
#define FS 100
#define BUFFER_SIZE  (FS* 5) 
#define HR_FIFO_SIZE 7
#define MA4_SIZE  4 // 请勿修改
#define HAMMING_SIZE  5// 请勿修改
#define min(x,y) ((x) < (y) ? (x) : (y))



#define max30102_WR_address 0xAE

#define I2C_WRITE_ADDR 0xAE
#define I2C_READ_ADDR 0xAF

//寄存器地址
#define REG_INTR_STATUS_1 0x00
#define REG_INTR_STATUS_2 0x01
#define REG_INTR_ENABLE_1 0x02
#define REG_INTR_ENABLE_2 0x03
#define REG_FIFO_WR_PTR 0x04
#define REG_OVF_COUNTER 0x05
#define REG_FIFO_RD_PTR 0x06
#define REG_FIFO_DATA 0x07
#define REG_FIFO_CONFIG 0x08
#define REG_MODE_CONFIG 0x09
#define REG_SPO2_CONFIG 0x0A
#define REG_LED1_PA 0x0C
#define REG_LED2_PA 0x0D
#define REG_PILOT_PA 0x10
#define REG_MULTI_LED_CTRL1 0x11
#define REG_MULTI_LED_CTRL2 0x12
#define REG_TEMP_INTR 0x1F
#define REG_TEMP_FRAC 0x20
#define REG_TEMP_CONFIG 0x21
#define REG_PROX_INT_THRESH 0x30
#define REG_REV_ID 0xFE
#define REG_PART_ID 0xFF


//IIC所有操作函数
void MAX30102_IIC_Init(void);                //初始化IIC的IO口				 
void MAX30102_IIC_Start(void);				//发送IIC开始信号
void MAX30102_IIC_Stop(void);	  			//发送IIC停止信号
void MAX30102_IIC_Send_Byte(u8 txd);			//IIC发送一个字节
u8 MAX30102_IIC_Read_Byte(unsigned char ack);//IIC读取一个字节
u8 MAX30102_IIC_Wait_Ack(void); 				//IIC等待ACK信号
void MAX30102_IIC_Ack(void);					//IIC发送ACK信号
void MAX30102_IIC_NAck(void);				//IIC不发送ACK信号

void MAX30102_IIC_Write_One_Byte(u8 daddr,u8 addr,u8 data);
void MAX30102_IIC_Read_One_Byte(u8 daddr,u8 addr,u8* data);

void MAX30102_IIC_WriteBytes(u8 WriteAddr,u8* data,u8 dataLength);
void MAX30102_IIC_ReadBytes(u8 deviceAddr, u8 writeAddr,u8* data,u8 dataLength);

//MAX30102所有操作函数
void MAX30102_Init(void);  
void MAX30102_Reset(void);
u8 max30102_Bus_Write(u8 Register_Address, u8 Word_Data);
u8 max30102_Bus_Read(u8 Register_Address);
void max30102_FIFO_ReadWords(u8 Register_Address,u16  Word_Data[][2],u8 count);
void max30102_FIFO_ReadBytes(u8 Register_Address,u8* Data);

void maxim_max30102_write_reg(uint8_t uch_addr, uint8_t uch_data);
void maxim_max30102_read_reg(uint8_t uch_addr, uint8_t *puch_data);
void maxim_max30102_read_fifo(uint32_t *pun_red_led, uint32_t *pun_ir_led);

//心率血氧算法所有函数
void maxim_heart_rate_and_oxygen_saturation(uint32_t *pun_ir_buffer ,  int32_t n_ir_buffer_length, uint32_t *pun_red_buffer ,   int32_t *pn_spo2, int8_t *pch_spo2_valid ,  int32_t *pn_heart_rate , int8_t  *pch_hr_valid);
void maxim_find_peaks( int32_t *pn_locs, int32_t *pn_npks,  int32_t *pn_x, int32_t n_size, int32_t n_min_height, int32_t n_min_distance, int32_t n_max_num );
void maxim_peaks_above_min_height( int32_t *pn_locs, int32_t *pn_npks,  int32_t *pn_x, int32_t n_size, int32_t n_min_height );
void maxim_remove_close_peaks( int32_t *pn_locs, int32_t *pn_npks,   int32_t  *pn_x, int32_t n_min_distance );
void maxim_sort_ascend( int32_t *pn_x, int32_t n_size );
void maxim_sort_indices_descend(  int32_t  *pn_x, int32_t *pn_indx, int32_t n_size);

#endif















