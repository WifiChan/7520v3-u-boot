// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Wifi Chan Lab
 * Sanechip ZX297520V3 GMAC Ethernet Driver
 */

#include <dm.h>
#include <net.h>
#include <cpu_func.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <asm/arch-zx297520v3/gmac.h>

#undef GMAC_RX_DESC_NUM
#undef GMAC_TX_DESC_NUM
#define GMAC_RX_DESC_NUM        8
#define GMAC_TX_DESC_NUM        8
#define GMAC_BUF_SIZE           1536

struct zx_gmac_priv {
	T_GMAC_GmacRegs *mac;
	T_GMAC_DmaRegs *dma;
	u8 rx_buf[GMAC_RX_DESC_NUM * GMAC_BUF_SIZE] __aligned(8);
	u8 tx_buf[GMAC_TX_DESC_NUM * GMAC_BUF_SIZE] __aligned(8);
	T_GMAC_RxDescr rx_desc[GMAC_RX_DESC_NUM] __aligned(8);
	T_GMAC_TxDescr tx_desc[GMAC_TX_DESC_NUM] __aligned(8);
	u32 rx_idx;
	u32 tx_idx;
};

static void zx_gmac_init_rings(struct zx_gmac_priv *priv)
{
	int i;

	priv->tx_idx = 0;
	for (i = 0; i < GMAC_TX_DESC_NUM; i++) {
		priv->tx_desc[i].dwDescr0 = GMAC_ENDIAN32_SWAP(GMAC_DESC_TXSTS_TXINT | GMAC_DESC_TXSTS_TXCHAIN);
		priv->tx_desc[i].dwDescr1 = GMAC_ENDIAN32_SWAP(GMAC_BUF_SIZE & GMAC_DESC_TXCTRL_SIZE1MASK);
		priv->tx_desc[i].dwBuf1Addr = GMAC_ENDIAN32_SWAP((u32)&priv->tx_buf[i * GMAC_BUF_SIZE]);
		if (i == GMAC_TX_DESC_NUM - 1) {
			priv->tx_desc[i].ptNext = (T_GMAC_TxDescr *)GMAC_ENDIAN32_SWAP((u32)&priv->tx_desc[0]);
			priv->tx_desc[i].dwDescr0 |= GMAC_ENDIAN32_SWAP(GMAC_DESC_TXSTS_TXRINGEND);
		} else {
			priv->tx_desc[i].ptNext = (T_GMAC_TxDescr *)GMAC_ENDIAN32_SWAP((u32)&priv->tx_desc[i + 1]);
		}
	}

	priv->rx_idx = 0;
	for (i = 0; i < GMAC_RX_DESC_NUM; i++) {
		priv->rx_desc[i].dwDescr0 = GMAC_ENDIAN32_SWAP(GMAC_DESC_RXSTS_OWNBYDMA);
		priv->rx_desc[i].dwDescr1 = GMAC_ENDIAN32_SWAP((GMAC_BUF_SIZE & GMAC_DESC_RXCTRL_SIZE1MASK) |
							       GMAC_DESC_RXCTRL_RXCHAIN);
		priv->rx_desc[i].dwBuf1Addr = GMAC_ENDIAN32_SWAP((u32)&priv->rx_buf[i * GMAC_BUF_SIZE]);
		if (i == GMAC_RX_DESC_NUM - 1) {
			priv->rx_desc[i].ptNext = (T_GMAC_RxDescr *)GMAC_ENDIAN32_SWAP((u32)&priv->rx_desc[0]);
			priv->rx_desc[i].dwDescr1 |= GMAC_ENDIAN32_SWAP(GMAC_DESC_RXCTRL_RXRINGEND);
		} else {
			priv->rx_desc[i].ptNext = (T_GMAC_RxDescr *)GMAC_ENDIAN32_SWAP((u32)&priv->rx_desc[i + 1]);
		}
	}
}

