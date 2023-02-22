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

#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/version.h>

#include <linux/percpu-defs.h>
#include <linux/sched/clock.h>

#include "nvmev.h"
#include "pci.h"

extern struct nvmev_dev *vdev;

static int apicid_to_cpuid[256];

static void __init_apicid_to_cpuid(void)
{
	int i;
	for_each_possible_cpu(i) {
		apicid_to_cpuid[per_cpu(x86_cpu_to_apicid, i)] = i;
	}
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,17,0)
void nvmev_signal_irq(int msi_index)
{
	struct xarray *xa = &(&vdev->pdev->dev)->msi.data->__store;
	struct msi_desc *msi_desc;
	unsigned long idx;

	//TODO: Does it have to be 0 ~ NR_MAX_IO_QUEUE * PCI_MSIX_ENTRY_SIZE?
	xa_for_each_range(xa, idx, msi_desc, 0, NR_MAX_IO_QUEUE * PCI_MSIX_ENTRY_SIZE) {
		if (msi_desc->msi_index == msi_index) {
			struct irq_data *irqd = irq_get_irq_data(msi_desc->irq);
			struct irq_cfg *irqc = irqd_cfg(irqd);

			unsigned int target = irqc->dest_apicid;
			unsigned int target_cpu = apicid_to_cpuid[target];

			NVMEV_DEBUG("vector %d, dest_apicid %d, target_cpu %d\n",
					irqc->vector, target, target_cpu);
			apic->send_IPI(target_cpu, irqc->vector);
			return;
		}
	}
	NVMEV_INFO("Failed to send IPI\n");
	BUG_ON(!msi_desc);
}
#else
void nvmev_signal_irq(int msi_index)
{
	struct msi_desc *msi_desc, *tmp;

	for_each_msi_entry_safe(msi_desc, tmp, (&vdev->pdev->dev)) {
		if (msi_desc->msi_attrib.entry_nr == msi_index) {
			struct irq_data *irqd = irq_get_irq_data(msi_desc->irq);
			struct irq_cfg *irqc = irqd_cfg(irqd);

			unsigned int target = irqc->dest_apicid;
			unsigned int target_cpu = apicid_to_cpuid[target];

			NVMEV_DEBUG("vector %d, dest_apicid %d, target_cpu %d\n",
					irqc->vector, target, target_cpu);
			apic->send_IPI(target_cpu, irqc->vector);
			return;
		}
	}
	BUG_ON(!msi_desc);
}
#endif

