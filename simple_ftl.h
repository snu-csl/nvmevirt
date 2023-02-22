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

#ifndef _NVMEVIRT_SIMPLE_FTL_H
#define _NVMEVIRT_SIMPLE_FTL_H

#include "nvmev.h"

struct simple_ftl {
	struct ssd *ssd;
};

bool simple_proc_nvme_io_cmd(struct nvmev_ns *ns, struct nvmev_request *req, struct nvmev_result *ret);
void simple_init_namespace(struct nvmev_ns *ns, uint32_t id,  uint64_t size, void *mapped_addr, uint32_t cpu_nr_dispatcher);

#endif
