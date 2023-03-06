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

#ifndef _APPEND_ONLY_H_
#define _APPEND_ONLY_H_

#include <linux/types.h>

int append_only_allocator_init(u64 size);
size_t append_only_allocate(u64 length, void* args);
void append_only_kill(void);

#endif