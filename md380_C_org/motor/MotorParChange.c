/****************************************************************
文件功能：处理从功能部分获得的运行参数
文件版本： 
更新日期： 

****************************************************************/
#include "MotorInclude.h"

// // 停机参数转换
void SystemParChg2Ms()	
{
    long	m_Long;
	Ulong	m_ULong;
    Uint    minType, maxType;
    Uint    *pInvVolt, *pInvCur;
    Uint 	m_UData;
    //Uint	m_UData1,m_UData2,m_UData3;
    
// 获取变频器系统参数
    //gInvInfo.GPType = gInvInfo.GpTypeSet;
	if(100 > gInvInfo.InvTypeSet)           // 380V
	{
        pInvVolt  = (Uint *)gInvVoltageInfo380T;
        pInvCur = (Uint *)gInvCurrentTable380T;
        gInvInfo.InvType  = gInvInfo.InvTypeSet;
        gInvInfo.InvVoltageType = INV_VOLTAGE_380V;
        gInvInfo.GPType = gInvInfo.GpTypeSet;
	}
	else if(200 > gInvInfo.InvTypeSet)      // 220T, 没有P型机
	{
	    pInvVolt  = (Uint *)gInvVoltageInfo220T;
        //pInvCur = (Uint *)gInvCurrentTable380T;            //使用380V变频器的额定电流，也就是被改制的机型
        pInvCur   = (Uint *)gInvCurrentTable220T;
        gInvInfo.InvType  = gInvInfo.InvTypeSet - 100;
        
        gInvInfo.InvVoltageType = INV_VOLTAGE_220V;
        gInvInfo.GPType   = 1;                                      //不允许P型机
	}
    else if(300 > gInvInfo.InvTypeSet) //220S
    {
        pInvVolt  = (Uint *)gInvVoltageInfo220S;
        pInvCur = (Uint *)gInvCurrentTable220S;
        gInvInfo.InvType  = gInvInfo.InvTypeSet - 200;
        gInvInfo.InvVoltageType = INV_VOLTAGE_220V;  
        gInvInfo.GPType = gInvInfo.GpTypeSet;
    }
    else if(400 > gInvInfo.InvTypeSet) //480V
    {
        pInvVolt  = (Uint *)gInvVoltageInfo480T;
        pInvCur = (Uint *)gInvCurrentTable380T;  // 与380V共用电流表
        gInvInfo.InvType  = gInvInfo.InvTypeSet - 300;
        gInvInfo.InvVoltageType = INV_VOLTAGE_480V;
        gInvInfo.GPType = gInvInfo.GpTypeSet;
    }
    else if(500 > gInvInfo.InvTypeSet) //690V                               //690V
    {
        pInvVolt  = (Uint *)gInvVoltageInfo690T;
        pInvCur = (Uint *)gInvCurrentTable690T;
        gInvInfo.InvType  = gInvInfo.InvTypeSet - 400;
        gInvInfo.InvVoltageType = INV_VOLTAGE_690V;
        gInvInfo.GPType = gInvInfo.GpTypeSet;
    }
    else
    {
        pInvVolt  = (Uint *)gInvVoltageInfo1140T;
        pInvCur = (Uint *)gInvCurrentTable1140T;
        gInvInfo.InvType  = gInvInfo.InvTypeSet - 500;
        gInvInfo.InvVoltageType = INV_VOLTAGE_1140V;
        gInvInfo.GPType = gInvInfo.GpTypeSet;
    }    
    
	gInvInfo.InvVolt     = *(pInvVolt + 0);
	gInvInfo.InvLowUdcStad = *(pInvVolt + 1);
	gInvInfo.InvUpUDC    = *(pInvVolt + 2);
	gInvInfo.BaseUdc     = *(pInvVolt + 3);
    minType              = *(pInvVolt + 4);  //变频器允许的机型下限
    maxType              = *(pInvVolt + 5);  //变频器允许的机型上限
    gInLose.ForeInvType  = *(pInvVolt + 6);  //开始缺相保护的起始机型
    gUDC.uDCADCoff       = *(pInvVolt + 7); 
    gVFPar.ovPointCoff   = *(pInvVolt + 8);     // 用于计算过压抑制点

	gInvInfo.InvType = (gInvInfo.InvType > maxType) ? maxType : gInvInfo.InvType;
	if(gInvInfo.InvType <= minType)
    {
        gInvInfo.GPType = 1;	                        //最小机型无法区分GP
        gInvInfo.InvType = minType;
	}

    if(gInvInfo.GPType == 1)    gInvInfo.InvTypeApply = gInvInfo.InvType;
    else                        gInvInfo.InvTypeApply = gInvInfo.InvType - 1;   //P型机采样电流低一挡
    
	gInvInfo.InvCurrent  = *(pInvCur + gInvInfo.InvTypeApply - minType);        //电流采样使用的额定电流
    gInvInfo.InvCurrentOvload = *(pInvCur + gInvInfo.InvType - minType);
    if(gInvInfo.InvVoltageType == INV_VOLTAGE_220V)                             // 电流小数点的确定
    {   
        gMotorExtInfo.UnitCoff = (gInvInfo.InvType > 19) ? 10 : 1;
    }
    else
    {
        gMotorExtInfo.UnitCoff = (gInvInfo.InvType > 21) ? 10 : 1; 
    }
    
	if((1 != gInvInfo.GPType) && (22 == gInvInfo.InvType))
	{
        //gInvInfo.InvCurrent /= 10;      // 机型为22的P型机，电流应该是1位小数点，但是读取的采样电流是2位   
        gInvInfo.InvCurrent = (Ulong)gInvInfo.InvCurrent * 3264 >> 15;
    }
    gInvInfo.InvCurrForP = *(pInvCur + gInvInfo.InvType - minType);      //P型机使用的额定电流

//死区和死区补偿参数
	m_UData = (gInvInfo.InvTypeApply > 24) ? 24 : gInvInfo.InvTypeApply;
	m_UData = (m_UData < 12) ? 12 : m_UData;
    if(500 <= gInvInfo.InvTypeSet)                //1140V死区和死区补偿量确定 2011.5.8 L1082
    {
        gDeadBand.DeadBand = DBTIME_1140V*gDeadBand.DeadTimeSet/10;                      //死区固定7.0us，补偿量2.5us，功能码可调
        gDeadBand.Comp     = DCTIME_1140V*gDeadBand.DeadTimeSet/10;
    }
    else if(400 <= gInvInfo.InvTypeSet)                       //690V死区补偿和补偿量固定
    {
	    gDeadBand.DeadBand = gDeadBandTable[13];         //死区固定4.8us，补偿量2.5us
	    gDeadBand.Comp     = gDeadCompTable[13];
    }
    else
    {
	    gDeadBand.DeadBand = gDeadBandTable[m_UData - 12];
	    gDeadBand.Comp     = gDeadCompTable[m_UData - 12];
    }
	EALLOW;									//设置死区时间
	EPwm1Regs.DBFED = gDeadBand.DeadBand;
	EPwm1Regs.DBRED = gDeadBand.DeadBand;
	EPwm2Regs.DBFED = gDeadBand.DeadBand;
	EPwm2Regs.DBRED = gDeadBand.DeadBand;
	EPwm3Regs.DBFED = gDeadBand.DeadBand;
	EPwm3Regs.DBRED = gDeadBand.DeadBand;
	EDIS;
        
//启动ADC采样延迟时间
    #ifdef		DSP_CLOCK100
	gADC.DelayApply = gADC.DelaySet * 10;       // default: 0.5us
    #else
	gADC.DelayApply = gADC.DelaySet * 6;
    #endif

//计算电流系数和电压系数
	//电机电流太小的处理
	m_UData = gInvInfo.InvCurrent>>2;
	DINT;
	gMotorInfo.Current = (gMotorInfo.CurrentGet < m_UData) ? m_UData : gMotorInfo.CurrentGet;
	m_ULong = (((Ulong)gMotorInfo.Current)<<8) / gMotorInfo.CurrentGet;
	gMotorInfo.CurBaseCoff = (m_ULong > 32767) ? 32767 : m_ULong;
	EINT;
    
	//AD值32767对应变频器额定电流值的2倍 转换为电机额定电流的标么值表示(Q24)
	//AD转换值切换为电机标么值表示值的方法为: (AD/32767 * 2sqrt(2) * Iv/Im) << 24 *8
	// (1/32767 * 2sqrt(2) << 24) * 8 == 11586
	// CPU28035时，32767对应的电流值为: 2sqrt(2) *3.3/3.0 * Iv
/*
#ifdef TMS320F2808
    m_Long = ((long)gInvInfo.InvCurrent * 11586L)/gMotorInfo.Current;
#else
	m_Long = ((long)gInvInfo.InvCurrent * 12745L)/gMotorInfo.Current;
#endif
	gCurSamp.Coff = (m_Long * (long)gInvInfo.CurrentCoff) / 1000;

#ifdef TMS320F28035
    gUDC.uDCADCoff = (long)gUDC.uDCADCoff * 3300L / 3000;   // *1.1
#endif
	gUDC.Coff = ((Ulong)gUDC.uDCADCoff * (Ulong)gInvInfo.UDCCoff) / 1000;
*/

//计算不同分辨率的频率表示
    if( 0 == gExtendCmd.bit.FreqUint )      // unit: 1Hz
    {
        gMainCmd.si2puCoeff = 1;
        gMainCmd.pu2siCoeff = 100;
    }        
    else if(1 == gExtendCmd.bit.FreqUint)   // unit: 0.1Hz
    {
        gMainCmd.si2puCoeff = 10;
        gMainCmd.pu2siCoeff = 10;
    }
    else // 2 == frqUnit                        // unit: 0.01Hz
    {
        gMainCmd.si2puCoeff = 100;       // si 2 pu
        gMainCmd.pu2siCoeff = 1;         // pu 2 si
    }    
	gBasePar.FullFreq01 = (Ulong)gBasePar.MaxFreq * (Ulong)gMainCmd.pu2siCoeff + 2000;	//32767表示的频率值
	gBasePar.FullFreq =   gBasePar.MaxFreq + 20 * gMainCmd.si2puCoeff;	//频率基值
	gMotorInfo.FreqPer =  ((Ulong)gMotorInfo.Frequency <<15) / gBasePar.FullFreq;

    gMotorInfo.Motor_HFreq = ((Ulong)gMotorInfo.Frequency * 410) >>10;
    gMotorInfo.Motor_LFreq = ((Ulong)gMotorInfo.Frequency * 205) >>10;

    // 编码器一些常量的计算，节省cpu时间开销
    //gUVWPG.UvwPolesRatio = ((Ulong)gMotorExtInfo.Poles << 8) / gUVWPG.UvwPoles;    // Q8 电机
    gRotorTrans.PolesRatio = ((Ulong)gMotorExtInfo.Poles << 8) / gRotorTrans.Poles; // Q8
}

