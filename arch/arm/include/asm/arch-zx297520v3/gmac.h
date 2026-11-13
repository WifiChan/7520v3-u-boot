/*---------------------------------------------------------------------------------------------------------------------
 * 版权所有(C)2000-2011, 中兴通讯股份有限公司 
 * 
 * 文 件 名: drv_gmac.h  
 * 
 * 作    者: 赵利斌10113809
 * 
 * 接口描述:
 * <此处填接口详细描述, 填完后空一行>
 * 
 * 版    本: v0.0
 * 
 * ------------------------------------------------------------------------------------------------------------------ 
 * 
 *     修改日期 修改人            修改内容
 * @li 20120114 赵利斌10113809          创建
 *      20150302 徐夏敏10180991         为7520s修改
 *     
 *-------------------------------------------------------------------------------------------------------------------*/
   
/**  
 * @addtogroup DRV_GMAC  
 * @{  
 */
   
#ifndef _DRV_GMAC_H
#define _DRV_GMAC_H
  
/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

//#include "SysInc.h"
#include <linux/types.h>
#ifndef _BYTE_DEFINED_
#define _BYTE_DEFINED_
typedef u8 BYTE;
typedef u16 WORD16;
typedef u32 WORD32;
#endif

/* 使用HAL_GMAC_ENDIAN32_SWAP进行大小端转换用在描述符内存存放字节序与CPU处理描述符字节序相反的情况 2012-9-13 */

//#define GMAC_BIGENDIAN                 (1)

#ifdef GMAC_BIGENDIAN
#define GMAC_ENDIAN32_SWAP(x)          (WORD32)( (((x)>>24) & 0xFF) \
                                               | ((((x)>>16) & 0xFF)<<8) \
                                               | ((((x)>>8) & 0xFF)<<16) \
                                               | (((x) & 0xFF)<< 24) \
                                               )
#else
#define GMAC_ENDIAN32_SWAP(x)          (x)
#endif

/* 宏开关 选择相应的PHY */
#define GMAC_PHY_USE 		GMAC_PHY_IP101A
#define GMAC_PHY_88E1111                     1
//#define GMAC_PHY_88E1119                     (1)
//#define GMAC_PHY_RTL8201CP                   (1)
#define GMAC_PHY_IP101A                      2

/* GMAC基地址 根据不同项目修改基地址 */
#define GMAC_ADDR_BASE                      (0x01307000)
#define GMAC_MAC_ADDR_BASE                  (GMAC_ADDR_BASE)              /* GMAC寄存器基地址 */
#define GMAC_DMA_ADDR_BASE                  (GMAC_ADDR_BASE+0x1000)       /* GMAC DMA寄存器基地址 */

#define GMAC_POART_MODE 					GMAC_MAC_MII 
#define GMAC_MAC_DUPLEX_MODE 				GMAC_MAC_DUPLEX_FULL 


/* 定义中断号 根据实际用的中断号做相应修改 */
#define INT_NUM_GMAC                        (61+32)
#define INT_NUM_EXT_INT_7                   (56+32)

/*  值       总线时钟    表示的分频系数 */
/* 0x02       20 -35M        16分频  */
/* 0x03       35 -60M        26分频  */
/* 0x00       60 -100M       42分频  */
/* 0x01       100-150M       62分频  */
/* ...         ...            ...    */
/* 根据实际使用总线频率 修改mdio的工作时钟 */
#define GMAC_GMII_MDC                       (0x03)

#define GMAC_PHY_SPEED_1000	                (2)
#define GMAC_PHY_SPEED_100                  (1)
#define GMAC_PHY_SPEED_10                   (0)

#define GMAC_PHY_DUPLEX_HALF                (0)
#define GMAC_PHY_DUPLEX_FULL                (1)

#define GMAC_PHY_AUTONEG_EN                 (1)
#define GMAC_PHY_AUTONEG_DIS                (0)

#define GMAC_PHY_EXLOOP                     (2)
#define GMAC_PHY_LOOP                       (1)
#define GMAC_PHY_NORMAL                     (0)

