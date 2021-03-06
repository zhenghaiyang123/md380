/****************************************************************
文件功能：和电机控制相关的程序文件，电机控制模块的主体部分
文件版本： 
更新日期： 

****************************************************************/
#include "MotorInclude.h"
#include "MotorDataExchange.h"
#include "SystemDefine.h"
#include "MotorDefine.h"

// // 内部函数声明
void RunCaseDeal05Ms(void);
void RunCaseDeal2Ms(void);
void RunCaseRun05Ms(void);
void RunCaseRun2Ms(void);
void RunCaseStop(void);
void SendDataPrepare(void);		

/************************************************************
    性能模块初始化程序：主循环前初始化性能部分的变量
(所有等于0的变量无需初始化)
************************************************************/
void InitForMotorApp(void)
{
	DisableDrive();
	TurnOffFan();
    
// 公共变量初始化
	gMainStatus.RunStep = STATUS_LOW_POWER;	//主步骤
	gMainStatus.SubStep = 1;				//辅步骤
	gMainStatus.ParaCalTimes = 0;
	gError.LastErrorCode = gError.ErrorCode.all;
	//gMainStatus.StatusWord.all = 0;
	gCBCProtect.EnableFlag = 1;				//默认启动逐波限流功能
	gADC.ZeroCnt = 0;
	gADC.DelaySet = 100;
	gADC.DelayApply = 600;
	gFcCal.FcBak = 50;
	gBasePar.FcSetApply = 50;
	gUVCoff.UDivV = 4096;
    gPWM.gPWMPrd = C_INIT_PRD;

    gMainStatus.StatusWord.all = 0;
    
// 矢量相关变量初始化
    gRotorTrans.Flag = 0;   //同步机初始化, 旋变测速用到
    gFVCSpeed.MTCnt = 0;
    gFVCSpeed.MTLimitTime = 0;
    gFVCSpeed.MSpeedSmooth.LastSpeed = 0;
    gFVCSpeed.MSpeedSmooth.SpeedMaxErr = 1500;
    gFVCSpeed.MTSpeedSmooth.LastSpeed = 0;
    gFVCSpeed.MTSpeedSmooth.SpeedMaxErr = 1500;
    gFVCSpeed.TransRatio = 1000;                  // 测速传动比固定值
    gPGData.QEPIndex = QEP_SELECT_NONE; 
    gPGData.PGType = PG_TYPE_NULL;                  // 初始化为 null，
    gPWM.gZeroLengthPhase = ZERO_VECTOR_NONE; 

// 电机类型相关的初始化，默认是异步机电机
    gMotorInfo.MotorType = MOTOR_TYPE_IM;
    //gMotorInfo.LastMotorType = gMotorInfo.MotorType;
    gMotorInfo.LastMotorType = MOTOR_NONE;                // 保证进入主程序后能进行相关初始化
    
    //(*EQepRegs).QEINT.all = 0x0;  //取消QEP的I信号中断
    
    gPGData.PGMode = 0;
    gFVCSpeed.MDetaPosBak = 0;

    gIPMPos.ZErrCnt = 0	;
    gIPMPosCheck.UvwRevCnt = 0	;
    gIPMInitPos.Flag = 0;

    gIPMInitPos.InitPWMTs = (50 * DSP_CLOCK);	  //500us
    gPWM.PWMModle = MODLE_CPWM;

    gIPMPos.Zcounter  = 0;              // 记录进入z中断的次数，用于监视ABZ,UVW 编码器同步机辨识不成功的问题
                                        // 查看uf-25;
//
    ParSend2Ms();
    ParSend05Ms();
}

/************************************************************
主程序不等待循环：用于执行需要快速刷新的程序，要求程序非常简短
************************************************************/
void Main0msMotor(void)
{
    if(gPGData.PGMode == 0)
    {
    GetMDetaPos();
    GetMTTimeNum();
    }
}

