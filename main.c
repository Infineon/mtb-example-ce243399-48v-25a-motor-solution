/******************************************************************************
* File Name:   main.c
*
* Description: This code example demonstrates the implementation of PMSM sensorless
* field-oriented control (FOC) using the Infineon MCUs.
*
* Related Document: See README.md
*
*******************************************************************************
* (c) 2024-2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "General.h"
#include "HardwareIface.h"
#include "Params.h"
#include "cybsp.h"
#include "Controller.h"
#include "MotorCtrlHWConfig.h"
#include "ParamConfig.h"

//#include "cy_retarget_io.h"
#include "ENC_TLx_5012B.h"
#include "CtrlVars.h"
#include "BiIntFilt.h"

/*******************************************************************************
* Global variable
********************************************************************************/

/*******************************************************************************
* Function prototype
********************************************************************************/

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;
    
    result = cybsp_init();                 /* Initialize the device and board peripherals */
    CY_ASSERT(result == CY_RSLT_SUCCESS);  /* Board init failed. Stop program execution   */

    // Initialize controller
    HW_IFACE_ConnectFcnPointers();         /* must be called before STATE_MACHINE_Init()  */
    // If encoder IIF mode is used initialize the state machine immediately. In SPI mode this must be done after the encoder is initialized
    #if defined(ENC_IN_IIF_MODE)
    STATE_MACHINE_Init();
    #endif

    // Enable encoder at startup if in SPI mode
    Cy_GPIO_Write(ENC_ENC_EN_PORT, ENC_ENC_EN_PIN, 0);

    // Enable 250 microsecond timer used to keep track of time 
    Cy_TCPWM_PWM_Enable(TIMER_250US_HW, TIMER_250US_NUM);
    Cy_TCPWM_TriggerStart_Single(TIMER_250US_HW, TIMER_250US_NUM);

    #if defined(ENC_IN_SPI_MODE)
        //---------------------------------------------------------
        //--- Initialise the SPI for sensor interface based on which sensor is used
        //---------------------------------------------------------
        // Initialize Encoder
        uint32_t status;
        #if defined (ENC_TLx5012)
            status = Init_SPI_TLx_5012B();
            if (INIT_FAILURE == status) { CY_ASSERT(0); }
        
            status = Config_SPI_TLx_5012B_TxDMA();
            if (INIT_FAILURE == status) { CY_ASSERT(0); }
        
            status = Config_SPI_TLx_5012B_RxDMA();
            if (INIT_FAILURE == status) { CY_ASSERT(0); }
        #endif
        
        // Initialize state machine
        STATE_MACHINE_Init();
        
        // Initialize double integrator filter to get filtered theta and omega from sensor over SPI (calculation based on mechanical speed)
        BiIntFilt_Init(&BiIntFilt_obj,
                   1.0f/MOTOR_CTRL_FASTLOOP_FREQ, // Sampling time (e.g.25kHz)
                   BIINTFILT_L1,                   // k_L1 mechanical
                   BIINTFILT_L2,                   // k_L2 mechanical
                   MOTOR_POLE/2.0f,                  // Number of pole pairs
                   BIINTFILT_SPEED_LIMIT,          // omega_limit (rad/s)
                   BIINTFILT_DEADBAND,              // Deadband (rad) [unused]
                   BIINTFILT_FIXED_OFFSET_MECH,      // Magnet to shaft offset
                   MOTOR_CTRL_ALIGN_TIME*1000      // Time used for alignment in ms. Theta electrical will be aligned to pi/2 after half this time. Rest is used to reach steady state
                   );        
        //---------------------------------------------------------
    #endif

    #if defined(CTRL_METHOD_RFO)
        // Disable the drive for motors configured in position control mode.
        // MOTOR_CTRL_SetDriveEnable must be called after STATE_MACHINE_Init.
        for (uint8_t i = 0U; i < MOTOR_CTRL_NO_OF_MOTOR; i++)
        {
            if (params[i].ctrl.mode == Position_Mode_FOC_Encoder_Align_Startup)
            {
                MOTOR_CTRL_SetDriveEnable(&motor[i], false);
            }
        }
    #endif

    // Enable global interrupts
    __enable_irq();

    (void) (result);
    for (;;) {
        // Make sure the angle sensing mode is set correct. With this check the control mode can be changed in the GUI without reprogramming or power cycle. Needed because encoder can be ran in IIF or SPI mode.
        if (motor[0].params_ptr->ctrl.mode == Position_Mode_FOC_Encoder_Align_Startup || motor[0].params_ptr->ctrl.mode == Speed_Mode_FOC_Encoder_Align_Startup || motor[0].params_ptr->ctrl.mode == Curr_Mode_FOC_Encoder_Align_Startup){
            #if defined(ENC_IN_IIF_MODE) 
                motor[0].params_ptr->sys.fb.mode = AqB_Enc;
            #elif defined(ENC_IN_SPI_MODE)
                motor[0].params_ptr->sys.fb.mode = Direct;
            #endif 
        }
        else if (motor[0].params_ptr->ctrl.mode == Speed_Mode_FOC_Hall || motor[0].params_ptr->ctrl.mode == Curr_Mode_FOC_Hall){
            motor[0].params_ptr->sys.fb.mode = Hall;
        }
        else{
            motor[0].params_ptr->sys.fb.mode = Sensorless;
        }
        

#if (CPU_LOAD_CALC_ENABLED)
        MCU_CPULoadCalc();
#endif
    }
}

