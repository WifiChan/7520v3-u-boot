// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Wifi Chan Lab
 * Platform: Sanechip ZX297520V3 (Cortex-A53, 32-bit AArch32)
 * Embedded Virtual Filesystem Lookup for Web Failsafe Assets
 */

#include <linux/string.h>
#include <linker_lists.h>
#include "fs.h"

const struct fs_desc *fs_find_file(const char *path)
{
	struct fs_desc *start = ll_entry_start(struct fs_desc, fs);
	int len = ll_entry_count(struct fs_desc, fs);

	while (len) {
		if (start->path && !strcmp(start->path, path))
			return start;

		len--;
		start++;
	}

	return NULL;
}
