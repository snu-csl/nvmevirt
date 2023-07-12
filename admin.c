// SPDX-License-Identifier: GPL-2.0-only

#include "nvmev.h"
#include "conv_ftl.h"
#include "zns_ftl.h"

#define sq_entry(entry_id) \
	queue->nvme_sq[SQ_ENTRY_TO_PAGE_NUM(entry_id)][SQ_ENTRY_TO_PAGE_OFFSET(entry_id)]
#define cq_entry(entry_id) \
	queue->nvme_cq[CQ_ENTRY_TO_PAGE_NUM(entry_id)][CQ_ENTRY_TO_PAGE_OFFSET(entry_id)]

#define prp_address_offset(prp, offset) \
	(page_address(pfn_to_page(prp >> PAGE_SHIFT) + offset) + (prp & ~PAGE_MASK))
#define prp_address(prp) prp_address_offset(prp, 0)

static void __make_cq_entry_results(int eid, u16 ret, u32 result0, u32 result1)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	struct nvme_common_command *cmd = &sq_entry(eid).common;
	int cq_head = queue->cq_head;

	cq_entry(cq_head) = (struct nvme_completion) {
		.command_id = cmd->command_id,
		.sq_id = 0,
		.sq_head = eid,
		.result0 = result0,
		.result1 = result1,
		.status = queue->phase | (ret << 1),
	};

	if (++cq_head == queue->cq_depth) {
		cq_head = 0;
		queue->phase = !queue->phase;
	}
	queue->cq_head = cq_head;
}

static void __make_cq_entry(int eid, u16 ret)
{
	__make_cq_entry_results(eid, ret, 0, 0);
}


/***
 * Queue managements
 */
static void __nvmev_admin_create_cq(int eid)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	struct nvmev_completion_queue *cq;
	struct nvme_create_cq *cmd = &sq_entry(eid).create_cq;
	unsigned int num_pages, i;
	int dbs_idx;

	cq = kzalloc(sizeof(struct nvmev_completion_queue), GFP_KERNEL);

	cq->qid = cmd->cqid;

	cq->irq_enabled = cmd->cq_flags & NVME_CQ_IRQ_ENABLED ? true : false;
	if (cq->irq_enabled) {
		cq->irq_vector = cmd->irq_vector;
	}
	cq->interrupt_ready = false;

	cq->queue_size = cmd->qsize + 1;
	cq->phase = 1;

	cq->cq_head = 0;
	cq->cq_tail = -1;

	spin_lock_init(&cq->entry_lock);
	spin_lock_init(&cq->irq_lock);

	/* TODO Physically non-contiguous prp list */
	cq->phys_contig = cmd->cq_flags & NVME_QUEUE_PHYS_CONTIG ? true : false;
	WARN_ON(!cq->phys_contig);

	num_pages = DIV_ROUND_UP(cq->queue_size * sizeof(struct nvme_completion), PAGE_SIZE);
	cq->cq = kzalloc(sizeof(struct nvme_completion *) * num_pages, GFP_KERNEL);
	for (i = 0; i < num_pages; i++) {
		cq->cq[i] = prp_address_offset(cmd->prp1, i);
	}

	nvmev_vdev->cqes[cq->qid] = cq;

	dbs_idx = cq->qid * 2 + 1;
	nvmev_vdev->dbs[dbs_idx] = nvmev_vdev->old_dbs[dbs_idx] = 0;

	__make_cq_entry(eid, NVME_SC_SUCCESS);
}

static void __nvmev_admin_delete_cq(int eid)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	struct nvmev_completion_queue *cq;
	unsigned int qid;

	qid = sq_entry(eid).delete_queue.qid;

	cq = nvmev_vdev->cqes[qid];
	nvmev_vdev->cqes[qid] = NULL;

	if (cq) {
		kfree(cq->cq);
		kfree(cq);
	}

	__make_cq_entry(eid, NVME_SC_SUCCESS);
}

