/*******************************************************************************
* Copyright 2025, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/
/*
 * TLx_5012B.c
 *
 *  Created on: Apr 24, 2025
 *      Author: leeyoung
 */

#include "ENC_TLx_5012B.h"
#include "cy_dma.h"


/*******************************************************************************
 *                       Macro definitions
 ******************************************************************************/
#define RXDMA_INTERRUPT_PRIORITY (0u)    /* Interrupt priority for RXDMA */
#define TXDMA_INTERRUPT_PRIORITY (0u)    /* Interrupt priority for TXDMA */

#define SPI_MODE_BOTH    0
#define SPI_MODE_MASTER  1
#define SPI_MODE_SLAVE   2

#define SPI_MODE SPI_MODE_MASTER

/* Communication status */
#define TRANSFER_COMPLETE       (0)
#define TRANSFER_FAILURE        (1)
#define TRANSFER_IN_PROGRESS    (2)
#define IDLE                    (3)

/* TX Packet Head and Tail */
#define PACKET_SOP              (0x8021UL)
#define PACKET_EOP              (0x17UL)

/* Element index in the packet */
#define PACKET_SOP_POS          (0UL)
#define PACKET_CMD_POS          (1UL)
#define PACKET_EOP_POS          (2UL)


volatile bool rx_dma_done = false;        /* varibale to check the rx dma transection status */
volatile bool tx_dma_done = false;        /* varibale to check the tx dma transection status */

volatile uint32_t SPI_TLx_5012B_Tx_Buf[3];
volatile uint32_t SPI_TLx_5012B_Rx_Buf[3];
volatile uint32_t TxDMA_IsrCnt = 0;
volatile uint32_t RxDMA_IsrCnt = 0;

BiIntFilt_t BiIntFilt_obj;

//-----------------------------------------------------------------------------
//-- Power up/down the TLx-5012B
//-----------------------------------------------------------------------------
void PwrUp_Enc1(void)
{
    //Cy_GPIO_Clr(ENC_ENABLE_1_PORT, ENC_ENABLE_1_PIN);        // Power Up Encoder Chip 1
    Cy_GPIO_Write(ENC_ENC_EN_PORT, ENC_ENC_EN_PIN, 0);
}
void PwrDn_Enc1(void)
{
    //Cy_GPIO_Set(ENC_ENABLE_1_PORT, ENC_ENABLE_1_PIN);        // Power Dn Encoder Chip 1
    Cy_GPIO_Write(ENC_ENC_EN_PORT, ENC_ENC_EN_PIN, 1);
}


