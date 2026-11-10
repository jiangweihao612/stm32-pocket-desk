#ifndef __SDIO_SDCARD_H
#define __SDIO_SDCARD_H																			   
#include "stm32f10x.h" 													   
////////////////////////////////////////////////////////////////////////////////////////////////////
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ALIENTEK战舰STM32开发板V3
//SDIO 驱动代码	   
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//创建日期:2015/1/20
//版本：V1.1
//版权所有，盗版必究。
//Copyright(C) 广州市星翼电子科技有限公司 2009-2019
//版权所有 
//********************************************************************************
//V1.1修改说明  20150731
//去掉了SD_WriteDisk和SD_ReadDisk函数if(CardType!=SDIO_STD_CAPACITY_SD_CARD_V1_1)的判断.
////////////////////////////////////////////////////////////////////////////////////////////////////  				
							   


//SDIO相关标志位
#define SDIO_FLAG_CCRCFAIL                  ((uint32_t)0x00000001)
#define SDIO_FLAG_DCRCFAIL                  ((uint32_t)0x00000002)
#define SDIO_FLAG_CTIMEOUT                  ((uint32_t)0x00000004)
#define SDIO_FLAG_DTIMEOUT                  ((uint32_t)0x00000008)
#define SDIO_FLAG_TXUNDERR                  ((uint32_t)0x00000010)
#define SDIO_FLAG_RXOVERR                   ((uint32_t)0x00000020)
#define SDIO_FLAG_CMDREND                   ((uint32_t)0x00000040)
#define SDIO_FLAG_CMDSENT                   ((uint32_t)0x00000080)
#define SDIO_FLAG_DATAEND                   ((uint32_t)0x00000100)
#define SDIO_FLAG_STBITERR                  ((uint32_t)0x00000200)
#define SDIO_FLAG_DBCKEND                   ((uint32_t)0x00000400)
#define SDIO_FLAG_CMDACT                    ((uint32_t)0x00000800)
#define SDIO_FLAG_TXACT                     ((uint32_t)0x00001000)
#define SDIO_FLAG_RXACT                     ((uint32_t)0x00002000)
#define SDIO_FLAG_TXFIFOHE                  ((uint32_t)0x00004000)
#define SDIO_FLAG_RXFIFOHF                  ((uint32_t)0x00008000)
#define SDIO_FLAG_TXFIFOF                   ((uint32_t)0x00010000)
#define SDIO_FLAG_RXFIFOF                   ((uint32_t)0x00020000)
#define SDIO_FLAG_TXFIFOE                   ((uint32_t)0x00040000)
#define SDIO_FLAG_RXFIFOE                   ((uint32_t)0x00080000)
#define SDIO_FLAG_TXDAVL                    ((uint32_t)0x00100000)
#define SDIO_FLAG_RXDAVL                    ((uint32_t)0x00200000)
#define SDIO_FLAG_SDIOIT                    ((uint32_t)0x00400000)
#define SDIO_FLAG_CEATAEND                  ((uint32_t)0x00800000)


//用户配置区			  
//SDIO时钟计算公式:SDIO_CK时钟=SDIOCLK/[clkdiv+2];其中,SDIOCLK一般为72Mhz
//使用DMA模式的时候,传输速率可以到24Mhz,不过如果你的卡不是高速卡,可能也会出错
//出错就请降低时钟,使用查询模式的话,推荐SDIO_TRANSFER_CLK_DIV设置为3或者更大
#define SDIO_INIT_CLK_DIV        0xB2 		//SDIO初始化频率，最大400Kh  
#define SDIO_TRANSFER_CLK_DIV    0x06		//SDIO传输频率=72/(n+2)MHz;0x06≈9MHz,1位总线+杜邦线更稳

