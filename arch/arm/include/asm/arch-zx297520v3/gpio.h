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
#ifndef __GPIO_7520_H__
#define __GPIO_7520_H__

#define ZX297520_A1_GPIO0       0x00144000
#define ZX297520_A1_GPIO1       0x00145000

#define GPIO_MAXNUM_0           112
#define GPIO_MAXNUM_1 			124

#define PAD_CTRL_REG_BASE       0x00143000

#define GPIOPDD_REG0(a)   (unsigned int *)(ZX297520_A1_GPIO0 + ((a) * 16) * 4)
#define RECV_REG0(a)   	  (unsigned int *)(ZX297520_A1_GPIO0 + ((a) * 16 + 5) * 4)
#define SET1_SEND_REG0(a) (unsigned int *)(ZX297520_A1_GPIO0 + ((a) * 16 + 6) * 4)
#define SET0_SEND_REG0(a) (unsigned int *)(ZX297520_A1_GPIO0 + ((a) * 16 + 7)*4)

#define GPIOPDD_REG1(a)   (unsigned int *)(ZX297520_A1_GPIO1 + ((a) * 16) * 4)
#define RECV_REG1(a)   	  (unsigned int *)(ZX297520_A1_GPIO1 + ((a) * 16 + 5) * 4)
#define SET1_SEND_REG1(a) (unsigned int *)(ZX297520_A1_GPIO1 + ((a) * 16 + 6) * 4)
#define SET0_SEND_REG1(a) (unsigned int *)(ZX297520_A1_GPIO1 + ((a) * 16 + 7) * 4)


#define PIN_STATE(a, b, c, d) {\
	.number = a, \
	.mux_reg = (unsigned int *)(PAD_CTRL_REG_BASE + b), \
	.mux_bit = c, \
	.mux_bitnum = d,  \
}

#define PIN_UPDOWN(a,b,c){\
	.number = a, \
	.conf_reg = (unsigned int *)(PAD_CTRL_REG_BASE + b), \
	.conf_bit = c, \
}

typedef struct _T_Pin_State{
	unsigned int number;		//gpio number
	unsigned int *mux_reg; 		//mux reg of pin
	unsigned int mux_bit; 		//bit of the reg to config mux
	unsigned int mux_bitnum;
}T_Pin_State;

typedef struct _T_Pin_UpDown{
	unsigned int number;		//gpio number
	unsigned int *conf_reg; 		//mux reg of pin
	unsigned int conf_bit; 		//bit of the reg to config mux
}T_Pin_UpDown;
T_Pin_State g_PinState[] = {	\
	/*number*//*mux_reg*//*mux_bit*//*m_num*//*pin_name*/
	PIN_STATE(6,		0x18,		24,			0xf),	/*"scl0",*/
	PIN_STATE(7,		0x18,		28,			0xf),	/*"sda0",*/
	PIN_STATE(8,		0x1c,		0,			0xf),	/*"scl1",*/
	PIN_STATE(9,		0x1c,		4,			0xf),	/*"sda1",*/
    PIN_STATE(11,		0x1c,		12,			0xf),	/*"CHG INT",*/  \
	PIN_STATE(49,		0x14,		28,			0xf),	/*"UART0_TXD",*/\
	PIN_STATE(48,		0x14,		24,			0xf),	/*"UART0_RXD",*/ \
	PIN_STATE(189,		0x38,		24,			0xf),	/*"CHG STATE",*/  \
	PIN_STATE(210,		0x38,		28,			0xf),	/*"CHG TYPE",*/  \
	//PIN_STATE(191,		0x38,		0,			0xf),	/*"CHG IN",*/  \
	PIN_STATE(199,		0x3C,		0,			0xf),	/*"CHG EN",*/  \
	PIN_STATE(208,		0x40,		4,			0xf),	/*"CHG M0",*/  \
	PIN_STATE(209,		0x40,		8,			0xf),	/*"CHG M1",*/  \
	PIN_STATE(201,		0x3C,		8,			0xf),	/*"pshold",*/

    PIN_STATE(12,		0x1C,		16,			0xf),	/*"power key",*/

	PIN_STATE(190,	   0x34,	   28,		   0xf),   /*ssp1 CS*/   
	PIN_STATE(191,	   0x38,	   0,		   0xf),   /*ssp1 CLK*/ 
	PIN_STATE(192,	   0x38,	   4,		   0xf),   /*ssp1 RXD*/ 
	PIN_STATE(193,	   0x38,	   8,		   0xf),   /*ssp1 TXD*/ 

	PIN_STATE(148,      0xb4,       0,          0xf),   /*"sim2_rs",*/
	PIN_STATE(150,      0xb4,       8,          0xf),   /*"sim2_data",*/
	PIN_STATE(196,      0x38,       20,         0xf),   /*"dbi_data_1",*/
	PIN_STATE(200,      0x3c,       4,          0xf),   /*"dbi_data_5",*/
	PIN_STATE(225,      0xb0,       28,         0xf),   /*"sim2_det",*/

    

};

T_Pin_UpDown g_PinUpDown[] = {	\
		/*number*//*conf_reg*//*conf_bit*//*pin_name*/
	PIN_UPDOWN(205,		0x80c,		7),	/*"scl0",*/
	PIN_UPDOWN(12,		0x804,		19),	/*"scl0",*/
};
#endif /*__GPIO_7520_H__*/