/******************************************************************************
* Function Name: Config_SPI_TLx_5012B_TxDMA
*******************************************************************************
*
* Summary:      This function configure the transmit DMA block
*
* Parameters:   tx_buffer
*
* Return:       (uint32_t) INIT_SUCCESS or INIT_FAILURE
*
******************************************************************************/
uint32_t Config_SPI_TLx_5012B_TxDMA(void)
{
    cy_en_dma_status_t dma_init_status;

    SPI_TLx_5012B_Tx_Buf[0] = 0x8021;    // 1 0000 0 000010 0010: Read/Write, Lock, Address(0x02), Angle
    SPI_TLx_5012B_Tx_Buf[1] = 0x0000;
    SPI_TLx_5012B_Tx_Buf[2] = 0x0000;

    dma_init_status = Cy_DMA_Descriptor_Init(&ENC_SPI_TX_DMA_Descriptor_0, &ENC_SPI_TX_DMA_Descriptor_0_config);        // Initialize DMA descriptor
    if (dma_init_status != CY_DMA_SUCCESS)
    {
        return INIT_FAILURE;
    }

    dma_init_status = Cy_DMA_Channel_Init(ENC_SPI_TX_DMA_HW, ENC_SPI_TX_DMA_CHANNEL, &ENC_SPI_TX_DMA_channelConfig);    // Initialize DMA Channel
    if (dma_init_status != CY_DMA_SUCCESS)
    {
        return INIT_FAILURE;
    }

    Cy_DMA_Descriptor_SetSrcAddress(&ENC_SPI_TX_DMA_Descriptor_0, (uint8_t*) SPI_TLx_5012B_Tx_Buf);    // Set source      for descriptor 1
    Cy_DMA_Descriptor_SetDstAddress(&ENC_SPI_TX_DMA_Descriptor_0, (void*) &ENCODER_SPI_HW->TX_FIFO_WR);    // Set destination for descriptor 1

    #if 0
    const cy_stc_sysint_t intTxDma_cfg =    // ISR Handler Info for the completion or Tx DMA
    {
        .intrSrc = ENC_SPI_TX_DMA_IRQ,
        .intrPriority = TXDMA_INTERRUPT_PRIORITY,
    };

    Cy_SysInt_Init(&intTxDma_cfg, &TxDMA_IRQ_Handler);        // Install the System Interrupt Service Handler with Priority
    NVIC_EnableIRQ((IRQn_Type) intTxDma_cfg.intrSrc);        // Enable the IRQ at the CPU NVIC level
    Cy_DMA_Channel_SetInterruptMask(ENC_SPI_TX_DMA_HW, ENC_SPI_TX_DMA_CHANNEL, CY_DMA_INTR_MASK);    // Enable DMA interrupt source and the peripheral level
    #endif

    Cy_DMA_Enable(ENC_SPI_TX_DMA_HW);                                                                    // Enable DMA block to start descriptor execution process

    return INIT_SUCCESS;
}

/******************************************************************************
* Function Name: TxDMA_IRQ_Handler
*******************************************************************************
*
* Summary:      This function check the tx DMA status
*
* Parameters:   None
*
* Return:       None
*
******************************************************************************/
void TxDMA_IRQ_Handler(void)
{
    #if 0
    /* Check tx DMA status */
    if ((CY_DMA_INTR_CAUSE_COMPLETION != Cy_DMA_Channel_GetStatus(ENC_SPI_TX_DMA_HW, ENC_SPI_TX_DMA_CHANNEL))
     && (CY_DMA_INTR_CAUSE_CURR_PTR_NULL != Cy_DMA_Channel_GetStatus(ENC_SPI_TX_DMA_HW, ENC_SPI_TX_DMA_CHANNEL)))
    {
        Mtr1.MotorVar.error_status.SPI_TLx_ERR = 1;        // Mark up there is SPI DMA error for TLx-5012B
    }
    Cy_GPIO_Set(TEST_PIN4_PORT, TEST_PIN4_NUM);
    SendPacket_SPI_TLx_5012B();                            // transfers data from txBuffer to mSPI TX-FIFO
    Cy_DMA_Channel_Enable(ENC_SPI_TX_DMA_HW, ENC_SPI_TX_DMA_CHANNEL);            // Enable DMA channel to transfer 12 bytes of data from txBuffer into mSPI TX-FIFO

    tx_dma_done = true;
    Cy_GPIO_Clr(TEST_PIN4_PORT, TEST_PIN4_NUM);
    #endif

    Cy_DMA_Channel_ClearInterrupt(ENC_SPI_TX_DMA_HW, ENC_SPI_TX_DMA_CHANNEL);    // Clear tx DMA interrupt

    TxDMA_IsrCnt++;
}

