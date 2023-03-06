/**********************************************************************
 * Copyright (c) 2020-2023
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTIABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 **********************************************************************/

#include "nvmev.h"
#include <linux/bitmap.h>
#include <linux/types.h>
#include <asm/bitops.h>
#include <linux/log2.h>
#include <linux/hashtable.h>
#include <linux/kernel.h>

static unsigned long long latest;
static unsigned long long dev_size;
static unsigned long long total_written;

int append_only_allocator_init(u64 size) {
	dev_size 	  = size;
	latest 		  = 0;
	total_written = 0;
	NVMEV_INFO("Initialized an append memory pool with size %llu", size);
	return 1;
}

size_t append_only_allocate(u64 length, void* args) {
	size_t ret = latest;
	latest 		  += length;	
	total_written += length;

	NVMEV_DEBUG("Returning offset %llu for length %llu", ret, length);

	if (latest + 65536 >= dev_size) {
		NVMEV_ERROR("append-only allocator is nearly full!!");
	}

	return ret;
}

void append_only_kill(void) {

}
