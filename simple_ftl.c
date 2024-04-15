// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ktime.h>
#include <linux/sched/clock.h>

#include "simple_ftl.h"

static inline unsigned long long __get_wallclock(void)
{
	return cpu_clock(nvmev_vdev->config.cpu_nr_dispatcher);
}

static size_t __cmd_io_size(struct nvme_rw_command *cmd)
{
	NVMEV_DEBUG_VERBOSE("[%c] %llu + %d, prp %llx %llx\n",
			cmd->opcode == nvme_cmd_write ? 'W' : 'R', cmd->slba, cmd->length,
		    cmd->prp1, cmd->prp2);

	return (cmd->length + 1) << LBA_BITS;
}

/* Return the time to complete */
static unsigned long long __schedule_io_units(int opcode, unsigned long lba, unsigned int length,
					      unsigned long long nsecs_start)
{
	unsigned int io_unit_size = 1 << nvmev_vdev->config.io_unit_shift;
	unsigned int io_unit =
		(lba >> (nvmev_vdev->config.io_unit_shift - LBA_BITS)) % nvmev_vdev->config.nr_io_units;
	int nr_io_units = min(nvmev_vdev->config.nr_io_units, DIV_ROUND_UP(length, io_unit_size));

	unsigned long long latest; /* Time of completion */
	unsigned int delay = 0;
	unsigned int latency = 0;
	unsigned int trailing = 0;

	if (opcode == nvme_cmd_write) {
		delay = nvmev_vdev->config.write_delay;
		latency = nvmev_vdev->config.write_time;
		trailing = nvmev_vdev->config.write_trailing;
	} else if (opcode == nvme_cmd_read) {
		delay = nvmev_vdev->config.read_delay;
		latency = nvmev_vdev->config.read_time;
		trailing = nvmev_vdev->config.read_trailing;
	}

	latest = max(nsecs_start, nvmev_vdev->io_unit_stat[io_unit]) + delay;

	do {
		latest += latency;
		nvmev_vdev->io_unit_stat[io_unit] = latest;

		if (nr_io_units-- > 0) {
			nvmev_vdev->io_unit_stat[io_unit] += trailing;
		}

		length -= min(length, io_unit_size);
		if (++io_unit >= nvmev_vdev->config.nr_io_units)
			io_unit = 0;
	} while (length > 0);

	return latest;
}

static unsigned long long __schedule_flush(struct nvmev_request *req)
{
	unsigned long long latest = 0;
	int i;

	for (i = 0; i < nvmev_vdev->config.nr_io_units; i++) {
		latest = max(latest, nvmev_vdev->io_unit_stat[i]);
	}

	return latest;
}

bool simple_proc_nvme_io_cmd(struct nvmev_ns *ns, struct nvmev_request *req,
			     struct nvmev_result *ret)
{
	struct nvme_command *cmd = req->cmd;

	BUG_ON(ns->csi != NVME_CSI_NVM);
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
	default:
		NVMEV_ERROR("%s: command not implemented: %s (0x%x)\n", __func__,
			    nvme_opcode_string(cmd->common.opcode), cmd->common.opcode);
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

void simple_remove_namespace(struct nvmev_ns *ns)
{
	// Nothing to do here
}
