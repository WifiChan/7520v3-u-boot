/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Stefan Dösinger
 * Stefan Dösinger <stefandoesinger@gmail.com>
 */

#include <asm-offsets.h>
#include <config.h>
#include <linux/linkage.h>
#include <asm/macro.h>
#include <asm/gic.h>
#include <asm/ptrace.h>

/* Copypaste from Linux's arm-gic-v3.h */
#define ICC_SRE_EL1_SRE              (1U << 0)
#define GICD_CTLR_ARE_NS             (1U << 4)
#define GICD_CTLR_DS                 (1U << 6)
#define ICC_SRE_EL2_ENABLE           (1 << 3)
#define ICC_SRE_EL2_SRE              (1 << 0)

	.globl zx_gic_secure_setup
zx_gic_secure_setup:
	@ zx297520v3 has essentially an ARMv8 CPU with a GICv3 and secure mode support, but there's
	@ no ARM TrustZone firmware. So we have to set up the CPU and GIC according to arm64 boot
	@ requirements specified in Documentation/arch/arm64/booting.rst.
	@ Leave r0, r1 and r2 alone to preserve the DTB pointer.

	@ This allows EL1 to handle ints hat are normally handled by EL2/3.
	ldr	r3, =0xf2000000
	ldr	r4, =(GICD_CTLR_ARE_NS | GICD_CTLR_DS)
	str	r4, [r3]

	cps	#MON_MODE

	@ Work in non-secure physical addres space: SCR_EL3.NS = 1. At least the UART seems to
	@ respond only to non-secure addresses. I have taken insipiration from Raspberry pi's
	@ armstub7.S here.
	@
	@ ARM docs say modify this bit in monitor mode only...
	mrc	p15, 0, r5, c1, c1, 0
	mov	r3, #0x131			@ non-secure
	mcr	p15, 0, r3, c1, c1, 0

	@ AP_PPI_MODE_REG: Configure timer PPIs (10, 11, 13, 14) to active-low.
	ldr	r3, =0xF22020a8
	ldr	r4, =0x50
	str	r4, [r3]
	ldr	r3, =0xF22020ac
	ldr	r4, =0x14
	str	r4, [r3]

	@ Enable EL access to ICC_SRE (bit 3, ICC_SRE_EL3.Enable). Enable system reg access to
	@ GICv3 registers (bit 0, ICC_SRE_EL3.SRE).
	mrc	p15, 6, r3, c12, c12, 5		@ ICC_SRE_EL3
	orr	r3, #0x9			@ FIXME: No defines for SRE_EL3 values?
	mcr	p15, 6, r3, c12, c12, 5
	mrc	p15, 0, r3, c12, c12, 5		@ ICC_SRE_EL1
	orr	r3, #(ICC_SRE_EL1_SRE)
	mcr	p15, 0, r3, c12, c12, 5

	@ Like ICC_SRE_EL3, enable non-secure access and allow register based access.
	mrc	p15, 4, r3, c12, c9, 5		@ ICC_SRE_EL2 aka ICC_HSRE
	orr	r3, r3, #(ICC_SRE_EL2_ENABLE | ICC_SRE_EL2_SRE)
	mcr	p15, 4, r3, c12, c9, 5
	isb

	cps	#SVC_MODE

	mov	pc, lr