/************************************************************
任务:
1. 编码器测速、SVC速度计算；
2. 速度闭环控制；
3. 向下计算gMainCmd.FreqSetApply， 向上传递gMainCmd.FreqToFunc；

4. SVC 不进行编码器测速， FVC和VF会计算编码器速度；

************************************************************/
void Main05msMotor(void)
{
  if(ASYNC_SVC == gCtrMotorType)  // SVC
    {
        //gFVCSpeed.SpeedEncoder = 0;
        VCGetFeedBackSpeed();               //编码器测速，确保间隔均匀        
    }
            // 2808时，SVC编码器也测速
    else
    {
        VCGetFeedBackSpeed();               //编码器测速，确保间隔均匀        
    }    

    #ifdef MOTOR_SYSTEM_DEBUG
    DebugSaveDeal(3);
    #endif 
}
/************************************************************
主程序的单2ms循环：用于执行电机控制程序
思路：数据输入->数据转换->控制算法->公共变量计算->自我保护->控制输出
执行时间：不被中断打断的情况下约120us
************************************************************/
void Main2msMotorA(void)
{

//从控制板获取参数	
	ParGet2Ms();
    ParGet05Ms();

//异步机参数辨识时，对参数设置有特殊要求，需要优化
    if(STATUS_GET_PAR == gMainStatus.RunStep)
    {
        ChgParForEst();
    }
    
//ParameterChange2Ms();
    if(gMainCmd.Command.bit.Start == 0)
    {
        SystemParChg2Ms();
        SystemParChg05Ms();                 // 运行时不转换的参数

        ChangeMotorPar();       //电机参数转换， 运行不转换
    }
    RunStateParChg2Ms();
    //RunStateParChg05Ms();

}

