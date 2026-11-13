/*  
 * (C) Copyright 2016, ZIXC Corporation.
 *
 */


#ifndef _ZX297520_I2C_H_
#define _ZX297520_I2C_H_



/****************************************************************************
* 	                                        Include files
****************************************************************************/

/****************************************************************************
* 	                                        Macros
****************************************************************************/

#define ARM_I2C0_BASE       0x00132000   
#define ARM_I2C1_BASE       0x01409000  
#define ARM_PMICI2C_BASE       ARM_I2C0_BASE        /* pmic i2c  */

#define I2C_INT_ENABLE          0

/* CMD */
#define I2C_CMD_START               (0x1<<6) //   
#define I2C_CMD_CMB_RW              (0x1<<5) //  
#define I2C_CMD_RW                  (0x1<<4) //   
#define I2C_CMD_IRQ_EN              (0x1<<3) //  
#define I2C_CMD_SPEED_MODE          (0x1<<2) //  
#define I2C_CMD_ADDR_MODE           (0x1<<1) // 
#define I2C_CMD_MS_SEL              (0x1<<0) //                     

/* TXF_CTRL */
#define I2C_TX_FIFO_RST             (0x1<<7) //   

/* RXF_CTRL */
#define I2C_RX_FIFO_RST             (0x1<<7) //   


/* I2C_STATUS */
#define I2C_IRQ_ACK                 (0x1<<7) //W   
#define I2C_BUSY                    (0x1<<6) //R  
#define I2C_IRQ_RXF_FULL            (0x1<<5) //R  
#define I2C_IRQ_TXF_EMPTY           (0x1<<4) //R
#define I2C_IRQ_TIME_OUT            (0x1<<3) //R
#define I2C_IRQ_ERR_DATA            (0x1<<2) //R
#define I2C_IRQ_ERR_DEVICE          (0x1<<1) //R
#define I2C_IRQ_TRANS_DONE          (0x1<<0) //R

/* TXF_STATUS */
#define I2C_TX_FIFO_EMPTY           (0x1<<1) // 
#define I2C_TX_FIFO_FULL            (0x1<<0) // 

/* RXF_STATUS */
#define I2C_RX_FIFO_EMPTY           (0x1<<1) // 
#define I2C_RX_FIFO_FULL            (0x1<<0) // 

/****************************************************************************
* 	                                        Types
****************************************************************************/
/**********************************************************/


/**********************************************************/
/** @defgroup T_I2C_Regs 
  */ 
typedef volatile struct _T_I2C_Regs
{ 
    uint32_t VER;                 //0x00  
    uint32_t CMD;                 //0x04 
    uint32_t MST_CODE;            //0x08  
    uint32_t DEVICE_H;            //0x0C   
    uint32_t DEVICE_L;            //0x10   
    uint32_t CLK_DIV_FS;          //0x14
    uint32_t CLK_DIV_HS;          //0x18
    uint32_t TXF_CTRL;            //0x1C
    uint32_t RXF_CTRL;            //0x20
    uint32_t DATA;                //0x24
    uint32_t I2C_STATUS;          //0x28
    uint32_t TXF_STATUS;          //0x2C
    uint32_t RXF_STATUS;          //0x30   
}T_I2C_Regs;          


/**********************************************************/
/** @T_ZDrvI2c_BusFreq 
  */ 
typedef enum{
    I2C_FREQ_ONE_HUNDRED_KILO_HZ    = 100*1000,
    I2C_FREQ_THREE_HUNDRED_KILO_HZ  = 300*1000,
    I2C_FREQ_FOUR_HUNDRED_KILO_HZ   = 400*1000,
    MAX_I2C_BUS_FREQ
}T_ZDrvI2c_BusFreq;


/**********************************************************/
/** @T_I2C_Mode 
  */ 
typedef enum
{
    I2C_SIMPLE_WRITE,
    I2C_COMBINE_READ,
    I2C_SIMPLE_READ
}T_I2C_Mode;

#endif/*_ZX297520_I2C_H_*/

