/***************************************************************
文件功能： 所有编码器测速的处理，ABZ, UVW信号，旋变
文件版本： 
最新更新： 
	
****************************************************************/
// 说明:
//2808的gpio20-23可以配置为QEP用作编码器测速，也可以配置为spiC用作旋变测速， 在更改时需要初始化其gpio的复用；
//28035的gpio20-23可以配置为QEP，或者gpio口，用模拟旋变测速，需要注意区分；

#include "MotorEncoder.h"
#include "MotorPmsmMain.h"
#include "MotorPmsmParEst.h"
#include "MotorVCInclude.h"
#include "SubPrgInclude.h"

// 全局变量定义 
struct EQEP_REGS       *EQepRegs;      // QEP 指针

PG_DATA_STRUCT			gPGData;        // 编码器公用结构体
IPM_UVW_PG_STRUCT       gUVWPG;         // UVW编码器的数据结构
IPM_PG_DIR_STRUCT       gPGDir;         // 辨识编码器接线方式的结构体
FVC_SPEED_STRUCT		gFVCSpeed;    // 编码器反馈速度数据结构 
ROTOR_TRANS_STRUCT		gRotorTrans;    // 旋转变压器速度位置反馈数据
BURR_FILTER_STRUCT		gSpeedFilter;   // 速度反馈滤波结构
CUR_LINE_STRUCT_DEF		gSpeedLine;	    // ..

Uint const gUVWAngleTable[6] = {        // UVW 角度编码表格
    //001                //010               //011
    (330*65536L/360), (210*65536L/360),	(270*65536L/360), 
    //100                //101                //110
    (90 *65536L/360), (30 *65536L/360), (150*65536L/360)       
};

// // 内部函数声明
void GetSpeedMMethod(void);
void GetSpeedMTMethod(void);
void SpeedSmoothDeal( ROTOR_SPEED_SMOOTH * pSpeedError,int iSpeed );
void RotorTransCalVel_old(void);

/*************************************************************
    旋变硬件接口的初始化， 2808 和28035不一样
*************************************************************/
void InitRtInterface(void)
{
    EALLOW;
#ifdef TMS320F2808                             // 2808 使用spiC与旋变芯片通讯

	SysCtrlRegs.PCLKCR0.bit.SPICENCLK = 1;      //BIT6 SPI-C
    // 读取旋转变压器位置SPI复用
    GpioCtrlRegs.GPAPUD.bit.GPIO21  = 0;        // Enable pull-up
    GpioCtrlRegs.GPAMUX2.bit.GPIO21 = 2;        // SPIC MI 
    GpioCtrlRegs.GPAQSEL2.bit.GPIO21= 3;        // Asynch        
    GpioCtrlRegs.GPAPUD.bit.GPIO22  = 0;        // Enable pull-up
    GpioCtrlRegs.GPAMUX2.bit.GPIO22 = 2;        // SPIC CLK 
    GpioCtrlRegs.GPAQSEL2.bit.GPIO22= 3;        // Asynch        
    GpioCtrlRegs.GPAPUD.bit.GPIO23  = 0;        // Enable pull-up
    GpioCtrlRegs.GPAMUX2.bit.GPIO23 = 2;        // SPIC STE
    GpioCtrlRegs.GPAQSEL2.bit.GPIO23= 3;        // Asynch    

    // Initialize SPI FIFO registers
    SpicRegs.SPIFFTX.all=0xE040;
    SpicRegs.SPIFFRX.all=0x405f;                // Receive FIFO reset
    SpicRegs.SPIFFRX.all=0x205f;                // Re-enable transmit FIFO operation
    SpicRegs.SPIFFCT.all=0x0000;

    // Initialize  SPI
    SpicRegs.SPICCR.all =0x000F;                // Reset on, , 16-bit char bits

    #if 1                                       // 12位PG卡spi模式
    SpicRegs.SPICCR.bit.CLKPOLARITY = 1;        // falling edge receive
    SpicRegs.SPICTL.all =0x000E;                // Enable master mode, SPICLK signal delayed by one half-cycle
                                                // enable talk, and SPI int disabled.
                                                // CLOCK PHASE = 1
    #else                                       // 16位PG卡spi模式
	SpicRegs.SPICCR.bit.CLKPOLARITY = 0;	    //16bit pg
    SpicRegs.SPICTL.all =0x0006;      		    //16bit pg
    #endif
                                       
    // SPI波特率    LSPCLK = 100MHz/4
    SpicRegs.SPIBRR = 48;                       // 100/4 * 10^6 / (49)  = 510KHz
   
    
    SpicRegs.SPICCR.bit.SPISWRESET = 1;     
    SpicRegs.SPIPRI.bit.FREE = 1;          

    // 旋变的RD信号GPIO34 配置
    GpioCtrlRegs.GPBPUD.bit.GPIO34 = 0;         // Enable pull-up       
    GpioCtrlRegs.GPBMUX1.bit.GPIO34= 0;        
    GpioDataRegs.GPBSET.bit.GPIO34 = 1;         // Set sample to High
    GpioCtrlRegs.GPBDIR.bit.GPIO34 = 1;		    // Resolver sample signal config: output
    
#else                                           // 28035 需要使用IO口模拟串口通讯

    GpioCtrlRegs.GPAMUX2.bit.GPIO20 = 0;        // REDVEL\  ->
    GpioCtrlRegs.GPAMUX2.bit.GPIO21 = 0;        // SO       <-
    GpioCtrlRegs.GPAMUX2.bit.GPIO23 = 0;        // SCLK     ->
    GpioCtrlRegs.AIOMUX1.bit.AIO10 = 0;         // SAMPLE    AIO-port ->
    GpioCtrlRegs.AIOMUX1.bit.AIO12 = 0;         // RD\       AIO-port ->

    GpioCtrlRegs.GPADIR.bit.GPIO20 = 1;         // 
    GpioCtrlRegs.GPADIR.bit.GPIO21 = 0;         // 
    GpioCtrlRegs.GPADIR.bit.GPIO23 = 1;         //
    GpioCtrlRegs.AIODIR.bit.AIO10 = 1;
    GpioCtrlRegs.AIODIR.bit.AIO12 = 1;

    ROTOR_TRANS_RDVEL = 1;                      // 一直选择读取位置
#endif
    EDIS;
}