void nvmev_proc_bars()
{
	struct __nvme_bar *old_bar = vdev->old_bar;
	struct nvme_ctrl_regs *bar = vdev->bar;
	struct nvmev_admin_queue *queue;
	unsigned int num_pages, i;

#if 0 /* Read-only register */
	if (old_bar->cap != bar->u_cap) {
		memcpy(&old_bar->cap, &bar->cap, sizeof(old_bar->cap));
	}
	if (old_bar->vs != bar->u_vs) {
		memcpy(&old_bar->vs, &bar->vs, sizeof(old_bar->vs));
	}
	if (old_bar->cmbloc != bar->u_cmbloc) {
		memcpy(&old_bar->cmbloc, &bar->cmbloc, sizeof(old_bar->cmbloc));
	}
	if (old_bar->cmbsz != bar->u_cmbsz) {
		memcpy(&old_bar->cmbsz, &bar->cmbsz, sizeof(old_bar->cmbsz));
	}
	if (old_bar->rsvd1 != bar->rsvd1) {
		memcpy(&old_bar->rsvd1, &bar->rsvd1, sizeof(old_bar->rsvd1));
	}
	if (old_bar->csts != bar->u_csts) {
		memcpy(&old_bar->csts, &bar->csts, sizeof(old_bar->csts));
	}
#endif
	if (old_bar->intms != bar->intms) {
		memcpy(&old_bar->intms, &bar->intms, sizeof(old_bar->intms));
	}
	if (old_bar->intmc != bar->intmc) {
		memcpy(&old_bar->intmc, &bar->intmc, sizeof(old_bar->intmc));
	}
	if (old_bar->nssr != bar->nssr) {
		memcpy(&old_bar->nssr, &bar->nssr, sizeof(old_bar->nssr));
	}
	if (old_bar->aqa != bar->u_aqa) {
		// Initalize admin queue
		memcpy(&old_bar->aqa, &bar->aqa, sizeof(old_bar->aqa));

		queue =	kzalloc(sizeof(struct nvmev_admin_queue), GFP_KERNEL);

		if (vdev->admin_q == NULL) {
			queue->cq_head = 0;
			queue->phase = 1;
			queue->sq_depth = bar->aqa.asqs + 1; /* asqs and acqs are 0-based */
			queue->cq_depth = bar->aqa.acqs + 1;
			vdev->dbs[0] = vdev->old_dbs[0] = 0;
			vdev->dbs[1] = vdev->old_dbs[1] = 0;

			vdev->admin_q = queue;
		}
	}
	if (old_bar->asq != bar->u_asq) {
		memcpy(&old_bar->asq, &bar->asq, sizeof(old_bar->asq));

		queue = vdev->admin_q;

		if (queue->nvme_sq) {
			kfree(queue->nvme_sq);
			queue->nvme_sq = NULL;
		}

		queue->sq_depth = bar->aqa.asqs + 1; /* asqs and acqs are 0-based */

		num_pages = DIV_ROUND_UP(queue->sq_depth * sizeof(struct nvme_command), PAGE_SIZE);
		queue->nvme_sq = kcalloc(num_pages, sizeof(struct nvme_command *), GFP_KERNEL);
		BUG_ON(!queue->nvme_sq && "Error on setup admin submission queue");
		NVMEV_DEBUG("made admin SQ - %d entries\n", num_pages);
		vdev->dbs[0] = vdev->old_dbs[0] = 0;

		for (i = 0; i < num_pages; i++) {
			queue->nvme_sq[i] = page_address(pfn_to_page(vdev->bar->u_asq >> PAGE_SHIFT) + i);
		}
	}
	if (old_bar->acq != bar->u_acq) {
		memcpy(&old_bar->acq, &bar->acq, sizeof(old_bar->acq));

		queue = vdev->admin_q;

		if (queue->nvme_cq) {
			kfree(queue->nvme_cq);
			queue->nvme_cq = NULL;
		}

		queue->cq_depth = bar->aqa.acqs + 1; /* asqs and acqs are 0-based */

		num_pages = DIV_ROUND_UP(queue->cq_depth * sizeof(struct nvme_completion), PAGE_SIZE);
		queue->nvme_cq = kcalloc(num_pages, sizeof(struct nvme_completion *), GFP_KERNEL);
		BUG_ON(!queue->nvme_cq && "Error on setup admin completion queue");
		NVMEV_DEBUG("made admin CQ - %d entries\n", num_pages);
		vdev->dbs[1] = vdev->old_dbs[1] = 0;
		queue->cq_head = 0;

		for (i = 0; i < num_pages; i++) {
			queue->nvme_cq[i] = page_address(pfn_to_page(vdev->bar->u_acq >> PAGE_SHIFT) + i);
		}
	}
	if (old_bar->cc != bar->u_cc) {
		/* Enable */
		if (bar->cc.en == 1) {
			if (vdev->admin_q) {
				bar->csts.rdy = 1;
			} else {
				return;
			}
		} else if (bar->cc.en == 0) {
			bar->csts.rdy = 0;
			bar->u_cc = 0;
		}

		/* Shutdown */
		if (bar->cc.shn == 1) {
			bar->csts.shst = 2;
			vdev->dbs[0] = vdev->old_dbs[0] = 0;
			vdev->dbs[1] = vdev->old_dbs[1] = 0;
			vdev->admin_q->cq_head = 0;
		}

		memcpy(&old_bar->cc, &bar->cc, sizeof(old_bar->cc));
	}
}

int nvmev_pci_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *val)
{
	if (devfn != 0) return 1;

	memcpy(val, vdev->virtDev + where, size);

	return 0;
};