void Main2msMotorB(void)
{  
  int     m05HzPu;
  int     m20HzPu;
    m20HzPu = (200L<<15) / gBasePar.FullFreq01;

	if(gIPMZero.zFilterCnt)				    // pm Z filter	
	{
		gIPMZero.zFilterCnt--;  
	}
    gMainCmd.FreqSetApply = (long)gMainCmd.FreqSet;
//    gMainCmd.FreqSetApply = Filter2((long)gMainCmd.FreqSet,gMainCmd.FreqSetApply);
    switch(gCtrMotorType)
    {
        case ASYNC_SVC:
            if(gMainStatus.RunStep != STATUS_SPEED_CHECK)          // SVC speed-check
            {   
             	if(0 == gVCPar.SvcMode)  //使用原380算法，以便于和320兼容
                {   
                    m05HzPu = (50L<<15) / gBasePar.FullFreq01;
                    if((abs(gMainCmd.FreqSet)+3) < m05HzPu) 
                    {
                        gMainCmd.FreqSet = 0;
                        gMainCmd.FreqSetApply = 0;
                    }
                    SVCCalRotorSpeed();
                    VcAsrControl();
                    CalWsAndSynFreq();   // 计算转差频率
                }
    			else
    			{
                    if((1 == gMainCmd.Command.bit.TorqueCtl)||(1 == gTestDataReceive.TestData1))
					{}
					else
					{
                        m05HzPu = (50L<<15) / gBasePar.FullFreq01;
                        if((abs(gMainCmd.FreqSet)+3) < m05HzPu) 
                        {
                            gMainCmd.FreqSet = 0;
                            gMainCmd.FreqSetApply = 0;
                        }
					}
                    SVCCalRotorSpeed_380();
                    VcAsrControl();
                    CalWsAndSynFreq_380();   // 计算转差频率
    			
    			}
                gMainCmd.FreqToFunc = gMainCmd.FreqFeed;
                }
            else
            {
                gMainCmd.FreqSyn = gFeisu.SpeedCheck;
                gMainCmd.FreqToFunc = gFeisu.SpeedCheck;
                
               //IMTSet.M = (long)gMotorExtPer.IoVsFreq <<12;
                gIMTSet.T = 0L <<12;
                gOutVolt.VoltApply = (long)gOutVolt.VoltApply * gFeisu.VoltCheck >>12;
            }
            break;
            
        case ASYNC_FVC:
            VcAsrControl();     // FVC 速度环
            CalWsAndSynFreq();  // 计算转差频率
            gMainCmd.FreqToFunc = gMainCmd.FreqFeed;
            break;

        case SYNC_SVC:
        case SYNC_VF:
        case ASYNC_VF:
            if(gMainStatus.RunStep == STATUS_SPEED_CHECK)
            {                
                gOutVolt.Volt           = gFeisu.VoltCheck;
                gMainCmd.FreqSyn      = gFeisu.SpeedCheck;
                gMainCmd.FreqToFunc     = gMainCmd.FreqSyn;
                gVFPar.FreqApply        = gMainCmd.FreqSyn;
                gOutVolt.VoltPhaseApply = (gFeisu.SpeedLast > 0) ? 16384 : -16384;                
            }            
            break;

        case SYNC_FVC:
//            gMainCmd.FreqFeed = gFVCSpeed.SpeedEncoder;
            VcAsrControl();     // synFVC 速度环
            gMainCmd.FreqSyn = gMainCmd.FreqFeed;
            gMainCmd.FreqToFunc = gMainCmd.FreqFeed;
            break;           

        case DC_CONTROL:
            gMainCmd.FreqSyn = 0;
            RunCaseDcBrake();
            gOutVolt.VoltPhaseApply = 0;        // 考虑到同步机，输出电压对准转子磁极，直流电流就会在该方向
		                                        // 定子磁链就会在该方向上；
            gMainCmd.FreqToFunc = 0;		    
            break;

        case RUN_SYNC_TUNE:  // 目爸鳘是同步机参数辨识
            ;
            // 参数辨识后，电流环参数需要调用辨识得到的pi参数, 得到pi参数前的辨识过程不会使用电流环
            gImAcrQ24.KP = (long)gPmParEst.IdKp * gBasePar.FcSetApply / 80;                
            gItAcrQ24.KP = (long)gPmParEst.IqKp * gBasePar.FcSetApply / 80;
            gImAcrQ24.KI = gPmParEst.IdKi;
            gItAcrQ24.KI = gPmParEst.IqKi;
            
           
            
            if((TUNE_STEP_NOW == PM_EST_NO_LOAD) ||                 // pm 空载辨识编码器角度
                (TUNE_STEP_NOW == PM_EST_BEMF))                      // pm 反电动势辨识，
            {           
                gMainCmd.FreqSyn = 0;
                gIMTSet.T = 0;

                if(TUNE_STEP_NOW == PM_EST_BEMF)
                {
                    gMainCmd.FreqSyn = gEstBemf.TuneFreqSet;
                    gEstBemf.IdSetFilt = Filter4(gEstBemf.IdSet, gEstBemf.IdSetFilt);
                    gIMTSet.M = (long)gEstBemf.IdSetFilt << 12;       // Q12->Q24
                }
                gMainCmd.FreqToFunc = gMainCmd.FreqSyn;
            }
            
            if(TUNE_STEP_NOW == PM_EST_WITH_LOAD)       // pm 带载辨识
            {
                PrepareAsrPar(); 
                CalTorqueLimitPar();
                CalUdcLimitIT();                   //矢量控制的过压抑制功能
                VcAsrControl();                         // synFVC 速度环
                
                gIMTSet.M = 0;
                gMainCmd.FreqSyn = gMainCmd.FreqFeed;
                gMainCmd.FreqToFunc = gMainCmd.FreqFeed;
            }
                
            break;

        default:
            gMainCmd.FreqSyn = 0;
            gMainCmd.FreqToFunc = 0;
            break;
    }

    //计算载波频率
	CalCarrierWaveFreq();

    // 设置控制模式和电机类型的组合，用于控制逻辑的区分
	if(MOTOR_TYPE_PM != gMotorInfo.MotorType)  
    {   
        if(gMainStatus.RunStep != STATUS_GET_PAR)
        {
            gCtrMotorType = (CONTROL_MOTOR_TYPE_ENUM)gMainCmd.Command.bit.ControlMode;
        }
        else        // im tune
        {
            gCtrMotorType = ASYNC_VF;
        }        
    }
    else if(MOTOR_TYPE_PM ==gMotorInfo.MotorType)
    {
        gCtrMotorType = (CONTROL_MOTOR_TYPE_ENUM)(gMainCmd.Command.bit.ControlMode + 10);
    }
    // 直流制动
    if((1 == gMainCmd.Command.bit.StartDC) || (1 == gMainCmd.Command.bit.StopDC))
    {
        gCtrMotorType = DC_CONTROL;
    }

    //根据变频器状态分别处理, 可能会重新更新 gCtrMotorType(但必须在该2ms函数中，不然会导致错误)
	switch(gMainStatus.RunStep)
	{
		case STATUS_RUN:		                    //运行状态，区分VF/FVC/SVC运行
			RunCaseRun2Ms();
			break;

        case STATUS_STOP:
            RunCaseStop();
            break;

        case STATUS_IPM_INIT_POS:                   //同步机初始位置角检测阶段
			RunCaseIpmInitPos();            
            break;
            
		case STATUS_SPEED_CHECK:                    //转速跟踪状态
		
			if(gComPar.SpdSearchMethod == 3)    RunCaseSpeedCheck();
            else                                RunCaseSpeedCheck();
			break;

		case STATUS_GET_PAR:	                    //参数辨识状态，移到0.5ms时要同时修改参数传递
			RunCaseGetPar();

            if(TUNE_STEP_NOW == IDENTIFY_LM_IO)
            { 
                 
        		VfOverCurDeal();
        		VfOverUdcDeal();
        		VfFreqDeal();                        // gVFPar.FreqApply

                gMainCmd.FreqToFunc = gVFPar.FreqApply;
                gMainCmd.FreqSetApply = gVFPar.FreqApply;

                gWsComp.CompFreq = 0;       // 转差补偿为0， 转矩提升也为0
                VFSpeedControl();
                CalTorqueUp(); 
                HVfOscDampDeal();             // HVf 振荡抑制， 产生电压相位，取消MD320震荡抑制方式，2011.5.7 L1082
                gOutVolt.VoltPhaseApply = gHVfOscDamp.VoltPhase;            
                gOutVolt.Volt = gHVfOscDamp.VoltAmp;   
                VFOverMagneticControl();     
            }
			break;

		case STATUS_LOW_POWER:	                    //上电缓冲状态/欠压状态
			RunCaseLowPower();
			break;
            
		case STATUS_SHORT_GND:	                    //上电对地短路判断状态
			RunCaseShortGnd();
			break;
                       
		default:
            gMainStatus.RunStep = STATUS_STOP;      // 上电第一排会出现
			break;
	}	
}