/*************************************************************
	设置QEP接口
*************************************************************/
void InitSetQEP(void)
{
   	EALLOW;
	(*EQepRegs).QEPCTL.bit.QPEN = 0;
	(*EQepRegs).QDECCTL.all = 0;
	(*EQepRegs).QEPCTL.bit.PCRM = 1;    // pos reset mode
	(*EQepRegs).QEPCTL.bit.QPEN = 1;
	(*EQepRegs).QEPCTL.bit.QCLM = 1;
	(*EQepRegs).QEPCTL.bit.UTE = 1;
	(*EQepRegs).QEPCTL.bit.WDE = 0;

	(*EQepRegs).QPOSCTL.all = 0;
	(*EQepRegs).QCAPCTL.bit.CEN = 0;
	(*EQepRegs).QCAPCTL.bit.CCPS = 2;		//CAP时钟周期4分频,保证最大计数为2ms
	(*EQepRegs).QCAPCTL.bit.UPPS = 2;
	(*EQepRegs).QCAPCTL.bit.CEN = 1;

	(*EQepRegs).QPOSCNT = 0;
	(*EQepRegs).QPOSINIT = 0;
	(*EQepRegs).QPOSMAX = 0xFFFFFFFF;
	(*EQepRegs).QPOSCMP = 0;
	(*EQepRegs).QUTMR = 0;
	(*EQepRegs).QWDTMR = 0;
	(*EQepRegs).QUPRD = C_TIME_05MS;			//0.5ms定时器(该时间要大于测速判断间隔)
	(*EQepRegs).QCLR.all = 0x0FFF;

	(*EQepRegs).QCTMR = 0;
	(*EQepRegs).QCPRD = 0;

    (*EQepRegs).QEPCTL.bit.IEL = 1;

	EDIS;
}

//**************************************************************************
// 修改PG卡类型后，需要重新初始化部分外设

//**************************************************************************
void ReInitForPG(void)
{
    switch(gPGData.PGType)
    {
        case PG_TYPE_UVW:
            gPGData.PGMode = 0;
            EALLOW;
            GpioCtrlRegs.GPAMUX2.bit.GPIO20 = 1;    //eQEP1A
            GpioCtrlRegs.GPAMUX2.bit.GPIO21 = 1;    //eQEP1B
            GpioCtrlRegs.GPAMUX2.bit.GPIO23 = 1;    //eQEP1I
        #ifdef TMS320F2808
            GpioCtrlRegs.GPAMUX2.bit.GPIO24 = 2;    //eQEP2A
            GpioCtrlRegs.GPAMUX2.bit.GPIO25 = 2;    //eQEP2B
            GpioCtrlRegs.GPAMUX2.bit.GPIO26 = 2;    //eQEP2I
        #endif   
        #ifdef TMS320F28035
            GpioCtrlRegs.AIOMUX1.bit.AIO6  = 2;     // ad uvw信号
            GpioCtrlRegs.AIOMUX1.bit.AIO10 = 2;
            GpioCtrlRegs.AIOMUX1.bit.AIO12 = 2;
        #endif
            EDIS;            
            break;
            
        case PG_TYPE_SPECIAL_UVW:
            gPGData.PGMode = 0;
            break;
            
        case PG_TYPE_RESOLVER:      // 区分2808和28035进行配置
            gPGData.PGMode = 1;
            EALLOW;
            SysCtrlRegs.PCLKCR1.bit.EQEP1ENCLK = 0;//不需要QEP模块
        #ifdef TMS320F2808
            SysCtrlRegs.PCLKCR1.bit.EQEP2ENCLK = 0;
        #endif
            EDIS;
            
            InitRtInterface();      // 初始化旋变硬件接口
            break;
            
        case PG_TYPE_SC:
            gPGData.PGMode = 0;
            break;
            
        default:        // PG_TYPE_ABZ
            gPGData.PGMode = 0;
            EALLOW;
            GpioCtrlRegs.GPAMUX2.bit.GPIO20 = 1;    //eQEP1A
            GpioCtrlRegs.GPAMUX2.bit.GPIO21 = 1;    //eQEP1B
            GpioCtrlRegs.GPAMUX2.bit.GPIO23 = 1;    //eQEP1I
            //GpioCtrlRegs.GPAQSEL2.bit.GPIO20 = 0x2;              //滤波设定  
   			//GpioCtrlRegs.GPAQSEL2.bit.GPIO21 = 0x2;              //滤波设定  
            //GpioCtrlRegs.GPAQSEL2.bit.GPIO23 = 0x2;              //滤波设定  
			//GpioCtrlRegs.GPACTRL.bit.QUALPRD2 = 0x03; //PULS_IN滤波时间5*5*20ns = 300ns，端口滤波会造成静差
        #ifdef TMS320F2808
            GpioCtrlRegs.GPAMUX2.bit.GPIO24 = 2;    //eQEP2A
            GpioCtrlRegs.GPAMUX2.bit.GPIO25 = 2;    //eQEP2B
            GpioCtrlRegs.GPAMUX2.bit.GPIO26 = 2;    //eQEP2I
        #endif            
            EDIS;
            break;
    }

    // 确保编码器类型修改后，能正确初始化选定的qep
    if(gPGData.PGMode == 0)
    {
        gPGData.QEPIndex = QEP_SELECT_NONE;
    }
    else        // 如果是旋变，就不用再初始化qep接口电路
    {
        gPGData.QEPIndex = (QEP_INDEX_ENUM_STRUCT)gExtendCmd.bit.QepIndex;
    }
}