static void __nvmev_admin_create_sq(int eid)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	struct nvme_create_sq *cmd = &sq_entry(eid).create_sq;
	struct nvmev_submission_queue *sq;
	unsigned int num_pages, i;
	int dbs_idx;

	sq = kzalloc(sizeof(struct nvmev_submission_queue), GFP_KERNEL);

	sq->qid = cmd->sqid;
	sq->cqid = cmd->cqid;

	sq->priority = cmd->sq_flags & 0xFFFE;
	sq->queue_size = cmd->qsize + 1;

	/* TODO Physically non-contiguous prp list */
	sq->phys_contig = (cmd->sq_flags & NVME_QUEUE_PHYS_CONTIG) ? true : false;
	WARN_ON(!sq->phys_contig);

	num_pages = DIV_ROUND_UP(sq->queue_size * sizeof(struct nvme_command), PAGE_SIZE);
	sq->sq = kzalloc(sizeof(struct nvme_command *) * num_pages, GFP_KERNEL);

	for (i = 0; i < num_pages; i++) {
		sq->sq[i] = prp_address_offset(cmd->prp1, i);
	}
	nvmev_vdev->sqes[sq->qid] = sq;

	dbs_idx = sq->qid * 2;
	nvmev_vdev->dbs[dbs_idx] = 0;
	nvmev_vdev->old_dbs[dbs_idx] = 0;

	__make_cq_entry(eid, NVME_SC_SUCCESS);
}

static void __nvmev_admin_delete_sq(int eid)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	struct nvme_delete_queue *cmd = &sq_entry(eid).delete_queue;
	struct nvmev_submission_queue *sq;
	unsigned int qid;

	qid = cmd->qid;

	sq = nvmev_vdev->sqes[qid];
	nvmev_vdev->sqes[qid] = NULL;

	if (sq) {
		kfree(sq->sq);
		kfree(sq);
	}

	__make_cq_entry(eid, NVME_SC_SUCCESS);
}


/***
 * Log pages
 */
static void __nvmev_admin_get_log_page(int eid)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	struct nvme_get_log_page_command *cmd = &sq_entry(eid).get_log_page;
	void *page;
	uint32_t len = ((((uint32_t)cmd->numdu << 16) | cmd->numdl) + 1) << 2;

	page = prp_address(cmd->prp1);

	switch (cmd->lid) {
	case NVME_LOG_SMART: {
		static const struct nvme_smart_log smart_log = {
			.critical_warning = 0,
			.spare_thresh = 20,
			.host_reads[0] = cpu_to_le64(0),
			.host_writes[0] = cpu_to_le64(0),
			.num_err_log_entries[0] = cpu_to_le64(0),
			.temperature[0] = 0 & 0xff,
			.temperature[1] = (0 >> 8) & 0xff,
		};

		__memcpy(page, &smart_log, len);
		break;
	}
	case NVME_LOG_CMD_EFFECTS: {
		static const struct nvme_effects_log effects_log = {
			.acs = {
				[nvme_admin_get_log_page] = cpu_to_le32(NVME_CMD_EFFECTS_CSUPP),
				[nvme_admin_identify] = cpu_to_le32(NVME_CMD_EFFECTS_CSUPP),
				// [nvme_admin_abort_cmd] = cpu_to_le32(NVME_CMD_EFFECTS_CSUPP),
				[nvme_admin_set_features] = cpu_to_le32(NVME_CMD_EFFECTS_CSUPP),
				[nvme_admin_get_features] = cpu_to_le32(NVME_CMD_EFFECTS_CSUPP),
				[nvme_admin_async_event] = cpu_to_le32(NVME_CMD_EFFECTS_CSUPP),
				// [nvme_admin_keep_alive] = cpu_to_le32(NVME_CMD_EFFECTS_CSUPP),
			},
			.iocs = {
#if SUPPORTED_SSD_TYPE(ZNS)
				/*
				 * Zone Append is unsupported at the moment, but we fake it so that
				 * Linux device driver doesn't lock it to R/O.
				 *
				 * A zone append command will result in device failure.
				 */
				[nvme_cmd_zone_append] = cpu_to_le32(NVME_CMD_EFFECTS_CSUPP),
				[nvme_cmd_zone_mgmt_send] = cpu_to_le32(NVME_CMD_EFFECTS_CSUPP | NVME_CMD_EFFECTS_LBCC),
				[nvme_cmd_zone_mgmt_recv] = cpu_to_le32(NVME_CMD_EFFECTS_CSUPP),
#endif
			},
			.resv = { 0, },
		};

		__memcpy(page, &effects_log, len);
		break;
	}
	default:
		/*
		 * The NVMe protocol mandates several commands (lid) to be implemented, but some
		 * aren't in NVMeVirt.
		 *
		 * As the NVMe host device driver will always assume that the device will return
		 * the correct values, blindly memset'ing the return buffer will always result in
		 * heavy system malfunction due to incorrect memory dereferences.
		 *
		 * Warn the users and make it perfectly clear that this needs to be implemented.
		 */
		NVMEV_ERROR("Unimplemented log page identifier: 0x%hhx,"
			    "the system will be unstable!\n", cmd->lid);
		__memset(page, 0, len);
		break;
	}

	__make_cq_entry(eid, NVME_SC_SUCCESS);
}