void Main2msMotorC(void)
{
    InvCalcPower();     // 功率、转矩的计算
    VfOscIndexCalc();
    
//变频器自身检测和保护	
	InvDeviceControl();			
}

void Main2msMotorD(void)
{
//电流零漂检测，AD零漂和线性度检测	
	GetCurExcursion();				    

//准备需要传送给控制板的数据
    SendDataPrepare(); 
    
//把实时数据传送给控制板	
	ParSend2Ms();
    ParSend05Ms();

    gCpuTime.CpuBusyCoff = (Ulong)gCpuTime.Det05msClk * 655 >> 16;  // div100
    gCpuTime.CpuCoff0Ms = gCpuTime.tmp0Ms;
    gCpuTime.tmp0Ms = 0;
// End

    #ifdef MOTOR_SYSTEM_DEBUG
    DebugSaveDeal(2);
    #endif 
}

/*************************************************************
	为功能模块准备需要的所有参数
*************************************************************/
void SendDataPrepare(void)		
{
    Uint tempU;
    int   mAiCounter;
    Ulong mTotal1;
    Ulong mTotal2;
    Ulong mTotal3;
    Uint   mRatio;
    
	///////////////////////////////////////////////停机时候显示电流为0处理
	if((gMainStatus.RunStep == STATUS_LOW_POWER) ||
	   (gMainStatus.RunStep == STATUS_STOP) ||
	   (gLineCur.CurBaseInv < (4096/50))    ||          //检测电流小于变频器额定电流2%，显示0
	   (1 == gMainStatus.StatusWord.bit.OutOff ))	
	{
		gLineCur.CurPerShow = 0;
        gLineCur.CurTorque  = 0;
	}
	else
	{
		gLineCur.CurPerShow = gLineCur.CurPerFilter >> 7;
        gLineCur.CurTorque  = Filter32(abs(gIMTQ12.T), gLineCur.CurTorque);
	}
    
	//同步机角度转换
	tempU = (Uint)((int)gRotorTrans.RTPos + gRotorTrans.PosComp);
    gRotorTrans.RtRealPos = ((Ulong)tempU * 3600L + 10) >> 16;
	if(gMotorInfo.MotorType == MOTOR_TYPE_PM)
    {   
	    gIPMPos.RealPos = ((Ulong)gIPMPos.RotorPos * 3600L + 10) >> 16;
    }
    
    // ai 采样处理
    DINT;
    mTotal1 = gAI.ai1Total;
    mTotal2 = gAI.ai2Total;
    mTotal3 = gAI.ai3Total;
    mAiCounter = gAI.aiCounter;
    
    gAI.ai1Total = 0;
    gAI.ai2Total = 0;
    gAI.ai3Total = 0;
    gAI.aiCounter = 0;
    EINT;
    
    gAI.gAI1 = mTotal1 / mAiCounter;
    gAI.gAI2 = mTotal2 / mAiCounter;
    gAI.gAI3 = mTotal3 / mAiCounter;

    // 计算实际输出电压
    mRatio = __IQsat(gRatio, 4096, 0);                              // 没有过调制
    mRatio= (Ulong)mRatio * gUDC.uDC / gInvInfo.BaseUdc;            // 以变频器额定电压为基值
    gOutVolt.VoltDisplay = Filter4(mRatio, gOutVolt.VoltDisplay);
}

