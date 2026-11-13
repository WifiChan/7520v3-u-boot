/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Wifi Chan Lab
 * Platform: Sanechip ZX297520V3 (Cortex-A53, 32-bit AArch32)
 * Embedded Virtual Filesystem Header
 */

#ifndef _FAILSAFE_FS_H_
#define _FAILSAFE_FS_H_

#include <linux/types.h>

struct fs_desc {
	const char *path;
	size_t size;
	const void *data;
};

const struct fs_desc *fs_find_file(const char *path);

#endif /* _FAILSAFE_FS_H_ */