/***
 * Identify functions
 */
static void __nvmev_admin_identify_namespace(int eid)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	struct nvme_id_ns *ns;
	struct nvme_identify *cmd = &sq_entry(eid).identify;
	size_t nsid = cmd->nsid - 1;

	ns = prp_address(cmd->prp1);
	memset(ns, 0x0, PAGE_SIZE);

	ns->lbaf[0].ms = 0;
	ns->lbaf[0].ds = 9;
	ns->lbaf[0].rp = NVME_LBAF_RP_GOOD;

	ns->lbaf[1].ms = 8;
	ns->lbaf[1].ds = 9;
	ns->lbaf[1].rp = NVME_LBAF_RP_GOOD;

	ns->lbaf[2].ms = 16;
	ns->lbaf[2].ds = 9;
	ns->lbaf[2].rp = NVME_LBAF_RP_GOOD;

	ns->lbaf[3].ms = 0;
	ns->lbaf[3].ds = 12;
	ns->lbaf[3].rp = NVME_LBAF_RP_BEST;

	ns->lbaf[4].ms = 8;
	ns->lbaf[4].ds = 12;
	ns->lbaf[4].rp = NVME_LBAF_RP_BEST;

	ns->lbaf[5].ms = 64;
	ns->lbaf[5].ds = 12;
	ns->lbaf[5].rp = NVME_LBAF_RP_BEST;

	ns->lbaf[6].ms = 128;
	ns->lbaf[6].ds = 12;
	ns->lbaf[6].rp = NVME_LBAF_RP_BEST;

	ns->nsze = (nvmev_vdev->ns[nsid].size >> ns->lbaf[ns->flbas].ds);

	ns->ncap = ns->nsze;
	ns->nuse = ns->nsze;
	ns->nlbaf = 6;
	ns->flbas = 0;
	ns->dps = 0;

	__make_cq_entry(eid, NVME_SC_SUCCESS);
}

static void __nvmev_admin_identify_namespaces(int eid)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	struct nvme_identify *cmd = &sq_entry(eid).identify;
	unsigned int *ns;
	int i;

	ns = prp_address(cmd->prp1);
	memset(ns, 0x00, PAGE_SIZE * 2);

	for (i = 1; i <= nvmev_vdev->nr_ns; i++) {
		if (i > cmd->nsid) {
			*ns = i;
			ns++;
		}
	}

	__make_cq_entry(eid, NVME_SC_SUCCESS);
}

static void __nvmev_admin_identify_namespace_desc(int eid)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	struct nvme_identify *cmd = &sq_entry(eid).identify;
	struct nvme_id_ns_desc *ns_desc;
	int nsid = cmd->nsid - 1;

	ns_desc = prp_address(cmd->prp1);
	memset(ns_desc, 0x00, sizeof(*ns_desc));

	ns_desc->nidt = NVME_NIDT_CSI;
	ns_desc->nidl = 1;

	ns_desc->nid[0] = nvmev_vdev->ns[nsid].csi; // Zoned Name Space Command Set

	__make_cq_entry(eid, NVME_SC_SUCCESS);
}

static void __nvmev_admin_identify_zns_namespace(int eid)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	struct nvme_identify *cmd = &sq_entry(eid).identify;
	struct nvme_id_zns_ns *ns;
	int nsid = cmd->nsid - 1;
	struct zns_ftl *zns_ftl = (struct zns_ftl *)nvmev_vdev->ns[nsid].ftls;
	struct znsparams *zpp = &zns_ftl->zp;

	BUG_ON(nvmev_vdev->ns[nsid].csi != NVME_CSI_ZNS);

	ns = prp_address(cmd->prp1);
	memset(ns, 0x00, sizeof(*ns));

	ns->zoc = 0; //currently not support variable zone capacity
	ns->ozcs = 0;
	ns->mar = zpp->nr_active_zones - 1; // 0-based

	ns->mor = zpp->nr_open_zones - 1; // 0-based

	/* zrwa enabled */
	if (zpp->nr_zrwa_zones > 0) {
		ns->ozcs |= OZCS_ZRWA; //Support ZRWA

		ns->numzrwa = zpp->nr_zrwa_zones - 1;

		ns->zrwafg = zpp->zrwafg_size;

		ns->zrwasz = zpp->zrwa_size;

		ns->zrwacap = 0; // explicit zrwa flush
		ns->zrwacap |= ZRWACAP_EXPFLUSHSUP;
	}
	// Zone Size
	ns->lbaf[0].zsze = BYTE_TO_LBA(zpp->zone_size);

	// Zone Descriptor Extension Size
	ns->lbaf[0].zdes = 0; // currently not support

	__make_cq_entry(eid, NVME_SC_SUCCESS);
}