/************************************************************
    区分驱动方式，主要完成为05ms速度环控制准备好参数；
    
************************************************************/
void RunCaseRun2Ms(void)
{
    //EnableDrive();
    if(gMainCmd.Command.bit.Start == 0)         // 结束运行
    {
        gMainStatus.RunStep = STATUS_STOP;
        RunCaseStop();
        return;
    }

    gMainStatus.StatusWord.bit.StartStop = 1;        
    // 为转速追踪准备参数
    gFeisu.SpeedLast = (gMainCmd.FreqSyn) ? gMainCmd.FreqSyn : gFeisu.SpeedLast; 
    
    switch(gCtrMotorType)
    {
        case ASYNC_SVC:  //异步机矢量控制            
        case ASYNC_FVC:
            CalTorqueLimitPar();                // 计算转矩上限和转矩控制
            PrepareAsrPar();
            PrepImCsrPara();
            CalIMSet();							// 励磁电流给定      
            CalUdcLimitIT();                   //矢量控制的过压抑制功能
            break;
            
        case ASYNC_VF:  //异步机和同步机VF控制,暂辈磺�?
        case SYNC_VF:            
            VFWsTorqueBoostComm();				//转差补偿和转矩提升公共变量计算。
            VFWSCompControl();					//转差补偿处理(调整F)
            VFAutoTorqueBoost();        
            #if 1                           // 减速限制功能频率增加给定，庋麓砦�
            if(speed_DEC &&(abs(gMainCmd.FreqSet) > abs(gVFPar.tpLst)))
            {
              gMainCmd.FreqSet = gVFPar.tpLst;
            }
            gVFPar.tpLst = gMainCmd.FreqSet;
            #endif
    		VfOverCurDeal();				        //过流抑制处理(调整F)
    		VfOverUdcDeal();					    //过压抑制处理(调整F)
    		VfFreqDeal();                           // gVFPar.FreqApply

            gMainCmd.FreqToFunc = gVFPar.FreqApply;
            gMainCmd.FreqSetApply = gVFPar.FreqApply;

            VFSpeedControl();
            CalTorqueUp(); 
            
            HVfOscDampDeal();             // HVf 振荡抑制， 产生电压相位
            gOutVolt.VoltPhaseApply = gHVfOscDamp.VoltPhase;            
            gOutVolt.Volt = gHVfOscDamp.VoltAmp;
            
            VFOverMagneticControl();               
            break;
            
        case SYNC_SVC:
            break;
            
        case SYNC_FVC:
            CalTorqueLimitPar();
            CalUdcLimitIT();                   //矢量控制的过压抑制功能// 计算转矩上限和转矩控制
            PrepareAsrPar();
            //PrepareCsrPara();
            PrepPmsmCsrPrar();
            
            PmFluxWeakDeal();                   // pm 弱磁处理
            break;

        case DC_CONTROL:
            ;
            break;
                        
        default:            
            break;         
    }
}