// // 运行中参数转换
void RunStateParChg2Ms()	
{
    //int     temp;
    Uint    m_UData, tempU;
    Ulong   m_Long;    
    Uint    m_AbsFreq;
    //long    mCurM, mCurT;
     Ulong   tmpAmp;
	gIUVWQ12.U = (int)(gIUVWQ24.U>>12);				
	gIUVWQ12.V = (int)(gIUVWQ24.V>>12);
	gIUVWQ12.W = (int)(gIUVWQ24.W>>12);

    gIMTSetQ12.M = (int)(gIMTSetApply.M >> 12);
    gIMTSetQ12.T = (int)(gIMTSetApply.T >> 12);

	m_AbsFreq = abs(gMainCmd.FreqSyn);
    tempU = ((Ulong)gMotorExtPer.I0 * gMotorInfo.FreqPer) / m_AbsFreq;
    gMotorExtPer.IoVsFreq = (m_AbsFreq < gMotorInfo.FreqPer) ? gMotorExtPer.I0 : tempU;
      
    //计算线电流
    gIAmpTheta.CurTmpM = abs(gIMTQ12.M);
    gIAmpTheta.CurTmpT = abs(gIMTQ12.T);
    tmpAmp = (long)gIAmpTheta.CurTmpM * gIAmpTheta.CurTmpM;
    tmpAmp += (long)gIAmpTheta.CurTmpT * gIAmpTheta.CurTmpT;
    gIAmpTheta.Amp = (Uint)qsqrt((Ulong)tmpAmp);
    //...................................计算线电流
    if((gMainCmd.Command.bit.StartDC == 1) || 
       (gMainCmd.Command.bit.StopDC == 1))	/****直流制动状态表示电流放大处理****/
    {
        gIAmpTheta.Amp = ((Ulong)gIAmpTheta.Amp * 5792)>>12;
    }
	gLineCur.CurPer = Filter2(gIAmpTheta.Amp, gLineCur.CurPer);   
	gLineCur.CurPerFilter += gLineCur.CurPer - (gLineCur.CurPerFilter>>7);	

	// 计算变频器额定电流为基值的标么值电流
	m_Long = (Ulong)gLineCur.CurPer * gMotorInfo.Current;
	gLineCur.CurBaseInv = m_Long/gInvInfo.InvCurrentOvload;
    
    // 同步机 用于死区补偿
    gDeadBand.InvCurFilter = Filter2(gLineCur.CurBaseInv, gDeadBand.InvCurFilter);
	m_UData = abs(gMainCmd.FreqSyn);                                   //计算用实际值表镜脑诵衅德?
	gMainCmd.FreqReal = ((Ullong)m_UData * gBasePar.FullFreq01 + (1<<14))>>15;
    gMainCmd.FreqRealFilt += (gMainCmd.FreqReal>>4) - (gMainCmd.FreqRealFilt>>4);
    m_UData = abs(gMainCmd.FreqDesired);
    gMainCmd.FreqDesiredReal = ((Ullong)m_UData * gBasePar.FullFreq01 + (1<<14))>>15;
    m_UData = abs(gMainCmd.FreqSet);
    gMainCmd.FreqSetReal = ((Ullong)m_UData * gBasePar.FullFreq01 + (1<<14))>>15;
    gMainCmd.FreqSetBak = gMainCmd.FreqSet;
    
// 欠压点可根据功能码调整
	gInvInfo.InvLowUDC = (long)gInvInfo.InvLowUdcStad * gInvInfo.LowUdcCoff / 1000;     

// 调谐时两相增益处理
    if(gMainStatus.RunStep == STATUS_GET_PAR)
	{
		gUVCoff.UDivV = 4096;
	}
	else
	{
		//gUVCoff.UDivV = ((Ulong)gUVCoff.UDivVGet<<12)/1000;
        gUVCoff.UDivV = (Ulong)gUVCoff.UDivVGet * 4160L >> 10; // Q12
	}
    

// 根据母线电压计算最大输出电压
    //当电机额定电压很小时，有可能溢出，需要特别处理	
    if((abs(gUDC.uDC - gUDC.uDcCalMax) > 200)//母线电压变化20V，重新计算最大电压
       || (gMainStatus.RunStep == STATUS_STOP))
    {
        gUDC.uDcCalMax = gUDC.uDC;    
        m_UData = ((long)gUDC.uDC * 3251L)>>13;//正弦发波，最大电压为Udc/2.52
        gOutVolt.MaxOutVolt = ((long)m_UData * 710L) / gMotorInfo.Votage;//最大电压值是0.1V为单位，电机电压是1V，使用相电压为基值 
    }

// VF 过励磁处理
    gVarAvr.CoffApply = gVFPar.VFOverExc;
   
// 同步机相关参数特殊处理 
	if(MOTOR_TYPE_PM == gMotorInfo.MotorType)
	{
        // 转换功能设定的编码器零点位置角
        //gIPMPos.RotorZero = ((Ulong)gIPMPos.RotorZeroGet<<16)/3600;
        gIPMPos.RotorZero = (Ulong)gIPMPos.RotorZeroGet * 18641L >> 10;
        //gUVWPG.UvwZeroPhase = (Ulong)gUVWPG.UvwZeroPhase_deg * 18641L >> 10;
        gUVWPG.UvwZeroPos = gUVWPG.UvwZeroPos_deg * 18641L >> 10;

        gUVWPG.UvwZIntErr_deg = (long)gUVWPG.UvwZIntErr * 180L >>15;
        gIPMPosCheck.UvwStopErr_deg = (long)gIPMPosCheck.UvwStopErr *180L >>15;
        gIPMPos.AbzErrPos_deg = (long)gIPMPos.AbzErrPos * 180L >>15;
        #if 0
        tempU = gUVWPG.UVWAngle + gUVWPG.UvwZeroPos;
        gUVWPG.UvwRealErr_deg = (int)(gIPMPos.RotorPos - tempU) * 180L >>15;
        #endif
        
        // 手动修改零点位置角时需要响应, 旋变会自动计算
        if(gPGData.PGMode == 0 && gMainStatus.ParaCalTimes == 1 && // 已经上电
            gMainStatus.RunStep != STATUS_GET_PAR &&
            gIPMPos.ZeroPosLast != gIPMPos.RotorZero)
        {
            tempU = gIPMPos.RotorZero - gIPMPos.ZeroPosLast;
            
            SetIPMPos(gIPMPos.RotorPos + tempU);
            SetIPMPos_ABZRef(gIPMPos.RotorPos + tempU);            
        }   
        gIPMPos.ZeroPosLast = gIPMPos.RotorZero;
        gPmDecoup.EnableDcp = 0;          
	}

// 根据辨识得到的方向，修改QEP设置
// 参数辨识的时候这里不会影响
    if(gMainStatus.RunStep != STATUS_GET_PAR)
    {
        EQepRegs->QDECCTL.bit.SWAP = gPGData.SpeedDir;
    }

// 电流电压校正系数实时计算，方便调试
#ifdef TMS320F2808
    m_Long = ((long)gInvInfo.InvCurrent * 11586L)/gMotorInfo.Current;
#else
	m_Long = ((long)gInvInfo.InvCurrent * 12745L)/gMotorInfo.Current;
#endif
	//gCurSamp.Coff = (m_Long * (long)gInvInfo.CurrentCoff) / 1000;
	gCurSamp.Coff = (m_Long * (Ulong)gInvInfo.CurrentCoff) * 33 >>15;
#ifdef TMS320F2808
    //gUDC.Coff = (Ulong)gUDC.uDCADCoff * gInvInfo.UDCCoff / 1000;
    gUDC.Coff = (Ulong)gUDC.uDCADCoff * gInvInfo.UDCCoff * 33 >>15;
#else   // TMS320F28035
    gUDC.Coff = (Ulong)gUDC.uDCADCoff * gInvInfo.UDCCoff * 36 >>15;     // *1.1
#endif
    gOvUdc.Limit = gVFPar.ovPoint * gVFPar.ovPointCoff;
    if     (100 > gInvInfo.InvTypeSet)
        {
          gOvUdc.Limit = (gOvUdc.Limit>7500)?7500:gOvUdc.Limit;
        }
    else if(300 > gInvInfo.InvTypeSet)
        {
          gOvUdc.Limit = (gOvUdc.Limit>3700)?3700:gOvUdc.Limit;
        }
    else if(400 > gInvInfo.InvTypeSet)
        {
          gOvUdc.Limit = (gOvUdc.Limit>8300)?8300:gOvUdc.Limit;
        }
    else if(500 > gInvInfo.InvTypeSet)
        {
          gOvUdc.Limit = (gOvUdc.Limit>13000)?13000:gOvUdc.Limit;
        }
    else
        {
         gOvUdc.Limit = (gOvUdc.Limit>19000)?19000:gOvUdc.Limit;
        }
}

