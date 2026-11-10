#include "max30102.h"
#include "delay.h"



u8 max30102_Bus_Write(u8 Register_Address, u8 Word_Data)
{

	/* 采用串行EEPROM随即读取指令序列，连续读取若干字节 */

	/* 第1步：发起I2C总线启动信号 */
	MAX30102_IIC_Start();

	/* 第2步：发起控制字节，高7bit是地址，bit0是读写控制位，0表示写，1表示读 */
	MAX30102_IIC_Send_Byte(max30102_WR_address | I2C_WR);	/* 此处是写指令 */

	/* 第3步：发送ACK */
	if (MAX30102_IIC_Wait_Ack() != 0)
	{
		goto cmd_fail;	/* EEPROM器件无应答 */
	}

	/* 第4步：发送字节地址 */
	MAX30102_IIC_Send_Byte(Register_Address);
	if (MAX30102_IIC_Wait_Ack() != 0)
	{
		goto cmd_fail;	/* EEPROM器件无应答 */
	}
	
	/* 第5步：开始写入数据 */
	MAX30102_IIC_Send_Byte(Word_Data);

	/* 第6步：发送ACK */
	if (MAX30102_IIC_Wait_Ack() != 0)
	{
		goto cmd_fail;	/* EEPROM器件无应答 */
	}

	/* 发送I2C总线停止信号 */
	MAX30102_IIC_Stop();
	return 1;	/* 执行成功 */

cmd_fail: /* 命令执行失败后，切记发送停止信号，避免影响I2C总线上其他设备 */
	/* 发送I2C总线停止信号 */
	MAX30102_IIC_Stop();
	return 0;
}



u8 max30102_Bus_Read(u8 Register_Address)
{
	u8  data;


	/* 第1步：发起I2C总线启动信号 */
	MAX30102_IIC_Start();

	/* 第2步：发起控制字节，高7bit是地址，bit0是读写控制位，0表示写，1表示读 */
	MAX30102_IIC_Send_Byte(max30102_WR_address | I2C_WR);	/* 此处是写指令 */

	/* 第3步：发送ACK */
	if (MAX30102_IIC_Wait_Ack() != 0)
	{
		goto cmd_fail;	/* EEPROM器件无应答 */
	}

	/* 第4步：发送字节地址， */
	MAX30102_IIC_Send_Byte((uint8_t)Register_Address);
	if (MAX30102_IIC_Wait_Ack() != 0)
	{
		goto cmd_fail;	/* EEPROM器件无应答 */
	}
	

	/* 第6步：重新启动I2C总线。下面开始读取数据 */
	MAX30102_IIC_Start();

	/* 第7步：发起控制字节，高7bit是地址，bit0是读写控制位，0表示写，1表示读 */
	MAX30102_IIC_Send_Byte(max30102_WR_address | I2C_RD);	/* 此处是读指令 */

	/* 第8步：发送ACK */
	if (MAX30102_IIC_Wait_Ack() != 0)
	{
		goto cmd_fail;	/* EEPROM器件无应答 */
	}

	/* 第9步：读取数据（Read_Byte(0) 内部已经发过 NACK，这里不要再发，
	 * 多发一个时钟会让从机多移一位） */
	data = MAX30102_IIC_Read_Byte(0);
	/* 发送I2C总线停止信号 */
	MAX30102_IIC_Stop();
	return data;	/* 执行成功 返回data值 */

cmd_fail: /* 命令执行失败后，切记发送停止信号，避免影响I2C总线上其他设备 */
	/* 发送I2C总线停止信号 */
	MAX30102_IIC_Stop();
	return 0;
}


void max30102_FIFO_ReadWords(u8 Register_Address,u16 Word_Data[][2],u8 count)
{
	u8 i=0;
	u8 no = count;
	u8 data1, data2;
	/* 第1步：发起I2C总线启动信号 */
	MAX30102_IIC_Start();

	/* 第2步：发起控制字节，高7bit是地址，bit0是读写控制位，0表示写，1表示读 */
	MAX30102_IIC_Send_Byte(max30102_WR_address | I2C_WR);	/* 此处是写指令 */

	/* 第3步：发送ACK */
	if (MAX30102_IIC_Wait_Ack() != 0)
	{
		goto cmd_fail;	/* EEPROM器件无应答 */
	}

	/* 第4步：发送字节地址， */
	MAX30102_IIC_Send_Byte((uint8_t)Register_Address);
	if (MAX30102_IIC_Wait_Ack() != 0)
	{
		goto cmd_fail;	/* EEPROM器件无应答 */
	}
	

	/* 第6步：重新启动I2C总线。下面开始读取数据 */
	MAX30102_IIC_Start();

	/* 第7步：发起控制字节，高7bit是地址，bit0是读写控制位，0表示写，1表示读 */
	MAX30102_IIC_Send_Byte(max30102_WR_address | I2C_RD);	/* 此处是读指令 */

	/* 第8步：发送ACK */
	if (MAX30102_IIC_Wait_Ack() != 0)
	{
		goto cmd_fail;	/* EEPROM器件无应答 */
	}

	/* 第9步：读取数据 */
	while (no)
	{
		data1 = MAX30102_IIC_Read_Byte(0);	
		MAX30102_IIC_Ack();
		data2 = MAX30102_IIC_Read_Byte(0);
		MAX30102_IIC_Ack();
		Word_Data[i][0] = (((u16)data1 << 8) | data2);  //

		
		data1 = MAX30102_IIC_Read_Byte(0);	
		MAX30102_IIC_Ack();
		data2 = MAX30102_IIC_Read_Byte(0);
		if(1==no)
			MAX30102_IIC_NAck();	/* 最后1个字节读完后，CPU产生NACK信号(驱动SDA = 1) */
		else
			MAX30102_IIC_Ack();
		Word_Data[i][1] = (((u16)data1 << 8) | data2); 

		no--;	
		i++;
	}
	/* 发送I2C总线停止信号 */
	MAX30102_IIC_Stop();

cmd_fail: /* 命令执行失败后，切记发送停止信号，避免影响I2C总线上其他设备 */
	/* 发送I2C总线停止信号 */
	MAX30102_IIC_Stop();
}