#define GMAC_MAC_DUPLEX_FULL                (0x00000800)          /**< 全双工 */
#define GMAC_MAC_DUPLEX_HALF                (0x00000000)          /**< 半双工 */
#define GMAC_MAC_LOOP                       (0x00001000)
#define GMAC_MAC_NORMAL                     (0x00000000)
#define GMAC_MAC_100M                       (0x00004000)          /**< 100M */
#define GMAC_MAC_10M                        (0x00000000)          /**< 10M  */
#define GMAC_MAC_MII                        (0x00008000)          /**< MII端口(10M/100M) */
#define GMAC_MAC_GMII                       (0x00000000)          /**< GMII接口(1000M) */
#define GMAC_MAC_JD                         (0x00400000)          /**< Jabber disable，若需要接收超长帧(>2048字节)使能该位 */
#define GMAC_MAC_JDRST                      (0x00000000)          /**< 复位Watchdog disable */
#define GMAC_MAC_WD                         (0x00800000)          /**< Watchdog disable，若需要发送超长帧(>2048字节)使能该位 */
#define GMAC_MAC_WDRST                      (0x00000000)          /**< 复位Jabber disable */
#define GMAC_MAC_JE                      	(0x00100000)          /**< Jumbo Frame Enable */

#define GMAC_MAC_RCVALL_EN                  (0x80000000)          /**< 表示使能全部接收 */
#define GMAC_MAC_RCVALL_DIS                 (0x00000000)          /**< 表示禁止全部接收 */
#define GMAC_MAC_PM_EN                      (0x1u << 4)    //PM: Pass All Multicast
#define GMAC_MAC_HMC_EN                     (0x1u << 2)    //HMC: Hash Multicast
#define GMAC_MAC_DFB_EN                     (0x1u << 5)    //DBF: Disable Broadcast Frames
#define GMAC_MAC_DCRS                       (0x1u << 16)          //DCRS: Disable Carrier Sense During Transmission

#define GMAC_DMA_TSF                        (0x1u << 21)   //TSF: Transmit Store and Forward
#define GMAC_DMA_RSF                        (0x1u << 25)   //RSF: Receive Store and Forward

#define GMAC_BUSMODE_PBL_1                  (0x00000100)
#define GMAC_BUSMODE_PBL_2                  (0x00000200)
#define GMAC_BUSMODE_PBL_4                  (0x00000400)
#define GMAC_BUSMODE_PBL_8                  (0x00000800)
#define GMAC_BUSMODE_PBL_16                 (0x00001000)
#define GMAC_BUSMODE_PBL_32                 (0x00002000)

#define GMAC_INT_AIE                        (0x1u << 15)  //AIE: Abnormal Interrupt Summary Enable
#define GMAC_INT_FBE                        (0x1u << 13)  //FBE: Fatal Bus Error Enable
#define GMAC_INT_RWE                        (0x1u << 9)   //RWE: Receive Watchdog Timeout Enable
#define GMAC_INT_OVE                        (0x1u << 4)   //OVE: Overflow Interrupt Enable
#define GMAC_INT_ERE                        (0x00004000)  //ERE: Early Receive Interrupt Enable
#define GMAC_INT_EREIS                      (0x00000000)
#define GMAC_INT_ETE                        (0x00000400)  //ETE: Early Transmit Interrupt Enable
#define GMAC_INT_ETEIS                      (0x00000000)
#define GMAC_INT_TIE                        (0x00000001)
#define GMAC_INT_TIDIS                      (0x00000000)
#define GMAC_INT_RIE                        (0x00000040)
#define GMAC_INT_RIDIS                      (0x00000000)
#define GMAC_INT_NIE                        (0x00010000)
#define GMAC_INT_NIDIS                      (0x00000000)

/* 通常情况下GMAC收发一帧数据的长度不超过8*256 = 2048字节，若一帧数据大于2048则数据将会截断，并产生错误 */
/* 若要发送超长帧需要禁止watchdog及Jabber，注: 通常网络上的数据帧不超过2048字节 */
#define  GMAC_RX_BUF_SIZE                   (1600) //(1520)     // (8*128)      /* Set equal to 128 bytes each since thats what the SAM7X256 supports   */
#define  GMAC_RX_BUF_NUM                    (2)                  // (2*12)       /* MUST even ,Number of RX descriptors AND buffers.                     */
#define  GMAC_TX_BUF_SIZE                   (1600) //(1520)     // (8*128)      /* Set equal to 128 bytes each since thats what the SAM7X256 supports   */
#define  GMAC_TX_BUF_NUM                    (2)                  // (2*12)       /* MUST even ,Number of RX descriptors AND buffers.                     */
#define  GMAC_TX_DESC_NUM                   (GMAC_TX_BUF_NUM)                    /* Number of RX descriptors AND buffers.                                */
#define  GMAC_RX_DESC_NUM                   (GMAC_RX_BUF_NUM)                    /* Number of RX descriptors AND buffers.                                */