/************************************************************
    控制电机机械上正转(或者反转)一圈，识别AB信号的时序。
************************************************************/
Uint JudgeABDir(void)
{
    int m_Deta;

    m_Deta = gIPMZero.FeedPos - gPGDir.ABAngleBak;
    if(m_Deta > 1820)
    {
        if(gPGDir.ABDirCnt < 32767)
        {
            gPGDir.ABDirCnt ++;
        }
        gPGDir.ABAngleBak = gIPMZero.FeedPos;
    }
    else if(m_Deta < -1820)
    {
        if(gPGDir.ABDirCnt > -32767)
        {
            gPGDir.ABDirCnt --;
        }
        gPGDir.ABAngleBak = gIPMZero.FeedPos;
    }

    if(gPGDir.ABDirCnt > 2)
    {
        return (DIR_FORWARD);
    }
    else if(gPGDir.ABDirCnt < -2)
    {
        return (DIR_BACKWARD);
    }
	else
    {
        return (DIR_ERROR);
    }
}

/************************************************************
    控制电机机械上正转(或者反转)一圈，识别UVW信号的时序。
************************************************************/
Uint JudgeUVWDir(void)
{
    int m_Deta;

    m_Deta = (int)gUVWPG.UVWAngle - (int)gPGDir.UVWAngleBak;
    if(m_Deta > 1820)   // 10度，10/360  *65536(Q16格式)
    {
        if(gPGDir.UVWDirCnt < 32767)
        {
            gPGDir.UVWDirCnt ++;
        }
        gPGDir.UVWAngleBak = gUVWPG.UVWAngle;
    }
    else if(m_Deta < -1820)
    {
        if(gPGDir.UVWDirCnt > -32767)
        {
            gPGDir.UVWDirCnt --;
        }
        gPGDir.UVWAngleBak = gUVWPG.UVWAngle;
    }

    if(gPGDir.UVWDirCnt > 2)
    {
        if(gMainCmd.FreqSet >= 0)
        {
             return (DIR_FORWARD);
        }
        else
        {
            return (DIR_BACKWARD);
        }
    }
    else if(gPGDir.UVWDirCnt < -2)
    {
        if(gMainCmd.FreqSet >= 0)
        {
            return (DIR_BACKWARD);
        }
        else
        {
            return (DIR_FORWARD);
        }
    }
	else
    {
        return (DIR_ERROR);
    }
}

/************************************************************
    控制电机机械上正转(或者反转)一圈，识别 旋变 信号的时序。
************************************************************/
Uint JudgeRTDir()
{
    int m_Deta;

    m_Deta = (int)gRotorTrans.RTPos - gPGDir.RtPhaseBak;
    
    if(m_Deta > 1820)   // 10deg
    {
        if(gPGDir.RtDirCnt < 32767)
        {
            gPGDir.RtDirCnt ++;
        }
        gPGDir.RtPhaseBak = (int)gRotorTrans.RTPos;
    }
    else if(m_Deta < -1820)
    {
        if(gPGDir.RtDirCnt > -32767)
        {
            gPGDir.RtDirCnt --;
        }
        gPGDir.RtPhaseBak = (int)gRotorTrans.RTPos;
    }

    if(gPGDir.RtDirCnt > 2)
    {
        return (DIR_FORWARD);
    }
    else if(gPGDir.RtDirCnt < -2)
    {
        return (DIR_BACKWARD);
    }
	else
    {
        return (DIR_ERROR);
    }
}