void max30102_FIFO_ReadBytes(u8 Register_Address,u8* Data)
{	
	max30102_Bus_Read(REG_INTR_STATUS_1);
	max30102_Bus_Read(REG_INTR_STATUS_2);
	
	/* 第1步：发起I2C总线启动信号 */
	MAX30102_IIC_Start();

	/* 第2步：发起控制字节，高7bit是地址，bit0是读写控制位，0表示写，1表示读 */
	MAX30102_IIC_Send_Byte(max30102_WR_address | I2C_WR);	/* 此处是写指令 */

	/* 第3步：发送ACK */
	if (MAX30102_IIC_Wait_Ack() != 0)
	{
		goto cmd_fail;	/* EEPROM器件无应答 */
	}

	/* 第4步：发送字节地址， */
	MAX30102_IIC_Send_Byte((uint8_t)Register_Address);
	if (MAX30102_IIC_Wait_Ack() != 0)
	{
		goto cmd_fail;	/* EEPROM器件无应答 */
	}
	

	/* 第6步：重新启动I2C总线。下面开始读取数据 */
	MAX30102_IIC_Start();

	/* 第7步：发起控制字节，高7bit是地址，bit0是读写控制位，0表示写，1表示读 */
	MAX30102_IIC_Send_Byte(max30102_WR_address | I2C_RD);	/* 此处是读指令 */

	/* 第8步：发送ACK */
	if (MAX30102_IIC_Wait_Ack() != 0)
	{
		goto cmd_fail;	/* EEPROM器件无应答 */
	}

	/* 第9步：读取数据 */
	Data[0] = MAX30102_IIC_Read_Byte(1);	
	Data[1] = MAX30102_IIC_Read_Byte(1);	
	Data[2] = MAX30102_IIC_Read_Byte(1);	
	Data[3] = MAX30102_IIC_Read_Byte(1);
	Data[4] = MAX30102_IIC_Read_Byte(1);	
	Data[5] = MAX30102_IIC_Read_Byte(0);
	/* 最后1个字节读完后，CPU产生NACK信号(驱动SDA = 1) */
	/* 发送I2C总线停止信号 */
	MAX30102_IIC_Stop();

cmd_fail: /* 命令执行失败后，切记发送停止信号，避免影响I2C总线上其他设备 */
	/* 发送I2C总线停止信号 */
	MAX30102_IIC_Stop();

//	u8 i;
//	u8 fifo_wr_ptr;
//	u8 firo_rd_ptr;
//	u8 number_tp_read;
//	//获取FIFO_WR_PTR
//	fifo_wr_ptr = max30102_Bus_Read(REG_FIFO_WR_PTR);
//	//获取FIFO_RD_PTR
//	firo_rd_ptr = max30102_Bus_Read(REG_FIFO_RD_PTR);
//	
//	number_tp_read = fifo_wr_ptr - firo_rd_ptr;
//	
//	//for(i=0;i<number_tp_read;i++){
//	if(number_tp_read>0){
//		MAX30102_IIC_ReadBytes(max30102_WR_address,REG_FIFO_DATA,Data,6);
//	}
	
	//max30102_Bus_Write(REG_FIFO_RD_PTR,fifo_wr_ptr);
}

void MAX30102_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

 	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);	
	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_14;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
 	GPIO_Init(GPIOB, &GPIO_InitStructure);
	
	MAX30102_IIC_Init();
	
	MAX30102_Reset();
	
//	max30102_Bus_Write(REG_MODE_CONFIG, 0x0b);  //模式配置: temp_en[3]      MODE[2:0]=010 仅心率使能    011 血氧使能
//	max30102_Bus_Write(REG_INTR_STATUS_2, 0xF0); //打开所有中断
//	max30102_Bus_Write(REG_INTR_STATUS_1, 0x00); //清除所有中断
//	max30102_Bus_Write(REG_INTR_ENABLE_2, 0x02); //DIE_TEMP_RDY_EN
//	max30102_Bus_Write(REG_TEMP_CONFIG, 0x01); //设置TEMP_EN

//	max30102_Bus_Write(REG_SPO2_CONFIG, 0x47); //SPO2_SR[4:2]=001 每秒100次    LED_PW[1:0]=11 16位