#define GMAC_GMII_ADDR_SHIFT                (11)
#define GMAC_GMII_ADDR_MSK                  (0x1Fu << 11)
#define GMAC_GMII_PHYREG_SHIFT              (6)
#define GMAC_GMII_PHYREG_MSK                (0x1Fu << 6)
#define GMAC_GMII_MDC_SHIFT                 (2)
#define GMAC_GMII_MDC_MSK                   (0xFu << 2)
#define GMAC_GMII_WRITE                     (0x1u << 1)
#define GMAC_GMII_BUSY                      (0x1u)

/* 发送描述符字段0参数字段宏 */
#define GMAC_DESC_TXSTS_OWNBYDMAMASK        (0x1u << 31)
#define GMAC_DESC_TXSTS_OWNBYDMA		    (0x1u << 31)         /**< own 所有权设置 */
#define GMAC_DESC_TXSTS_TXINT               (0x1u << 30)         /**< 设置发送完成后产生中断 */
#define GMAC_DESC_TXSTS_TXLAST		        (0x1u << 29)         /**< 发送的最后一个描述符*/
#define GMAC_DESC_TXSTS_TXFIRST             (0x1u << 28)         /**< 发送的第一个描述符 */
#define GMAC_DESC_TXSTS_TXCRCDIS            (0x1u << 27)         /**< 禁止CRC */
#define GMAC_DESC_TXSTS_TXPADDIS            (0x1u << 26)         /**< 禁止自动填充 */
#define GMAC_DESC_TXSTS_TXCHECKINSCTRL	    (0x3u << 22)         /**< 校验和插入控制 */
#define GMAC_DESC_TXSTS_TXRINGEND           (0x1u << 21)         /**< 最后一个描述符*/
#define GMAC_DESC_TXSTS_TXCHAIN	            (0x1u << 20)         /**< RING CHAIN 描述符选择 */

/* 发送描述符控制字段宏 */
#define GMAC_DESC_TXCTRL_SIZE1SHFT		    (0)
#define GMAC_DESC_TXCTRL_SIZE1MASK		    (0x1FFFu << 0)
#define GMAC_DESC_TXCTRL_SIZE2SHFT		    (16)
#define GMAC_DESC_TXCTRL_SIZE2MASK		    (0x1FFFu << 16)

/* 接收描述符状态字段宏 */
#define GMAC_DESC_RXSTS_OWNBYDMAMASK	    (0x1u << 31)
#define GMAC_DESC_RXSTS_OWNBYDMA	     	(0x1u << 31)         /**< own所有权 */
#define GMAC_DESC_RXSTS_FRAMELENSHFT        (16)
#define GMAC_DESC_RXSTS_FRAMELENMASK   	    (0x3FFFu << 16)
#define GMAC_DESC_RXSTS_RXFIRST       	    (0x1u << 9)
#define GMAC_DESC_RXSTS_RXLAST       	    (0x1u << 8)
#define GMAC_DESC_RXSTS_RXES       	        (0x1u << 15)//ES: Error Summary
#define GMAC_DESC_RXSTS_RXCE       	        (0x1u << 1) //CE: CRC Error
#define GMAC_DESC_RXSTS_VLAN       	        (0x1u << 10)//VLAN: VLAN Tag

/* 接收描述符控制字段宏 */
#define GMAC_DESC_RXCTRL_RXRINGEND		    (0x1u << 15)
#define GMAC_DESC_RXCTRL_RXCHAIN	     	(0x1u << 14)
#define GMAC_DESC_RXCTRL_DIC	     	    (0x1u << 31)//DIC: Disable Interrupt on Completion
#define GMAC_DESC_RXCTRL_SIZE1SHFT	        (0)
#define GMAC_DESC_RXCTRL_SIZE1MASK	        (0x1FFFu << 0)
#define GMAC_DESC_RXCTRL_SIZE2SHFT          (16)
#define GMAC_DESC_RXCTRL_SIZE2MASK	        (0x1FFFu << 16)

