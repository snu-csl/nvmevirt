// SPDX-License-Identifier: GPL-2.0-only

#include "nvmev.h"
#include "ssd.h"
#include "zns_ftl.h"

static inline uint32_t __nr_lbas_from_rw_cmd(struct nvme_rw_command *cmd)
{
	return cmd->length + 1;
}

static bool __check_boundary_error(struct zns_ftl *zns_ftl, uint64_t slba, uint32_t nr_lba)
{
	return lba_to_zone(zns_ftl, slba) == lba_to_zone(zns_ftl, slba + nr_lba - 1);
}

static void __increase_write_ptr(struct zns_ftl *zns_ftl, uint32_t zid, uint32_t nr_lba)
{
	struct zone_descriptor *zone_descs = zns_ftl->zone_descs;
	uint64_t cur_write_ptr = zone_descs[zid].wp;
	uint64_t zone_capacity = zone_descs[zid].zone_capacity;

	cur_write_ptr += nr_lba;

	zone_descs[zid].wp = cur_write_ptr;

	if (cur_write_ptr == (zone_to_slba(zns_ftl, zid) + zone_capacity)) {
		//change state to ZSF
		release_zone_resource(zns_ftl, OPEN_ZONE);
		release_zone_resource(zns_ftl, ACTIVE_ZONE);

		if (zone_descs[zid].zrwav)
			ASSERT(0);

		change_zone_state(zns_ftl, zid, ZONE_STATE_FULL);
	} else if (cur_write_ptr > (zone_to_slba(zns_ftl, zid) + zone_capacity)) {
		NVMEV_ERROR("[%s] Write Boundary error!!\n", __func__);
	}
}

static inline struct ppa __lpn_to_ppa(struct zns_ftl *zns_ftl, uint64_t lpn)
{
	struct ssdparams *spp = &zns_ftl->ssd->sp;
	struct znsparams *zpp = &zns_ftl->zp;
	uint64_t zone = lpn_to_zone(zns_ftl, lpn); // find corresponding zone
	uint64_t off = lpn - zone_to_slpn(zns_ftl, zone);

	uint32_t sdie = (zone * zpp->dies_per_zone) % spp->tt_luns;
	uint32_t die = sdie + ((off / spp->pgs_per_oneshotpg) % zpp->dies_per_zone);

	uint32_t channel = die_to_channel(zns_ftl, die);
	uint32_t lun = die_to_lun(zns_ftl, die);
	struct ppa ppa = {
		.g = {
			.lun = lun,
			.ch = channel,
			.pg = off % spp->pgs_per_oneshotpg,
		},
	};

	return ppa;
}

static bool __zns_write(struct zns_ftl *zns_ftl, struct nvmev_request *req,
			struct nvmev_result *ret)
{
	struct zone_descriptor *zone_descs = zns_ftl->zone_descs;
	struct ssdparams *spp = &zns_ftl->ssd->sp;
	struct nvme_rw_command *cmd = &(req->cmd->rw);

	uint64_t slba = cmd->slba;
	uint64_t nr_lba = __nr_lbas_from_rw_cmd(cmd);
	uint64_t slpn, elpn, lpn, zone_elpn;
	// get zone from start_lbai
	uint32_t zid = lba_to_zone(zns_ftl, slba);
	enum zone_state state = zone_descs[zid].state;

	uint64_t nsecs_start = req->nsecs_start;
	uint64_t nsecs_xfer_completed = nsecs_start;
	uint64_t nsecs_latest = nsecs_start;
	uint32_t status = NVME_SC_SUCCESS;

	uint64_t pgs = 0;

	struct buffer *write_buffer;

	if (cmd->opcode == nvme_cmd_zone_append) {
		slba = zone_descs[zid].wp;
		cmd->slba = slba;
	}

	slpn = lba_to_lpn(zns_ftl, slba);
	elpn = lba_to_lpn(zns_ftl, slba + nr_lba - 1);
	zone_elpn = zone_to_elpn(zns_ftl, zid);

	NVMEV_ZNS_DEBUG("%s slba 0x%llx nr_lba 0x%lx zone_id %d state %d\n", __func__, slba,
			nr_lba, zid, state);

