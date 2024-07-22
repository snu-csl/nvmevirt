// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kthread.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/highmem.h>
#include <linux/sched/clock.h>

#include "nvmev.h"
#include "ssd.h"
#include "zns_ftl.h"

static uint64_t __prp_transfer_data(uint64_t prp1, uint64_t prp2, void *buffer, uint64_t length,
				    uint32_t io)
{
	size_t offset;
	size_t remaining;
	size_t prp_offs = 0;
	size_t prp2_offs = 0;
	uint64_t paddr;
	uint64_t *paddr_list = NULL;
	size_t mem_offs = 0;

	offset = 0;
	remaining = length;

	while (remaining) {
		size_t io_size;
		void *vaddr;

		mem_offs = 0;
		prp_offs++;
		if (prp_offs == 1) {
			paddr = prp1;
		} else if (prp_offs == 2) {
			paddr = prp2;
			if (remaining > PAGE_SIZE) {
				paddr_list = kmap_atomic_pfn(PRP_PFN(paddr)) +
					     (paddr & PAGE_OFFSET_MASK);
				paddr = paddr_list[prp2_offs++];
			}
		} else {
			paddr = paddr_list[prp2_offs++];
		}

		vaddr = kmap_atomic_pfn(PRP_PFN(paddr));

		io_size = min_t(size_t, remaining, PAGE_SIZE);

		if (paddr & PAGE_OFFSET_MASK) {
			mem_offs = paddr & PAGE_OFFSET_MASK;
			if (io_size + mem_offs > PAGE_SIZE)
				io_size = PAGE_SIZE - mem_offs;
		}

		if (io == 0) // output (buffer -> prp)
			memcpy(vaddr + mem_offs, buffer + offset, io_size);
		else // input (prp -> buffer)
			memcpy(buffer + offset, vaddr + mem_offs, io_size);

		kunmap_atomic(vaddr);

		remaining -= io_size;
		offset += io_size;
	}

	if (paddr_list != NULL)
		kunmap_atomic(paddr_list);

	return length;
}

static void __fill_zone_report(struct zns_ftl *zns_ftl, struct nvme_zone_mgmt_recv *cmd,
			       struct zone_report *report)
{
	struct zone_descriptor *zone_descs = zns_ftl->zone_descs;
	uint64_t slba = cmd->slba;
	uint64_t start_zid = lba_to_zone(zns_ftl, slba);

	uint64_t bytes_transfer = (cmd->nr_dw + 1) * sizeof(uint32_t);

	uint64_t nr_zone_to_report;

	if (cmd->zra_specific_features == 0) // all
		nr_zone_to_report = zns_ftl->zp.nr_zones - start_zid;
	else // partial. # of zone desc transferred
		nr_zone_to_report = (bytes_transfer / sizeof(struct zone_descriptor)) - 1;

	report->nr_zones = nr_zone_to_report;

	memcpy(report->zd, &(zone_descs[start_zid]),
	       sizeof(struct zone_descriptor) * nr_zone_to_report);
}

static bool __check_zmgmt_rcv_option_supported(struct zns_ftl *zns_ftl,
					       struct nvme_zone_mgmt_recv *cmd)
{
	if (lba_to_zone(zns_ftl, cmd->slba) >= zns_ftl->zp.nr_zones) {
		NVMEV_ERROR("Invalid lba range\n");
		return false;
	}

	if (cmd->zra != 0) {
		NVMEV_ERROR("Currently, Not support Extended Report Zones\n");
		return false;
	}

	if (cmd->zra_specific_field != 0) {
		NVMEV_ERROR("Currently, Only support listing all zone\n");
		return false;
	}

	return true;
}

void zns_zmgmt_recv(struct nvmev_ns *ns, struct nvmev_request *req, struct nvmev_result *ret)
{
	struct zns_ftl *zns_ftl = (struct zns_ftl *)ns->ftls;
	struct zone_report *buffer = zns_ftl->report_buffer;
	struct nvme_zone_mgmt_recv *cmd = (struct nvme_zone_mgmt_recv *)req->cmd;

	uint64_t prp1 = (uint64_t)cmd->prp1;
	uint64_t prp2 = (uint64_t)cmd->prp2;
	uint64_t length = (cmd->nr_dw + 1) * sizeof(uint32_t);
	uint32_t status;

	NVMEV_ZNS_DEBUG("%s slba 0x%llx nr_dw 0x%lx  action %u partial %u action_specific 0x%x\n",
			__func__, cmd->slba, length, cmd->zra, cmd->zra_specific_features,
			cmd->zra_specific_field);

	if (__check_zmgmt_rcv_option_supported(zns_ftl, cmd)) {
		__fill_zone_report(zns_ftl, cmd, buffer);

		__prp_transfer_data(prp1, prp2, buffer, length, 0);
		status = NVME_SC_SUCCESS;
	} else {
		status = NVME_SC_INVALID_FIELD;
	}

	ret->nsecs_target = req->nsecs_start; // no delay
	ret->status = status;
	return;
}