/*******************************************************************************
	函数:        UVW_GetUVWState(void)
	描述:        UVW编码器获取UVW绝对位置 ---- 在AD中断中执行
	             检查UVW编码器的UVW信号电平，用1~6表示,并计算对应角度
	判断方法:    首先判断编码器的接线方式，然后查表获取对应的角度
	输入:	 
	输出:        gUVWPG.UVWAngle
	
	被调用:      PG_Zero_isr(void)
*******************************************************************************/
void GetUvwPhase()
{
    if((MOTOR_TYPE_PM != gMotorInfo.MotorType) ||
        (gPGData.PGType != PG_TYPE_UVW)   ||
        (gMainCmd.Command.bit.ControlMode != IDC_FVC_CTL))
    {
        return;
    }
        
// 根据AD采样得到UVW信号的逻辑值
    if((Uint)UVW_PG_U > (Uint)0xA500)
    {
        gUVWPG.LogicU = ACTIVE_HARDWARE_LOGICAL_U;
    }
    else //if((Uint)UVW_PG_U < 0xC000)
    {
        gUVWPG.LogicU = ! ACTIVE_HARDWARE_LOGICAL_U;
    }
    if((Uint)UVW_PG_V > (Uint)0xA500)
    {
        gUVWPG.LogicV = ACTIVE_HARDWARE_LOGICAL_V;
    }
    else //if((Uint)UVW_PG_V < 0xC000)
    {
        gUVWPG.LogicV = ! ACTIVE_HARDWARE_LOGICAL_V;
    }

    if((Uint)UVW_PG_W > (Uint)0xA500)
    {
        gUVWPG.LogicW = ACTIVE_HARDWARE_LOGICAL_W;
    }
    else //if((Uint)UVW_PG_W < 0xC000)
    {
        gUVWPG.LogicW = ! ACTIVE_HARDWARE_LOGICAL_W;
    }


// 根据UVW逻辑值计算uvw绝对位置角度, 区分参数辨识时和非参数辨识
    if((gPmParEst.UvwDir == 0 && gMainStatus.RunStep == STATUS_GET_PAR) ||
        (gUVWPG.UvwDir == 0  && gMainStatus.RunStep != STATUS_GET_PAR))
    {
        gUVWPG.UVWData = (gUVWPG.LogicU<<2) + (gUVWPG.LogicV<<1) + gUVWPG.LogicW;
    }
    else
    {
        gUVWPG.UVWData = (gUVWPG.LogicU<<2) + (gUVWPG.LogicW<<1) + gUVWPG.LogicV;
    }
    
    if((gUVWPG.UVWData > 0) && (gUVWPG.UVWData < 7))
    {
        gUVWPG.UVWAngle =  gUVWAngleTable[gUVWPG.UVWData - 1];
    }
    else if(gMainCmd.Command.bit.Start)
    {
        gError.ErrorCode.all |= ERROR_ENCODER;
        if(gUVWPG.UVWData == 7) gError.ErrorInfo[4].bit.Fault1 = 11;     // 未接编码器
        else                    gError.ErrorInfo[4].bit.Fault1 = 12;     // uvw信号故障
    }

}

/*************************************************************
	闭环矢量下获取实际速度0.5ms执行 gFVCSpeed.SpeedEncoder

*************************************************************/
void VCGetFeedBackSpeed(void)
{
    long tempLong;
	//GetMTTimeNum();						//取测速时间间隔
	
    switch(gPGData.PGType)
    {        
        case PG_TYPE_UVW:
        case PG_TYPE_SPECIAL_UVW:
            GetUvwPhase();                                      // UVW 编码器获取uvw信号
            ;
        case PG_TYPE_ABZ:        
	         if((*EQepRegs).QFLG.bit.WTO == 1)		            //10ms内没有脉冲，认为0速
	         {
		        (*EQepRegs).QCLR.all = 0x0010;
	         } 

	         GetSpeedMMethod();
	         GetSpeedMTMethod();
             gFVCSpeed.SpeedTemp = gFVCSpeed.MTSpeed;        // 选择 MT 法
             //gFVCSpeed.SpeedTemp = gFVCSpeed.MSpeed;
             break;
             
        case PG_TYPE_RESOLVER:                                  //在中断里测速,是否要加测速滤波?实际验证
            RotorTransCalVel();   
            gFVCSpeed.SpeedTemp = gRotorTrans.FreqFeed;
            break;
              
        case PG_TYPE_SC:
            ;
            break;
        default:  
             break;
    }

    // 折算传动比
    //gFVCSpeed.SpeedEncoder = (long)gFVCSpeed.SpeedTemp * gFVCSpeed.TransRatio / 1000;
    tempLong = (long)gFVCSpeed.SpeedTemp * gFVCSpeed.TransRatio;
    gFVCSpeed.SpeedEncoder = (llong)tempLong * 16777 >> 24;
    
    gMainCmd.FreqFeed = gFVCSpeed.SpeedEncoder;

}