//	max30102_Bus_Write(REG_LED1_PA, 0x47); 
//	max30102_Bus_Write(REG_LED2_PA, 0x47); 
	
	
	
	max30102_Bus_Write(REG_INTR_ENABLE_1,0xc0);	// 中断设置
	max30102_Bus_Write(REG_INTR_ENABLE_2,0x00);
	max30102_Bus_Write(REG_FIFO_WR_PTR,0x00);  	//FIFO_WR_PTR[4:0]
	max30102_Bus_Write(REG_OVF_COUNTER,0x00);  	//OVF_COUNTER[4:0]
	max30102_Bus_Write(REG_FIFO_RD_PTR,0x00);  	//FIFO_RD_PTR[4:0]
	max30102_Bus_Write(REG_FIFO_CONFIG,0x0f);  	//采样平均=1, FIFO回卷=false, FIFO几乎满=17
	max30102_Bus_Write(REG_MODE_CONFIG,0x03);  	//0x02为仅红光模式, 0x03为血氧模式, 0x07为多LED模式
	max30102_Bus_Write(REG_SPO2_CONFIG,0x27);  	// SPO2_ADC量程=4096nA, SPO2采样率(100Hz), LED脉宽(400uS)
	max30102_Bus_Write(REG_LED1_PA,0x24);   	//为LED1选择约7mA的电流值
	max30102_Bus_Write(REG_LED2_PA,0x24);   	// 为LED2选择约7mA的电流值
	max30102_Bus_Write(REG_PILOT_PA,0x7f);   	// 为Pilot LED选择约25mA的电流值


}

void MAX30102_Reset(void)
{
	max30102_Bus_Write(REG_MODE_CONFIG,0x40);	//写一次即可
	delay_ms(20);                              //复位大约 10ms，等它完成
}






void maxim_max30102_write_reg(uint8_t uch_addr, uint8_t uch_data)
{
//  char ach_i2c_data[2];
//  ach_i2c_data[0]=uch_addr;
//  ach_i2c_data[1]=uch_data;
//	
//  MAX30102_IIC_WriteBytes(I2C_WRITE_ADDR, ach_i2c_data, 2);
	MAX30102_IIC_Write_One_Byte(I2C_WRITE_ADDR,uch_addr,uch_data);
}

void maxim_max30102_read_reg(uint8_t uch_addr, uint8_t *puch_data)
{
//  char ch_i2c_data;
//  ch_i2c_data=uch_addr;
//  MAX30102_IIC_WriteBytes(I2C_WRITE_ADDR, &ch_i2c_data, 1);
//	
//  i2c.read(I2C_READ_ADDR, &ch_i2c_data, 1);
//  
//   *puch_data=(uint8_t) ch_i2c_data;
	MAX30102_IIC_Read_One_Byte(I2C_WRITE_ADDR,uch_addr,puch_data);
}

void maxim_max30102_read_fifo(uint32_t *pun_red_led, uint32_t *pun_ir_led)
{
	uint32_t un_temp;
	unsigned char uch_temp;
	char ach_i2c_data[6];
	*pun_red_led=0;
	*pun_ir_led=0;

  
  //读取并清除状态寄存器
  maxim_max30102_read_reg(REG_INTR_STATUS_1, &uch_temp);
  maxim_max30102_read_reg(REG_INTR_STATUS_2, &uch_temp);
  
  MAX30102_IIC_ReadBytes(I2C_WRITE_ADDR,REG_FIFO_DATA,(u8 *)ach_i2c_data,6);
  
  un_temp=(unsigned char) ach_i2c_data[0];
  un_temp<<=16;
  *pun_red_led+=un_temp;
  un_temp=(unsigned char) ach_i2c_data[1];
  un_temp<<=8;
  *pun_red_led+=un_temp;
  un_temp=(unsigned char) ach_i2c_data[2];
  *pun_red_led+=un_temp;
  
  un_temp=(unsigned char) ach_i2c_data[3];
  un_temp<<=16;
  *pun_ir_led+=un_temp;
  un_temp=(unsigned char) ach_i2c_data[4];
  un_temp<<=8;
  *pun_ir_led+=un_temp;
  un_temp=(unsigned char) ach_i2c_data[5];
  *pun_ir_led+=un_temp;
  *pun_red_led&=0x03FFFF;  //屏蔽高6位[23:18]
  *pun_ir_led&=0x03FFFF;  //屏蔽高6位[23:18]
}




//MAX30102引脚输出模式控制
void MAX30102_IIC_SDA_OUT(void)//SDA输出方向配置
{
	GPIO_InitTypeDef GPIO_InitStructure;	
	
	GPIO_InitStructure.GPIO_Pin=MAX30102_IIC_SDA_PIN;
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_Out_PP;//SDA推挽输出
	GPIO_Init(MAX30102_IIC_PORT,&GPIO_InitStructure); 						

}

void MAX30102_IIC_SDA_IN(void)//SDA输入方向配置
{
	GPIO_InitTypeDef GPIO_InitStructure;	
	
	GPIO_InitStructure.GPIO_Pin=MAX30102_IIC_SDA_PIN;
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IPU;//SCL上拉输入
	GPIO_Init(MAX30102_IIC_PORT,&GPIO_InitStructure);
	
}