/***************************************************************
参数计算程序：处理0.5Ms循环中，功能传递的参数，完成参数转换、运行参数准备等工作
1. update gCtrMotorType;
2. measure speed ;
*************************************************************/
void SystemParChg05Ms()
{
    //Ulong m_Long;

// 电机类型更改
    if(gMotorInfo.LastMotorType != gMotorInfo.MotorType)
    {
        gMotorInfo.LastMotorType = gMotorInfo.MotorType;

        gPGData.PGType = PG_TYPE_NULL;     // 电机类型修改后，重新初始化编码器
    }
    
// PG卡类型更改
    if(gPGData.PGType != (PG_TYPE_ENUM_STRUCT)gPGData.PGTypeGetFromFun)
    {
        gPGData.PGType = (PG_TYPE_ENUM_STRUCT)gPGData.PGTypeGetFromFun;
        ReInitForPG();

        gIPMInitPos.Flag = 0;
    }
    
// QEP测速的处理 -- 选择QEP
	if(gPGData.QEPIndex != (QEP_INDEX_ENUM_STRUCT)gExtendCmd.bit.QepIndex)
	{
        gPGData.QEPIndex = (QEP_INDEX_ENUM_STRUCT)gExtendCmd.bit.QepIndex;
                
        if(gPGData.QEPIndex == QEP_SELECT_1) // 本地PG卡测速
        {
            EQepRegs = (struct EQEP_REGS *)&EQep1Regs;
            EALLOW;
            PieVectTable.EQEP1_INT = &PG_Zero_isr;
            PieCtrlRegs.PIEIER5.bit.INTx1 = 1;
            SysCtrlRegs.PCLKCR1.bit.EQEP1ENCLK = 1;
            EDIS;
        }
        #ifdef TMS320F2808                      // 28035 只有一个QEP
        if(gPGData.QEPIndex == QEP_SELECT_2)
        {
            EQepRegs = (struct EQEP_REGS *)&EQep2Regs;
            EALLOW;
            PieVectTable.EQEP2_INT = &PG_Zero_isr;
            PieCtrlRegs.PIEIER5.bit.INTx2 = 1;
            SysCtrlRegs.PCLKCR1.bit.EQEP2ENCLK = 1;
            EDIS;
        }
        #endif        
        InitSetQEP();        
    }    

    if(MOTOR_TYPE_PM != gMotorInfo.MotorType ||     // 异步机
        gPGData.PGMode != 0)                        // 绝对位置编码器 -- 旋变
    {
        EALLOW;
        (*EQepRegs).QEINT.all = 0x0;  //取消QEP的I信号中断
        EDIS;
    }
    else
    {
        EALLOW;
        (*EQepRegs).QEINT.all = 0x0400; //绝对位置编码器是否需要该中断?
        EDIS;
    }
}