int nvmev_pci_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 _val)
{
	u32 mask = 0xFFFFFFFF;
	u32 val;
	int target = where;

	memcpy(&val, vdev->virtDev + where, size);

	if (where < OFFS_PCI_PM_CAP) {
	// PCI_HDR
		if (target == 0x0) mask = 0x0;
		else if (target == 0x04) mask = 0x0547;
		else if (target == 0x06) mask = 0xF200;
		else if (target == 0x09) mask = 0x0;
		else if (target == 0x0d) mask = 0x0;
		else if (target == 0x0e) mask = 0x0;
		else if (target == 0x0f) mask = 0x40;
		else if (target == 0x10) mask = 0xFFFFC000;
		else if (target == 0x18) mask = 0x0;
		else if (target == 0x1c) mask = 0x0;
		else if (target == 0x20) mask = 0x0;
		else if (target == 0x24) mask = 0x0;
		else if (target == 0x28) mask = 0x0;
		else if (target == 0x2c) mask = 0x0;
		else if (target == 0x34) mask = 0x0;
		else if (target == 0x3c) mask = 0xF;
		else if (target == 0x3e) mask = 0x0;
		else if (target == 0x3f) mask = 0x0;
	} else if (where < OFFS_PCI_MSIX_CAP) {
	// PCI_PM_CAP
	} else if (where < OFFS_PCIE_CAP) {
	// PCI_MSIX_CAP
		target -= OFFS_PCI_MSIX_CAP;
		if (target == 0) mask = 0x0;
		else if (target == 2) {
			mask = 0xC000;

			if ((val & mask) == mask) {
				vdev->msix_enabled = true;

				NVMEV_DEBUG("msi-x enabled\n");
			}
		}
		else if (target == 4) mask = 0x0;
		else if (target == 8) mask = 0x0;
	} else {
	// PCIE_CAP
	}

	val = (val & (~mask)) | (_val & mask);
	memcpy(vdev->virtDev + where, &val, size);

	return 0;
};

static struct pci_bus *__create_pci_bus(void)
{
    struct pci_bus* nvmev_pci_bus = NULL;
	struct pci_dev *dev;

	memset(&vdev->pci_ops, 0, sizeof(vdev->pci_ops));
    vdev->pci_ops.read = nvmev_pci_read;
    vdev->pci_ops.write = nvmev_pci_write;

	memset(&vdev->pci_sd, 0, sizeof(vdev->pci_sd));
	vdev->pci_sd.domain = NVMEV_PCI_DOMAIN_NUM;
	vdev->pci_sd.node = cpu_to_node(vdev->config.cpu_nr_dispatcher); // PCI_NUMA_NODE

    nvmev_pci_bus = pci_scan_bus(NVMEV_PCI_BUS_NUM, &vdev->pci_ops, &vdev->pci_sd);

	if (!nvmev_pci_bus){
		NVMEV_ERROR("Unable to create PCI bus\n");
		return NULL;
	}

	/* XXX Only support a singe NVMeVirt instance in the system for now */
	list_for_each_entry(dev, &nvmev_pci_bus->devices, bus_list) {
		struct resource *res = &dev->resource[0];
		res->parent = &iomem_resource;

		vdev->pdev = dev;

		vdev->bar = memremap(pci_resource_start(dev, 0), PAGE_SIZE * 2, MEMREMAP_WT);
		memset(vdev->bar, 0x0, PAGE_SIZE * 2);

		vdev->dbs = ((void *)vdev->bar) + PAGE_SIZE;

		vdev->bar->vs.mjr = 1;
		vdev->bar->vs.mnr = 0;
		vdev->bar->cap.mpsmin = 0;
		vdev->bar->cap.mqes = 1024 - 1; // 0-based value

#if (BASE_SSD == ZNS_PROTOTYPE)
		vdev->bar->cap.css = CAP_CSS_BIT_SPECIFIC;
#endif

		vdev->old_dbs = kzalloc(PAGE_SIZE, GFP_KERNEL);
		BUG_ON(!vdev->old_dbs && "allocating old DBs memory");
		memcpy(vdev->old_dbs, vdev->dbs, sizeof(*vdev->old_dbs));

		vdev->old_bar = kzalloc(PAGE_SIZE, GFP_KERNEL);
		BUG_ON(!vdev->old_bar && "allocating old BAR memory");
		memcpy(vdev->old_bar, vdev->bar, sizeof(*vdev->old_bar));

		vdev->msix_table =
			memremap(pci_resource_start(vdev->pdev, 0) + PAGE_SIZE * 2,
					NR_MAX_IO_QUEUE * PCI_MSIX_ENTRY_SIZE, MEMREMAP_WT);
		memset(vdev->msix_table, 0x00, NR_MAX_IO_QUEUE * PCI_MSIX_ENTRY_SIZE);
	}