static void max30102_bus_recover(void);

//初始化IIC
void MAX30102_IIC_Init(void)
{					     
	GPIO_InitTypeDef GPIO_InitStructure;
	//IIC 挂在 PE0(SDA)/PE1(SCL)，INT 在 PE6，三根都在 J3 排针上
	RCC_APB2PeriphClockCmd(	MAX30102_IIC_CLK | MAX30102_INT_CLK | RCC_APB2Periph_AFIO, ENABLE );
	//JTAG 仍然要关：DHT11 在 PA15(JTDI)，OV7725 的 WREN/RCK 在 PB3(JTDO)/PB4(NJTRST)，
	//这几根复位后都被 JTAG 占用。关掉后只能用 SWD 下载调试。
	GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
	   
	GPIO_InitStructure.GPIO_Pin = MAX30102_IIC_SCL_PIN|MAX30102_IIC_SDA_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP ;   //推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(MAX30102_IIC_PORT, &GPIO_InitStructure);

	//INT 只做输入，上拉输入防止悬空噪声（本工程轮询 FIFO，用不到 INT）
	GPIO_InitStructure.GPIO_Pin = MAX30102_INT_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_Init(MAX30102_INT_PORT, &GPIO_InitStructure);

	MAX30102_IIC_SCL=1;
	MAX30102_IIC_SDA=1;

	max30102_bus_recover();	//把可能被从机拉死的总线顶回空闲态
}

/* 总线恢复：如果上一次传输被复位/掉电打断，从机可能一直拉低 SDA，
 * 之后任何起始信号都无效。补 9 个 SCL 时钟让从机把剩下的位移完，
 * 再发一个 STOP，总线就回到空闲。这是 I2C 卡死的标准解法。       */
static void max30102_bus_recover(void)
{
	u8 i;

	MAX30102_IIC_SDA_OUT();
	MAX30102_IIC_SDA=1;

	for (i = 0; i < 9; i++)
	{
		MAX30102_IIC_SCL=0; delay_us(4);
		MAX30102_IIC_SCL=1; delay_us(4);
	}

	MAX30102_IIC_SCL=0; delay_us(4);
	MAX30102_IIC_SDA=0; delay_us(4);
	MAX30102_IIC_SCL=1; delay_us(4);
	MAX30102_IIC_SDA=1; delay_us(4);
}
//产生IIC起始信号
void MAX30102_IIC_Start(void)
{
	MAX30102_IIC_SDA_OUT();     //sda线输出
	MAX30102_IIC_SDA=1;	  	  
	MAX30102_IIC_SCL=1;
	delay_us(4);
 	MAX30102_IIC_SDA=0;//起始信号:CLK为高电平时,DATA由高变低
	delay_us(4);
	MAX30102_IIC_SCL=0;//钳住I2C总线，准备发送或接收数据 
}	  
//产生IIC停止信号
void MAX30102_IIC_Stop(void)
{
	MAX30102_IIC_SDA_OUT();//sda线输出
	MAX30102_IIC_SCL=0;
	MAX30102_IIC_SDA=0;//停止信号:CLK为高电平时,DATA由低变高
 	delay_us(4);
	MAX30102_IIC_SCL=1; 
	MAX30102_IIC_SDA=1;//发送I2C总线结束信号
	delay_us(4);							   	
}
//等待应答信号到来
//返回值：1，接收应答失败
//        0，接收应答成功
u8 MAX30102_IIC_Wait_Ack(void)
{
	u8 ucErrTime=0;
	MAX30102_IIC_SDA_IN();      //SDA设置为输入  
	MAX30102_IIC_SDA=1;delay_us(1);	   
	MAX30102_IIC_SCL=1;delay_us(1);	 
	while(MAX30102_READ_SDA)
	{
		ucErrTime++;
		if(ucErrTime>250)
		{
			MAX30102_IIC_Stop();
			return 1;
		}
	}
	MAX30102_IIC_SCL=0;//时钟输出0 	   
	return 0;  
} 
//产生ACK应答
void MAX30102_IIC_Ack(void)
{
	MAX30102_IIC_SCL=0;
	MAX30102_IIC_SDA_OUT();
	MAX30102_IIC_SDA=0;
	delay_us(2);
	MAX30102_IIC_SCL=1;
	delay_us(2);
	MAX30102_IIC_SCL=0;
}
//不产生ACK应答		    
void MAX30102_IIC_NAck(void)
{
	MAX30102_IIC_SCL=0;
	MAX30102_IIC_SDA_OUT();
	MAX30102_IIC_SDA=1;
	delay_us(2);
	MAX30102_IIC_SCL=1;
	delay_us(2);
	MAX30102_IIC_SCL=0;
}					 				     
//IIC发送一个字节
//返回从机有无应答
//1，有应答
//0，无应答			  
void MAX30102_IIC_Send_Byte(u8 txd)
{                        
    u8 t;   
	MAX30102_IIC_SDA_OUT(); 	    
    MAX30102_IIC_SCL=0;//拉低时钟开始数据传输
    for(t=0;t<8;t++)
    {              
        MAX30102_IIC_SDA=(txd&0x80)>>7;
        txd<<=1; 	  
		delay_us(2);   //对TEA5767这三个延时都是必须的
		MAX30102_IIC_SCL=1;
		delay_us(2); 
		MAX30102_IIC_SCL=0;	
		delay_us(2);
    }	 
} 	    
//读1个字节，ack=1时，发送ACK，ack=0，发送nACK   
u8 MAX30102_IIC_Read_Byte(unsigned char ack)
{
	unsigned char i,receive=0;
	MAX30102_IIC_SDA_IN();//SDA设置为输入
    for(i=0;i<8;i++ )
	{
        MAX30102_IIC_SCL=0; 
        delay_us(2);
		MAX30102_IIC_SCL=1;
        receive<<=1;
        if(MAX30102_READ_SDA)receive++;   
		delay_us(1); 
    }					 
    if (!ack)
        MAX30102_IIC_NAck();//发送nACK
    else
        MAX30102_IIC_Ack(); //发送ACK   
    return receive;
}