/* busmode寄存器宏 */
#define GMAC_BUSMODE_PBL_SHFT               (8)
#define GMAC_BUSMODE_PBL_MASK               (0x3Fu << 8) 
#define GMAC_BUSMODE_SRST_MASK              (0x1u << 0)
#define GMAC_BUSMODE_SRST		            (0x1u << 0)



/* 中断状态定义 */
#define GMAC_DMA_STATUS_INT_NIS             (0x00010000u) //NIS: Normal Interrupt Summary
#define GMAC_DMA_STATUS_INT_TX              (0x00000001u) //TI: Transmit Interrupt
#define GMAC_DMA_STATUS_INT_RX              (0x00000040u) //RI: Receive Interrupt
#define GMAC_DMA_STATUS_INT_ETI             (0x1u << 10)  //ETI: Early Transmit Interrupt
#define GMAC_DMA_STATUS_INT_ERI             (0x1u << 14)  //ERI: Early Receive Interrupt

#define GMAC_DMA_STATUS_INT_AIS             (0x1u << 15)  //AIS: Abnormal Interrupt Summary
#define GMAC_DMA_STATUS_INT_FBI             (0x1u << 13)  //FBI: Fatal Bus Error Interrupt
#define GMAC_DMA_STATUS_ERR_TxDMA           (0x1u << 23) // /b1 Error during data transfer by TxDMA
                                                    								// //1’b0 Error during data transfer by RxDMA
#define GMAC_DMA_STATUS_INT_RWT             (0x1u << 9)   //RWT: Receive Watchdog Timeout
#define GMAC_DMA_STATUS_INT_OVF             (0x1u << 4)   //OVF: Receive Overflow

#if(GMAC_PHY_USE == GMAC_PHY_88E1111)
//#if defined(GMAC_PHY_88E1111)
#define PHY_88E1111_MAX_NUM                 (32)

#define GMAC_PHY_88E1111_ID                 (0x0141)

#define PHY_88E1111_PHYIDR1		            (0x0002)
#define PHY_88E1111_PHYIDR2		            (0x0003)

#define GMAC_PHY_LED_CTRL_REG               (24)
#define GMAC_PHY_LED_MANUAL_REG             (25)
#define GMAC_PHY_LED_MAX             		(12)


#endif

#if defined(GMAC_PHY_88E1119)
#define PHY_88E1119_MAX_NUM                 (32)

#define GMAC_PHY_88E1119_ID                 (0x0141)

#define PHY_88E1119_PHYIDR1		            (0x0002)
#define PHY_88E1119_PHYIDR2		            (0x0003)
#endif

#if defined(GMAC_PHY_RTL8201CP)
#define RTL8201_PHY_MAX_NUM                 (32)

#define RTL8201_PHY_ID                      (0x8201)

#define RTL8201_PHYIDR1		                (0x0002)
#define RTL8201_PHYIDR2		                (0x0003)
#endif

//#if defined(GMAC_PHY_IP101A)
#if(GMAC_PHY_USE == GMAC_PHY_IP101A)
#define IP101A_PHY_MAX_NUM                 (18)

#define IP101A_PHY_ID                      (0x0243)
//#define IP101A_PHY_ID2                      (0x0C50)
#endif


/* Generic MII registers. */

