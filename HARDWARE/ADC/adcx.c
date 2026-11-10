#include "adcx.h"

/**
  * @brief  ADC初始化程序
  * @param  
  * @retval 
  */
void ADCx_Init(void)
{
	ADC_InitTypeDef ADC_InitStructure;
	u32 timeout;

	//开启ADC时钟
	RCC_APB2PeriphClockCmd(ADC_CLK, ENABLE);
	
	//ADC频率进行6分频
	RCC_ADCCLKConfig(RCC_PCLK2_Div6);
	
	//配置ADC结构体
	ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;	//独立模式
	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;	//数据右对齐
	ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;	//软件触发
	ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;	//单次转换
	ADC_InitStructure.ADC_ScanConvMode = DISABLE;	//非扫描模式
	ADC_InitStructure.ADC_NbrOfChannel = 1;	//总通道数
	ADC_Init(ADCx, &ADC_InitStructure);	//初始化ADC1
	
	//开启ADCx
	ADC_Cmd(ADCx, ENABLE);
	
	//进行ADC校准（加超时，避免校准异常时死等，把整个界面卡住）
	ADC_ResetCalibration(ADCx);
	timeout = 0xFFFFF;
	while((ADC_GetResetCalibrationStatus(ADCx) == SET) && (--timeout));
	ADC_StartCalibration(ADCx);
	timeout = 0xFFFFF;
	while((ADC_GetCalibrationStatus(ADCx) == SET) && (--timeout));
}

/**
  * @brief  获取ADC转换后的数据
  * @param  ADC_Channel 	选择需要采集的ADC通道
  * @param  ADC_SampleTime  选择需要采样时间
  * @retval 返回转换后的模拟信号数值
  */
u16 ADC_GetValue(uint8_t ADC_Channel,uint8_t ADC_SampleTime)
{
	u32 timeout = 0xFFFF;

	//配置ADC通道
	ADC_RegularChannelConfig(ADCx, ADC_Channel, 1, ADC_SampleTime);
	
	ADC_SoftwareStartConvCmd(ADCx, ENABLE); //软件触发ADC转换
	//等待转换完成，正常约 14us；超时则返回 0，绝不死等
	while((ADC_GetFlagStatus(ADCx, ADC_FLAG_EOC) == RESET) && (--timeout));
	if (timeout == 0) return 0;

	return ADC_GetConversionValue(ADCx);
}