/*************************************************************
	M法测速，通过0.5ms内的脉冲数计算速度，对应发送频率的速度为：
	极对数×0.5ms内脉冲数×（1秒/0.5ms）/（编码器脉冲数×4）Hz
表示为：gMotorExtInfo.Poles * gFVCSpeed.DetaPos * 2000 / (gPGData.PulseNum * 4)Hz
标么值：Poles * DetaPos * 500 * 100 * 32768 / (PulseNum * gBasePar.FullFreq)
      = Poles * DetaPos * 50000 * 32768 / (PulseNum * FullFreq)
returnVar: gFVCSpeed.MSpeed
*************************************************************/
void GetSpeedMMethod(void)
{
	Ullong m_Llong,m_Long;
	Uint   m_UData;
	int	   m_Speed;
	
	m_UData = abs(gFVCSpeed.MDetaPos);
	m_Long  = (Ulong)gMotorExtInfo.Poles * (Ulong)m_UData;
	m_Llong = ((Ullong)m_Long * 50000)<<15;
	m_Long  = (Ullong)gPGData.PulseNum * (Ullong)gBasePar.FullFreq01;

	m_Speed = __IQsat((m_Llong / m_Long), 32767, -32767);
	if(gFVCSpeed.MDetaPosBak < 0)
	{
		m_Speed = -m_Speed;
	}
    SpeedSmoothDeal( &gFVCSpeed.MSpeedSmooth, m_Speed);
    //gFVCSpeed.MSpeed = Filter8(gFVCSpeed.MSpeedSmooth.LastSpeed, gFVCSpeed.MSpeed);
    gFVCSpeed.MSpeed = Filter4(gFVCSpeed.MSpeedSmooth.LastSpeed, gFVCSpeed.MSpeed);
    //gFVCSpeed.MSpeed = gFVCSpeed.MSpeedSmooth.LastSpeed;
    
    if( gFVCSpeed.MSpeedSmooth.LastSpeed > gFVCSpeed.MSpeed )
    {
        gFVCSpeed.MSpeed++;
    }
    else if(gFVCSpeed.MSpeedSmooth.LastSpeed < gFVCSpeed.MSpeed )
    {
        gFVCSpeed.MSpeed--;
    }
    if(abs(gFVCSpeed.MSpeed - gFVCSpeed.MSpeedSmooth.LastSpeed ) < 8 )
    {
        gFVCSpeed.MSpeed = gFVCSpeed.MSpeedSmooth.LastSpeed;
    }
}

/*************************************************************
	MT法测速，通过计算N个齿脉冲的准确时间计算速度
	极对数×Deta_T内龀迨粒?秒/Deta_T）/（编码器脉冲数×4）Hz
表示为：gMotorExtInfo.Poles * gFVCSpeed.DetaPos / (Deta_T(秒) * gPGData.PulseNum * 4)Hz
标么值：Poles * DetaPos * 100 * 32768 / (Deta_T(秒) * PulseNum * gBasePar.FullFreq * 4)
      = Poles * DetaPos * 375 * 10^6 * 32768 / (Deta_T * PulseNum * FullFreq)
returnVar: gFVCSpeed.MTSpeed
*************************************************************/
void GetSpeedMTMethod(void)
{
	Ullong m_Llong,m_Long;
	Ulong  m_Time;
	Uint   m_UData,m_DetaPos;
	int	   m_Speed;
    long   tempL;
	if((*EQepRegs).QEPSTS.bit.COEF == 1)		//CAP定时器溢出
	{
		gFVCSpeed.Flag = 1;
		(*EQepRegs).QEPSTS.all = 0x0008;
	}
    DINT;
	if((gFVCSpeed.MTPulseNum == 0) || 
	   (gFVCSpeed.MTTime > 65535) ||   //MS
	   (gFVCSpeed.Flag != 0))
	{
        if( 0 < gFVCSpeed.MTPulseNum )    gFVCSpeed.Flag = 0;    //溢出后捕捉的事件的时间是不准确的    	            
		gFVCSpeed.MTPulseNum  = 0;
		gFVCSpeed.MTTime      = 0;
		gFVCSpeed.MTLimitTime = 0;        
	EINT;
		gFVCSpeed.MTSpeed = gFVCSpeed.MSpeed; //M法测速有周期性波动,在没有接收到脉冲时，
        return;
	}

    DINT;
	m_DetaPos = gFVCSpeed.MTPulseNum;
	m_Time    = gFVCSpeed.MTTime;
	gFVCSpeed.MTPulseNum = 0;
	gFVCSpeed.MTTime     = 0;
	EINT;
	gFVCSpeed.MTLimitTime = (Uint)m_Time/m_DetaPos;

	m_UData = (Ulong)gMotorExtInfo.Poles * (Ulong)m_DetaPos;
    m_Long = ((Ullong)DSP_CLOCK * 100L * 1000000L) / (16 * (long)m_Time);
	m_Llong = ((Ullong)m_Long * (Ullong)m_UData)<<15;
	m_Long  = (Ullong)gPGData.PulseNum * (Ullong)gBasePar.FullFreq01;
	while((m_Long>>16) != 0)
	{
		m_Long  = m_Long>>1;
		m_Llong = m_Llong>>1;
	}
	if((m_Llong>>15) >= m_Long)	
	{
		m_Speed = 32767;
	}
	else
	{
		m_UData = m_Long>>1;
		m_Long  = m_Llong>>1;
		m_Speed = (int)(m_Long/m_UData);
	}

	if(gFVCSpeed.MDetaPosBak < 0)
	{
		m_Speed = -m_Speed;
	}
    if(gVCPar.VCSpeedFilter <= 1)
     {
       gFVCSpeed.MTSpeed = Filter4(m_Speed, gFVCSpeed.MTSpeed);
     }
    else
     {
       tempL =  (long)gFVCSpeed.MTSpeed * (gVCPar.VCSpeedFilter-1L) + 2L * m_Speed;
       gFVCSpeed.MTSpeed = tempL / (gVCPar.VCSpeedFilter + 1L);
     } /*MT法测速加入F2-07滤波，2011.5.7 L1082*/
    if(gFVCSpeed.MTSpeed < m_Speed)   gFVCSpeed.MTSpeed ++;
    else                                gFVCSpeed.MTSpeed --;
    if(abs(gFVCSpeed.MTSpeed - m_Speed) < 8)
    {
        gFVCSpeed.MTSpeed  = m_Speed;
    }
}