static void __nvmev_admin_identify_zns_ctrl(int eid)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	struct nvme_identify *cmd = &sq_entry(eid).identify;
	struct nvme_id_zns_ctrl *res;

	res = prp_address(cmd->prp1);

	res->zasl = 0; // currently not support zone append command

	__make_cq_entry(eid, NVME_SC_SUCCESS);
}

static void __nvmev_admin_identify_ctrl(int eid)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	struct nvme_identify *cmd = &sq_entry(eid).identify;
	struct nvme_id_ctrl *ctrl;

	ctrl = prp_address(cmd->prp1);
	memset(ctrl, 0x00, sizeof(*ctrl));

	ctrl->nn = nvmev_vdev->nr_ns;
	ctrl->oncs = 0; //optional command
	ctrl->acl = 3; //minimum 4 required, 0's based value
	ctrl->vwc = 0;
	snprintf(ctrl->sn, sizeof(ctrl->sn), "CSL_Virt_SN_%02d", 1);
	snprintf(ctrl->mn, sizeof(ctrl->mn), "CSL_Virt_MN_%02d", 1);
	snprintf(ctrl->fr, sizeof(ctrl->fr), "CSL_%03d", 2);
	ctrl->mdts = nvmev_vdev->mdts;
	ctrl->sqes = 0x66;
	ctrl->cqes = 0x44;

	__make_cq_entry(eid, NVME_SC_SUCCESS);
}

static void __nvmev_admin_identify(int eid)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	int cns = sq_entry(eid).identify.cns;

	switch (cns) {
	case 0x00:
		__nvmev_admin_identify_namespace(eid);
		break;
	case 0x01:
		__nvmev_admin_identify_ctrl(eid);
		break;
	case 0x02:
		__nvmev_admin_identify_namespaces(eid);
		break;
	case 0x03:
		__nvmev_admin_identify_namespace_desc(eid);
		break;
	case 0x05:
		__nvmev_admin_identify_zns_namespace(eid);
		break;
	case 0x06:
		__nvmev_admin_identify_zns_ctrl(eid);
		break;
	default:
		__make_cq_entry(eid, NVME_SC_INVALID_OPCODE);
		NVMEV_ERROR("I don't know %d\n", cns);
	}
}


/***
 * Set/get features
 */
static void __nvmev_admin_set_features(int eid)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	struct nvme_features *cmd = &sq_entry(eid).features;
	__le32 result0 = 0;
	__le32 result1 = 0;

	switch (cmd->fid) {
	case NVME_FEAT_ARBITRATION:
	case NVME_FEAT_POWER_MGMT:
	case NVME_FEAT_LBA_RANGE:
	case NVME_FEAT_TEMP_THRESH:
	case NVME_FEAT_ERR_RECOVERY:
	case NVME_FEAT_VOLATILE_WC:
		break;
	case NVME_FEAT_NUM_QUEUES: {
		int num_queue;

		// # of sq in 0-base
		num_queue = (sq_entry(eid).features.dword11 & 0xFFFF) + 1;
		nvmev_vdev->nr_sq = min(num_queue, NR_MAX_IO_QUEUE);

		// # of cq in 0-base
		num_queue = ((sq_entry(eid).features.dword11 >> 16) & 0xFFFF) + 1;
		nvmev_vdev->nr_cq = min(num_queue, NR_MAX_IO_QUEUE);

		result0 = ((nvmev_vdev->nr_cq - 1) << 16 | (nvmev_vdev->nr_sq - 1));
		break;
	}
	case NVME_FEAT_IRQ_COALESCE:
	case NVME_FEAT_IRQ_CONFIG:
	case NVME_FEAT_WRITE_ATOMIC:
	case NVME_FEAT_ASYNC_EVENT:
	case NVME_FEAT_AUTO_PST:
	case NVME_FEAT_SW_PROGRESS:
	case NVME_FEAT_HOST_ID:
	case NVME_FEAT_RESV_MASK:
	case NVME_FEAT_RESV_PERSIST:
	default:
		break;
	}

	__make_cq_entry_results(eid, NVME_SC_SUCCESS, result0, result1);
}