static int zx_gmac_start(struct udevice *dev)
{
	struct zx_gmac_priv *priv = dev_get_priv(dev);
	int timeout;

	/* Software reset GMAC DMA */
	priv->dma->dwBusMode = GMAC_BUSMODE_SRST;
	zx_gmac_init_rings(priv);

	priv->mac->dwMacConfig = GMAC_POART_MODE;

	timeout = 1000;
	while ((priv->dma->dwBusMode & GMAC_BUSMODE_SRST_MASK) == GMAC_BUSMODE_SRST && timeout--) {
		priv->mac->dwMacConfig = GMAC_POART_MODE;
		udelay(100);
	}

	/* Set PBL */
	priv->dma->dwBusMode &= ~GMAC_BUSMODE_PBL_MASK;
	priv->dma->dwBusMode |= (GMAC_BUSMODE_PBL_8 & GMAC_BUSMODE_PBL_MASK);

	/* Set descriptor ring base addresses */
	priv->dma->dwRxDesListAddr = (u32)&priv->rx_desc[0];
	priv->dma->dwTxDesListAddr = (u32)&priv->tx_desc[0];

	/* Clear status and enable DMA interrupts */
	priv->dma->dwStatus = 0x0001ffff;
	priv->dma->dwIntEnable = (GMAC_INT_TIE | GMAC_INT_RIE | GMAC_INT_NIE);

	while (priv->mac->dwGMIIAddr & GMAC_GMII_BUSY)
		;

	/* Configure MAC speed and duplex */
	priv->mac->dwMacConfig |= (GMAC_MAC_100M | GMAC_MAC_NORMAL | GMAC_MAC_DUPLEX_FULL);
	priv->mac->dwMacFrameFilter &= ~GMAC_MAC_RCVALL_EN;
	priv->mac->dwMacFrameFilter |= GMAC_MAC_RCVALL_EN;

	/* Start DMA RX and TX operations */
	priv->dma->dwOpMode |= (1 << 1);  /* enable DMA rx */
	priv->dma->dwOpMode |= (1 << 13); /* enable DMA tx */
	priv->dma->dwOpMode |= (1 << 3);  /* rx store and forward */
	priv->dma->dwOpMode |= (1 << 4);  /* forward undersized good frames */
	priv->dma->dwOpMode |= (1 << 14); /* tx store and forward */

	/* Enable MAC RX and TX */
	priv->mac->dwMacConfig |= (1 << 3); /* TE: Transmitter Enable */
	priv->mac->dwMacConfig |= (1 << 2); /* RE: Receiver Enable */

	return 0;
}

static int zx_gmac_send(struct udevice *dev, void *packet, int length)
{
	struct zx_gmac_priv *priv = dev_get_priv(dev);
	T_GMAC_TxDescr *ptTmp;
	int timeout = 10000;

	if (length > GMAC_BUF_SIZE)
		return -EINVAL;

	ptTmp = &priv->tx_desc[priv->tx_idx];

	if ((GMAC_ENDIAN32_SWAP(ptTmp->dwDescr0) & GMAC_DESC_TXSTS_OWNBYDMAMASK) == GMAC_DESC_TXSTS_OWNBYDMA)
		return -EBUSY;

	memcpy((void *)GMAC_ENDIAN32_SWAP(ptTmp->dwBuf1Addr), packet, length);

	ptTmp->dwDescr1 &= GMAC_ENDIAN32_SWAP(~GMAC_DESC_TXCTRL_SIZE1MASK);
	ptTmp->dwDescr1 |= GMAC_ENDIAN32_SWAP(length & GMAC_DESC_TXCTRL_SIZE1MASK);
	ptTmp->dwDescr0 |= GMAC_ENDIAN32_SWAP(GMAC_DESC_TXSTS_TXFIRST | GMAC_DESC_TXSTS_TXLAST | GMAC_DESC_TXSTS_OWNBYDMA);

	/* Trigger TX poll */
	priv->dma->dwTxPollDemand = 1;

	/* Wait until DMA releases ownership */
	while ((GMAC_ENDIAN32_SWAP(ptTmp->dwDescr0) & GMAC_DESC_TXSTS_OWNBYDMAMASK) == GMAC_DESC_TXSTS_OWNBYDMA && timeout--) {
		udelay(10);
	}

	priv->tx_idx = (priv->tx_idx + 1) % GMAC_TX_DESC_NUM;

	return (timeout > 0) ? 0 : -ETIMEDOUT;
}