/******************************************************************************
    速度滤波，剔除毛刺的处理
******************************************************************************/
void SpeedSmoothDeal( ROTOR_SPEED_SMOOTH * pSpeedError, int iSpeed )
{
    if( abs( pSpeedError->LastSpeed - iSpeed ) < pSpeedError->SpeedMaxErr )
    {
        pSpeedError->LastSpeed = iSpeed;
        pSpeedError->SpeedMaxErr = pSpeedError->SpeedMaxErr>>1;
        if( 1000 > pSpeedError->SpeedMaxErr )
            pSpeedError->SpeedMaxErr = 1000;
    }
    else
    {
        if( 16383 > pSpeedError->SpeedMaxErr )
            pSpeedError->SpeedMaxErr = pSpeedError->SpeedMaxErr<<1;
        else
            pSpeedError->SpeedMaxErr = 32767;
    }
}

/*************************************************************
    M法测速，在中断程序中判断0.5MS时间是否到(在PWM中断中执行)
*************************************************************/
void GetMDetaPos(void)
{
	DINT;
	if((*EQepRegs).QFLG.bit.UTO == 0)		
	{
	EINT;
		return;							//QEP中的2ms定时器没有到
	}
    EALLOW;
	(*EQepRegs).QCLR.all = 0x0800;
    EDIS;

	gFVCSpeed.MDetaPos = (int)((long)(*EQepRegs).QPOSLAT - gFVCSpeed.MLastPos);
    if(0 != gFVCSpeed.MDetaPos)
    {
        gFVCSpeed.MDetaPosBak = gFVCSpeed.MDetaPos;
    }    
	gFVCSpeed.MLastPos = (*EQepRegs).QPOSLAT;
	EINT;
}

/*************************************************************
T法测速褂玫乃鸭龀寮涓粜畔⒌某绦颍贸绦蚍稚⒃诓煌牡胤街葱?
*************************************************************/
void GetMTTimeNum(void)
{
    EALLOW;
	if((*EQepRegs).QEPSTS.bit.UPEVNT == 0)	//还没有接收到足够的脉冲数
	{
		return;
	}
	if((Uint)(*EQepRegs).QCPRD < (Uint)gFVCSpeed.MTLimitTime)
	{
	    (*EQepRegs).QEPSTS.all = 0x0080;	
		return;
	}
    DINT;
	gFVCSpeed.MTPulseNum += 4;
	gFVCSpeed.MTTime += (*EQepRegs).QCPRD;
	EINT;

	(*EQepRegs).QEPSTS.all = 0x0080;
    EDIS;
}

