/*******************************************************************************
 * Copyright (C) 2016, ZIXC Corporation.
 *
 * File Name:    
 * File Mark:    
 * Description:  
 * Others:        
 * Version:       v1.0
 * Author:        zhouqi
 * Date:          2013-4-7
 * History 1:      
 *     Date: 
 *     Version:
 *     Author: 
 *     Modification:  
 * History 2: 
  ********************************************************************************/
#ifndef __TIMER_7520_H__
#define __TIMER_7520_H__

/********************************************************************************
 * timer
 *******************************************************************************/
#define TIMER0_BASE		0x0142000
#define TIMER1_BASE		0x0145000
#define AON_CRM_BASE	0x013B000

#define TIMER_VERSION_REG   (TIMER0_BASE + 0x00)
#define TIMER_CONFIG_REG    (TIMER0_BASE + 0x04)
#define     PTV_MASK        	(0x1F << 5)		    /* 时钟分频  2** PTV */
#define     PTV_DIV_1 		    (0x0 << 5)		    /* 1 分频  */
#define     PTV_DIV_2           (0x1 << 5)		    /* 2 分频 */
#define     PTV_DIV_4	        (0x2 << 5)		    /* 4 分频 */
#define     PTV_DIV_8 		    (0x3 << 5)		    /* 8 分频 */
#define     AUTO  		        (0x1 << 1)		    /* 中断后自动重装初值 */
#define TIMER_LOAD_REG      (TIMER0_BASE + 0x08)    /* 计数初值 */
#define TIMER_START_REG     (TIMER0_BASE + 0x0C) 
#define     TIMER_START		    (0x1 << 0)		    /* 开始 */
#define TIMER_SET_EN_REG    (TIMER0_BASE + 0x10)
#define     SET_EN_CONFIG	    2	
#define     SET_EN_LOAD	        0	
#define TIMER_ACK_REG       (TIMER0_BASE + 0x14)    /* read only */
#define TIMER_COUNT_REG     (TIMER0_BASE + 0x18)

#define RM_MOD_CLKSEL       (AON_CRM_BASE + 0x3c)
#define RM_TIMER0_SEL		1
#define RM_TIMER1_SEL		(1<<3)
#define RM_MOD_CLKDIV       (AON_CRM_BASE + 0x48)
#define RM_TIMER0_DIV1		0
#define RM_TIMER0_DIV12		0xB
#define RM_TIMER0_DIV16		0xF
#define RM_TIMER1_DIV1		(0<<4)
#define RM_TIMER1_DIV12		(0xB<<4)
#define RM_TIMER1_DIV16		(0xF<<4)



#endif /*__TIMER_7520_H__*/
