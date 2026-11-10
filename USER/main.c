#include "led.h" /* 引入LED驱动头文件 */
#include "beep.h" /* 引入蜂鸣器驱动头文件 */
#include "delay.h" /* 引入延时函数头文件 */
#include "key.h" /* 引入按键驱动头文件 */
#include "sys.h" /* 引入系统配置头文件 */
#include "lcd.h" /* 引入LCD驱动头文件 */
#include "usart.h"	 /* 引入串口驱动头文件 */
#include "24cxx.h" /* 引入AT24Cxx EEPROM驱动头文件 */
#include "w25qxx.h" /* 引入W25Qxx SPI Flash驱动头文件 */
#include "touch.h" /* 引入触摸屏驱动头文件 */
#include "game.h" /* 引入游戏逻辑头文件 */
#include "menu.h" /* 引入菜单逻辑头文件 */
#include "rtc.h" /* 引入RTC实时时钟头文件 */
 
 

//清空屏幕并在右上角显示"RST" /* 清屏并显示RST区域说明 */
void Load_Drow_Dialog(void) /* 定义清屏并显示RST区域的函数 */
{
	LCD_Clear(WHITE);	//清屏    /* 将LCD清为白色 */
 	POINT_COLOR=BLUE;	//设置字体为蓝色  /* 将画笔颜色设为蓝色 */
	LCD_ShowString(lcddev.width-24,0,200,16,16,"RST");//显示清屏区域 /* 在右上角显示RST字符串 */
 	POINT_COLOR=RED;	//设置画笔蓝色  /* 将画笔颜色设为红色 */
}


//////////////////////////////////////////////////////////////////////////////// /* 分隔线 */
//电容触摸屏专有部分 /* 电容触摸屏相关代码说明 */
//画水平线 /* 画水平线函数说明 */
//x0,y0:坐标 /* 参数x0、y0为坐标 */
//len:线长度 /* 参数len为线长度 */
//color:颜色 /* 参数color为颜色 */
void gui_draw_hline(u16 x0,u16 y0,u16 len,u16 color) /* 定义画水平线函数 */
{
	if(len==0)return; /* 长度为0则直接返回 */
	LCD_Fill(x0,y0,x0+len-1,y0,color);	 /* 用指定颜色填充水平线段 */
}


//画实心圆 /* 画实心圆函数说明 */
//x0,y0:坐标 /* 参数x0、y0为圆心坐标 */
//r:半径 /* 参数r为半径 */
//color:颜色 /* 参数color为颜色 */
void gui_fill_circle(u16 x0,u16 y0,u16 r,u16 color) /* 定义画实心圆函数 */
{
	u32 i; /* 定义循环变量i */
	u32 imax = ((u32)r*707)/1000+1; /* 计算循环最大半径相关值 */
	u32 sqmax = (u32)r*(u32)r+(u32)r/2; /* 计算平方阈值 */
	u32 x=r; /* 初始化x半径为r */
	gui_draw_hline(x0-r,y0,2*r,color); /* 先画圆中间的水平线 */
	for (i=1;i<=imax;i++)  /* 从1到imax循环画圆 */
	{
		if ((i*i+x*x)>sqmax)// draw lines from outside   /* 判断是否超出平方阈值 */
		{
 			if (x>imax)  /* 如果x仍大于imax */
			{
				gui_draw_hline (x0-i+1,y0+x,2*(i-1),color); /* 画上半部分对应水平线 */
				gui_draw_hline (x0-i+1,y0-x,2*(i-1),color); /* 画下半部分对应水平线 */
			}
			x--; /* x递减 */
		}
		// 从内部(中心)开始画线   /* 从中心开始画线说明 */
		gui_draw_hline(x0-x,y0+i,2*x,color); /* 画中心上方水平线 */
		gui_draw_hline(x0-x,y0-i,2*x,color); /* 画中心下方水平线 */
	}
}


//两个数之差的绝对值  /* 求两数差的绝对值函数说明 */
//x1,x2：需取差值的两个数 /* 参数x1、x2为需取差值的两个数 */
//返回值：|x1-x2| /* 返回值为两数之差的绝对值 */
u16 my_abs(u16 x1,u16 x2) /* 定义求两数差的绝对值函数 */
{
	if(x1>x2)return x1-x2; /* 如果x1大于x2，返回x1-x2 */
	else return x2-x1; /* 否则返回x2-x1 */
}