/******************************************************************************
* Function Name: Config_SPI_TLx_5012B_RxDMA
*******************************************************************************
*
* Summary:      This function configure the receive DMA block
*
* Parameters:   rx_buffer
*
* Return:       (uint32_t) INIT_SUCCESS or INIT_FAILURE
*
******************************************************************************/
uint32_t Config_SPI_TLx_5012B_RxDMA(void)
{
    cy_en_dma_status_t dma_init_status;

    SPI_TLx_5012B_Rx_Buf[0] = 0x0000;
    SPI_TLx_5012B_Rx_Buf[1] = 0x0000;
    SPI_TLx_5012B_Rx_Buf[2] = 0x0000;

    dma_init_status = Cy_DMA_Descriptor_Init(&ENC_SPI_RX_DMA_Descriptor_0, &ENC_SPI_RX_DMA_Descriptor_0_config);    // Initialize descriptor
    if (dma_init_status != CY_DMA_SUCCESS)
    {
        return INIT_FAILURE;
    }

    dma_init_status = Cy_DMA_Channel_Init(ENC_SPI_RX_DMA_HW, ENC_SPI_RX_DMA_CHANNEL, &ENC_SPI_RX_DMA_channelConfig);
    if (dma_init_status != CY_DMA_SUCCESS)
    {
        return INIT_FAILURE;
    }

    Cy_DMA_Descriptor_SetSrcAddress(&ENC_SPI_RX_DMA_Descriptor_0, (void*) &ENCODER_SPI_HW->RX_FIFO_RD);    // Set source      for descriptor 1
    Cy_DMA_Descriptor_SetDstAddress(&ENC_SPI_RX_DMA_Descriptor_0, (uint8_t*) SPI_TLx_5012B_Rx_Buf);    // Set destination for descriptor 1

    #if 0
    const cy_stc_sysint_t intRxDma_cfg =    // ISR Handler Info for the completion or Tx DMA
    {
        .intrSrc = ENC_SPI_RX_DMA_IRQ,
        .intrPriority = RXDMA_INTERRUPT_PRIORITY,
    };

    Cy_SysInt_Init(&intRxDma_cfg, &RxDMA_IRQ_Handler);        // Install the System Interrupt Service Handler with Priority
    NVIC_EnableIRQ((IRQn_Type) intRxDma_cfg.intrSrc);        // Enable the IRQ at the CPU NVIC level
    Cy_DMA_Channel_SetInterruptMask(ENC_SPI_RX_DMA_HW, ENC_SPI_RX_DMA_CHANNEL, CY_DMA_INTR_MASK);    // Enable DMA interrupt source and the peripheral level
    #endif

    Cy_DMA_Enable(ENC_SPI_RX_DMA_HW);                                        // Enable DMA block to start descriptor execution process
    return INIT_SUCCESS;
}

/******************************************************************************
* Function Name: RxDMA_IRQ_Handler
*******************************************************************************
*
* Summary:      This function check the rx DMA status
*
* Parameters:   None
*
* Return:       None
*
******************************************************************************/
void RxDMA_IRQ_Handler(void)
{
    #if 0
    if (CY_DMA_INTR_MASK == Cy_DMA_Channel_GetInterruptStatusMasked(ENC_SPI_RX_DMA_HW, ENC_SPI_RX_DMA_CHANNEL))    // Scenario: Inside the interrupt service routine for block DW0 channel 23:
    {
        cy_en_dma_intr_cause_t cause = Cy_DMA_Channel_GetStatus(ENC_SPI_RX_DMA_HW, ENC_SPI_RX_DMA_CHANNEL);        // Get the interrupt cause
        if (CY_DMA_INTR_CAUSE_COMPLETION != cause)
        {
            Mtr1.MotorVar.error_status.SPI_TLx_ERR = 1;        // Mark up there is SPI DMA error for TLx-5012B
        }
        else
        {
            Cy_GPIO_Set(TEST_PIN4_PORT, TEST_PIN4_NUM);
            ReceivePacket_SPI_TLx_5012B();                    // transfers data from sSPI RX-FIFO to rxBuffer
            Cy_DMA_Channel_Enable(ENC_SPI_RX_DMA_HW, ENC_SPI_RX_DMA_CHANNEL);    // Enable DMA channel to transfer 12 bytes of data from sSPI RX-FIFO to rxBuffer.
            rx_dma_done = true;
          Cy_GPIO_Clr(TEST_PIN4_PORT, TEST_PIN4_NUM);
        }
    }
    #endif
    Cy_DMA_Channel_ClearInterrupt(ENC_SPI_RX_DMA_HW, ENC_SPI_RX_DMA_CHANNEL);    // Clear the interrupt

    RxDMA_IsrCnt++;
}