/************************************************************
    获取旋转变压器位置信号
* 旋变高速时需要相位补偿；
* 由于辨识零点位置的需要，模拟了一个Z信号
************************************************************/
void GetRotorTransPos()
{    
#ifdef TMS320F28035
	Uint  mData,mBit;
	//Uint  mWatie;

    mData = 0;
	DINT;
	ROTOR_TRANS_RD=0;		//begin to transmit data
    
    gCpuTime.tmpBase = GetTime();
// 1st Fall-edge
	ROTOR_TRANS_SCLK  = 0;	//Set SCLK
    //asm(" RPT #5 || NOP ");
	mBit   = (ROTOR_TRANS_SO) << 15;     //MSB-bit15	    
	mData  = mData | mBit;	
	ROTOR_TRANS_SCLK = 1;
    asm(" RPT #5 || NOP ");
// 2nd Fall-edge
    ROTOR_TRANS_SCLK  = 0;
    //asm(" RPT #5 || NOP ");
	mBit   = (ROTOR_TRANS_SO) << 14;     // bit14
	mData  = mData | mBit;	
	ROTOR_TRANS_SCLK = 1;
    asm(" RPT #5 || NOP ");
// 3rd Fall-edge
    ROTOR_TRANS_SCLK  = 0;
    //asm(" RPT #5 || NOP ");
	mBit   = (ROTOR_TRANS_SO) << 13;     // bit13
	mData  = mData | mBit;	
	ROTOR_TRANS_SCLK = 1;
    asm(" RPT #5 || NOP ");
// 4th Fall-edge
    ROTOR_TRANS_SCLK  = 0;
    //asm(" RPT #5 || NOP ");
	mBit   = (ROTOR_TRANS_SO) << 12;     // bit12
	mData  = mData | mBit;	
	ROTOR_TRANS_SCLK = 1;
    asm(" RPT #5 || NOP ");
// 5th Fall-edge
    ROTOR_TRANS_SCLK  = 0;
    //asm(" RPT #5 || NOP ");
	mBit   = (ROTOR_TRANS_SO) << 11;     // bit11
	mData  = mData | mBit;	
	ROTOR_TRANS_SCLK = 1;
    asm(" RPT #5 || NOP ");
// 6th Fall-edge
    ROTOR_TRANS_SCLK  = 0;
    //asm(" RPT #5 || NOP ");
	mBit   = (ROTOR_TRANS_SO) << 10;     // bit10
	mData  = mData | mBit;	
	ROTOR_TRANS_SCLK = 1;
    asm(" RPT #5 || NOP ");
// 7th Fall-edge
    ROTOR_TRANS_SCLK  = 0;
    //asm(" RPT #5 || NOP ");
	mBit   = (ROTOR_TRANS_SO) << 9;     // bit9
	mData  = mData | mBit;	
	ROTOR_TRANS_SCLK = 1;
    asm(" RPT #5 || NOP ");
// 8th Fall-edge
    ROTOR_TRANS_SCLK  = 0;
    //asm(" RPT #5 || NOP ");
	mBit   = (ROTOR_TRANS_SO) << 8;     // bit8
	mData  = mData | mBit;	
	ROTOR_TRANS_SCLK = 1;
    asm(" RPT #5 || NOP ");
// 9th Fall-edge
    ROTOR_TRANS_SCLK  = 0;
    //asm(" RPT #5 || NOP ");
	mBit   = (ROTOR_TRANS_SO) << 7;     // bit7
	mData  = mData | mBit;	
	ROTOR_TRANS_SCLK = 1;
    asm(" RPT #5 || NOP ");
// 10th Fall-edge
    ROTOR_TRANS_SCLK  = 0;
    //asm(" RPT #5 || NOP ");
	mBit   = (ROTOR_TRANS_SO) << 6;     // bit6
	mData  = mData | mBit;	
	ROTOR_TRANS_SCLK = 1;
    asm(" RPT #5 || NOP ");
// 11th Fall-edge
    ROTOR_TRANS_SCLK  = 0;
    //asm(" RPT #5 || NOP ");
	mBit   = (ROTOR_TRANS_SO) << 5;     // bit5
	mData  = mData | mBit;	
	ROTOR_TRANS_SCLK = 1;
    asm(" RPT #5 || NOP ");
// 12th Fall-edge
    ROTOR_TRANS_SCLK  = 0;
    //asm(" RPT #5 || NOP ");
	mBit   = (ROTOR_TRANS_SO) << 4;     // bit4
	mData  = mData | mBit;	
	ROTOR_TRANS_SCLK = 1;
    //asm(" RPT #5 || NOP ");
    EINT;
    // 旋变正反方向的处理   (位置处理方向螅俣鹊姆较蚓筒挥么砹�)
    if((gPGData.SpeedDir && gMainStatus.RunStep != STATUS_GET_PAR) ||   // 运行时
        (gPGData.PGDir && gMainStatus.RunStep == STATUS_GET_PAR))  // 辨识时
    {
	    gRotorTrans.RtorBuffer = 65535 - (Uint)mData;
    }
    else
    {
        gRotorTrans.RtorBuffer = (Uint)mData;
    }

    gCpuTime.tmpTime = gCpuTime.tmpBase- GetTime();

#else   // TMS320F2808
 
    // 旋变正反方向的处理   (位置处理方向后，速度的方向就不用处理了)
    if((gPGData.SpeedDir && gMainStatus.RunStep != STATUS_GET_PAR) ||   // 运行时
        (gPGData.PGDir && gMainStatus.RunStep == STATUS_GET_PAR))  // 辨识时
    {
        gRotorTrans.RtorBuffer = 65535 - (Uint)SpicRegs.SPIRXBUF;
    }
    else
    {
        gRotorTrans.RtorBuffer = SpicRegs.SPIRXBUF;
    }
#endif

    gRotorTrans.AbsRotPos = gRotorTrans.RtorBuffer >> 4;    // 供功能包含使用的旋变机械位置角度

    // 模拟一个Z信号， 用于带载辨识；产生条件是: 连续两拍负角度，当前拍正角度, 同时满足4ms 间隔时间
    if(gMainStatus.RunStep == STATUS_GET_PAR)
    {        
        if(((int)gRotorTrans.RtorBuffer > 0) &&
            ((int)gRotorTrans.SimuZBack <= 0) &&
            ((int)gRotorTrans.SimuZBack2 <= 0) &&
            (gIPMZero.zFilterCnt == 0))
        {
            gIPMZero.Flag = 1;
            gIPMZero.zFilterCnt = 8; 
            gIPMPos.PosInZInfo = gIPMPos.RotorPos;
        }
        gRotorTrans.SimuZBack2 = gRotorTrans.SimuZBack;
        gRotorTrans.SimuZBack = gRotorTrans.RtorBuffer;
    }
    
    // 处理旋变的极对数， 获取电角度位置
    //gRotorTrans.Poles = (gRotorTrans.Poles > 0) ? gRotorTrans.Poles : 1;
    gRotorTrans.RTPos = (Ulong)gRotorTrans.RtorBuffer * gRotorTrans.PolesRatio >>8; // Q8
    
#ifdef TMS320F2808
    // 准备下次读取
    SpicRegs.SPITXBUF = 0xFFFF;
#endif
}
/*************************************************************
	旋转变压器获取位置前的数据采样处理，同时记录准确的时间间隔
	每0.45ms检测一次速度，下溢中断处理
*************************************************************/
void RotorTransSamplePos(void)
{
	Ulong	mTime;
	Ulong 	mDetaTime;	

	mTime = GetTime();
	mDetaTime = labs((long)(gRotorTrans.TimeBak - mTime));
	gRotorTrans.Flag = 0;
	if(mDetaTime >= C_TIME_045MS)
	{
		gRotorTrans.Flag = 1;					//表示应该开始计算一次速度
		gRotorTrans.DetaTime = mDetaTime;
		gRotorTrans.TimeBak = mTime;
	}
}