//画一条粗线 /* 画粗线函数说明 */
//(x1,y1),(x2,y2):线条的起始坐标 /* 参数为线条起止坐标 */
//size：线条的粗细程度 /* 参数size为线条粗细程度 */
//color：线条的颜色 /* 参数color为线条颜色 */
void lcd_draw_bline(u16 x1, u16 y1, u16 x2, u16 y2,u8 size,u16 color) /* 定义画粗线函数 */
{
	u16 t;  /* 定义循环变量t */
	int xerr=0,yerr=0,delta_x,delta_y,distance;  /* 定义误差、坐标增量和距离变量 */
	int incx,incy,uRow,uCol;  /* 定义单步方向和当前坐标变量 */
	if(x1<size|| x2<size||y1<size|| y2<size)return;  /* 坐标小于线宽则直接返回，防止越界 */
	delta_x=x2-x1; //计算坐标增量  /* 计算x方向坐标增量 */
	delta_y=y2-y1;  /* 计算y方向坐标增量 */
	uRow=x1;  /* 当前绘制点x坐标初始化为x1 */
	uCol=y1;  /* 当前绘制点y坐标初始化为y1 */
	if(delta_x>0)incx=1; //设置单步方向  /* x增量为正，单步方向为1 */
	else if(delta_x==0)incx=0;//垂直线  /* x增量为0，为垂直线 */
	else {incx=-1;delta_x=-delta_x;}  /* x增量为负，方向取-1并取绝对值 */
	if(delta_y>0)incy=1;  /* y增量为正，单步方向为1 */
	else if(delta_y==0)incy=0;//水平线  /* y增量为0，为水平线 */
	else{incy=-1;delta_y=-delta_y;}  /* y增量为负，方向取-1并取绝对值 */
	if( delta_x>delta_y)distance=delta_x; //选取基本增量坐标轴  /* 选取x增量作为基本距离 */
	else distance=delta_y;  /* 否则选取y增量作为基本距离 */
	for(t=0;t<=distance+1;t++ )//画线输出  /* 按距离循环绘制线条 */
	{
		gui_fill_circle(uRow,uCol,size,color);//画点  /* 在当前坐标画实心圆点 */
		xerr+=delta_x ;  /* 累加x方向误差 */
		yerr+=delta_y ;  /* 累加y方向误差 */
		if(xerr>distance)  /* 判断x误差是否超过距离 */
		{
			xerr-=distance;  /* 修正x误差 */
			uRow+=incx;  /* x坐标按方向步进 */
		}
		if(yerr>distance)  /* 判断y误差是否超过距离 */
		{
			yerr-=distance;  /* 修正y误差 */
			uCol+=incy;  /* y坐标按方向步进 */
		}
	}
}
//////////////////////////////////////////////////////////////////////////////// /* 分隔线 */
 //5个触控点的颜色												  /* 5个触控点颜色表说明 */