// 总线宽度开关：0 = 1 位总线(只用 SD_D0)，1 = 4 位总线(SD_D0~D3)
// 很多模块/杜邦线只接了 CLK、CMD、D0，这种情况下开 4 位会出现
// 初始化能过、但每次读块都失败(表现为一直重试、画面卡住)。
// 所以默认用 1 位：速度约 1/4，但接线最省、最不容易出问题。
// 如果确认 DAT1~DAT3 都接了，把它改成 1 可以明显加快相册读图。
#define SD_USE_4BIT_BUS          0

//////////////////////////////////////////////////////////////////////////////////////////////////// 
//SDIO工作模式定义,通过SD_SetDeviceMode函数设置.
#define SD_POLLING_MODE    	0  	//查询模式,该模式下,如果读写有问题,建议增大SDIO_TRANSFER_CLK_DIV的设置.
#define SD_DMA_MODE    		1	//DMA模式,该模式下,如果读写有问题,建议增大SDIO_TRANSFER_CLK_DIV的设置.   

//SDIO 各种错误枚举定义
typedef enum
{	 
	//特殊错误定义 
	SD_CMD_CRC_FAIL                    = (1), /*!< 收到命令响应,但 CRC 校验失败 */
	SD_DATA_CRC_FAIL                   = (2), /*!< 数据块收发失败(CRC 校验错误) */
	SD_CMD_RSP_TIMEOUT                 = (3), /*!< 命令响应超时 */
	SD_DATA_TIMEOUT                    = (4), /*!< 数据超时 */
	SD_TX_UNDERRUN                     = (5), /*!< 发送 FIFO 下溢 */
	SD_RX_OVERRUN                      = (6), /*!< 接收 FIFO 溢出 */
	SD_START_BIT_ERR                   = (7), /*!< 宽总线模式下,部分数据线没检测到起始位 */
	SD_CMD_OUT_OF_RANGE                = (8), /*!< CMD 的参数超出范围 */
	SD_ADDR_MISALIGNED                 = (9), /*!< 地址未对齐 */
	SD_BLOCK_LEN_ERR                   = (10), /*!< 传输块长度不被该卡允许,或传输字节数与块长度不一致 */
	SD_ERASE_SEQ_ERR                   = (11), /*!< 擦除命令序列出错 */
	SD_BAD_ERASE_PARAM                 = (12), /*!< 擦除组选择无效 */
	SD_WRITE_PROT_VIOLATION            = (13), /*!< 试图写入写保护的块 */
	SD_LOCK_UNLOCK_FAILED              = (14), /*!< 解锁命令的序列或密码错误,或试图访问已锁定的卡 */
	SD_COM_CRC_FAILED                  = (15), /*!< 上一条命令的 CRC 校验失败 */
	SD_ILLEGAL_CMD                     = (16), /*!< 该命令在当前卡状态下非法 */
	SD_CARD_ECC_FAILED                 = (17), /*!< 卡内部 ECC 已启用,但未能纠正数据 */
	SD_CC_ERROR                        = (18), /*!< 卡内部控制器错误 */
	SD_GENERAL_UNKNOWN_ERROR           = (19), /*!< 一般错误或未知错误 */
	SD_STREAM_READ_UNDERRUN            = (20), /*!< 流式读操作中,卡无法维持数据传输 */
	SD_STREAM_WRITE_OVERRUN            = (21), /*!< 流模式下,卡无法维持数据写入 */
	SD_CID_CSD_OVERWRITE               = (22), /*!< CID/CSD 覆写错误 */
	SD_WP_ERASE_SKIP                   = (23), /*!< 只擦除了部分地址空间 */
	SD_CARD_ECC_DISABLED               = (24), /*!< 命令执行时未使用内部 ECC */
	SD_ERASE_RESET                     = (25), /*!< 执行前收到擦除序列之外的命令,擦除序列被清除 */
	SD_AKE_SEQ_ERROR                   = (26), /*!< 认证序列出错 */
	SD_INVALID_VOLTRANGE               = (27),
	SD_ADDR_OUT_OF_RANGE               = (28),
	SD_SWITCH_ERROR                    = (29),
	SD_SDIO_DISABLED                   = (30),
	SD_SDIO_FUNCTION_BUSY              = (31),
	SD_SDIO_FUNCTION_FAILED            = (32),
	SD_SDIO_UNKNOWN_FUNCTION           = (33),
	//标准错误定义
	SD_INTERNAL_ERROR, 
	SD_NOT_CONFIGURED,
	SD_REQUEST_PENDING, 
	SD_REQUEST_NOT_APPLICABLE, 
	SD_INVALID_PARAMETER,  
	SD_UNSUPPORTED_FEATURE,  
	SD_UNSUPPORTED_HW,  
	SD_ERROR,  
	SD_OK = 0 
} SD_Error;		  