/************************************************************
    启动电机运行前的数据初始化处理，为电机运行准备初始参数

************************************************************/
void PrepareParForRun(void)
{
// 公共变量初始化
    gMainStatus.StatusWord.bit.StartStop = 0;
    gMainStatus.StatusWord.bit.SpeedSearchOver = 0;

	gMainStatus.PrgStatus.all = 0;
	gMainStatus.PrgStatus.bit.ACRDisable = 1;    
    gGetParVarable.StatusWord = TUNE_INITIAL;
    gVarAvr.UDCFilter = gInvInfo.BaseUdc;
	gMainCmd.FreqSyn = 0;
	gMainCmd.FreqReal = 0;
	gOutVolt.Volt = 0;
	gOutVolt.VoltApply = 0;
	gRatio = 0;
	gCurSamp.U = 0;
	gCurSamp.V = 0;
	gCurSamp.W = 0;
	gCurSamp.UErr = 600L<<12;
	gCurSamp.VErr = 600L<<12;
    gIUVWQ24.U = 0;
    gIUVWQ24.V = 0;
    gIUVWQ24.W = 0;
	gIUVWQ12.U = 0;
	gIUVWQ12.V = 0;
	gIUVWQ12.W = 0;
	gLineCur.CurPerShow = 0;
    gLineCur.CurTorque  = 0;
	gLineCur.CurBaseInv = 0;
	gLineCur.CurPer = 0;
	gLineCur.CurPerFilter = 0;
	gIMTQ12.M = 0;
	gIMTQ12.T = 0;
    gIMTQ24.M = 0;
    gIMTQ24.T = 0;
	gDCBrake.Time = 0;
    gPWM.gZeroLengthPhase = ZERO_VECTOR_NONE;
    gIAmpTheta.ThetaFilter = gIAmpTheta.Theta;

// Vf 相关都初始化
    VfVarInitiate();
    
// 矢量相关变量初始化
    gSpeedFilter.Max = 32767;	//速度滤波		
    gSpeedFilter.Min = 3277;
    gSpeedFilter.Output = 0;
    //if((IDC_SVC_CTL == gMainCmd.Command.bit.ControlMode) ||
    //    (IDC_FVC_CTL == gMainCmd.Command.bit.ControlMode))
    if(gMainCmd.Command.bit.ControlMode != IDC_VF_CTL)
    {
  	    ResetParForVC();  //VF调用该函数有问题，因为它改变了输出电压和频率
    }
    
//同步机控制相关变量
	gFluxWeak.AdjustId = 0;

//转速跟踪方式2变量初始化
	gFeisuNew.gWs_out = 0;
	gFeisuNew.t_DetaTime = 0;
	gFeisuNew.stop_time = 0;
	gFeisuNew.inh_mag = 0;
	gFeisuNew.ang_amu =0;
	gFeisuNew.jicicg  =0;
	gFeisuNew.jicics=0;
}

/************************************************************
	切换到停机状态(公用子函数)
************************************************************/
void TurnToStopStatus(void)
{
	DisableDrive();
	gMainStatus.RunStep = STATUS_STOP;
	gMainStatus.SubStep = 1;
}

/*******************************************************************
    停机状态的处理
********************************************************************/
void RunCaseStop(void)
{
//停机封锁输出
	DisableDrive();	    
	PrepareParForRun();
    gMainCmd.FreqToFunc = 0; 

//等待零漂检测完成
	if(gMainStatus.StatusWord.bit.RunEnable != 1)
    {
        return;
    }

//判断是否需要对地短路检测
	if((1 == gExtendCmd.bit.ShortGnd) && (gMainStatus.StatusWord.bit.ShortGndOver == 0))
	{
		gMainStatus.RunStep = STATUS_SHORT_GND;
		gMainStatus.SubStep = 1;        // 重新进行对地短路检测
		return;
	}
	else
	{
		gMainStatus.StatusWord.bit.ShortGndOver = 1;
	}

// 同步机停机时位置校验
    if(gMotorInfo.MotorType == MOTOR_TYPE_PM)
	{
		IPMCheckInitPos();                  
	}

//判断是否需要起动电机
	if(gMainCmd.Command.bit.Start == 1)	
	{
    #ifdef MOTOR_SYSTEM_DEBUG
        if(gTestDataReceive.TestData16)         // Cf-15
        {
            ResetDebugBuffer();
        }
    #endif
        // 
        PmChkInitPosRest();                     // 同步机停机位置校验复位

	    // 参数辨识
	    if(TUNE_NULL != gGetParVarable.TuneType)
	    {
		    gMainStatus.RunStep = STATUS_GET_PAR;
            PrepareParForTune();
		    return;			
	    }
        //转速跟踪起动
		if(0 != gExtendCmd.bit.SpeedSearch)	
		{
			gMainStatus.RunStep = STATUS_SPEED_CHECK;
			gMainStatus.SubStep = 1;
			PrepareForSpeedCheck();
			EnableDrive();
            return;
		}
        //同步机识别磁极初始位置角阶段
    	if((gIPMInitPos.Flag == 0) &&
		    (gMotorInfo.MotorType == MOTOR_TYPE_PM) && 
		    (gPGData.PGType == PG_TYPE_ABZ))
		{
			gMainStatus.RunStep = STATUS_IPM_INIT_POS;
			gMainStatus.SubStep = 1;
            gIPMInitPos.Step = 0;

            return;
		}
        
        // else ...STATUS_RUN
		gMainStatus.RunStep = STATUS_RUN;
		gMainStatus.PrgStatus.all = 0;            
		gMainStatus.SubStep = 1;      
        
		EnableDrive();

        RunCaseRun2Ms();        // 优化启动时间，在该拍就能发波
	}
}

