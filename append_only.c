// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitmap.h>
#include <asm/bitops.h>
#include <linux/log2.h>
#include <linux/hashtable.h>
#include <linux/kernel.h>

#include "nvmev.h"
#include "append_only.h"

static unsigned long long latest;
static unsigned long long dev_size;
static unsigned long long total_written;

int append_only_allocator_init(u64 size)
{
	dev_size = size;
	latest = 0;
	total_written = 0;
	NVMEV_INFO("Initialized an append memory pool with size %llu", size);
	return 1;
}

size_t append_only_allocate(u64 length, void *args)
{
	size_t ret = latest;
	latest += length;
	total_written += length;

	NVMEV_DEBUG("Returning offset %lu for length %llu", ret, length);

	if (latest + 65536 >= dev_size) {
		NVMEV_ERROR("append-only allocator is nearly full!!");
	}

	return ret;
}

void append_only_kill(void)
{
}