void MAX30102_IIC_WriteBytes(u8 WriteAddr,u8* data,u8 dataLength)
{		
	u8 i;	
    MAX30102_IIC_Start();  

	MAX30102_IIC_Send_Byte(WriteAddr);	    //发送写命令
	MAX30102_IIC_Wait_Ack();
	
	for(i=0;i<dataLength;i++)
	{
		MAX30102_IIC_Send_Byte(data[i]);
		MAX30102_IIC_Wait_Ack();
	}				    	   
    MAX30102_IIC_Stop();//产生一个停止条件 
	delay_ms(10);	 
}

void MAX30102_IIC_ReadBytes(u8 deviceAddr, u8 writeAddr,u8* data,u8 dataLength)
{		
	u8 i;	
    MAX30102_IIC_Start();  

	MAX30102_IIC_Send_Byte(deviceAddr);	    //发送写命令
	MAX30102_IIC_Wait_Ack();
	MAX30102_IIC_Send_Byte(writeAddr);
	MAX30102_IIC_Wait_Ack();

	/* 关键：这里必须再来一次起始信号（Sr），否则芯片根本不会切到读模式，
	 * 读回来的全是 0xFF。原驱动漏了这一句，FIFO 永远读不出数据。      */
	MAX30102_IIC_Start();
	MAX30102_IIC_Send_Byte(deviceAddr|0X01);//进入接收模式			   
	MAX30102_IIC_Wait_Ack();
	
	for(i=0;i<dataLength-1;i++)
	{
		data[i] = MAX30102_IIC_Read_Byte(1);
	}		
	data[dataLength-1] = MAX30102_IIC_Read_Byte(0);	
    MAX30102_IIC_Stop();//产生一个停止条件 
}

void MAX30102_IIC_Read_One_Byte(u8 daddr,u8 addr,u8* data)
{				  	  	    																 
    MAX30102_IIC_Start();  
	
	MAX30102_IIC_Send_Byte(daddr);	   //发送写命令
	MAX30102_IIC_Wait_Ack();
	MAX30102_IIC_Send_Byte(addr);//发送地址
	MAX30102_IIC_Wait_Ack();		 
	MAX30102_IIC_Start();  	 	   
	MAX30102_IIC_Send_Byte(daddr|0X01);//进入接收模式			   
	MAX30102_IIC_Wait_Ack();	 
    *data = MAX30102_IIC_Read_Byte(0);		   
    MAX30102_IIC_Stop();//产生一个停止条件	    
}

void MAX30102_IIC_Write_One_Byte(u8 daddr,u8 addr,u8 data)
{				   	  	    																 
    MAX30102_IIC_Start();  
	
	MAX30102_IIC_Send_Byte(daddr);	    //发送写命令
	MAX30102_IIC_Wait_Ack();
	MAX30102_IIC_Send_Byte(addr);//发送地址
	MAX30102_IIC_Wait_Ack();	   	 										  		   
	MAX30102_IIC_Send_Byte(data);     //发送字节							   
	MAX30102_IIC_Wait_Ack();  		    	   
    MAX30102_IIC_Stop();//产生一个停止条件 
	delay_ms(10);	 
}







const uint16_t auw_hamm[31]={ 41,    276,    512,    276,     41 }; //Hamm=  long16(512* hamming(5)');
//uch_spo2_table 由 -45.060*ratioAverage* ratioAverage + 30.354 *ratioAverage + 94.845 计算得到 ;
const uint8_t uch_spo2_table[184]={ 95, 95, 95, 96, 96, 96, 97, 97, 97, 97, 97, 98, 98, 98, 98, 98, 99, 99, 99, 99, 
                            99, 99, 99, 99, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 
                            100, 100, 100, 100, 99, 99, 99, 99, 99, 99, 99, 99, 98, 98, 98, 98, 98, 98, 97, 97, 
                            97, 97, 96, 96, 96, 96, 95, 95, 95, 94, 94, 94, 93, 93, 93, 92, 92, 92, 91, 91, 
                            90, 90, 89, 89, 89, 88, 88, 87, 87, 86, 86, 85, 85, 84, 84, 83, 82, 82, 81, 81, 
                            80, 80, 79, 78, 78, 77, 76, 76, 75, 74, 74, 73, 72, 72, 71, 70, 69, 69, 68, 67, 
                            66, 66, 65, 64, 63, 62, 62, 61, 60, 59, 58, 57, 56, 56, 55, 54, 53, 52, 51, 50, 
                            49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 31, 30, 29, 
                            28, 27, 26, 25, 23, 22, 21, 20, 19, 17, 16, 15, 14, 12, 11, 10, 9, 7, 6, 5, 
                            3, 2, 1 } ;