/*************************************************************
	同步电机、异步电机参数变换
	
*************************************************************/
void ChangeMotorPar(void)
{
	Uint m_UData,m_BaseL;
	Ulong m_Ulong;
	//Uint m_AbsFreq;
    
	//电感基值为：阻抗�/2*pi*最大频率
	m_BaseL = ((Ulong)gMotorInfo.Votage * 3678)/gMotorInfo.Current;
	m_BaseL = ((Ulong)m_BaseL * 5000)/gBasePar.FullFreq01;
    
    if(MOTOR_TYPE_PM != gMotorInfo.MotorType)
    {
    	//阻抗基值为相电压/相电流，电阻标么值Q16格式
    	// sqrt(3)/1000/100 <<16 = 18597 >>14
    	m_UData = ((Ulong)gMotorExtInfo.R1 * (Ulong)gMotorInfo.Current)/gMotorInfo.Votage;	
    	gMotorExtPer.R1 = ((Ulong)m_UData * 18597)>>14;                 // 异步机定子电阻
        
    	m_UData = ((Ulong)gMotorExtInfo.R2 * (Ulong)gMotorInfo.Current)/gMotorInfo.Votage;	
    	gMotorExtPer.R2 = ((Ulong)m_UData * 18597)>>14;                 // 异步机定子电阻

    	gMotorExtInfo.L1 = 	gMotorExtInfo.LM + ((Ulong)gMotorExtInfo.L0 * 102 >> 10);
    	gMotorExtInfo.L2 = gMotorExtInfo.L1;

    	m_Ulong = (((Ulong)gMotorExtInfo.L1<<11) + m_BaseL)>>1;		
    	gMotorExtPer.L1 = m_Ulong/m_BaseL;
    	gMotorExtPer.L2 = gMotorExtPer.L1;							//定子、转子电感标么值Q9格式
    	m_Ulong = (((Ulong)gMotorExtInfo.LM<<11) + m_BaseL)>>1;
    	gMotorExtPer.LM = m_Ulong/m_BaseL;							//漏感标么值Q14格式
    	gMotorExtPer.L0 = (gMotorExtPer.L1 - gMotorExtPer.LM)<<5;

        gMotorExtPer.I0 = (((Ulong)gMotorExtInfo.I0)<<12)/gMotorInfo.Current;	//空载电流

        m_Ulong = 4096L * 4096L - (long)gMotorExtPer.I0 * gMotorExtPer.I0;
        gMotorExtPer.ItRated = qsqrt(m_Ulong);
        gPowerTrq.rpItRated = (1000L<<12) / gMotorExtPer.ItRated;
    }
    else    // PMSM
    {
        m_UData = ((Ulong)gMotorExtInfo.RsPm * (Ulong)gMotorInfo.Current)/gMotorInfo.Votage;	
        gMotorExtPer.Rpm = ((Ulong)m_UData * 18597)>>14;
        
        m_Ulong = (((Ulong)gMotorExtInfo.LD <<11) + m_BaseL) >>1;
        gMotorExtPer.LD = m_Ulong / m_BaseL / 10;                   // 同步机d轴电感Q9, 单位比异步机小个数量级
        m_Ulong = (((Ulong)gMotorExtInfo.LQ <<11) + m_BaseL) >>1;
        gMotorExtPer.LQ = m_Ulong / m_BaseL / 10;                   // 同步机q轴电感Q9，单位比异步机小个数量级

        // 计算同步机转子磁链
        m_Ulong = ((long)gMotorExtInfo.BemfVolt <<12) / (gMotorInfo.Votage *10);      // Q12
        gMotorExtPer.FluxRotor = (m_Ulong << 15) / gMotorInfo.FreqPer;                   // Q12\

        //gMotorExtPer.ItRated = 4096L;
        //gPowerTrq.rpItRated = (1000L<<12) / gMotorExtPer.ItRated;
        gPowerTrq.rpItRated = 1000;
    }

	//....计算电机极对数
	m_Ulong = (((Ullong)gMotorInfo.Frequency * (Ullong)gMainCmd.pu2siCoeff * 19661L)>>15);
	gMotorExtInfo.Poles = (m_Ulong + (gMotorExtInfo.Rpm>>1)) / gMotorExtInfo.Rpm;

    //0.01Hz为单位的额定转差率
    //m_Ulong = ((Ulong)gMotorExtInfo.Rpm * gMotorExtInfo.Poles * 100L)/60;
    m_Ulong = gMotorExtInfo.Rpm * gMotorExtInfo.Poles * 6830L >> 12;
    gMotorExtInfo.RatedComp = (Ulong)gMotorInfo.Frequency * gMainCmd.pu2siCoeff - m_Ulong;
                              
    //标么化的额定转差率       
    gMotorExtPer.RatedComp = ((long)gMotorExtInfo.RatedComp << 15)/gBasePar.FullFreq01; 
}