/*************************************************************
	计算调制系数：从输出电压计算调制系数
*************************************************************/
void CalRatioFromVot(void)
{
	Uint	m_Ratio;
    
    // 计算调制系数
    if( 1 == gMainStatus.StatusWord.bit.OutOff )   //输出掉载
    {
	    gOutVolt.VoltApply = 287;                       
    } 
	
	m_Ratio = ((Ulong)gOutVolt.VoltApply * (Ulong)gMotorInfo.Votage)/gInvInfo.InvVolt;	
    //gOutVolt.VoltDisplay = (m_Ratio > 4096) ? 4096 : m_Ratio;
	m_Ratio = ((Ulong)m_Ratio<<2) * gInvInfo.BaseUdc / gUDC.uDC >> 2;  // 电压低时最多小1
	gRatio = (m_Ratio > 8192) ? 8192 : m_Ratio;
}

/*************************************************************
    周期中断：完成模拟量采样、电流计算、VC电流环控制等操作

注意:该函数在参数辨识中也有使用，对它的修改，需要检查是否影响空载辨识
*************************************************************/
void ADCOverInterrupt()
{
//    Uint tempU;
    
    if(gPGData.PGType == PG_TYPE_RESOLVER)          // 旋变芯片开始采样
	{	
		//GpioDataRegs.GPBCLEAR.bit.GPIO34 = 1;       //GPIO34上升沿启动旋变位置接收
		RT_SAMPLE_START;
        //gRotorTrans.IntCnt ++;          // 记录采样次数
	}

    if(GetOverUdcFlag())                    //过压处理
    {
       HardWareOverUDCDeal();				
    }

// 获取模拟量
	//ADCProcess();							//ADC矫正处理
	GetUDCInfo();							//获取母线电压采样数据    
	GetCurrentInfo();						//获取采样电流, 以及温度、母线电压的采样
	
	ChangeCurrent();						//计算各种场合下的电流量
	OutputLoseAdd();						//累加用于输出缺相判断的电流

// 电机运行处理，电流环控制，完成输出电压，计算调制系数
    switch(gCtrMotorType)
    {
        case ASYNC_SVC:                                     //异步机开环矢量控制
            CalcABVolt();
            SVCCalFlux_380();        

	    	if(0 == gVCPar.SvcMode)  //使用原380算法，以便于和320兼容
            {
                SvcCalOutVolt();     // 计算电压设定
			}
			else
			{
       	        AlphBetaToDQ((ALPHABETA_STRUCT*)&gIAlphBeta,gFluxR.Theta, &gIMTQ24_obs);
                //gIMTQ24_obs.T = ((llong)gIMTQ24_obs.T>>12) * ((llong)gIMTQ24_obs.M>>12) / (llong)gMotorExtPer.I0;
                //gIMTQ24_obs.T(Q12) = (llong)gIMTQ24_obs.T(Q24) * (llong)gMotorExtPer.I0(Q12) / (llong)gIMTQ24_obs.M(Q24);
                gIMTQ12_obs.M = (gIMTQ24_obs.M>>12);
                gIMTQ12_obs.T = (gIMTQ24_obs.T>>12);

               SvcCalOutVolt_380();     // 计算电压设定
			   //VCCsrControl_380();							        //闭环矢量IT和IM调节

			}
            break;
            
        case ASYNC_FVC:                                     //异步机闭环矢量控制
            CalcABVolt();
            SVCCalFlux_380();
		    VCCsrControl();							        //闭环矢量IT和IM调节
            break;
            
        case ASYNC_VF:                                      //异步机和同步机VF控制,暂时不区分?
        case SYNC_VF:

            break;
            
        case SYNC_SVC:
            ;
            break;
            
        case SYNC_FVC:
            PmDecoupleDeal();
            VCCsrControl();
            break;
            
        case RUN_SYNC_TUNE:
            ;                                                   // 借用同步机电流环
            if(TUNE_STEP_NOW == PM_EST_NO_LOAD ||
                TUNE_STEP_NOW == PM_EST_BEMF ||
                TUNE_STEP_NOW == PM_EST_WITH_LOAD)
            {                
                PmDecoupleDeal();
                VCCsrControl();
            }
            break;
            
        default:            
            break;         
    }  	
   if(DEADBAND_COMP_280 == gExtendCmd.bit.DeadCompMode)  
    {
         CalDeadBandComp();
    }
   else if(DEADBAND_COMP_380== gExtendCmd.bit.DeadCompMode)
    {
         HVfDeadBandComp();
    }
   else  
    {
     gDeadBand.CompU = 0;
     gDeadBand.CompV = 0;
     gDeadBand.CompW = 0;  /*死区补偿模式为0时。不进行死区补偿 2011.5.7 L1082*/       
    }
     
// 同步机参数辨识，或者ABZ编码器第一次上电初始位置检测
    if((STATUS_GET_PAR ==gMainStatus.RunStep || gMainStatus.RunStep ==STATUS_IPM_INIT_POS)
        && (MOTOR_TYPE_PM == gMotorInfo.MotorType) 
        && (gIPMInitPos.Step != 0))
	{
		SynInitPosDetect();
	}
// 同步机带载辨识
    else if((STATUS_GET_PAR == gMainStatus.RunStep) && 
             (TUNE_STEP_NOW == PM_EST_WITH_LOAD) &&
             (gUVWPG.UvwEstStep == 2))
    {
        GetUvwPhase();
        gUVWPG.TotalErr += (long)(gIPMPos.RotorPos - gUVWPG.UVWAngle);
        gUVWPG.UvwCnt ++;
    }
//异步机参数辨识计算空载电流程序
	else if((STATUS_GET_PAR == gMainStatus.RunStep) &&
             (MOTOR_TYPE_PM != gMotorInfo.MotorType))
	{
		LmIoInPeriodInt();					
	}
#ifndef MOTOR_SYSTEM_DEBUG

#endif
    
    if(gPGData.PGMode == 0)
    {
        GetMDetaPos();
       GetMTTimeNum();
    }
    else if(gPGData.PGType == PG_TYPE_RESOLVER)  // 旋变信号采样完成，等待下益中断读取
	{
		//GpioDataRegs.GPBSET.bit.GPIO34 = 1;
		RT_SAMPLE_END;
        
        #ifdef TMS320F28035
        ROTOR_TRANS_RD = 1;
        ROTOR_TRANS_SCLK   = 1;
        #endif
    }

#ifdef MOTOR_SYSTEM_DEBUG       //rt debug
    #ifdef TMS320F2808
    DebugSaveDeal(0);
    #endif
#endif 
}

