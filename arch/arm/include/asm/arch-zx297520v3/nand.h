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

#ifndef _ZX297520_NAND_H_
#define _ZX297520_NAND_H_

#define     NAND_FLASH_REG			0x1214000  
#define	  	NAND_FLASH_MEM			0x1215000  

/*
*	nand_flash_timing
*/
struct nand_flash_timing
{  
    unsigned char Twhr;  /*reg offset 0x100*/ /* rd_delay */
    unsigned char Trr1;                       /* cs_setup */      
    unsigned char Tadl;  /*reg offset 0x110*/ /* cs_hold */
    unsigned char Trr2;                       /* cs_desel */
    unsigned char Trhw;  /*reg offset 0x120*/
    unsigned char Trea;  /*reg offset 0x130*/
    unsigned char Trp;   /*reg offset 0x1f0, or Twp*/
    unsigned char Treh;  /*reg offset 0x200, or Tweh*/
    unsigned char Tcs;   /*reg offset 0x220*/
    unsigned char Trhz;  /*reg offset 0x290*/
};
//BRUCE
struct nand_ecc
{
	 unsigned int  strength; /*ECC 纠错能力*/
	 unsigned int  sector_size;     /*ECC 纠错的数据块大小*/
};

    
/*nand 设备描述结构体*/
struct nand_flash_device_para
{
    unsigned char manuf_id;            /* 厂家ID */
    unsigned char device_id;           /* 设备ID */
    unsigned char res_id;              /* 器件ID */
    unsigned char bus_num;             /* 0:8位  1:16位 */
    unsigned int page_size;            /* 每页main区域大小 */
    unsigned int oob_size;             /* 每页spare区域大小 */
    unsigned int column_addr_num;     /* 列地址数 */
    unsigned int row_addr_num;     /* 行地址数 */
    unsigned int block_size;           /* 每块的大小 */
    unsigned int pages_per_block;   /* 每块的大小 */
    unsigned int block_num;            /* 块数 */
    unsigned int die_num;
    unsigned int planes;
    unsigned int bad_block_markpos;     /* 坏块标志位位置,从0开始 */
    struct nand_flash_timing nand_timeing;
	struct nand_ecc ecc; 
	
};

#endif/*_ZX297520_NAND_H_*/


