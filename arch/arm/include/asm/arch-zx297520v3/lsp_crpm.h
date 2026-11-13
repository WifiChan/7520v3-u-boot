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
#ifndef __LSP_CRPM__
#define __LSP_CRPM__

//-----------A1_CRM------------------------------

#define A1_CRM_SSP_CLKDIV  (0x00101004)
#define A1_SSP_DIV_MASK    ( 0xF << 0 )
#define A1_SSP_DIV4         (0x3 << 0)

#define A1_CRM_I2C_CLKDIV  0x00101008
#define     A1_I2C_DIV_MASK    (0xF << 0)   /* [3:0] */
#define     A1_I2C_DIV4        (0x3 << 0)

//-----------LSP_CRPM----------------------------
#define LSP_CRPM_BASE   0x01400000

#define CRPM_CLKSET (LSP_CRPM_BASE + 0x0)
#define     COM_CLK_SEL             (0x1 << 0)

#define CRPM_CLKDIV1 (LSP_CRPM_BASE + 0x24)
#define     CRPM_CLKDIV1_MASK       (0xF << 16)
#define     TIMER0_DIV_12           (0xB << 16)

//-----------SOCCRM-------------------------------
#define SOCCRM_BASE   0x0010C000

#define MERCURY_CLKDIV_CLKSEL   (SOCCRM_BASE + 0x4)
#define     LSP_WCK_SEL             (0x1 << 2)

#define MOD_RSTEN               (SOCCRM_BASE + 0x18)
#define     IRAM2_SW_RSTEN          (0x1 << 24)
#define     IRAM3_SW_RSTEN          (0x1 << 25)
#define     IRAM6_SW_RSTEN          (0x1 << 26)

#define NAND_CLKDIV   (SOCCRM_BASE + 0x40)
#define     NAND_WCLK_SEL           (0x1 << 2)

#define SPIFC_CLKSEL   (LSP_CRPM_BASE + 0x24) 


//----------- TOP_SYSCLK ----------------------------------------------
#define TOP_SYSCLK_BASE      0x0013b000

/* --------------------------------------------------------------- */
#define PLL_624_208_CFG0_REG	( TOP_SYSCLK_BASE + 0x008 )
#define     PLL_624_208_CFG0_LOCK           (0x1 << 30)

/* --------------------------------------------------------------- */
#define PLL_CPU_BUS_CFG0_REG	( TOP_SYSCLK_BASE + 0x018 )
#define     PLL_CFG0_PD					(0x1 << 31)
#define     PLL_CFG0_LOCK				(0x1 << 30)
#define     PLL_CFG0_DSMPD				(0x1 << 27)
#define     PLL_CFG0_BYPASS				(0x1 << 24)
#define     PLL_CFG0_REFDIV				(0x1 << 18)
#define     PLL_CFG0_FBDIV				(0x1 << 6)
#define     PLL_CFG0_POSTDIV1			(0x1 << 2)
#define     PLL_CFG0_POSTDIV2			(0x1 << 0)
#define PLL_CPU_BUS_CFG1_REG	( TOP_SYSCLK_BASE + 0x01C )
#define     PLL_CFG1_DACAP				(0x1 << 27)
#define     PLL_CFG1_FOUT4PHASEPD		(0x1 << 26)
#define     PLL_CFG1_FOUTPOSTDIVPD		(0x1 << 25)
#define     PLL_CFG1_FOUTVCOPD			(0x1 << 24)
#define     PLL_CFG1_FRAC				(0x1 << 0)

/* --------------------------------------------------------------- */
#define PLL_TD_LTE_CFG0_REG	    ( TOP_SYSCLK_BASE + 0x020 )
#define     PLL_TD_LTE_CFG0_LOCK          (0x1 << 30)
#define PLL_TD_LTE_CFG1_REG	    ( TOP_SYSCLK_BASE + 0x024 )

#define PLL_TD_LTE_MUX_REG		( TOP_SYSCLK_BASE + 0x028 )
#define     TD_LTE_MUX_MAIN_CLK                 0x0    
#define     TD_LTE_MUX_LTE_REF_CLK              0x1
#define     TD_LTE_MUX_SSBUER                   0x2
#define     TD_LTE_MUX_0                        0x3

/* --------------------------------------------------------------- */
#define PS_CLK_SEL_REG		( TOP_SYSCLK_BASE + 0x048 )
#define     PS_PLL_CLK_PS               0x0    
#define     PS_MAIN_CLK                 0x1
#define     PLL_CLK_491M52              0x2
#define     PS_CLK_312M                 0x3
#define PS_CLK_DIV_REG		( TOP_SYSCLK_BASE + 0x070 )
#define     PS_DIV_1                0x0
#define     PS_DIV_2                0x1
#define     PS_DIV_4                0x2
#define     PS_DIV_8                0x3

#define PHY_CLK_SEL_REG		( TOP_SYSCLK_BASE + 0x04C )
#define     PHY_PLL_CLK_PS           0x0    
#define     PHY_MAIN_CLK             0x1
#define     PLL_CLK_491M52           0x2
#define     PHY_CLK_312M             0x3
#define PHY_CLK_DIV_REG		( TOP_SYSCLK_BASE + 0x074 )
#define     PHY_DIV_1                0x0
#define     PHY_DIV_2                0x1
#define     PHY_DIV_4                0x2
#define     PHY_DIV_8                0x3

/* --------------------------------------------------------------- */
#define PLL_CPU_UFI_CFG0_REG	    ( TOP_SYSCLK_BASE + 0x010)
#define     PLL_CPU_UFI_CFG0_LOCK          (0x1 << 30)
#define PLL_CPU_UFI_CFG1_REG	    ( TOP_SYSCLK_BASE + 0x014)
#define PLL_CPU_UFI_DELAY_REG	    ( TOP_SYSCLK_BASE + 0x030)

#define UFI_CLK_SEL_REG		( TOP_SYSCLK_BASE + 0x044 )
#define     UFI_PLL_CLK_UFI             0x0     /* 800M */
#define     UFI_MAIN_CLK                0x1
#define     UFI_CLK_624M                0x2
#define     UFI_PLL_CLK_PS              0x3
#define UFI_CLK_DIV_REG		( TOP_SYSCLK_BASE + 0x06C )
#define     UFI_DIV_1                0x0
#define     UFI_DIV_2                0x1
#define     UFI_DIV_4                0x2
#define     UFI_DIV_8                0x3

/* -----------------PAD_CTRL_AO------------------------------*/
#define PAD_CTRL_AO_BASE	0x13c000

/* -----------------PIN_MUX_CTRL_PD------------------------------*/
#define PIN_MUX_CTRL_PD_BASE	0x1303000


#endif /*__LSP_CRPM__*/