const u16 POINT_COLOR_TBL[CT_MAX_TOUCH]={RED,GREEN,BLUE,BROWN,GRED};   /* 定义5个触控点对应的颜色表 */
//电阻触摸屏测试函数 /* 电阻触摸屏测试函数说明 */
void rtp_test(void) /* 定义电阻触摸屏测试函数 */
{
	u8 key; /* 定义按键值变量 */
	u8 i=0;	   /* 定义计数变量并初始化为0 */
	while(1) /* 无限循环进行触摸测试 */
	{
	 	key=KEY_Scan(0); /* 扫描按键 */
		tp_dev.scan(0); 		  /* 扫描触摸屏 */
		if(tp_dev.sta&TP_PRES_DOWN)			//触摸屏被按下 /* 判断触摸屏是否被按下 */
		{
		 	if(tp_dev.x[0]<lcddev.width&&tp_dev.y[0]<lcddev.height) /* 判断触摸坐标是否在LCD范围内 */
			{
				if(tp_dev.x[0]>(lcddev.width-24)&&tp_dev.y[0]<16)Load_Drow_Dialog();//清除 /* 触摸右上角RST区域则清屏 */
				else TP_Draw_Big_Point(tp_dev.x[0],tp_dev.y[0],RED);		//画图	  			    /* 否则在触摸点画红色大点 */
			}
		}else delay_ms(10);	//没有按键按下的时候 	     /* 没有触摸按下时延时10ms */
		if(key==KEY0_PRES)	//KEY0按下,则执行校准程序 /* 如果KEY0按下则执行校准程序 */
		{
			LCD_Clear(WHITE);//清屏 /* 清屏 */
		    TP_Adjust();  	//屏幕校准   /* 执行触摸屏校准 */
			Load_Drow_Dialog(); /* 重新绘制清屏区域和RST提示 */
		}
		i++; /* 计数加1 */
		if(i%20==0)LED0=!LED0; /* 每20次循环翻转一次LED0 */
	}
}
//电容触摸屏测试函数 /* 电容触摸屏测试函数说明 */
void ctp_test(void) /* 定义电容触摸屏测试函数 */
{
	u8 t=0; /* 定义触摸点循环变量t */
	u8 i=0;	  	     /* 定义计数变量i */
 	u16 lastpos[5][2];		//记录最后一次的数据  /* 记录每个触摸点最后一次的坐标 */
	while(1) /* 无限循环进行电容触摸测试 */
	{
		tp_dev.scan(0); /* 扫描触摸屏 */
		for(t=0;t<CT_MAX_TOUCH;t++) /* 遍历所有电容触摸点 */
		{
			if((tp_dev.sta)&(1<<t)) /* 判断第t个触摸点是否有效 */
			{
				if(tp_dev.x[t]<lcddev.width&&tp_dev.y[t]<lcddev.height) /* 判断触摸坐标是否在LCD范围内 */
				{
					if(lastpos[t][0]==0XFFFF) /* 如果是第一次记录该触摸点坐标 */
					{
						lastpos[t][0] = tp_dev.x[t]; /* 保存当前x坐标 */
						lastpos[t][1] = tp_dev.y[t]; /* 保存当前y坐标 */
					}
					lcd_draw_bline(lastpos[t][0],lastpos[t][1],tp_dev.x[t],tp_dev.y[t],2,POINT_COLOR_TBL[t]);//画线 /* 从上次坐标到当前坐标画粗线 */
					lastpos[t][0]=tp_dev.x[t]; /* 更新最后一次x坐标 */
					lastpos[t][1]=tp_dev.y[t]; /* 更新最后一次y坐标 */
					if(tp_dev.x[t]>(lcddev.width-24)&&tp_dev.y[t]<16) /* 判断是否触摸右上角RST区域 */
					{
						Load_Drow_Dialog();//清除 /* 清屏并显示RST */
					}
				}
			}else lastpos[t][0]=0XFFFF; /* 触摸点无效时重置最后坐标标志 */
		}
		
		delay_ms(5);i++; /* 延时5ms并计数 */
		if(i%20==0)LED0=!LED0; /* 每20次循环翻转一次LED0 */
	}	
}


 int main(void) /* 定义主函数 */
 {
	delay_init();	    															//延时函数初始化 /* 初始化延时函数 */
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);	//中断优先级分组 /* 设置中断优先级分组为2 */
	uart_init(115200);	 														//串口初始化为115200 /* 初始化串口1为115200 */
												
	LED_Init();			    														//LED初始化 /* 初始化LED */
	BEEP_Init();		    														//蜂鸣器初始化(PB8,拍照与删除照片时响一声) /* 初始化蜂鸣器 */
	LCD_Init();			    														//TFTLCD初始化 /* 初始化TFTLCD */
	LCD_Display_Dir(1);	    												//先切到横屏，这样触摸屏校准也是横屏的 /* 切换LCD为横屏 */
	KEY_Init();	 		    														//按键初始化 /* 初始化按键 */
												
	AT24CXX_Init();		    													//EEPROM初始化(保存触摸校准与难度设置) /* 初始化AT24Cxx EEPROM */
	Touch_Cal_Check();	    												//首次运行时强制重新校准触摸屏 /* 检查触摸校准 */
	tp_dev.init();		    													//触摸屏初始化(未校准会自动进入校准) /* 初始化触摸屏设备 */
												
	RTC_Init();			    														//RTC实时时钟初始化(LSE 32.768kHz, VBAT掉电走时) /* 初始化RTC实时时钟 */
	Main_Menu();																		//左上角SET设置难度 /* 进入主菜单/游戏入口 */

	while(1); /* 主循环空转，程序停在这里 */

 }
 
 