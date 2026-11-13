/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Embedded Data Section for 32-bit ARM (ZX297520V3 Cortex-A53, AArch32)
 */

.align	2
.section	DATA_SECT_NAME, "a"
.type	DATA_OBJ_NAME, %object
DATA_OBJ_NAME:
	.incbin DATA_OBJ_FILE
.equ	DATA_SIZE_NAME, . - DATA_OBJ_NAME
.size	DATA_OBJ_NAME, DATA_SIZE_NAME

.align	2
.section	DATA_SECT_FSPATH_NAME, "a"
.type	DATA_OBJ_FSPATH_NAME, %object
DATA_OBJ_FSPATH_NAME:
	.asciz DATA_OBJ_FSPATH
.size	DATA_OBJ_FSPATH_NAME, . - DATA_OBJ_FSPATH_NAME

.align	2
.section	UBOOT_LIST_SECT_NAME, "a"
.globl	UBOOT_LIST_NAME
.type	UBOOT_LIST_NAME, %object
UBOOT_LIST_NAME:
	.long	DATA_OBJ_FSPATH_NAME
	.long	DATA_SIZE_NAME
	.long	DATA_OBJ_NAME
.size	UBOOT_LIST_NAME, . - UBOOT_LIST_NAME