//SD卡CSD寄存器数据		  
typedef struct
{
	u8  CSDStruct;            /*!< CSD 结构版本 */
	u8  SysSpecVersion;       /*!< 系统规范版本 */
	u8  Reserved1;            /*!< 保留 */
	u8  TAAC;                 /*!< 数据读访问时间 1 */
	u8  NSAC;                 /*!< 数据读访问时间 2(单位:CLK 周期) */
	u8  MaxBusClkFrec;        /*!< 最大总线时钟频率 */
	u16 CardComdClasses;      /*!< 卡支持的命令类 */
	u8  RdBlockLen;           /*!< 最大读数据块长度 */
	u8  PartBlockRead;        /*!< 是否允许部分块读 */
	u8  WrBlockMisalign;      /*!< 写块地址未对齐 */
	u8  RdBlockMisalign;      /*!< 读块地址未对齐 */
	u8  DSRImpl;              /*!< 是否实现 DSR */
	u8  Reserved2;            /*!< 保留 */
	u32 DeviceSize;           /*!< 设备容量 */
	u8  MaxRdCurrentVDDMin;   /*!< VDD 最小时的读电流最大值 */
	u8  MaxRdCurrentVDDMax;   /*!< VDD 最大时的读电流最大值 */
	u8  MaxWrCurrentVDDMin;   /*!< VDD 最小时的写电流最大值 */
	u8  MaxWrCurrentVDDMax;   /*!< VDD 最大时的写电流最大值 */
	u8  DeviceSizeMul;        /*!< 容量乘数 */
	u8  EraseGrSize;          /*!< 擦除组大小 */
	u8  EraseGrMul;           /*!< 擦除组大小乘数 */
	u8  WrProtectGrSize;      /*!< 写保护组大小 */
	u8  WrProtectGrEnable;    /*!< 写保护组使能 */
	u8  ManDeflECC;           /*!< 厂商默认 ECC */
	u8  WrSpeedFact;          /*!< 写入速度因子 */
	u8  MaxWrBlockLen;        /*!< 最大写数据块长度 */
	u8  WriteBlockPaPartial;  /*!< 是否允许部分块写 */
	u8  Reserved3;            /*!< 保留 */
	u8  ContentProtectAppli;  /*!< 内容保护应用 */
	u8  FileFormatGrouop;     /*!< 文件格式组 */
	u8  CopyFlag;             /*!< 复制标志(OTP) */
	u8  PermWrProtect;        /*!< 永久写保护 */
	u8  TempWrProtect;        /*!< 临时写保护 */
	u8  FileFormat;           /*!< 文件格式 */
	u8  ECC;                  /*!< ECC 编码 */
	u8  CSD_CRC;              /*!< CSD CRC 校验 */
	u8  Reserved4;            /*!< 固定为 1 */
} SD_CSD;   

