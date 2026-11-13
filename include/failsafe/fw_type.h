/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Wifi Chan Lab
 * Platform: Sanechip ZX297520V3 (Cortex-A53, 32-bit AArch32)
 * Failsafe Firmware Partition Target Types
 */

#ifndef _FAILSAFE_FW_TYPE_H_
#define _FAILSAFE_FW_TYPE_H_

typedef enum {
	FW_TYPE_ROOTFS,   /* rootfs */
	FW_TYPE_IMAGEFS,  /* imagefs (kernel/system) */
	FW_TYPE_UBOOT,    /* uboot */
	FW_TYPE_NVROFS,   /* nvrofs */
	FW_TYPE_USERDATA, /* userdata */
	FW_TYPE_ZLOADER,  /* zloader */
	FW_TYPE_FULL,     /* full flash (raw) */
	FW_TYPE_CUSTOM,   /* custom partition or raw offset */
	/* Compatibility aliases */
	FW_TYPE_FW = FW_TYPE_IMAGEFS,
	FW_TYPE_BOOT = FW_TYPE_UBOOT,
	FW_TYPE_NV = FW_TYPE_NVROFS,
	FW_TYPE_LOADER = FW_TYPE_ZLOADER,
} failsafe_fw_t;

#endif /* _FAILSAFE_FW_TYPE_H_ */