static int zx_gmac_recv(struct udevice *dev, int flags, uchar **packetp)
{
	struct zx_gmac_priv *priv = dev_get_priv(dev);
	T_GMAC_RxDescr *ptTmp;
	u32 status;
	int length;

	ptTmp = &priv->rx_desc[priv->rx_idx];
	status = GMAC_ENDIAN32_SWAP(ptTmp->dwDescr0);

	if ((status & GMAC_DESC_RXSTS_OWNBYDMAMASK) == GMAC_DESC_RXSTS_OWNBYDMA)
		return -EAGAIN;

	if (status & GMAC_DESC_RXSTS_RXES) {
		/* Error packet, drop and recycle */
		ptTmp->dwDescr0 = GMAC_ENDIAN32_SWAP(GMAC_DESC_RXSTS_OWNBYDMA);
		priv->rx_idx = (priv->rx_idx + 1) % GMAC_RX_DESC_NUM;
		return -EAGAIN;
	}

	length = ((status & GMAC_DESC_RXSTS_FRAMELENMASK) >> GMAC_DESC_RXSTS_FRAMELENSHFT) - 4;
	if (length <= 0 || length > GMAC_BUF_SIZE) {
		ptTmp->dwDescr0 = GMAC_ENDIAN32_SWAP(GMAC_DESC_RXSTS_OWNBYDMA);
		priv->rx_idx = (priv->rx_idx + 1) % GMAC_RX_DESC_NUM;
		return -EAGAIN;
	}

	*packetp = (uchar *)GMAC_ENDIAN32_SWAP(ptTmp->dwBuf1Addr);
	return length;
}

static int zx_gmac_free_pkt(struct udevice *dev, uchar *packet, int length)
{
	struct zx_gmac_priv *priv = dev_get_priv(dev);
	T_GMAC_RxDescr *ptTmp = &priv->rx_desc[priv->rx_idx];

	ptTmp->dwDescr0 = GMAC_ENDIAN32_SWAP(GMAC_DESC_RXSTS_OWNBYDMA);

	/* If DMA receiver suspended, poll demand */
	if (((priv->dma->dwStatus >> 17) & 0x7) == 0x4)
		priv->dma->dwRxPollDemand = 1;

	priv->rx_idx = (priv->rx_idx + 1) % GMAC_RX_DESC_NUM;
	return 0;
}

static void zx_gmac_stop(struct udevice *dev)
{
	struct zx_gmac_priv *priv = dev_get_priv(dev);

	priv->dma->dwOpMode &= ~((1 << 1) | (1 << 13));
	priv->mac->dwMacConfig &= ~((1 << 2) | (1 << 3));
	priv->dma->dwIntEnable = 0x00;

	priv->dma->dwOpMode |= (1 << 20); /* clear TX FIFO */
	while (priv->dma->dwOpMode & (1 << 20))
		;
}

static int zx_gmac_write_hwaddr(struct udevice *dev)
{
	struct zx_gmac_priv *priv = dev_get_priv(dev);
	struct eth_pdata *pdata = dev_get_plat(dev);
	u32 mac_high, mac_low;

	mac_high = (pdata->enetaddr[5] << 8) | pdata->enetaddr[4];
	mac_low  = (pdata->enetaddr[3] << 24) | (pdata->enetaddr[2] << 16) |
		   (pdata->enetaddr[1] << 8) | pdata->enetaddr[0];

	priv->mac->dwMacAddr0High = mac_high;
	priv->mac->dwMacAddr0Low  = mac_low;

	return 0;
}

static int zx_gmac_probe(struct udevice *dev)
{
	struct zx_gmac_priv *priv = dev_get_priv(dev);

	priv->mac = (T_GMAC_GmacRegs *)GMAC_MAC_ADDR_BASE;
	priv->dma = (T_GMAC_DmaRegs *)GMAC_DMA_ADDR_BASE;

	return 0;
}

static const struct eth_ops zx_gmac_ops = {
	.start        = zx_gmac_start,
	.send         = zx_gmac_send,
	.recv         = zx_gmac_recv,
	.free_pkt     = zx_gmac_free_pkt,
	.stop         = zx_gmac_stop,
	.write_hwaddr = zx_gmac_write_hwaddr,
};

static const struct udevice_id zx_gmac_ids[] = {
	{ .compatible = "sanechips,zx297520v3-gmac" },
	{ }
};

U_BOOT_DRIVER(zx_gmac) = {
	.name      = "zx_gmac",
	.id        = UCLASS_ETH,
	.of_match  = zx_gmac_ids,
	.probe     = zx_gmac_probe,
	.ops       = &zx_gmac_ops,
	.priv_auto = sizeof(struct zx_gmac_priv),
	.plat_auto = sizeof(struct eth_pdata),
};