/*************************************************************
下溢中断：用于发送PWM。

注意:该函数在参数辨识中也有使用，对它的修改，需要检查是否影响空载辨识
*************************************************************/
void PWMZeroInterrupt()
{    
    CalRatioFromVot();// 计算调制系数   
    SoftPWMProcess();//随机PWM处理，获取载波周期和角度步长						
   	CalOutputPhase();//计算输出相位						

    BrakeResControl();      //制动电阻控制

// 根据控制方式 PWM 发波, 参数辨识时可能不需要这里发波
    if(gMainStatus.PrgStatus.bit.PWMDisable)
    {
        asm(" NOP");
    }
	else
	{	
	    OutPutPWMVF();      // SVPWM
	    //OutPutPWMVC();      // SPWM
	}
    
    if(gPGData.PGMode == 0)
    {
        GetMDetaPos();
        GetMTTimeNum();
    }
    else if(gPGData.PGType == PG_TYPE_RESOLVER)
    {
        GetRotorTransPos();
     //    RotorTransSamplePos();
        gRotorTrans.IntCnt ++;          // 记录采样次数
    }
    
#ifdef MOTOR_SYSTEM_DEBUG       //rt debug
    #ifdef TMS320F2808
    DebugSaveDeal(1);
    #endif
#endif 
}