static  int32_t an_dx[ BUFFER_SIZE-MA4_SIZE]; //差分值
static  int32_t an_x[ BUFFER_SIZE]; //红外
static  int32_t an_y[ BUFFER_SIZE]; //红光

void maxim_heart_rate_and_oxygen_saturation(uint32_t *pun_ir_buffer,  int32_t n_ir_buffer_length, uint32_t *pun_red_buffer, int32_t *pn_spo2, int8_t *pch_spo2_valid, 
                              int32_t *pn_heart_rate, int8_t  *pch_hr_valid)
/**
* \brief        计算心率与血氧饱和度
* \par          详细说明
*               通过检测PPG周期的峰值以及红光/红外信号对应的交流/直流分量,计算出血氧比例值.
*               由于该算法面向Arm M0/M3,受寄存器溢出影响,血氧计算公式无法达到所需精度.
*               因此精确的血氧值已按各比例预先计算好并保存在uch_spo2_table[]中.
*
* \param[in]    *pun_ir_buffer           - 红外传感器数据缓冲区
* \param[in]    n_ir_buffer_length      - 红外传感器数据缓冲区长度
* \param[in]    *pun_red_buffer          - 红光传感器数据缓冲区
* \param[out]    *pn_spo2                - 计算得到的血氧值
* \param[out]    *pch_spo2_valid         - 计算得到的血氧值有效时置1
* \param[out]    *pn_heart_rate          - 计算得到的心率值
* \param[out]    *pch_hr_valid           - 计算得到的心率值有效时置1
*
* \retval       无
*/
{
    uint32_t un_ir_mean ,un_only_once ;
    int32_t k ,n_i_ratio_count;
    int32_t i, s, m, n_exact_ir_valley_locs_count ,n_middle_idx;
    int32_t n_th1, n_npks,n_c_min;      
    int32_t an_ir_valley_locs[15] ;
    int32_t an_exact_ir_valley_locs[15] ;
    int32_t an_dx_peak_locs[15] ;
    int32_t n_peak_interval_sum;
    
    int32_t n_y_ac, n_x_ac;
    int32_t n_spo2_calc; 
    int32_t n_y_dc_max, n_x_dc_max; 
    int32_t n_y_dc_max_idx, n_x_dc_max_idx; 
    int32_t an_ratio[5],n_ratio_average; 
    int32_t n_nume,  n_denom ;
    // 去除红外信号的直流分量
    un_ir_mean =0; 
    for (k=0 ; k<n_ir_buffer_length ; k++ ) un_ir_mean += pun_ir_buffer[k] ;
    un_ir_mean =un_ir_mean/n_ir_buffer_length ;
    for (k=0 ; k<n_ir_buffer_length ; k++ )  an_x[k] =  pun_ir_buffer[k] - un_ir_mean ; 
    
    // 4点滑动平均
    for(k=0; k< BUFFER_SIZE-MA4_SIZE; k++){
        n_denom= ( an_x[k]+an_x[k+1]+ an_x[k+2]+ an_x[k+3]);
        an_x[k]=  n_denom/(int32_t)4; 
    }

    // 求平滑后红外信号的差分
    
    for( k=0; k<BUFFER_SIZE-MA4_SIZE-1;  k++)
        an_dx[k]= (an_x[k+1]- an_x[k]);

    // 对an_dx做2点滑动平均
    for(k=0; k< BUFFER_SIZE-MA4_SIZE-2; k++){
        an_dx[k] =  ( an_dx[k]+an_dx[k+1])/2 ;
    }
    
    // 汉明窗
    // 翻转波形,便于用峰值检测器检测波谷
    for ( i=0 ; i<BUFFER_SIZE-HAMMING_SIZE-MA4_SIZE-2 ;i++){
        s= 0;
        for( k=i; k<i+ HAMMING_SIZE ;k++){
            s -= an_dx[k] *auw_hamm[k-i] ; 
                     }
        an_dx[i]= s/ (int32_t)1146; // 除以auw_hamm之和
    }

 
    n_th1=0; // 阈值计算
    for ( k=0 ; k<BUFFER_SIZE-HAMMING_SIZE ;k++){
        n_th1 += ((an_dx[k]>0)? an_dx[k] : ((int32_t)0-an_dx[k])) ;
    }
    n_th1= n_th1/ ( BUFFER_SIZE-HAMMING_SIZE);
    // 波形已被翻转,故峰值位置实际是原始信号最陡处的索引
    maxim_find_peaks( an_dx_peak_locs, &n_npks, an_dx, BUFFER_SIZE-HAMMING_SIZE, n_th1, 8, 5 );//峰值高度, 峰间距, 最大峰值数

    n_peak_interval_sum =0;
    if (n_npks>=2){
        for (k=1; k<n_npks; k++)
            n_peak_interval_sum += (an_dx_peak_locs[k]-an_dx_peak_locs[k -1]);
        n_peak_interval_sum=n_peak_interval_sum/(n_npks-1);
        *pn_heart_rate=(int32_t)(6000/n_peak_interval_sum);// 每分钟心跳次数
        *pch_hr_valid  = 1;
    }
    else  {
        *pn_heart_rate = -999;
        *pch_hr_valid  = 0;
    }
            
    for ( k=0 ; k<n_npks ;k++)
        an_ir_valley_locs[k]=an_dx_peak_locs[k]+HAMMING_SIZE/2; 


    // 原始值: 红光(=y) 与 红外(=x)
    // 需要求出红外与红光PPG的直流和交流分量.
    for (k=0 ; k<n_ir_buffer_length ; k++ )  {
        an_x[k] =  pun_ir_buffer[k] ; 
        an_y[k] =  pun_red_buffer[k] ; 
    }

    // 在an_ir_valley_locs附近寻找精确的极小值
    n_exact_ir_valley_locs_count =0; 
    for(k=0 ; k<n_npks ;k++){
        un_only_once =1;
        m=an_ir_valley_locs[k];
        n_c_min= 16777216;//2^24;
        if (m+5 <  BUFFER_SIZE-HAMMING_SIZE  && m-5 >0){
            for(i= m-5;i<m+5; i++)
                if (an_x[i]<n_c_min){
                    if (un_only_once >0){
                       un_only_once =0;
                   } 
                   n_c_min= an_x[i] ;
                   an_exact_ir_valley_locs[k]=i;
                }
            if (un_only_once ==0)
                n_exact_ir_valley_locs_count ++ ;
        }
    }
    if (n_exact_ir_valley_locs_count <2 ){
       *pn_spo2 =  -999 ; // 信号比例超出范围,不使用该血氧值
       *pch_spo2_valid  = 0; 
       return;
    }
    // 4点滑动平均
    for(k=0; k< BUFFER_SIZE-MA4_SIZE; k++){
        an_x[k]=( an_x[k]+an_x[k+1]+ an_x[k+2]+ an_x[k+3])/(int32_t)4;
        an_y[k]=( an_y[k]+an_y[k+1]+ an_y[k+2]+ an_y[k+3])/(int32_t)4;
    }

    //用an_exact_ir_valley_locs求红外与红光的直流和交流,用于血氧校准比例
    //在两个波谷之间求原始红外*红光的交流/直流最大值
    n_ratio_average =0; 
    n_i_ratio_count =0; 
    
    for(k=0; k< 5; k++) an_ratio[k]=0;
    for (k=0; k< n_exact_ir_valley_locs_count; k++){
        if (an_exact_ir_valley_locs[k] > BUFFER_SIZE ){             
            *pn_spo2 =  -999 ; // 波谷位置超出范围,不使用该血氧值
            *pch_spo2_valid  = 0; 
            return;
        }
    }
    // 求两个波谷之间的最大值
    // 并用红外与红光的交流分量之比、直流分量之比计算血氧

    for (k=0; k< n_exact_ir_valley_locs_count-1; k++){
        n_y_dc_max= -16777216 ; 
        n_x_dc_max= - 16777216; 
        if (an_exact_ir_valley_locs[k+1]-an_exact_ir_valley_locs[k] >10){
            for (i=an_exact_ir_valley_locs[k]; i< an_exact_ir_valley_locs[k+1]; i++){
                if (an_x[i]> n_x_dc_max) {n_x_dc_max =an_x[i];n_x_dc_max_idx =i; }
                if (an_y[i]> n_y_dc_max) {n_y_dc_max =an_y[i];n_y_dc_max_idx=i;}
            }
            n_y_ac= (an_y[an_exact_ir_valley_locs[k+1]] - an_y[an_exact_ir_valley_locs[k] ] )*(n_y_dc_max_idx -an_exact_ir_valley_locs[k]); //红光
            n_y_ac=  an_y[an_exact_ir_valley_locs[k]] + n_y_ac/ (an_exact_ir_valley_locs[k+1] - an_exact_ir_valley_locs[k])  ; 
        
        
            n_y_ac=  an_y[n_y_dc_max_idx] - n_y_ac;    // 从原始值中减去线性直流分量
            n_x_ac= (an_x[an_exact_ir_valley_locs[k+1]] - an_x[an_exact_ir_valley_locs[k] ] )*(n_x_dc_max_idx -an_exact_ir_valley_locs[k]); // 红外
            n_x_ac=  an_x[an_exact_ir_valley_locs[k]] + n_x_ac/ (an_exact_ir_valley_locs[k+1] - an_exact_ir_valley_locs[k]); 
            n_x_ac=  an_x[n_y_dc_max_idx] - n_x_ac;      // 从原始值中减去线性直流分量
            n_nume=( n_y_ac *n_x_dc_max)>>7 ; //放大100倍以保留浮点精度
            n_denom= ( n_x_ac *n_y_dc_max)>>7;
            if (n_denom>0  && n_i_ratio_count <5 &&  n_nume != 0)
            {   
                an_ratio[n_i_ratio_count]= (n_nume*20)/n_denom ; //formular is ( n_y_ac *n_x_dc_max) / ( n_x_ac *n_y_dc_max) ;  ///*************************n_nume原来是*100************************//
                n_i_ratio_count++;
            }
        }
    }

    maxim_sort_ascend(an_ratio, n_i_ratio_count);
    n_middle_idx= n_i_ratio_count/2;

    if (n_middle_idx >1)
        n_ratio_average =( an_ratio[n_middle_idx-1] +an_ratio[n_middle_idx])/2; // 取中位数
    else
        n_ratio_average = an_ratio[n_middle_idx ];

    if( n_ratio_average>2 && n_ratio_average <184){
        n_spo2_calc= uch_spo2_table[n_ratio_average] ;
        *pn_spo2 = n_spo2_calc ;
        *pch_spo2_valid  = 1;//  float_SPO2 =  -45.060*n_ratio_average* n_ratio_average/10000 + 30.354 *n_ratio_average/100 + 94.845 ;  //  用于与表格对比
    }
    else{
        *pn_spo2 =  -999 ; // 信号比例超出范围,不使用该血氧值
        *pch_spo2_valid  = 0; 
    }
}


