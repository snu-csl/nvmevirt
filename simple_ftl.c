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

#include <linux/ktime.h>
#include <linux/sched/clock.h>

#include "nvmev.h"

extern struct nvmev_dev *vdev;

static inline unsigned long long __get_wallclock(void)
{
	return cpu_clock(vdev->config.cpu_nr_dispatcher);
}

static size_t __cmd_io_size(struct nvme_rw_command *cmd)
{
	NVMEV_DEBUG("%d lba %llu length %d, %llx %llx\n", cmd->opcode, cmd->slba, cmd->length,
		    cmd->prp1, cmd->prp2);

	return (cmd->length + 1) << 9;
}

/* Return the time to complete */
static unsigned long long __schedule_io_units(int opcode, unsigned long lba, unsigned int length,
					      unsigned long long nsecs_start)
{
	unsigned int io_unit_size = 1 << vdev->config.io_unit_shift;
	unsigned int io_unit = (lba >> (vdev->config.io_unit_shift - 9)) % vdev->config.nr_io_units;
	int nr_io_units = min(vdev->config.nr_io_units, DIV_ROUND_UP(length, io_unit_size));

	unsigned long long latest; /* Time of completion */
	unsigned int delay = 0;
	unsigned int latency = 0;
	unsigned int trailing = 0;

	if (opcode == nvme_cmd_write) {
		delay = vdev->config.write_delay;
		latency = vdev->config.write_time;
		trailing = vdev->config.write_trailing;
	} else if (opcode == nvme_cmd_read) {
		delay = vdev->config.read_delay;
		latency = vdev->config.read_time;
		trailing = vdev->config.read_trailing;
	}

	latest = max(nsecs_start, vdev->io_unit_stat[io_unit]) + delay;

	do {
		latest += latency;
		vdev->io_unit_stat[io_unit] = latest;

		if (nr_io_units-- > 0) {
			vdev->io_unit_stat[io_unit] += trailing;
		}

		length -= min(length, io_unit_size);
		if (++io_unit >= vdev->config.nr_io_units)
			io_unit = 0;
	} while (length > 0);

	return latest;
}

static unsigned long long __schedule_flush(struct nvmev_request *req)
{
	unsigned long long latest = 0;
	int i;

	for (i = 0; i < vdev->config.nr_io_units; i++) {
		latest = max(latest, vdev->io_unit_stat[i]);
	}

	return latest;
}

bool simple_proc_nvme_io_cmd(struct nvmev_ns *ns, struct nvmev_request *req,
			     struct nvmev_result *ret)
{
	struct nvme_command *cmd = req->cmd;

	NVMEV_ASSERT(ns->csi == NVME_CSI_NVM);
	BUG_ON(BASE_SSD != INTEL_OPTANE);

	switch (cmd->common.opcode) {
	case nvme_cmd_write:
	case nvme_cmd_read:
		ret->nsecs_target = __schedule_io_units(
			cmd->common.opcode, cmd->rw.slba,
			__cmd_io_size((struct nvme_rw_command *)cmd), __get_wallclock());
		break;
	case nvme_cmd_flush:
		ret->nsecs_target = __schedule_flush(req);
		break;
	case nvme_cmd_write_uncor:
	case nvme_cmd_compare:
	case nvme_cmd_write_zeroes:
	case nvme_cmd_dsm:
	case nvme_cmd_resv_register:
	case nvme_cmd_resv_report:
	case nvme_cmd_resv_acquire:
	case nvme_cmd_resv_release:
	default:
		break;
	}

	return true;
}

void simple_init_namespace(struct nvmev_ns *ns, uint32_t id, uint64_t size, void *mapped_addr,
			   uint32_t cpu_nr_dispatcher)
{
	ns->id = id;
	ns->csi = NVME_CSI_NVM;
	ns->size = size;
	ns->mapped = mapped_addr;
	ns->proc_io_cmd = simple_proc_nvme_io_cmd;

	return;
}