//SD卡CID寄存器数据
typedef struct
{
	u8  ManufacturerID;       /*!< 厂商 ID */
	u16 OEM_AppliID;          /*!< OEM/应用 ID */
	u32 ProdName1;            /*!< 产品名第 1 部分 */
	u8  ProdName2;            /*!< 产品名第 2 部分 */
	u8  ProdRev;              /*!< 产品版本 */
	u32 ProdSN;               /*!< 产品序列号 */
	u8  Reserved1;            /*!< 保留 */
	u16 ManufactDate;         /*!< 生产日期 */
	u8  CID_CRC;              /*!< CID CRC 校验 */
	u8  Reserved2;            /*!< 固定为 1 */
} SD_CID;	 
//SD卡状态
typedef enum
{
	SD_CARD_READY                  = ((uint32_t)0x00000001),
	SD_CARD_IDENTIFICATION         = ((uint32_t)0x00000002),
	SD_CARD_STANDBY                = ((uint32_t)0x00000003),
	SD_CARD_TRANSFER               = ((uint32_t)0x00000004),
	SD_CARD_SENDING                = ((uint32_t)0x00000005),
	SD_CARD_RECEIVING              = ((uint32_t)0x00000006),
	SD_CARD_PROGRAMMING            = ((uint32_t)0x00000007),
	SD_CARD_DISCONNECTED           = ((uint32_t)0x00000008),
	SD_CARD_ERROR                  = ((uint32_t)0x000000FF)
}SDCardState;

//SD卡信息,包括CSD,CID等数据
typedef struct
{
  SD_CSD SD_csd;
  SD_CID SD_cid;
  long long CardCapacity;  	//SD卡容量,单位:字节,最大支持2^64字节大小的卡.
  u32 CardBlockSize; 		//SD卡块大小	
  u16 RCA;					//卡相对地址
  u8 CardType;				//卡类型
} SD_CardInfo;
extern SD_CardInfo SDCardInfo;//SD卡信息			 
////////////////////////////////////////////////////////////////////////////////////////////////////
//SDIO 指令集
#define SD_CMD_GO_IDLE_STATE                       ((u8)0)
#define SD_CMD_SEND_OP_COND                        ((u8)1)
#define SD_CMD_ALL_SEND_CID                        ((u8)2)
#define SD_CMD_SET_REL_ADDR                        ((u8)3) /*!< SD 卡使用的 SDIO_SEND_REL_ADDR */
#define SD_CMD_SET_DSR                             ((u8)4)
#define SD_CMD_SDIO_SEN_OP_COND                    ((u8)5)
#define SD_CMD_HS_SWITCH                           ((u8)6)
#define SD_CMD_SEL_DESEL_CARD                      ((u8)7)
#define SD_CMD_HS_SEND_EXT_CSD                     ((u8)8)
#define SD_CMD_SEND_CSD                            ((u8)9)
#define SD_CMD_SEND_CID                            ((u8)10)
#define SD_CMD_READ_DAT_UNTIL_STOP                 ((u8)11) /*!< SD 卡不支持该命令 */
#define SD_CMD_STOP_TRANSMISSION                   ((u8)12)
#define SD_CMD_SEND_STATUS                         ((u8)13)
#define SD_CMD_HS_BUSTEST_READ                     ((u8)14)
#define SD_CMD_GO_INACTIVE_STATE                   ((u8)15)
#define SD_CMD_SET_BLOCKLEN                        ((u8)16)
#define SD_CMD_READ_SINGLE_BLOCK                   ((u8)17)
#define SD_CMD_READ_MULT_BLOCK                     ((u8)18)
#define SD_CMD_HS_BUSTEST_WRITE                    ((u8)19)
#define SD_CMD_WRITE_DAT_UNTIL_STOP                ((u8)20) 
#define SD_CMD_SET_BLOCK_COUNT                     ((u8)23) 
#define SD_CMD_WRITE_SINGLE_BLOCK                  ((u8)24)
#define SD_CMD_WRITE_MULT_BLOCK                    ((u8)25)
#define SD_CMD_PROG_CID                            ((u8)26)
#define SD_CMD_PROG_CSD                            ((u8)27)
#define SD_CMD_SET_WRITE_PROT                      ((u8)28)
#define SD_CMD_CLR_WRITE_PROT                      ((u8)29)
#define SD_CMD_SEND_WRITE_PROT                     ((u8)30)
#define SD_CMD_SD_ERASE_GRP_START                  ((u8)32) /*!< 设置要被擦除的第一个写块的地址
                                                                  (仅 SD 卡适用) */