/************************************************************
    计算旋变的速度反馈
经测试，两种方法结果一样，都会在速度中引入50Hz的周期纹波

************************************************************/
void RotorTransCalVel(void)
{      
	Ulong  mUlong;
	Ulong  m_05ms;
	Uint   m_DetaPos;
	int	   m_Speed;
    long   tempL;

#if 0       // IS300方式
    DINT;
    gRotorTrans.DetaTime = gRotorTrans.TimeBak - GetTime();
    gRotorTrans.TimeBak = GetTime();
    gRotorTrans.DetaPos = (int)((Uint)gRotorTrans.RTPos - (Uint)gRotorTrans.PosBak);
	gRotorTrans.PosBak  = gRotorTrans.RTPos;
    EINT;

    m_DetaPos = abs(gRotorTrans.DetaPos);
    tempL = gRotorTrans.DetaTime >> 1;
    mUlong = (50000L * (Ulong)m_DetaPos + tempL) / gRotorTrans.DetaTime;
    tempL = gBasePar.FullFreq01 >> 1;
    tempL = (mUlong * (1000L * DSP_CLOCK) + tempL) / gBasePar.FullFreq01;	
    m_Speed = __IQsat(tempL, 32767, -32767);

    if(gRotorTrans.DetaPos < 0)
	{
		m_Speed = -m_Speed;
	}
#else       // 380方式 记录中断个数的方式
    DINT;
    gRotorTrans.IntNum = gRotorTrans.IntCnt;
    gRotorTrans.IntCnt = 0;    
    gRotorTrans.DetaPos = (int)((Uint)gRotorTrans.RTPos - (Uint)gRotorTrans.PosBak);
	gRotorTrans.PosBak  = gRotorTrans.RTPos;
    EINT;

    if(gRotorTrans.IntNum == 0)         // 载频比05ms循环还低(2KHz)
    {
        return;
    }

    m_DetaPos = abs(gRotorTrans.DetaPos);
    m_05ms = gRotorTrans.IntNum << 2; // *4
    tempL = (Ulong)m_DetaPos * 100000L / (Ulong)gPWM.gPWMPrdApply;         // 1e5 -> 2500L
    tempL = tempL * DSP_CLOCK / m_05ms;
    m_Speed = tempL * 1000L / gBasePar.FullFreq01;

    if(gRotorTrans.DetaPos < 0)
	{
		m_Speed = -m_Speed;
	}
#endif

    //剔除毛刺滤波处理
    gSpeedFilter.Input = m_Speed;
    BurrFilter((BURR_FILTER_STRUCT * )&gSpeedFilter);

	// 对速度滤波及消除静差
    if(gVCPar.VCSpeedFilter <= 1)   //旋变测速默认情况下是需要加滤波
	{
        tempL =  (long)gRotorTrans.FreqFeed * 29L + 2L * m_Speed;
        gRotorTrans.FreqFeed = tempL / 31L;	
	}
	else                        //F2-07的滤波处理, 默认20ms
	{
        tempL =  (long)gRotorTrans.FreqFeed * (gVCPar.VCSpeedFilter-1L) + 2L * m_Speed;
        gRotorTrans.FreqFeed = tempL / (gVCPar.VCSpeedFilter + 1L);
	}

    gRotorTrans.RealTimeSpeed = gRotorTrans.FreqFeed;
    tempL = ((long)gRotorTrans.RealTimeSpeed * 500L /gBasePar.FcSetApply)>>3;/*旋变角度补偿固化500 2011.5.7L1082*/
	gRotorTrans.PosComp = tempL * gBasePar.FullFreq>>16;
	return;
}