	if (zns_ftl->zp.zone_wb_size)
		write_buffer = &(zns_ftl->zone_write_buffer[zid]);
	else
		write_buffer = zns_ftl->ssd->write_buffer;

	if (buffer_allocate(write_buffer, LBA_TO_BYTE(nr_lba)) < LBA_TO_BYTE(nr_lba))
		return false;

	if ((LBA_TO_BYTE(nr_lba) % spp->write_unit_size) != 0) {
		status = NVME_SC_ZNS_INVALID_WRITE;
		goto out;
	}

	if (__check_boundary_error(zns_ftl, slba, nr_lba) == false) {
		// return boundary error
		status = NVME_SC_ZNS_ERR_BOUNDARY;
		goto out;
	}

	// check if slba == current write pointer
	if (slba != zone_descs[zid].wp) {
		NVMEV_ERROR("%s WP error slba 0x%llx nr_lba 0x%llx zone_id %d wp %llx state %d\n",
			    __func__, slba, nr_lba, zid, zns_ftl->zone_descs[zid].wp, state);
		status = NVME_SC_ZNS_INVALID_WRITE;
		goto out;
	}

	switch (state) {
	case ZONE_STATE_EMPTY: {
		// check if slba == start lba in zone
		if (slba != zone_descs[zid].zslba) {
			status = NVME_SC_ZNS_INVALID_WRITE;
			goto out;
		}

		if (is_zone_resource_full(zns_ftl, ACTIVE_ZONE)) {
			status = NVME_SC_ZNS_NO_ACTIVE_ZONE;
			goto out;
		}
		if (is_zone_resource_full(zns_ftl, OPEN_ZONE)) {
			status = NVME_SC_ZNS_NO_OPEN_ZONE;
			goto out;
		}
		acquire_zone_resource(zns_ftl, ACTIVE_ZONE);
		// go through
	}
	case ZONE_STATE_CLOSED: {
		if (acquire_zone_resource(zns_ftl, OPEN_ZONE) == false) {
			status = NVME_SC_ZNS_NO_OPEN_ZONE;
			goto out;
		}

		// change to ZSIO
		change_zone_state(zns_ftl, zid, ZONE_STATE_OPENED_IMPL);
		break;
	}
	case ZONE_STATE_OPENED_IMPL:
	case ZONE_STATE_OPENED_EXPL: {
		break;
	}
	case ZONE_STATE_FULL:
		status = NVME_SC_ZNS_ERR_FULL;
	case ZONE_STATE_READ_ONLY:
		status = NVME_SC_ZNS_ERR_READ_ONLY;
	case ZONE_STATE_OFFLINE:
		status = NVME_SC_ZNS_ERR_OFFLINE;
		goto out;
	}

	__increase_write_ptr(zns_ftl, zid, nr_lba);

	// get delay from nand model
	nsecs_latest = nsecs_start;
	nsecs_latest = ssd_advance_write_buffer(zns_ftl->ssd, nsecs_latest, LBA_TO_BYTE(nr_lba));
	nsecs_xfer_completed = nsecs_latest;

	for (lpn = slpn; lpn <= elpn; lpn += pgs) {
		struct ppa ppa;
		uint64_t pg_off;

		ppa = __lpn_to_ppa(zns_ftl, lpn);
		pg_off = ppa.g.pg % spp->pgs_per_oneshotpg;
		pgs = min(elpn - lpn + 1, (uint64_t)(spp->pgs_per_oneshotpg - pg_off));

		/* Aggregate write io in flash page */
		if (((pg_off + pgs) == spp->pgs_per_oneshotpg) || ((lpn + pgs - 1) == zone_elpn)) {
			struct nand_cmd swr = {
				.type = USER_IO,
				.cmd = NAND_WRITE,
				.stime = nsecs_xfer_completed,
				.xfer_size = spp->pgs_per_oneshotpg * spp->pgsz,
				.interleave_pci_dma = false,
				.ppa = &ppa,
			};
			size_t bufs_to_release;
			uint32_t unaligned_space =
				zns_ftl->zp.zone_size % (spp->pgs_per_oneshotpg * spp->pgsz);
			uint64_t nsecs_completed = ssd_advance_nand(zns_ftl->ssd, &swr);

			nsecs_latest = max(nsecs_completed, nsecs_latest);
			NVMEV_ZNS_DEBUG("%s Flush slba 0x%llx nr_lba 0x%lx zone_id %d state %d\n",
					__func__, slba, nr_lba, zid, state);

			if (((lpn + pgs - 1) == zone_elpn) && (unaligned_space > 0))
				bufs_to_release = unaligned_space;
			else
				bufs_to_release = spp->pgs_per_oneshotpg * spp->pgsz;

			schedule_internal_operation(req->sq_id, nsecs_completed, write_buffer,
						    bufs_to_release);
		}
	}

out:
	ret->status = status;
	if ((cmd->control & NVME_RW_FUA) ||
	    (spp->write_early_completion == 0)) /*Wait all flash operations*/
		ret->nsecs_target = nsecs_latest;
	else /*Early completion*/
		ret->nsecs_target = nsecs_xfer_completed;