#define SD_CMD_SD_ERASE_GRP_END                    ((u8)33) /*!< 设置要被擦除的连续地址范围内最后一个写块的
                                                                  地址(仅 SD 卡适用) */
#define SD_CMD_ERASE_GRP_START                     ((u8)35) /*!< 设置要被擦除的第一个写块的地址
                                                                  (仅 MMC 卡,规范 3.31) */

#define SD_CMD_ERASE_GRP_END                       ((u8)36) /*!< 设置要被擦除的连续地址范围内最后一个写块的
                                                                  地址(仅 MMC 卡,规范 3.31) */

#define SD_CMD_ERASE                               ((u8)38)
#define SD_CMD_FAST_IO                             ((u8)39) /*!< SD 卡不支持该命令 */
#define SD_CMD_GO_IRQ_STATE                        ((u8)40) /*!< SD 卡不支持该命令 */
#define SD_CMD_LOCK_UNLOCK                         ((u8)42)
#define SD_CMD_APP_CMD                             ((u8)55)
#define SD_CMD_GEN_CMD                             ((u8)56)
#define SD_CMD_NO_CMD                              ((u8)64)

/** 
  * @brief 以下命令是 SD 卡专用命令。
  *        发送这些命令前,应先发送 SDIO_APP_CMD(CMD55)。 
  */
#define SD_CMD_APP_SD_SET_BUSWIDTH                 ((u8)6)  /*!< 仅 SD 卡适用 */
#define SD_CMD_SD_APP_STAUS                        ((u8)13) /*!< 仅 SD 卡适用 */
#define SD_CMD_SD_APP_SEND_NUM_WRITE_BLOCKS        ((u8)22) /*!< 仅 SD 卡适用 */
#define SD_CMD_SD_APP_OP_COND                      ((u8)41) /*!< 仅 SD 卡适用 */
#define SD_CMD_SD_APP_SET_CLR_CARD_DETECT          ((u8)42) /*!< 仅 SD 卡适用 */
#define SD_CMD_SD_APP_SEND_SCR                     ((u8)51) /*!< 仅 SD 卡适用 */
#define SD_CMD_SDIO_RW_DIRECT                      ((u8)52) /*!< 仅 SD I/O 卡适用 */
#define SD_CMD_SDIO_RW_EXTENDED                    ((u8)53) /*!< 仅 SD I/O 卡适用 */

/** 
  * @brief 以下命令是 SD 卡专用的安全命令。
  *        发送这些命令前,应先发送 SDIO_APP_CMD。 
  */
#define SD_CMD_SD_APP_GET_MKB                      ((u8)43) /*!< 仅 SD 卡适用 */
#define SD_CMD_SD_APP_GET_MID                      ((u8)44) /*!< 仅 SD 卡适用 */
#define SD_CMD_SD_APP_SET_CER_RN1                  ((u8)45) /*!< 仅 SD 卡适用 */
#define SD_CMD_SD_APP_GET_CER_RN2                  ((u8)46) /*!< 仅 SD 卡适用 */
#define SD_CMD_SD_APP_SET_CER_RES2                 ((u8)47) /*!< 仅 SD 卡适用 */
#define SD_CMD_SD_APP_GET_CER_RES1                 ((u8)48) /*!< 仅 SD 卡适用 */
#define SD_CMD_SD_APP_SECURE_READ_MULTIPLE_BLOCK   ((u8)18) /*!< 仅 SD 卡适用 */
#define SD_CMD_SD_APP_SECURE_WRITE_MULTIPLE_BLOCK  ((u8)25) /*!< 仅 SD 卡适用 */
#define SD_CMD_SD_APP_SECURE_ERASE                 ((u8)38) /*!< 仅 SD 卡适用 */
#define SD_CMD_SD_APP_CHANGE_SECURE_AREA           ((u8)49) /*!< 仅 SD 卡适用 */
#define SD_CMD_SD_APP_SECURE_WRITE_MKB             ((u8)48) /*!< 仅 SD 卡适用 */
  			   