	pci_bus_add_devices(nvmev_pci_bus);

	NVMEV_INFO("Successfully created virtual PCI bus (node %d)\n", vdev->pci_sd.node);

	return nvmev_pci_bus;
};


struct nvmev_dev *VDEV_INIT(void)
{
	struct nvmev_dev *vdev;
	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);

	vdev->virtDev = kzalloc(PAGE_SIZE, GFP_KERNEL);

	vdev->pcihdr = vdev->virtDev + OFFS_PCI_HDR;
	vdev->pmcap = vdev->virtDev + OFFS_PCI_PM_CAP;
	vdev->msixcap = vdev->virtDev + OFFS_PCI_MSIX_CAP;
	vdev->pciecap = vdev->virtDev + OFFS_PCIE_CAP;
	vdev->aercap = vdev->virtDev + PCI_CFG_SPACE_SIZE;
	vdev->pcie_exp_cap = vdev->virtDev + PCI_CFG_SPACE_SIZE;

	vdev->msix_enabled = true;

	vdev->admin_q = NULL;

	return vdev;
}

void VDEV_FINALIZE(struct nvmev_dev *vdev)
{
	if (vdev->msix_table)
		memunmap(vdev->msix_table);

	if (vdev->bar)
		memunmap(vdev->bar);

	if (vdev->old_bar)
		kfree(vdev->old_bar);

	if (vdev->old_dbs)
		kfree(vdev->old_dbs);

	if (vdev->admin_q)
		kfree(vdev->admin_q);

	if (vdev->virtDev)
		kfree(vdev->virtDev);

	if (vdev->io_unit_stat)
		kfree(vdev->io_unit_stat);

	if (vdev)
		kfree(vdev);
}

void PCI_HEADER_SETTINGS(struct pci_header *pcihdr, unsigned long base_pa)
{
	pcihdr->id.did = 0x0101;
	pcihdr->id.vid = 0x0c51;
	/*
	pcihdr->cmd.id = 1;
	pcihdr->cmd.bme = 1;
	*/
	pcihdr->cmd.mse = 1;
	pcihdr->sts.cl = 1;

	pcihdr->htype.mfd = 0;
	pcihdr->htype.hl = PCI_HEADER_TYPE_NORMAL;

	pcihdr->rid = 0x01;

	pcihdr->cc.bcc = PCI_BASE_CLASS_STORAGE;
	pcihdr->cc.scc = 0x08;
	pcihdr->cc.pi = 0x02;

	pcihdr->mlbar.tp = PCI_BASE_ADDRESS_MEM_TYPE_64 >> 1;
	pcihdr->mlbar.ba = (base_pa & 0xFFFFFFFF) >> 14;

	pcihdr->mulbar = base_pa >> 32;

	pcihdr->ss.ssid = 0x370d;
	pcihdr->ss.ssvid = 0x0c51;

	pcihdr->erom = 0x0; //PFN_PHYS(page_to_pfn(bar_pages));//page_to_pfn(bar_pages);//0xDF300000;

	pcihdr->cap = OFFS_PCI_PM_CAP;

	pcihdr->intr.ipin = 0;
	pcihdr->intr.iline = 0;
}

void PCI_PMCAP_SETTINGS(struct pci_pm_cap *pmcap)
{
	pmcap->pid.cid = PCI_CAP_ID_PM;
	pmcap->pid.next = OFFS_PCI_MSIX_CAP;

	pmcap->pc.vs = 3;
	pmcap->pmcs.nsfrst = 1;
	pmcap->pmcs.ps = PCI_PM_CAP_PME_D0 >> 16;
}