	return true;
}

static bool __zns_write_zrwa(struct zns_ftl *zns_ftl, struct nvmev_request *req,
			     struct nvmev_result *ret)
{
	struct zone_descriptor *zone_descs = zns_ftl->zone_descs;
	struct ssdparams *spp = &zns_ftl->ssd->sp;
	struct znsparams *zpp = &zns_ftl->zp;
	struct nvme_rw_command *cmd = &(req->cmd->rw);
	uint64_t slba = cmd->slba;
	uint64_t nr_lba = __nr_lbas_from_rw_cmd(cmd);
	uint64_t elba = cmd->slba + nr_lba - 1;

	// get zone from start_lbai
	uint32_t zid = lba_to_zone(zns_ftl, slba);
	enum zone_state state = zone_descs[zid].state;

	uint64_t prev_wp = zone_descs[zid].wp;
	const uint32_t lbas_per_zrwa = zpp->lbas_per_zrwa;
	const uint32_t lbas_per_zrwafg = zpp->lbas_per_zrwafg;
	uint64_t zrwa_impl_start = prev_wp + lbas_per_zrwa;
	uint64_t zrwa_impl_end = prev_wp + (2 * lbas_per_zrwa) - 1;

	uint64_t nsecs_start = req->nsecs_start;
	uint64_t nsecs_completed = nsecs_start;
	uint64_t nsecs_xfer_completed = nsecs_start;
	uint64_t nsecs_latest = nsecs_start;
	uint32_t status = NVME_SC_SUCCESS;

	struct ppa ppa;
	struct nand_cmd swr;

	uint64_t nr_lbas_flush = 0, lpn, remaining, pgs = 0, pg_off;

	NVMEV_DEBUG(
		"%s slba 0x%llx nr_lba 0x%llx zone_id %d state %d wp 0x%llx zrwa_impl_start 0x%llx zrwa_impl_end 0x%llx  buffer %lu\n",
		__func__, slba, nr_lba, zid, state, prev_wp, zrwa_impl_start, zrwa_impl_end,
		zns_ftl->zwra_buffer[zid].remaining);

	if ((LBA_TO_BYTE(nr_lba) % spp->write_unit_size) != 0) {
		status = NVME_SC_ZNS_INVALID_WRITE;
		goto out;
	}

	if (__check_boundary_error(zns_ftl, slba, nr_lba) == false) {
		// return boundary error
		status = NVME_SC_ZNS_ERR_BOUNDARY;
		goto out;
	}

	// valid range : wp <=  <= wp + 2*(size of zwra) -1
	if (slba < zone_descs[zid].wp || elba > zrwa_impl_end) {
		NVMEV_ERROR("%s slba 0x%llx nr_lba 0x%llx zone_id %d wp 0x%llx state %d\n",
			    __func__, slba, nr_lba, zid, zone_descs[zid].wp, state);
		status = NVME_SC_ZNS_INVALID_WRITE;
		goto out;
	}

