/*******************************************************************************
 * Copyright (C) 2016, ZIXC Corporation.
 *
 * File Name:    
 * File Mark:    
 * Description:  
 * Others:        
 * Version:       v1.0
 * Author:        zhouqi
 * Date:          2013-1-7
 * History 1:      
 *     Date: 
 *     Version:
 *     Author: 
 *     Modification:  
 * History 2: 
  ********************************************************************************/
#ifndef __SPI_7520_H__
#define __SPI_7520_H__

#include <drvs_gpio.h>

/* registers base address */
#define SPI_BASE  (0x00104000)
/* registers */
#define SPI_VER_REG        (SPI_BASE + 0x00)
#define SPI_COM_CTRL       (SPI_BASE + 0x04)
#define SPI_FMT_CTRL       (SPI_BASE + 0x08)
#define SPI_DR              (SPI_BASE + 0x0C)
#define SPI_FIFO_CTRL      (SPI_BASE + 0x10)
#define SPI_FIFO_SR        (SPI_BASE + 0x14)
#define SPI_INTR_EN        (SPI_BASE + 0x18)
#define SPI_INTR_SR_SCLR  (SPI_BASE + 0x1C)

/*
 * SPI Common Control Register - SPI_COM_CTRL
 */
#define SPI_COM_CTRL_MASK_LBM	    (0x1UL << 0)
#define SPI_COM_CTRL_MASK_SSPE    (0x1UL << 1)
#define SPI_COM_CTRL_MASK_MS      (0x1UL << 2)
#define SPI_COM_CTRL_MASK_SOD	    (0x1UL << 3)

/*
 * SPI Format Control Register - SPI_FMT_CTRL
 */
#define SPI_FMT_CTRL_MASK_FRF	    (0x3UL << 0)
#define SPI_FMT_CTRL_MASK_POL	    (0x1UL << 2)
#define SPI_FMT_CTRL_MASK_PHA	    (0x1UL << 3)
#define SPI_FMT_CTRL_MASK_DSS	    (0x1FUL << 4)

/*
 * SPI FIFO Control Register - SPI_FIFO_CTRL
 */
#define SPI_FIFO_CTRL_MASK_RX_DMA_EN        (0x1UL << 2)
#define SPI_FIFO_CTRL_MASK_TX_DMA_EN        (0x1UL << 3)
#define SPI_FIFO_CTRL_MASK_RX_FIFO_THRES    (0xFUL << 4)
#define SPI_FIFO_CTRL_MASK_TX_FIFO_THRES    (0xFUL << 8)

/*
 * SPI FIFO Status Register - SPI_FIFO_SR
 */
#define SPI_FIFO_SR_MASK_RX_BEYOND_THRES    (0x1UL << 0)
#define SPI_FIFO_SR_MASK_TX_BEYOND_THRES    (0x1UL << 1)
#define SPI_FIFO_SR_MASK_RX_FIFO_FULL       (0x1UL << 2)
#define SPI_FIFO_SR_MASK_TX_FIFO_EMPTY      (0x1UL << 3)
#define SPI_FIFO_SR_MASK_BUSY               (0x1UL << 4)
#define SPI_FIFO_SR_MASK_RX_FIFO_CNTR       (0x1FUL << 5)
#define SPI_FIFO_SR_MASK_TX_FIFO_CNTR       (0x1FUL << 10)

/*
 * SPI Interrupt Enable Register - SPI_INTR_EN
 */
#define SPI_INTR_EN_MASK_RX_OVERRUN_IE          (0x1UL << 0)
#define SPI_INTR_EN_MASK_TX_UNDERRUN_IE         (0x1UL << 1)
#define SPI_INTR_EN_MASK_RX_FULL_IE             (0x1UL << 2)
#define SPI_INTR_EN_MASK_TX_EMPTY_IE             (0x1UL << 3)
#define SPI_INTR_EN_MASK_RX_THRES_IE            (0x1UL << 4)
#define SPI_INTR_EN_MASK_TX_THRES_IE            (0x1UL << 5)

/*
 * SPI Interrupt Status Register OR Interrupt Clear Register - SPI_INTR_SR_SCLR
 */
#define SPI_INTR_SR_SCLR_MASK_RX_OVERRUN_INTR       (0x1UL << 0)
#define SPI_INTR_SR_SCLR_MASK_TX_UNDERRUN_INTR      (0x1UL << 1)
#define SPI_INTR_SR_SCLR_MASK_RX_FULL_INTR          (0x1UL << 2)
#define SPI_INTR_SR_SCLR_MASK_TX_EMPTY_INTR         (0x1UL << 3)
#define SPI_INTR_SR_SCLR_MASK_RX_THRES_INTR         (0x1UL << 4)
#define SPI_INTR_SR_SCLR_MASK_TX_THRES_INTR         (0x1UL << 5)

/* gpio */
#define GPIO_SPI_CS      GPIO2
#define GPIO_SPI_CLK     GPIO3
#define GPIO_SPI_RXD     GPIO4
#define GPIO_SPI_TXD     GPIO5

#define SPI_BUS_CLK_SRC   (104000000)   /* 104M */
#define SPI_BUS_CLK_WORK  (26000000)   /* 26M */


#endif /*__SPI_7520_H__*/