void PCI_MSIXCAP_SETTINGS(struct pci_msix_cap *msixcap)
{
	msixcap->mxid.cid = PCI_CAP_ID_MSIX;
	msixcap->mxid.next = OFFS_PCIE_CAP;

	msixcap->mxc.mxe = 1;
	msixcap->mxc.ts = 127; // encoded as n-1

	msixcap->mtab.tbir = 0;
	msixcap->mtab.to = 0x400;

	msixcap->mpba.pbao = 0x1000;
	msixcap->mpba.pbir = 0;
}

void PCI_PCIECAP_SETTINGS(struct pcie_cap *pciecap)
{
	pciecap->pxid.cid = PCI_CAP_ID_EXP;
	pciecap->pxid.next = 0x0;

	pciecap->pxcap.ver = PCI_EXP_FLAGS;
	pciecap->pxcap.imn = 0;
	pciecap->pxcap.dpt = PCI_EXP_TYPE_ENDPOINT;

	pciecap->pxdcap.mps = 1;
	pciecap->pxdcap.pfs = 0;
	pciecap->pxdcap.etfs = 1;
	pciecap->pxdcap.l0sl = 6;
	pciecap->pxdcap.l1l = 2;
	pciecap->pxdcap.rer = 1;
	pciecap->pxdcap.csplv = 0;
	pciecap->pxdcap.cspls = 0;
	pciecap->pxdcap.flrc = 1;
}

void PCI_AERCAP_SETTINGS(struct aer_cap *aercap)
{
	aercap->aerid.cid = PCI_EXT_CAP_ID_ERR;
	aercap->aerid.cver = 1;
	aercap->aerid.next = PCI_CFG_SPACE_SIZE + 0x50;
}

void PCI_PCIE_EXTCAP_SETTINGS(struct pci_exp_hdr *exp_cap)
{
	struct pci_exp_hdr *pcie_exp_cap;

	pcie_exp_cap = exp_cap + 0x50;
	pcie_exp_cap->id.cid = PCI_EXT_CAP_ID_VC;
	pcie_exp_cap->id.cver = 1;
	pcie_exp_cap->id.next = PCI_CFG_SPACE_SIZE + 0x80;

	pcie_exp_cap = exp_cap + 0x80;
	pcie_exp_cap->id.cid = PCI_EXT_CAP_ID_PWR;
	pcie_exp_cap->id.cver = 1;
	pcie_exp_cap->id.next = PCI_CFG_SPACE_SIZE + 0x90;

	pcie_exp_cap = exp_cap + 0x90;
	pcie_exp_cap->id.cid = PCI_EXT_CAP_ID_ARI;
	pcie_exp_cap->id.cver = 1;
	pcie_exp_cap->id.next = PCI_CFG_SPACE_SIZE + 0x170;

	pcie_exp_cap = exp_cap + 0x170;
	pcie_exp_cap->id.cid = PCI_EXT_CAP_ID_DSN;
	pcie_exp_cap->id.cver = 1;
	pcie_exp_cap->id.next = PCI_CFG_SPACE_SIZE + 0x1a0;

	pcie_exp_cap = exp_cap + 0x1a0;
	pcie_exp_cap->id.cid = PCI_EXT_CAP_ID_SECPCI;
	pcie_exp_cap->id.cver = 1;
	pcie_exp_cap->id.next = 0;
}

bool NVMEV_PCI_INIT(struct nvmev_dev *vdev)
{
	PCI_HEADER_SETTINGS(vdev->pcihdr, vdev->config.memmap_start);
	PCI_PMCAP_SETTINGS(vdev->pmcap);
	PCI_MSIXCAP_SETTINGS(vdev->msixcap);
	PCI_PCIECAP_SETTINGS(vdev->pciecap);
	PCI_AERCAP_SETTINGS(vdev->aercap);
	PCI_PCIE_EXTCAP_SETTINGS(vdev->pcie_exp_cap);

	__init_apicid_to_cpuid();

	vdev->virt_bus = __create_pci_bus();
	if (!vdev->virt_bus) return false;

	return true;
}