	switch (state) {
	case ZONE_STATE_CLOSED:
	case ZONE_STATE_EMPTY: {
		if (acquire_zone_resource(zns_ftl, OPEN_ZONE) == false) {
			status = NVME_SC_ZNS_NO_OPEN_ZONE;
			goto out;
		}

		if (!buffer_allocate(&zns_ftl->zwra_buffer[zid], zpp->zrwa_size))
			NVMEV_ASSERT(0);

		// change to ZSIO
		change_zone_state(zns_ftl, zid, ZONE_STATE_OPENED_IMPL);
		break;
	}
	case ZONE_STATE_OPENED_IMPL:
	case ZONE_STATE_OPENED_EXPL: {
		break;
	}
	case ZONE_STATE_FULL:
		status = NVME_SC_ZNS_ERR_FULL;
		goto out;
	case ZONE_STATE_READ_ONLY:
		status = NVME_SC_ZNS_ERR_READ_ONLY;
		goto out;
	case ZONE_STATE_OFFLINE:
		status = NVME_SC_ZNS_ERR_OFFLINE;
		goto out;
#if 0
		case ZONE_STATE_EMPTY :
			return NVME_SC_ZNS_INVALID_ZONE_OPERATION;
#endif
	}

	if (elba >= zrwa_impl_start) {
		nr_lbas_flush = DIV_ROUND_UP((elba - zrwa_impl_start + 1), lbas_per_zrwafg) *
				lbas_per_zrwafg;

		NVMEV_DEBUG("%s implicitly flush zid %d wp before 0x%llx after 0x%llx buffer %lu",
			    __func__, zid, prev_wp, zone_descs[zid].wp + nr_lbas_flush,
			    zns_ftl->zwra_buffer[zid].remaining);
	} else if (elba == zone_to_elba(zns_ftl, zid)) {
		// Workaround. move wp to end of the zone and make state full implicitly
		nr_lbas_flush = elba - prev_wp + 1;

		NVMEV_DEBUG("%s end of zone zid %d wp before 0x%llx after 0x%llx buffer %lu",
			    __func__, zid, prev_wp, zone_descs[zid].wp + nr_lbas_flush,
			    zns_ftl->zwra_buffer[zid].remaining);
	}

	if (nr_lbas_flush > 0) {
		if (!buffer_allocate(&zns_ftl->zwra_buffer[zid], LBA_TO_BYTE(nr_lbas_flush)))
			return false;

		__increase_write_ptr(zns_ftl, zid, nr_lbas_flush);
	}
	// get delay from nand model
	nsecs_latest = nsecs_start;
	nsecs_latest = ssd_advance_write_buffer(zns_ftl->ssd, nsecs_latest, LBA_TO_BYTE(nr_lba));
	nsecs_xfer_completed = nsecs_latest;

	lpn = lba_to_lpn(zns_ftl, prev_wp);
	remaining = nr_lbas_flush / spp->secs_per_pg;
	/* Aggregate write io in flash page */
	while (remaining > 0) {
		ppa = __lpn_to_ppa(zns_ftl, lpn);
		pg_off = ppa.g.pg % spp->pgs_per_oneshotpg;
		pgs = min(remaining, (uint64_t)(spp->pgs_per_oneshotpg - pg_off));

		if ((pg_off + pgs) == spp->pgs_per_oneshotpg) {
			swr.type = USER_IO;
			swr.cmd = NAND_WRITE;
			swr.stime = nsecs_xfer_completed;
			swr.xfer_size = spp->pgs_per_oneshotpg * spp->pgsz;
			swr.interleave_pci_dma = false;
			swr.ppa = &ppa;

			nsecs_completed = ssd_advance_nand(zns_ftl->ssd, &swr);
			nsecs_latest = max(nsecs_completed, nsecs_latest);

			schedule_internal_operation(req->sq_id, nsecs_completed,
						    &zns_ftl->zwra_buffer[zid],
						    spp->pgs_per_oneshotpg * spp->pgsz);
		}

		lpn += pgs;
		remaining -= pgs;
	}

out:
	ret->status = status;

	if ((cmd->control & NVME_RW_FUA) ||
	    (spp->write_early_completion == 0)) /*Wait all flash operations*/
		ret->nsecs_target = nsecs_latest;
	else /*Early completion*/
		ret->nsecs_target = nsecs_xfer_completed;