#if ((SPI_MODE == SPI_MODE_BOTH) || (SPI_MODE == SPI_MODE_MASTER))

/******************************************************************************
* Function Name: Init_SPI_TLx_5012B
*******************************************************************************
*
* Summary:      This function initializes the SPI Master based on the
*               configuration done in design.modus file.
*
* Parameters:   None
*
* Return:       (uint32_t) INIT_SUCCESS or INIT_FAILURE
*
******************************************************************************/
uint32_t Init_SPI_TLx_5012B(void)
{
    cy_en_scb_spi_status_t init_status;

    init_status = Cy_SCB_SPI_Init(ENCODER_SPI_HW, &ENCODER_SPI_config, NULL);    // Configure SPI block

    if (init_status != CY_SCB_SPI_SUCCESS)    // If the initialization fails, return failure status
    {
        return(INIT_FAILURE);
    }

    Cy_SCB_SPI_SetActiveSlaveSelect(ENCODER_SPI_HW, CY_SCB_SPI_SLAVE_SELECT0);    // Set active slave select to line 0
    Cy_SCB_SPI_Enable(ENCODER_SPI_HW);        // Enable SPI master block.
    return(INIT_SUCCESS);                // Initialization completed
}


/******************************************************************************
* Function Name: SendPacket_SPI_TLx_5012B
*******************************************************************************
*
* Summary:      This function transfers data from txBuffer to mSPI TX-FIFO. The
*               below function enables channel and DMA block to start descriptor
*               execution process for txDMA.
*
* Parameters:   None
*
* Return:       None
*
******************************************************************************/
void SendPacket_SPI_TLx_5012B(void)
{
    Cy_DMA_Channel_Enable(ENC_SPI_TX_DMA_HW, ENC_SPI_TX_DMA_CHANNEL);        // Enable DMA channel to transfer 12 bytes of data from txBuffer into mSPI TX-FIFO
}

#endif

/******************************************************************************
* Function Name: init_slave
*******************************************************************************
*
* Summary:      This function initializes the SPI Slave based on the
*               configuration done in design.modus file.
*
* Parameters:   None
*
* Return:       (uint32_t) INIT_SUCCESS or INIT_FAILURE
*
******************************************************************************/
#if 0
uint32_t init_slave(void)
{
    cy_stc_scb_spi_context_t sSPI_context;
    cy_en_scb_spi_status_t init_status;

    init_status = Cy_SCB_SPI_Init(sSPI_HW, &sSPI_config, &sSPI_context);    // Configure the SPI block

    if (init_status != CY_SCB_SPI_SUCCESS)    // If the initialization fails, return failure status
    {
        return(INIT_FAILURE);
    }

    Cy_SCB_SPI_SetActiveSlaveSelect(sSPI_HW, CY_SCB_SPI_SLAVE_SELECT0);    // Set active slave select to line 0
    Cy_SCB_SPI_Enable(sSPI_HW);        // Enable the SPI Slave block
    return(INIT_SUCCESS);            // Initialization completed
}
#endif

/******************************************************************************
* Function Name: ReceivePacket_SPI_TLx_5012B
*******************************************************************************
*
* Summary:      This function transfers data from sSPI RX-FIFO to rxBuffer. The
*               below function enables channel and DMA block to start descriptor
*               execution process for rxDMA.
*
* Parameters:   None
*
* Return:       None
*
******************************************************************************/
void ReceivePacket_SPI_TLx_5012B(void)
{
    Cy_DMA_Channel_Enable(ENC_SPI_RX_DMA_HW, ENC_SPI_RX_DMA_CHANNEL);    // Enable DMA channel to transfer 12 bytes of data from sSPI RX-FIFO to rxBuffer.
}
