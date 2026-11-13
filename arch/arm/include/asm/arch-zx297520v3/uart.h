/*******************************************************************************
 * Copyright (C) 2016, ZIXC Corporation.
 *
 * File Name:    
 * File Mark:    
 * Description:  
 * Others:        
 * Version:       v1.0
 * Author:        zhouqi
 * Date:          2013-4-2
 * History 1:      
 *     Date: 
 *     Version:
 *     Author: 
 *     Modification:  
 * History 2: 
  ********************************************************************************/
#ifndef __UART_7520_H__
#define __UART_7520_H__
#include <config.h>
/********************************************************************************
 * UART
 *******************************************************************************/

#define UART0_BASE			   0x131000
#define UART1_BASE			   0x1408000
#define PAD_TOP_FUNC_BASE      0x13c000
#define PAD_PD_FUNC_BASE       0x01303000

#define UART_BASE		UART1_BASE

#define UART_VS             (UART_BASE + 0x00)
#define UART_DR             (UART_BASE + 0x04)
#define UART_TSC            (UART_BASE + 0x08)
#define UART_RSR            (UART_BASE + 0x10)
#define UART_FR             (UART_BASE + 0x14)
#define     UART_TXFF           (0x1 << 5)
#define     UART_RXFE           (0x1 << 4)
#define UART_ILPR           (UART_BASE + 0x20)
#define UART_IBRD           (UART_BASE + 0x24)
#define UART_FBRD           (UART_BASE + 0x28)
#define UART_LCR_H          (UART_BASE + 0x30)
#define     UART_SPS			(0x1 << 7)		// Stick Parity select
#define     UART_WLEN_8  		(0x3 << 5)		// Word length: 8 bits     
#define     UART_FEN			(0x1 << 4)		// Enable FIFOs     
#define     UART_STP2 		    (0x1 << 3)		// Number of stop bits,"1": 2stop bits transmitted    
#define     UART_EPS   			(0x1 << 2)		// Even parity select
#define     UART_PEN	        (0x1 << 1)		// Parity enable
#define     UART_BREAK 			(0x1 << 0)		// Send break
#define UART_CR             (UART_BASE + 0x34)
#define     UART_CTSE			(0x1 << 15)		// CTS hardware flow control enable 
#define     UART_RTSE			(0x1 << 14)		// RTS hardware flow control enable
#define     UART_OUT2			(0x1 << 13)		// out2                       
#define     UART_OUT1  	 	    (0x1 << 12) 	// out1 
#define     UART_RTS    		(0x1 << 11) 	// request to send
#define     UART_DTR    		(0x1 << 10) 	// data transmit ready 
#define     UART_RXE			(0x1 << 9)		// Receive Enable (RXE)"1"
#define     UART_TXE			(0x1 << 8)		// "1": Transmit Enable (TXE) 
#define     UART_LBE    		(0x1 << 7)		// "1": Loop Back Enable 
#define     UART_SIRLP			(0x1 << 2)		// "1": IrDA SIR Low Power Mode 
#define     UART_SIR_EN		    (0x1 << 1)		// "0": SIR disable; "1": SIR enable 
#define     UART_EN				(0x1 << 0)		// "0": uart disable; "1": uart enable 
#define UART_IFLS           (UART_BASE + 0x38)
#define UART_IMSC           (UART_BASE + 0x40)
#define UART_RIS            (UART_BASE + 0x44)
#define UART_MIS            (UART_BASE + 0x48)
#define UART_ICR            (UART_BASE + 0x4C)
#define UART_DMACR          (UART_BASE + 0x50)

#define UART_INT_MASK       (0xFFFFFFFF)

#endif /*__UART_7520_H__*/