//支持的SD卡定义
#define SDIO_STD_CAPACITY_SD_CARD_V1_1             ((u32)0x00000000)
#define SDIO_STD_CAPACITY_SD_CARD_V2_0             ((u32)0x00000001)
#define SDIO_HIGH_CAPACITY_SD_CARD                 ((u32)0x00000002)
#define SDIO_MULTIMEDIA_CARD                       ((u32)0x00000003)
#define SDIO_SECURE_DIGITAL_IO_CARD                ((u32)0x00000004)
#define SDIO_HIGH_SPEED_MULTIMEDIA_CARD            ((u32)0x00000005)
#define SDIO_SECURE_DIGITAL_IO_COMBO_CARD          ((u32)0x00000006)
#define SDIO_HIGH_CAPACITY_MMC_CARD                ((u32)0x00000007)

//SDIO相关参数定义
#define NULL 0
#define SDIO_STATIC_FLAGS               ((u32)0x000005FF)
#define SDIO_CMD0TIMEOUT                ((u32)0x00010000)	  
#define SDIO_DATATIMEOUT                ((u32)0x00200000)   /* 原来 0xFFFFFFFF：一次读失败要等好几分钟，改成约 0.2s 就返回 */	  
#define SDIO_FIFO_Address               ((u32)0x40018080)

//卡状态R1的错误掩码(OCR寄存器)  
#define SD_OCR_ADDR_OUT_OF_RANGE        ((u32)0x80000000)
#define SD_OCR_ADDR_MISALIGNED          ((u32)0x40000000)
#define SD_OCR_BLOCK_LEN_ERR            ((u32)0x20000000)
#define SD_OCR_ERASE_SEQ_ERR            ((u32)0x10000000)
#define SD_OCR_BAD_ERASE_PARAM          ((u32)0x08000000)
#define SD_OCR_WRITE_PROT_VIOLATION     ((u32)0x04000000)
#define SD_OCR_LOCK_UNLOCK_FAILED       ((u32)0x01000000)
#define SD_OCR_COM_CRC_FAILED           ((u32)0x00800000)
#define SD_OCR_ILLEGAL_CMD              ((u32)0x00400000)
#define SD_OCR_CARD_ECC_FAILED          ((u32)0x00200000)
#define SD_OCR_CC_ERROR                 ((u32)0x00100000)
#define SD_OCR_GENERAL_UNKNOWN_ERROR    ((u32)0x00080000)
#define SD_OCR_STREAM_READ_UNDERRUN     ((u32)0x00040000)
#define SD_OCR_STREAM_WRITE_OVERRUN     ((u32)0x00020000)
#define SD_OCR_CID_CSD_OVERWRIETE       ((u32)0x00010000)
#define SD_OCR_WP_ERASE_SKIP            ((u32)0x00008000)
#define SD_OCR_CARD_ECC_DISABLED        ((u32)0x00004000)
#define SD_OCR_ERASE_RESET              ((u32)0x00002000)
#define SD_OCR_AKE_SEQ_ERROR            ((u32)0x00000008)
#define SD_OCR_ERRORBITS                ((u32)0xFDFFE008)

//R6响应的掩码 
#define SD_R6_GENERAL_UNKNOWN_ERROR     ((u32)0x00002000)
#define SD_R6_ILLEGAL_CMD               ((u32)0x00004000)
#define SD_R6_COM_CRC_FAILED            ((u32)0x00008000)

