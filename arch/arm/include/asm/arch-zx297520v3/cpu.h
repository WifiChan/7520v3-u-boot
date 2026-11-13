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
#ifndef __ZX297520_H__
#define __ZX297520_H__

#include <config.h>

#define SYS_BOOTSEL_BASE    0x0010c03c		/* ¶¨ÒåBOOTSEL¼Ä´æÆ÷µØÖ· */

#define WDT_SYS_RESET       0x0010D30c		

#define SYS_IRAM0_BASE      0x82000000 		/* for cpu_a9 */
#define SYS_IRAM1_BASE  	0x00100000		/* for cpu_r7 */
#define SYS_IRAM2_BASE      0x00080000      /* for cpu_m0*/

#define SYS_IRAM3_BASE      0x10000000      
#define SYS_IRAM4_BASE      0x00080000      
#define SYS_IRAM5_BASE
#define SYS_IRAM6_BASE      0x90020000      

#define SYS_CPUCAP_BOOT_BASE	SYS_IRAM1_BASE + 0x2000

#define CPU_CAP_SUBSYS_CFG    0x013b134     /* for cap a53 reset */
#define CPU_CAP_SW_RSTEN      0xf      

#define CPU_PHY_SUBSYS_CFG      0x0010C044      /* for ZPM_PHY reset */
#define     CPU_PHY_SW_RSTEN      0x1     

//#define CPU_UFI_SUBSYS_CFG      0x0130604c      /* for ZPM_A9 reset */
#define     CPU_UFI_SW_RSTEN      0xf  

#define CPU_A9_SUBSYS_CFG      0x0130604c   

#define CORE_OUTPUT_SWITCH_CONFIG_REG (0x0013a000 + 0x0AC)
#define CORE_OUTPUT_SW_CONFIG_REG1  (0x0013a000 + 0x0B8)
#define CORE_OUTPUT_SW_CONFIG_REG2  (0x0013a000 + 0x0BC) 
 
#endif /*__ZX297520_H__*/
