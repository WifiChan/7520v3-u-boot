/*
 * arch/arm/mach-zx297520/include/mach/irqs.h
 *
 *  Copyright (C) 2016 ZIXC-TSP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H


#define GIC_DIST_BASE   0x00801000
#define GIC_CPU_BASE    0x00800100

/* 0-15:  SGI (software generated interrupts)
 * 16-31: PPI (private peripheral interrupts)
 * 32+:   SPI (shared peripheral interrupts)
 */

#define GIC_PPI_START 16
#define GIC_SPI_START 32

/*
 * private peripheral interrupts
 */
#define GLOBAL_TIMER_INT       (27)
#define LEGACY_FIQ_INT         (28)
#define PRIVATE_TIMER_INT      (29)
#define PRIVATE_WDT_INT        (30)
#define LEGACY_IRQ_INT         (31)

#define	NR_IRQS		(77) 
/*
 * shared peripheral interrupts
 */
#define UART0_MIX_INT				(GIC_SPI_START+0)
#define UART1_MIX_INT				(GIC_SPI_START+1)
#define SSP0_MIX_INT				(GIC_SPI_START+2)
#define RTC_ALARM_INT				(GIC_SPI_START+3)
#define RTC_TIMER_INT				(GIC_SPI_START+4)
#define I2S0_INT					(GIC_SPI_START+5)
#define I2S1_INT					(GIC_SPI_START+6)
#define I2C1_INT					(GIC_SPI_START+7)
#define I2C0_INT					(GIC_SPI_START+8)
#define SD0_INT						(GIC_SPI_START+9)
#define SD1_DETECT_ON_INT			(GIC_SPI_START+10)
#define SD1_DETECT_OFF_INT			(GIC_SPI_START+11)
#define SD1_INT						(GIC_SPI_START+12)
#define ICP_M0_INT					(GIC_SPI_START+13)
#define ICP_ARM1_INT				(GIC_SPI_START+14)
#define ICP_ARM0_INT				(GIC_SPI_START+15)
#define TIMER2_INT					(GIC_SPI_START+16)
#define TIMER3_INT					(GIC_SPI_START+17)
#define TIMER0_INT					(GIC_SPI_START+18)
#define TIMER1_INT					(GIC_SPI_START+19)
#define GSM_SYS0_INT				(GIC_SPI_START+20)
#define GSM_SYS1_INT				(GIC_SPI_START+21)
#define GSM_SYS2_INT				(GIC_SPI_START+22)
#define GSM_SYS3_INT				(GIC_SPI_START+23)
#define GSM_SYS4_INT				(GIC_SPI_START+24)
#define GSM_SYS5_INT				(GIC_SPI_START+25)
#define GSM_SYS6_INT				(GIC_SPI_START+26)
#define GSM_SYS7_INT				(GIC_SPI_START+27)
#define GSM_SYS8_INT				(GIC_SPI_START+28)
#define GSM_SYS9_INT				(GIC_SPI_START+29)
#define GSM_SYS10_INT				(GIC_SPI_START+30)
#define GSM_SYS11_INT				(GIC_SPI_START+31)
#define GSM_SYS12_INT				(GIC_SPI_START+32)
#define GSM_SYS13_INT				(GIC_SPI_START+33)
#define GSM_SYS14_INT				(GIC_SPI_START+34)
#define GSM_SYS15_INT				(GIC_SPI_START+35)
#define DMAC0_INT      				(GIC_SPI_START+36)
#define DMAC1_INT	      			(GIC_SPI_START+37)
#define DDR0_INT					(GIC_SPI_START+38)
#define NAND_INT                    (GIC_SPI_START+39)
#define USB0_INT					(GIC_SPI_START+40)
#define USB0_POWERDWN_UP_INT  		(GIC_SPI_START+41)
#define USB0_POWERDWN_DOWN_INT 		(GIC_SPI_START+42)
#define USB1_INT					(GIC_SPI_START+43)
#define USB1_POWERDWN_UP_INT  		(GIC_SPI_START+44)
#define USB1_POWERDWN_DOWN_INT  	(GIC_SPI_START+45)
#define USIM0_INT					(GIC_SPI_START+46)
#define USIM1_INT					(GIC_SPI_START+47)
#define EX8_INT						(GIC_SPI_START+48)
#define EX0_INT						(GIC_SPI_START+49)
#define EX1_INT						(GIC_SPI_START+50)
#define EX2_INT						(GIC_SPI_START+51)
#define EX3_INT						(GIC_SPI_START+52)
#define EX4_INT						(GIC_SPI_START+53)
#define EX5_INT						(GIC_SPI_START+54)
#define EX6_INT						(GIC_SPI_START+55)
#define EX7_INT						(GIC_SPI_START+56)
#define SSC0_CONFLICT_INT			(GIC_SPI_START+57)
#define SSC1_CONFLICT_INT			(GIC_SPI_START+58)
#define TD_LPM3_INT					(GIC_SPI_START+59)
#define TD_FRM_INT					(GIC_SPI_START+60)
#define LTE_LPM1_INT				(GIC_SPI_START+61)
#define LTE_LPM2_INT				(GIC_SPI_START+62)
#define LTE_LPM4_INT				(GIC_SPI_START+63)
#define LTE_TPU_INT					(GIC_SPI_START+64)
#define EDCP_ARM_INT				(GIC_SPI_START+65)
#define CHECKSUM_INT				(GIC_SPI_START+66)
#define SDIO_INT				    (GIC_SPI_START+67)
#define UART0_CTS_INT				(GIC_SPI_START+68)
#define UART1_CTS_INT				(GIC_SPI_START+69)
#define USIM0_DETECT_ON_INT			(GIC_SPI_START+70)
#define USIM0_DETECT_OFF_INT		(GIC_SPI_START+71)
#define USIM1_DETECT_ON_INT			(GIC_SPI_START+72)
#define USIM1_DETECT_OFF_INT		(GIC_SPI_START+73)
#define LTE_LPM5_INT				(GIC_SPI_START+74)
#define TD_LPM4_INT					(GIC_SPI_START+75)
#define GSM_LPM_INT					(GIC_SPI_START+76)





#endif