#define MII_BMCR	    0x00	/* Basic mode control register */
#define MII_BMSR	    0x01	/* Basic mode status register  */
#define MII_PHYSID1	    0x02	/* PHYS ID 1		       */
#define MII_PHYSID2	    0x03	/* PHYS ID 2		       */
#define MII_ADVERTISE	    0x04	/* Advertisement control reg   */
#define MII_LPA		    0x05	/* Link partner ability reg    */
#define MII_EXPANSION	    0x06	/* Expansion register	       */
#define MII_CTRL1000	    0x09	/* 1000BASE-T control	       */
#define MII_STAT1000	    0x0a	/* 1000BASE-T status	       */
#define MII_ESTATUS	    0x0f	/* Extended Status */
#define MII_DCOUNTER	    0x12	/* Disconnect counter	       */
#define MII_FCSCOUNTER	    0x13	/* False carrier counter       */
#define MII_NWAYTEST	    0x14	/* N-way auto-neg test reg     */
#define MII_RERRCOUNTER     0x15	/* Receive error counter       */
#define MII_SREVISION	    0x16	/* Silicon revision	       */
#define MII_RESV1	    0x17	/* Reserved...		       */
#define MII_LBRERROR	    0x18	/* Lpback, rx, bypass error    */
#define MII_PHYADDR	    0x19	/* PHY address		       */
#define MII_RESV2	    0x1a	/* Reserved...		       */
#define MII_TPISTATUS	    0x1b	/* TPI status for 10mbps       */
#define MII_NCONFIG	    0x1c	/* Network interface config    */

/* Basic mode control register. */
#define BMCR_RESV		0x003f	/* Unused...		       */
#define BMCR_SPEED1000		0x0040	/* MSB of Speed (1000)	       */
#define BMCR_CTST		0x0080	/* Collision test	       */
#define BMCR_FULLDPLX		0x0100	/* Full duplex		       */
#define BMCR_ANRESTART		0x0200	/* Auto negotiation restart    */
#define BMCR_ISOLATE		0x0400	/* Disconnect DP83840 from MII */
#define BMCR_PDOWN		0x0800	/* Powerdown the DP83840       */
#define BMCR_ANENABLE		0x1000	/* Enable auto negotiation     */
#define BMCR_SPEED100		0x2000	/* Select 100Mbps	       */
#define BMCR_LOOPBACK		0x4000	/* TXD loopback bits	       */
#define BMCR_RESET		0x8000	/* Reset the DP83840	       */

/* Basic mode status register. */
#define BMSR_ERCAP		0x0001	/* Ext-reg capability	       */
#define BMSR_JCD		0x0002	/* Jabber detected	       */
#define BMSR_LSTATUS		0x0004	/* Link status		       */
#define BMSR_ANEGCAPABLE	0x0008	/* Able to do auto-negotiation */
#define BMSR_RFAULT		0x0010	/* Remote fault detected       */
#define BMSR_ANEGCOMPLETE	0x0020	/* Auto-negotiation complete   */
#define BMSR_RESV		0x00c0	/* Unused...		       */
#define BMSR_ESTATEN		0x0100	/* Extended Status in R15 */
#define BMSR_100HALF2		0x0200	/* Can do 100BASE-T2 HDX */
#define BMSR_100FULL2		0x0400	/* Can do 100BASE-T2 FDX */
#define BMSR_10HALF		0x0800	/* Can do 10mbps, half-duplex  */
#define BMSR_10FULL		0x1000	/* Can do 10mbps, full-duplex  */
#define BMSR_100HALF		0x2000	/* Can do 100mbps, half-duplex */
#define BMSR_100FULL		0x4000	/* Can do 100mbps, full-duplex */
#define BMSR_100BASE4		0x8000	/* Can do 100mbps, 4k packets  */

/* Advertisement control register. */
#define ADVERTISE_SLCT		0x001f	/* Selector bits	       */
#define ADVERTISE_CSMA		0x0001	/* Only selector supported     */
#define ADVERTISE_10HALF	0x0020	/* Try for 10mbps half-duplex  */
#define ADVERTISE_1000XFULL	0x0020	/* Try for 1000BASE-X full-duplex */
#define ADVERTISE_10FULL	0x0040	/* Try for 10mbps full-duplex  */
#define ADVERTISE_1000XHALF	0x0040	/* Try for 1000BASE-X half-duplex */
#define ADVERTISE_100HALF	0x0080	/* Try for 100mbps half-duplex */
#define ADVERTISE_1000XPAUSE	0x0080	/* Try for 1000BASE-X pause    */
#define ADVERTISE_100FULL	0x0100	/* Try for 100mbps full-duplex */
#define ADVERTISE_1000XPSE_ASYM 0x0100	/* Try for 1000BASE-X asym pause */
#define ADVERTISE_100BASE4	0x0200	/* Try for 100mbps 4k packets  */
#define ADVERTISE_PAUSE_CAP	0x0400	/* Try for pause	       */
#define ADVERTISE_PAUSE_ASYM	0x0800	/* Try for asymetric pause     */
#define ADVERTISE_RESV		0x1000	/* Unused...		       */
#define ADVERTISE_RFAULT	0x2000	/* Say we can detect faults    */
#define ADVERTISE_LPACK		0x4000	/* Ack link partners response  */
#define ADVERTISE_NPAGE		0x8000	/* Next page bit	       */

