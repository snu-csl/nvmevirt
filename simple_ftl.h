// SPDX-License-Identifier: GPL-2.0-only

#ifndef _NVMEVIRT_SIMPLE_FTL_H
#define _NVMEVIRT_SIMPLE_FTL_H

#include "nvmev.h"

struct simple_ftl {
	struct ssd *ssd;
};

bool simple_proc_nvme_io_cmd(struct nvmev_ns *ns, struct nvmev_request *req,
			     struct nvmev_result *ret);
void simple_init_namespace(struct nvmev_ns *ns, uint32_t id, uint64_t size, void *mapped_addr,
			   uint32_t cpu_nr_dispatcher);
void simple_remove_namespace(struct nvmev_ns *ns);

#endif
