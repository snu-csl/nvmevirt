// SPDX-License-Identifier: GPL-2.0-only

#ifndef _BITMAP_H_
#define _BITMAP_H_

#include <linux/bitmap.h>
#include <linux/types.h>
#include <linux/bits.h>
#include <linux/bitops.h>
#include <linux/kernel.h>

#define SMALL_LENGTH 1024
#define LARGE_LENGTH 4096

int bitmap_allocator_init(u64 size);
size_t bitmap_allocate(u64 length, void *args);
void bitmap_kill(void);

#endif