#define ADVERTISE_FULL (ADVERTISE_100FULL | ADVERTISE_10FULL | \
			ADVERTISE_CSMA)
#define ADVERTISE_ALL (ADVERTISE_10HALF | ADVERTISE_10FULL | \
		       ADVERTISE_100HALF | ADVERTISE_100FULL)

/* Link partner ability register. */
#define LPA_SLCT		0x001f	/* Same as advertise selector  */
#define LPA_10HALF		0x0020	/* Can do 10mbps half-duplex   */
#define LPA_1000XFULL		0x0020	/* Can do 1000BASE-X full-duplex */
#define LPA_10FULL		0x0040	/* Can do 10mbps full-duplex   */
#define LPA_1000XHALF		0x0040	/* Can do 1000BASE-X half-duplex */
#define LPA_100HALF		0x0080	/* Can do 100mbps half-duplex  */
#define LPA_1000XPAUSE		0x0080	/* Can do 1000BASE-X pause     */
#define LPA_100FULL		0x0100	/* Can do 100mbps full-duplex  */
#define LPA_1000XPAUSE_ASYM	0x0100	/* Can do 1000BASE-X pause asym*/
#define LPA_100BASE4		0x0200	/* Can do 100mbps 4k packets   */
#define LPA_PAUSE_CAP		0x0400	/* Can pause		       */
#define LPA_PAUSE_ASYM		0x0800	/* Can pause asymetrically     */
#define LPA_RESV		0x1000	/* Unused...		       */
#define LPA_RFAULT		0x2000	/* Link partner faulted        */
#define LPA_LPACK		0x4000	/* Link partner acked us       */
#define LPA_NPAGE		0x8000	/* Next page bit	       */

#define LPA_DUPLEX		(LPA_10FULL | LPA_100FULL)
#define LPA_100			(LPA_100FULL | LPA_100HALF | LPA_100BASE4)

/* Expansion register for auto-negotiation. */
#define EXPANSION_NWAY		0x0001	/* Can do N-way auto-nego      */
#define EXPANSION_LCWP		0x0002	/* Got new RX page code word   */
#define EXPANSION_ENABLENPAGE	0x0004	/* This enables npage words    */
#define EXPANSION_NPCAPABLE	0x0008	/* Link partner supports npage */
#define EXPANSION_MFAULTS	0x0010	/* Multiple faults detected    */
#define EXPANSION_RESV		0xffe0	/* Unused...		       */

#define ESTATUS_1000_XFULL	0x8000	/* Can do 1000BX Full */
#define ESTATUS_1000_XHALF	0x4000	/* Can do 1000BX Half */
#define ESTATUS_1000_TFULL	0x2000	/* Can do 1000BT Full */
#define ESTATUS_1000_THALF	0x1000	/* Can do 1000BT Half */


/* 结构体类型: GMAC 模块寄存器组 */
typedef struct
{
    volatile WORD32 dwMacConfig;                                          /**<  MAC 配置寄存器 offset:0x0000  */
    volatile WORD32 dwMacFrameFilter;                                     /**<  MAC帧过滤寄存器 offset:0x0004 */
    volatile WORD32 dwRsv[2];                                             /**<  保留 offset:0x0008~0x00C     */
    volatile WORD32 dwGMIIAddr;                                           /**<   offset:0x0010 */
    volatile WORD32 dwGMIIData;                                           /**<   offset:0x0014 */
    volatile WORD32 dwRsv1[1]; 
    volatile WORD32 VlanTagReg;                                           /**<   offset:0x001C */ 
    volatile WORD32 dwRsv2[1]; 
    volatile WORD32 dwDebugReg;                                           /**<   offset:0x0024 */
    volatile WORD32 dwRsv3[4]; 
    volatile WORD32 dwIntStatus;                                          /**<  中断状态 offset:0x0038        */
    volatile WORD32 dwIntMask;                                            /**<  中断屏蔽 offset:0x003c        */
    volatile WORD32 dwMacAddr0High;                                       /**<  MAC ADDRESS0地址高位 offset:0x0040 */
    volatile WORD32 dwMacAddr0Low;                                        /**<  MAC ADDRESS0地址低位 offset:0x0044 */
    volatile WORD32 dwMacAddr1High;                                       /**<  MAC ADDRESS1地址高位 offset:0x0048 */
    volatile WORD32 dwMacAddr1Low;                                        /**<  MAC ADDRESS1地址低位 offset:0x004C */
    volatile WORD32 dwMacAddr2High;                                       /**<  MAC ADDRESS2地址高位 offset:0x0050 */
    volatile WORD32 dwMacAddr2Low;                                        /**<  MAC ADDRESS2地址低位 offset:0x0054 */
}T_GMAC_GmacRegs;

