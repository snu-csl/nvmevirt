// SPDX-License-Identifier: GPL-2.0-only

#ifndef _APPEND_ONLY_H_
#define _APPEND_ONLY_H_

#include <linux/types.h>

int append_only_allocator_init(u64 size);
size_t append_only_allocate(u64 length, void *args);
void append_only_kill(void);

#endif