static void __nvmev_admin_get_features(int eid)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	struct nvme_features *cmd = &sq_entry(eid).features;
	__le32 result0 = 0;
	__le32 result1 = 0;

	switch (cmd->fid) {
	case NVME_FEAT_ARBITRATION:
	case NVME_FEAT_POWER_MGMT:
	case NVME_FEAT_LBA_RANGE:
	case NVME_FEAT_TEMP_THRESH:
	case NVME_FEAT_ERR_RECOVERY:
	case NVME_FEAT_VOLATILE_WC:
		break;
	case NVME_FEAT_NUM_QUEUES:
		result0 = ((nvmev_vdev->nr_cq - 1) << 16 | (nvmev_vdev->nr_sq - 1));
		break;
	case NVME_FEAT_IRQ_COALESCE:
	case NVME_FEAT_IRQ_CONFIG:
	case NVME_FEAT_WRITE_ATOMIC:
	case NVME_FEAT_ASYNC_EVENT:
	case NVME_FEAT_AUTO_PST:
	case NVME_FEAT_SW_PROGRESS:
	case NVME_FEAT_HOST_ID:
	case NVME_FEAT_RESV_MASK:
	case NVME_FEAT_RESV_PERSIST:
	default:
		break;
	}

	__make_cq_entry_results(eid, NVME_SC_SUCCESS, result0, result1);
}


/***
 * Misc
 */
static void __nvmev_admin_async_event(int eid)
{
	__make_cq_entry(eid, NVME_SC_SUCCESS);
	// __make_cq_entry(eid, NVME_SC_ASYNC_LIMIT);
}


static void __nvmev_proc_admin_req(int entry_id)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	struct nvme_command *sqe = &sq_entry(entry_id);

	NVMEV_DEBUG("%s: %d 0x%x 0x%x\n", __func__, entry_id,
			sqe->common.opcode, sqe->common.command_id);

	switch (sqe->common.opcode) {
	case nvme_admin_delete_sq:
		__nvmev_admin_delete_sq(entry_id);
		break;
	case nvme_admin_create_sq:
		__nvmev_admin_create_sq(entry_id);
		break;
	case nvme_admin_get_log_page:
		__nvmev_admin_get_log_page(entry_id);
		break;
	case nvme_admin_delete_cq:
		__nvmev_admin_delete_cq(entry_id);
		break;
	case nvme_admin_create_cq:
		__nvmev_admin_create_cq(entry_id);
		break;
	case nvme_admin_identify:
		__nvmev_admin_identify(entry_id);
	case nvme_admin_abort_cmd:
		break;
	case nvme_admin_set_features:
		__nvmev_admin_set_features(entry_id);
		break;
	case nvme_admin_get_features:
		__nvmev_admin_get_features(entry_id);
		break;
	case nvme_admin_async_event:
		__nvmev_admin_async_event(entry_id);
		break;
	case nvme_admin_activate_fw:
	case nvme_admin_download_fw:
	case nvme_admin_format_nvm:
	case nvme_admin_security_send:
	case nvme_admin_security_recv:
	default:
		__make_cq_entry(entry_id, NVME_SC_INVALID_OPCODE);
		NVMEV_ERROR("Unhandled admin requests: %d", sqe->common.opcode);
		break;
	}
}

void nvmev_proc_admin_sq(int new_db, int old_db)
{
	struct nvmev_admin_queue *queue = nvmev_vdev->admin_q;
	int num_proc = new_db - old_db;
	int curr = old_db;
	int seq;

	if (num_proc < 0)
		num_proc += queue->sq_depth;

	for (seq = 0; seq < num_proc; seq++) {
		__nvmev_proc_admin_req(curr++);

		if (curr == queue->sq_depth) {
			curr = 0;
		}
	}

	nvmev_signal_irq(0); /* ACQ is always associated with interrupt vector 0 */
}

void nvmev_proc_admin_cq(int new_db, int old_db)
{
}