// // 变频器输出功率、转矩计算
void InvCalcPower(void)
{
    int  temp;
    //long InvPowerPU;
    long m_PowerN;
    
    gPowerTrq.anglePF = Filter16(abs(gIAmpTheta.PowerAngle), gPowerTrq.anglePF);
    gPowerTrq.anglePF = (gMainStatus.StatusWord.bit.StartStop) ? gPowerTrq.anglePF : 0;
    gPowerTrq.Cur_Ft4 = Filter16(gLineCur.CurPer, gPowerTrq.Cur_Ft4);
    
    gPowerTrq.InvPowerPU = (1732L * (long)gOutVolt.VoltApply /1000L) * gPowerTrq.Cur_Ft4 >> 12;
    gPowerTrq.InvPowerPU = (long)gPowerTrq.InvPowerPU * qsin(16384 - gPowerTrq.anglePF) >> 15;

	//m_PowerN = ((long)gMotorInfo.Current * gMotorInfo.Votage)/1000;
    m_PowerN = ((long)gMotorInfo.Current * gMotorInfo.Votage) >> 10;
    m_PowerN = (gInvInfo.InvTypeApply >= 22) ? m_PowerN : (m_PowerN*409L>>12);   // 0.01->0.1
	gPowerTrq.InvPower_si= ((long)gPowerTrq.InvPowerPU * m_PowerN) >> 12;    	
    // invType: < 22 : 0.01Kw;       current: 0.01A
    // invType: >=22 : 0.1Kw;        current: 0.1A
    gPowerTrq.InvPower_si = (gMainStatus.StatusWord.bit.StartStop) ? gPowerTrq.InvPower_si : 0;

    //temp = (gInvInfo.InvTypeApply < 22) ? (gMotorInfo.Power*10) : gMotorInfo.Power;
    //tempL = ((long)gInvPower_si << 10) / temp;
    //gTrqOut_pu = tempL * gMotorInfo.FreqPer / temp;
    //gTrqOut_pu = (gMainStatus.StatusWord.bit.StartStop) ? gTrqOut_pu : 0;       // 停机状态特殊处理
    //gTrqOut_pu = __IQsat(gTrqOut_pu, 3000, - 3000);      // abs(trq) < 300.0%
    
    gPowerTrq.TrqOut_pu = (long)gLineCur.CurTorque * gPowerTrq.rpItRated >> 13;
    gPowerTrq.TrqOut_pu = (gMainStatus.StatusWord.bit.StartStop) ? gPowerTrq.TrqOut_pu : 0;       // 停机状态特殊处理
}