/** 结构体类型: GMAC模块dma控制器寄存器组 */
typedef struct
{
  volatile WORD32 dwBusMode;                                              /**<  总线模式 offset:0x0000 */
  volatile WORD32 dwTxPollDemand;                                         /**<  发送查询请求 offset:0x0004 */
  volatile WORD32 dwRxPollDemand;                                         /**<  接收查询请求 offset:0x0008 */
  volatile WORD32 dwRxDesListAddr;                                        /**<  接收描述符列表首地址 offset:0x000C */
  volatile WORD32 dwTxDesListAddr;                                        /**<  发送描述符列表首地址 offset:0x0010 */
  volatile WORD32 dwStatus;                                               /**<  状态寄存器 offset:0x0014 */
  volatile WORD32 dwOpMode;                                               /**<  操作模式 offset:0x0018 */
  volatile WORD32 dwIntEnable;                                            /**<  中断使能 offset:0x001C */
  volatile WORD32 dwMissedFrameBufOverflowCnt;                            /**<  丢帧和缓冲区溢出计数器 offset:0x0020 */
  volatile WORD32 dwRxIntWtDogTimer;									  /*offset:0x0024 */
  volatile WORD32 dwAxiBusMode;  										  /*offset:0x0028 */	
  volatile WORD32 dwAxiStatus;											  /*offset:0x002c */
  volatile WORD32 dwRsv[6];                                               /**<  保留 offset:0x0030~0x0044 */
  volatile WORD32 dwCurHostTxDes;                                         /**<  当前主机发送描述符地址 offset:0x0048 */
  volatile WORD32 dwCurHostRxDes;                                         /**<  当前主机接收描述符地址 offset:0x004C */
} T_GMAC_DmaRegs;
   
/** 接收描述符结构体 */
typedef struct _T_GMAC_RxDescr
{    
   volatile WORD32 dwDescr0;                                              /**<  RDES0,offset:0x0 */
   volatile WORD32 dwDescr1;                                              /**<  RDES1,offset:0x4 */
   volatile WORD32 dwBuf1Addr;                                            /**<  RDES2,offset:0x8 */
   struct _T_GMAC_RxDescr *ptNext;                                        /**<  RDES3,offset:0xc */
}T_GMAC_RxDescr; 


/*发送描述符结构体*/
typedef struct _T_GMAC_TxDescr
{    
   volatile WORD32 dwDescr0;                                              /**<  TDES0,offset: 0x0 */
   volatile WORD32 dwDescr1;                                              /**<  TDES1,offset: 0x4 */
   volatile WORD32 dwBuf1Addr;                                            /**<  TDES2,offset: 0x8 */
   struct _T_GMAC_TxDescr *ptNext;                                        /**<  TDES3,offset: 0xc */
}T_GMAC_TxDescr;

//int zx_gmac_init(void);
//void zx_gmac_stop(void);
//int zx_gmac_send(BYTE  *const txdata,const WORD32 txlen);
int zx_gmac_getbd(WORD32 bdnum);
int zx_gmac_rx(BYTE *const prxbuf, WORD32 *const prxlen,WORD32 bdnum);
int gmac_phy_led(WORD32 tphyid,WORD32 ledctrl,WORD32 lednum);
void gmac_phy_setclock(WORD32 tphyid);
int gmac_phy_init(void);

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */
  
#endif  /* _DRV_GMAC_H */
      
/** @} */
      