void maxim_find_peaks(int32_t *pn_locs, int32_t *pn_npks, int32_t *pn_x, int32_t n_size, int32_t n_min_height, int32_t n_min_distance, int32_t n_max_num)
/**
* \brief        查找峰值
* \par          详细说明
*               在MIN_HEIGHT之上查找至多MAX_NUM个峰值,且相邻峰值间距不小于MIN_DISTANCE
*
* \retval       无
*/
{
    maxim_peaks_above_min_height( pn_locs, pn_npks, pn_x, n_size, n_min_height );
    maxim_remove_close_peaks( pn_locs, pn_npks, pn_x, n_min_distance );
    *pn_npks = min( *pn_npks, n_max_num );
}

void maxim_peaks_above_min_height(int32_t *pn_locs, int32_t *pn_npks, int32_t  *pn_x, int32_t n_size, int32_t n_min_height)
/**
* \brief        查找高于n_min_height的峰值
* \par          详细说明
*               查找所有高于MIN_HEIGHT的峰值
*
* \retval       无
*/
{
    int32_t i = 1, n_width;
    *pn_npks = 0;
    
    while (i < n_size-1){
        if (pn_x[i] > n_min_height && pn_x[i] > pn_x[i-1]){            // 寻找候选峰的左边缘
            n_width = 1;
            while (i+n_width < n_size && pn_x[i] == pn_x[i+n_width])    // 寻找平顶峰
                n_width++;
            if (pn_x[i] > pn_x[i+n_width] && (*pn_npks) < 15 ){                            // 寻找峰的右边缘
                pn_locs[(*pn_npks)++] = i;        
                // 为平顶峰时,峰位置取左边缘
                i += n_width+1;
            }
            else
                i += n_width;
        }
        else
            i++;
    }
}