#define SD_VOLTAGE_WINDOW_SD            ((u32)0x80100000)
#define SD_HIGH_CAPACITY                ((u32)0x40000000)
#define SD_STD_CAPACITY                 ((u32)0x00000000)
#define SD_CHECK_PATTERN                ((u32)0x000001AA)
#define SD_VOLTAGE_WINDOW_MMC           ((u32)0x80FF8000)

#define SD_MAX_VOLT_TRIAL               ((u32)0x00001000)   /* ACMD41 轮询次数,约 2 秒。SD 规范允许卡上电 1 秒内才就绪,
                                                                 * 原来写 0xFF(约 0.1 秒)会误报 27 号错误 SD_INVALID_VOLTRANGE */
#define SD_ALLZERO                      ((u32)0x00000000)

#define SD_WIDE_BUS_SUPPORT             ((u32)0x00040000)
#define SD_SINGLE_BUS_SUPPORT           ((u32)0x00010000)
#define SD_CARD_LOCKED                  ((u32)0x02000000)
#define SD_CARD_PROGRAMMING             ((u32)0x00000007)
#define SD_CARD_RECEIVING               ((u32)0x00000006)
#define SD_DATATIMEOUT                  ((u32)0x00200000)   /* 同上，块读/写等待的超时计数 */
#define SD_0TO7BITS                     ((u32)0x000000FF)
#define SD_8TO15BITS                    ((u32)0x0000FF00)
#define SD_16TO23BITS                   ((u32)0x00FF0000)
#define SD_24TO31BITS                   ((u32)0xFF000000)
#define SD_MAX_DATA_LENGTH              ((u32)0x01FFFFFF)

#define SD_HALFFIFO                     ((u32)0x00000008)
#define SD_HALFFIFOBYTES                ((u32)0x00000020)

//支持的命令类  
#define SD_CCCC_LOCK_UNLOCK             ((u32)0x00000080)
#define SD_CCCC_WRITE_PROT              ((u32)0x00000040)
#define SD_CCCC_ERASE                   ((u32)0x00000020)
																	 
//CMD8指令
#define SDIO_SEND_IF_COND               ((u32)0x00000008)
////////////////////////////////////////////////////////////////////////////////////////////////////
//相关函数定义
SD_Error SD_Init(void);
void SDIO_Clock_Set(u8 clkdiv);
void SDIO_Send_Cmd(u8 cmdindex,u8 waitrsp,u32 arg);
void SDIO_Send_Data_Cfg(u32 datatimeout,u32 datalen,u8 blksize,u8 dir);
SD_Error SD_PowerON(void);    
SD_Error SD_PowerOFF(void);
SD_Error SD_InitializeCards(void);
SD_Error SD_GetCardInfo(SD_CardInfo *cardinfo);		  
SD_Error SD_EnableWideBusOperation(u32 wmode);
SD_Error SD_SetDeviceMode(u32 mode);
SD_Error SD_SelectDeselect(u32 addr); 
SD_Error SD_SendStatus(uint32_t *pcardstatus);
SDCardState SD_GetState(void);
SD_Error SD_ReadBlock(u8 *buf,long long addr,u16 blksize);  
SD_Error SD_ReadMultiBlocks(u8 *buf,long long  addr,u16 blksize,u32 nblks);  
SD_Error SD_WriteBlock(u8 *buf,long long addr,  u16 blksize);	
SD_Error SD_WriteMultiBlocks(u8 *buf,long long addr,u16 blksize,u32 nblks);
SD_Error SD_ProcessIRQSrc(void);
 
 
void SD_DMA_Config(u32*mbuf,u32 bufsize,u32 dir); 

u8 SD_ReadDisk(u8*buf,u32 sector,u8 cnt); 	//读SD卡,fatfs/usb调用
u8 SD_WriteDisk(u8*buf,u32 sector,u8 cnt);	//写SD卡,fatfs/usb调用


#endif 