	return true;
}

bool zns_write(struct nvmev_ns *ns, struct nvmev_request *req, struct nvmev_result *ret)
{
	struct zns_ftl *zns_ftl = (struct zns_ftl *)ns->ftls;
	struct zone_descriptor *zone_descs = zns_ftl->zone_descs;
	struct nvme_rw_command *cmd = &(req->cmd->rw);
	uint64_t slpn = lba_to_lpn(zns_ftl, cmd->slba);

	// get zone from start_lba
	uint32_t zid = lpn_to_zone(zns_ftl, slpn);

	NVMEV_DEBUG("%s slba 0x%llx zone_id %d \n", __func__, cmd->slba, zid);

	if (zone_descs[zid].zrwav == 0)
		return __zns_write(zns_ftl, req, ret);
	else
		return __zns_write_zrwa(zns_ftl, req, ret);
}

bool zns_read(struct nvmev_ns *ns, struct nvmev_request *req, struct nvmev_result *ret)
{
	struct zns_ftl *zns_ftl = (struct zns_ftl *)ns->ftls;
	struct ssdparams *spp = &zns_ftl->ssd->sp;
	struct zone_descriptor *zone_descs = zns_ftl->zone_descs;
	struct nvme_rw_command *cmd = &(req->cmd->rw);

	uint64_t slba = cmd->slba;
	uint64_t nr_lba = __nr_lbas_from_rw_cmd(cmd);

	uint64_t slpn = lba_to_lpn(zns_ftl, slba);
	uint64_t elpn = lba_to_lpn(zns_ftl, slba + nr_lba - 1);
	uint64_t lpn;

	// get zone from start_lba
	uint32_t zid = lpn_to_zone(zns_ftl, slpn);
	uint32_t status = NVME_SC_SUCCESS;
	uint64_t nsecs_start = req->nsecs_start;
	uint64_t nsecs_completed = nsecs_start, nsecs_latest = 0;
	uint64_t pgs = 0, pg_off;
	struct ppa ppa;
	struct nand_cmd swr;

	NVMEV_ZNS_DEBUG(
		"%s slba 0x%llx nr_lba 0x%lx zone_id %d state %d wp 0x%llx last lba 0x%llx\n",
		__func__, slba, nr_lba, zid, zone_descs[zid].state, zone_descs[zid].wp,
		(slba + nr_lba - 1));

	if (zone_descs[zid].state == ZONE_STATE_OFFLINE) {
		status = NVME_SC_ZNS_ERR_OFFLINE;
	} else if (__check_boundary_error(zns_ftl, slba, nr_lba) == false) {
		// return boundary error
		status = NVME_SC_ZNS_ERR_BOUNDARY;
	}

	// get delay from nand model
	nsecs_latest = nsecs_start;
	if (LBA_TO_BYTE(nr_lba) <= KB(4))
		nsecs_latest += spp->fw_4kb_rd_lat;
	else
		nsecs_latest += spp->fw_rd_lat;

	swr.type = USER_IO;
	swr.cmd = NAND_READ;
	swr.stime = nsecs_latest;
	swr.interleave_pci_dma = false;

	for (lpn = slpn; lpn <= elpn; lpn += pgs) {
		ppa = __lpn_to_ppa(zns_ftl, lpn);
		pg_off = ppa.g.pg % spp->pgs_per_flashpg;
		pgs = min(elpn - lpn + 1, (uint64_t)(spp->pgs_per_flashpg - pg_off));
		swr.xfer_size = pgs * spp->pgsz;
		swr.ppa = &ppa;
		nsecs_completed = ssd_advance_nand(zns_ftl->ssd, &swr);
		nsecs_latest = (nsecs_completed > nsecs_latest) ? nsecs_completed : nsecs_latest;
	}

	if (swr.interleave_pci_dma == false) {
		nsecs_completed = ssd_advance_pcie(zns_ftl->ssd, nsecs_latest, nr_lba * spp->secsz);
		nsecs_latest = (nsecs_completed > nsecs_latest) ? nsecs_completed : nsecs_latest;
	}

	ret->status = status;
	ret->nsecs_target = nsecs_latest;
	return true;
}