void maxim_remove_close_peaks(int32_t *pn_locs, int32_t *pn_npks, int32_t *pn_x, int32_t n_min_distance)
/**
* \brief        剔除峰值
* \par          详细说明
*               剔除间距小于MIN_DISTANCE的峰值
*
* \retval       无
*/
{
    
    int32_t i, j, n_old_npks, n_dist;
    
    /* 将峰值从大到小排序 */
    maxim_sort_indices_descend( pn_x, pn_locs, *pn_npks );

    for ( i = -1; i < *pn_npks; i++ ){
        n_old_npks = *pn_npks;
        *pn_npks = i+1;
        for ( j = i+1; j < n_old_npks; j++ ){
            n_dist =  pn_locs[j] - ( i == -1 ? -1 : pn_locs[i] ); // 自相关的零延迟峰位于索引-1处
            if ( n_dist > n_min_distance || n_dist < -n_min_distance )
                pn_locs[(*pn_npks)++] = pn_locs[j];
        }
    }

    // 将索引重新按升序排列
    maxim_sort_ascend( pn_locs, *pn_npks );
}

void maxim_sort_ascend(int32_t *pn_x,int32_t n_size) 
/**
* \brief        数组排序
* \par          详细说明
*               按升序对数组排序(插入排序算法)
*
* \retval       无
*/
{
    int32_t i, j, n_temp;
    for (i = 1; i < n_size; i++) {
        n_temp = pn_x[i];
        for (j = i; j > 0 && n_temp < pn_x[j-1]; j--)
            pn_x[j] = pn_x[j-1];
        pn_x[j] = n_temp;
    }
}

void maxim_sort_indices_descend(int32_t *pn_x, int32_t *pn_indx, int32_t n_size)
/**
* \brief        索引排序
* \par          详细说明
*               按降序对索引排序(插入排序算法)
*
* \retval       无
*/ 
{
    int32_t i, j, n_temp;
    for (i = 1; i < n_size; i++) {
        n_temp = pn_indx[i];
        for (j = i; j > 0 && pn_x[n_temp] > pn_x[pn_indx[j-1]]; j--)
            pn_indx[j] = pn_indx[j-1];
        pn_indx[j] = n_temp;
    }
}


