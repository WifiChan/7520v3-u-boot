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

#ifndef _HARDWARE_H_
#define _HARDWARE_H_

#include <asm/sizes.h>

#define UData(Data)	((unsigned long) (Data))

#define __REG(x)	(*(vu_long *)(x))
#define __REGl(x)	(*(vu_long *)(x))
#define __REGw(x)	(*(vu_short *)(x))
#define __REGb(x)	(*(vu_char *)(x))
#define __REG2(x, y)	(*(vu_long *)((x) + (y)))


#define Fld(Size, Shft)	(((Size) << 16) + (Shft))

#define FSize(Field)	((Field) >> 16)
#define FShft(Field)	((Field) & 0x0000FFFF)
#define FMsk(Field)	(((UData (1) << FSize (Field)) - 1) << FShft (Field))
#define FAlnMsk(Field)	((UData (1) << FSize (Field)) - 1)
#define F1stBit(Field)	(UData (1) << FShft (Field))

#define FClrBit(Data, Bit)	(Data = (Data & ~(Bit)))
#define FClrFld(Data, Field)	(Data = (Data & ~FMsk(Field)))

#define FInsrt(Value, Field) \
			(UData (Value) << FShft (Field))

#define FExtr(Data, Field) \
			((UData (Data) >> FShft (Field)) & FAlnMsk (Field))

#endif /* _HARDWARE_H_ */
