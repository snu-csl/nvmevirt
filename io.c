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
 * *********************************************************************/

#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/highmem.h>
#include <linux/sched/clock.h>

#include "nvmev.h"
#include "dma.h"

#if ((BASE_SSD == SAMSUNG_970PRO) || (BASE_SSD) == ZNS_PROTOTYPE)
#include "ssd.h"
#else
struct buffer;
#endif

#undef PERF_DEBUG

#define PRP_PFN(x)	((unsigned long)((x) >> PAGE_SHIFT))

#define sq_entry(entry_id) \
	sq->sq[SQ_ENTRY_TO_PAGE_NUM(entry_id)][SQ_ENTRY_TO_PAGE_OFFSET(entry_id)]
#define cq_entry(entry_id) \
	cq->cq[CQ_ENTRY_TO_PAGE_NUM(entry_id)][CQ_ENTRY_TO_PAGE_OFFSET(entry_id)]

extern struct nvmev_dev *vdev;

static int io_using_dma = false;

static inline unsigned long long __get_wallclock(void)
{
	return cpu_clock(vdev->config.cpu_nr_dispatcher);
}

static unsigned int __do_perform_io(int sqid, int sq_entry)
{
	struct nvmev_submission_queue *sq = vdev->sqes[sqid];
	size_t offset;
	size_t length, remaining;
	int prp_offs = 0;
	int prp2_offs = 0;
	u64 paddr;
	u64 *paddr_list = NULL;
	size_t nsid = sq_entry(sq_entry).rw.nsid - 1; // 0-based

	offset = sq_entry(sq_entry).rw.slba << 9;
	length = (sq_entry(sq_entry).rw.length + 1) << 9;
	remaining = length;

	while (remaining) {
		size_t io_size;
		void *vaddr;
		size_t mem_offs = 0;

		prp_offs++;
		if (prp_offs == 1) {
			paddr = sq_entry(sq_entry).rw.prp1;
		} else if (prp_offs == 2) {
			paddr = sq_entry(sq_entry).rw.prp2;
			if (remaining > PAGE_SIZE) {
				paddr_list = kmap_atomic_pfn(PRP_PFN(paddr)) + (paddr & PAGE_OFFSET_MASK);
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

		if (sq_entry(sq_entry).rw.opcode == nvme_cmd_write) {
			memcpy(vdev->ns[nsid].mapped + offset, vaddr + mem_offs, io_size);
		} else if (sq_entry(sq_entry).rw.opcode == nvme_cmd_read) {
			memcpy(vaddr + mem_offs, vdev->ns[nsid].mapped + offset, io_size);
		}

		kunmap_atomic(vaddr);

		remaining -= io_size;
		offset += io_size;
	}

	if (paddr_list != NULL)
		kunmap_atomic(paddr_list);

	return length;
}

static u64 paddr_list[513] = {0,}; // Not using index 0 to make max index == num_prp
static unsigned int __do_perform_io_using_dma(int sqid, int sq_entry)
{
	struct nvmev_submission_queue *sq = vdev->sqes[sqid];
	size_t offset;
	size_t length, remaining;
	int prp_offs = 0;
	int prp2_offs = 0;
	int num_prps = 0;
	u64 paddr;
	u64 *tmp_paddr_list = NULL;
	size_t io_size;
	size_t mem_offs = 0;

	offset = sq_entry(sq_entry).rw.slba << 9;
	length = (sq_entry(sq_entry).rw.length + 1) << 9;
	remaining = length;

	memset(paddr_list, 0, sizeof(paddr_list));
	/* Loop to get the PRP list */
	while (remaining) {
		io_size = 0;

		prp_offs++;
		if (prp_offs == 1) {
			paddr_list[prp_offs] = sq_entry(sq_entry).rw.prp1;
		} else if (prp_offs == 2) {
			paddr_list[prp_offs] = sq_entry(sq_entry).rw.prp2;
			if (remaining > PAGE_SIZE) {
				tmp_paddr_list = kmap_atomic_pfn(PRP_PFN(paddr_list[prp_offs])) + (paddr_list[prp_offs] & PAGE_OFFSET_MASK);
				paddr_list[prp_offs] = tmp_paddr_list[prp2_offs++];
			}
		} else {
			paddr_list[prp_offs] = tmp_paddr_list[prp2_offs++];
		}

		io_size = min_t(size_t, remaining, PAGE_SIZE);

		if (paddr_list[prp_offs] & PAGE_OFFSET_MASK) {
			mem_offs = paddr_list[prp_offs] & PAGE_OFFSET_MASK;
			if (io_size + mem_offs > PAGE_SIZE)
				io_size = PAGE_SIZE - mem_offs;
		}

		remaining -= io_size;
	}
	num_prps = prp_offs;

	if (tmp_paddr_list != NULL)
		kunmap_atomic(tmp_paddr_list);

	remaining = length;
	prp_offs = 1;

	/* Loop for data transfer */
	while (remaining) {
		size_t page_size;
		mem_offs = 0;
		io_size = 0;
		page_size = 0;

		paddr = paddr_list[prp_offs];
		page_size = min_t(size_t, remaining, PAGE_SIZE);

		/* For non-page aligned paddr, it will never be between continuous PRP list (Always first paddr)  */
		if (paddr & PAGE_OFFSET_MASK) {
			mem_offs = paddr & PAGE_OFFSET_MASK;
			if (page_size + mem_offs > PAGE_SIZE) {
				page_size = PAGE_SIZE - mem_offs;
			}
		}

		for (prp_offs++; prp_offs <= num_prps; prp_offs++) {
			if (paddr_list[prp_offs] == paddr_list[prp_offs - 1] + PAGE_SIZE)
				page_size += PAGE_SIZE;
			else
				break;
		}

		io_size = min_t(size_t, remaining, page_size);

		if (sq_entry(sq_entry).rw.opcode == nvme_cmd_write) {
			dmatest_submit(paddr, vdev->config.storage_start + offset, io_size);
		} else if (sq_entry(sq_entry).rw.opcode == nvme_cmd_read) {
			dmatest_submit(vdev->config.storage_start + offset, paddr, io_size);
		}

		remaining -= io_size;
		offset += io_size;
	}

	return length;
}

static void __enqueue_io_req(int sqid, int cqid, int sq_entry, unsigned long long nsecs_start, struct nvmev_result *ret)
{
	struct nvmev_submission_queue *sq = vdev->sqes[sqid];

#if SUPPORT_MULTI_IO_WORKER_BY_SQ
	unsigned int proc_turn = (sqid - 1) % (vdev->config.nr_io_cpu);
#else
	unsigned int proc_turn = vdev->proc_turn;
#endif
	struct nvmev_proc_info *pi = &vdev->proc_info[proc_turn];
	unsigned int entry = pi->free_seq;

	if (pi->proc_table[entry].next >= NR_MAX_PARALLEL_IO) {
		WARN_ON_ONCE("IO queue is almost full");
		pi->free_seq = entry;
		return;
	}

	if (++proc_turn == vdev->config.nr_io_cpu) proc_turn = 0;
	vdev->proc_turn = proc_turn;
	pi->free_seq = pi->proc_table[entry].next;
	BUG_ON(pi->free_seq >= NR_MAX_PARALLEL_IO);

	NVMEV_DEBUG("%s/%u[%d], sq %d cq %d, entry %d %llu + %llu\n",
			pi->thread_name, entry, sq_entry(sq_entry).rw.opcode,
			sqid, cqid, sq_entry, nsecs_start, ret->nsecs_target - nsecs_start);

	/////////////////////////////////
	pi->proc_table[entry].sqid = sqid;
	pi->proc_table[entry].cqid = cqid;
	pi->proc_table[entry].sq_entry = sq_entry;
	pi->proc_table[entry].command_id = sq_entry(sq_entry).common.command_id;
	pi->proc_table[entry].nsecs_start = nsecs_start;
	pi->proc_table[entry].nsecs_enqueue = local_clock();
	pi->proc_table[entry].nsecs_target =  ret->nsecs_target;
	pi->proc_table[entry].status = ret->status;
	pi->proc_table[entry].is_completed = false;
	pi->proc_table[entry].is_copied = false;
	pi->proc_table[entry].prev = -1;
	pi->proc_table[entry].next = -1;

	pi->proc_table[entry].writeback_cmd = false;
	mb();	/* IO kthread shall see the updated pe at once */

	// (END) -> (START) order, nsecs target ascending order
	if (pi->io_seq == -1) {
		pi->io_seq = entry;
		pi->io_seq_end = entry;
	} else {
		unsigned int curr = pi->io_seq_end;

		while (curr != -1) {
			if (pi->proc_table[curr].nsecs_target <= pi->proc_io_nsecs)
				break;

			if (pi->proc_table[curr].nsecs_target <= ret->nsecs_target)
				break;

			curr = pi->proc_table[curr].prev;
		}

		if (curr == -1) { /* Head inserted */
			pi->proc_table[pi->io_seq].prev = entry;
			pi->proc_table[entry].next = pi->io_seq;
			pi->io_seq = entry;
		} else if (pi->proc_table[curr].next == -1) { /* Tail */
			pi->proc_table[entry].prev = curr;
			pi->io_seq_end = entry;
			pi->proc_table[curr].next = entry;
		} else { /* In between */
			pi->proc_table[entry].prev = curr;
			pi->proc_table[entry].next = pi->proc_table[curr].next;

			pi->proc_table[pi->proc_table[entry].next].prev = entry;
			pi->proc_table[curr].next = entry;
		}
	}
}


void enqueue_writeback_io_req(int sqid, unsigned long long nsecs_target, struct buffer *write_buffer, unsigned int buffs_to_release)
{
#if SUPPORT_MULTI_IO_WORKER_BY_SQ
	unsigned int proc_turn = (sqid - 1) % (vdev->config.nr_io_cpu);
#else
	unsigned int proc_turn = vdev->proc_turn;
#endif
	struct nvmev_proc_info *pi = &vdev->proc_info[proc_turn];
	unsigned int entry = pi->free_seq;

	if (pi->proc_table[entry].next >= NR_MAX_PARALLEL_IO) {
		WARN_ON_ONCE("IO queue is almost full");
		pi->free_seq = entry;
		return;
	}

	if (++proc_turn == vdev->config.nr_io_cpu) proc_turn = 0;
	vdev->proc_turn = proc_turn;
	pi->free_seq = pi->proc_table[entry].next;
	BUG_ON(pi->free_seq >= NR_MAX_PARALLEL_IO);

	NVMEV_DEBUG("%s/%u[%d], sq %d cq %d, entry %d %llu + %llu\n",
			pi->thread_name, entry, sq_entry(sq_entry).rw.opcode,
			sqid, cqid, sq_entry, nsecs_start, ret->nsecs_target - nsecs_start);

	/////////////////////////////////
	pi->proc_table[entry].sqid = sqid;
	pi->proc_table[entry].nsecs_start = local_clock();
	pi->proc_table[entry].nsecs_enqueue = local_clock();
	pi->proc_table[entry].nsecs_target =  nsecs_target;
	pi->proc_table[entry].is_completed = false;
	pi->proc_table[entry].is_copied = true;
	pi->proc_table[entry].prev = -1;
	pi->proc_table[entry].next = -1;

	pi->proc_table[entry].writeback_cmd = true;
	pi->proc_table[entry].buffs_to_release = buffs_to_release;
	pi->proc_table[entry].write_buffer = (void*)write_buffer;
	mb();	/* IO kthread shall see the updated pe at once */

	// (END) -> (START) order, nsecs target ascending order
	if (pi->io_seq == -1) {
		pi->io_seq = entry;
		pi->io_seq_end = entry;
	} else {
		unsigned int curr = pi->io_seq_end;

		while (curr != -1) {
			if (pi->proc_table[curr].nsecs_target <= pi->proc_io_nsecs)
				break;

			if (pi->proc_table[curr].nsecs_target <= nsecs_target)
				break;

			curr = pi->proc_table[curr].prev;
		}

		if (curr == -1) { /* Head inserted */
			pi->proc_table[pi->io_seq].prev = entry;
			pi->proc_table[entry].next = pi->io_seq;
			pi->io_seq = entry;
		} else if (pi->proc_table[curr].next == -1) { /* Tail */
			pi->proc_table[entry].prev = curr;
			pi->io_seq_end = entry;
			pi->proc_table[curr].next = entry;
		} else { /* In between */
			pi->proc_table[entry].prev = curr;
			pi->proc_table[entry].next = pi->proc_table[curr].next;

			pi->proc_table[pi->proc_table[entry].next].prev = entry;
			pi->proc_table[curr].next = entry;
		}
	}
}

static void __reclaim_completed_reqs(void)
{
	unsigned int turn;

	for (turn = 0; turn < vdev->config.nr_io_cpu; turn++) {
		struct nvmev_proc_info *pi;
		struct nvmev_proc_table *pe;

		unsigned int first_entry = -1;
		unsigned int last_entry = -1;
		unsigned int curr;
		int nr_reclaimed = 0;

		pi = &vdev->proc_info[turn];

		first_entry = pi->io_seq;
		curr = first_entry;

		while (curr != -1) {
			pe = &pi->proc_table[curr];
			if (pe->is_completed == true && pe->is_copied == true
					&& pe->nsecs_target <= pi->proc_io_nsecs) {
				last_entry = curr;
				curr = pe->next;
				nr_reclaimed++;
			} else {
				break;
			}
		}

		if (last_entry != -1) {
			pe = &pi->proc_table[last_entry];
			pi->io_seq = pe->next;
			if (pe->next != -1) {
				pi->proc_table[pe->next].prev = -1;
			}
			pe->next = -1;

			pe = &pi->proc_table[first_entry];
			pe->prev = pi->free_seq_end;

			pe = &pi->proc_table[pi->free_seq_end];
			pe->next = first_entry;

			pi->free_seq_end = last_entry;
			NVMEV_DEBUG("Reclaimed %u -- %u, %d\n", first_entry, last_entry, nr_reclaimed);
		}
	}
}

static size_t __nvmev_proc_io(int sqid, int sq_entry)
{
	struct nvmev_submission_queue *sq = vdev->sqes[sqid];
	unsigned long long nsecs_start = __get_wallclock();
	struct nvme_command *cmd = &sq_entry(sq_entry);
#if (BASE_SSD == KV_PROTOTYPE)
	uint32_t nsid = 0; // Some KVSSD programs give 0 as nsid for KV IO
#else
	uint32_t nsid = cmd->common.nsid - 1;
#endif
	struct nvmev_ns *ns = &vdev->ns[nsid];

	struct nvmev_request req = {
		.cmd = cmd,
		.sq_id = sqid,
		.nsecs_start = nsecs_start,
	};
	struct nvmev_result ret = {
		.nsecs_target = nsecs_start,
		.status = NVME_SC_SUCCESS,
	};

#ifdef PERF_DEBUG
	unsigned long long prev_clock = local_clock();
	unsigned long long prev_clock2 = 0;
	unsigned long long prev_clock3 = 0;
	unsigned long long prev_clock4 = 0;
	static unsigned long long clock1 = 0;
	static unsigned long long clock2 = 0;
	static unsigned long long clock3 = 0;
	static unsigned long long counter = 0;
#endif

	if (!ns->proc_io_cmd(ns, &req, &ret))
			return false;

#ifdef PERF_DEBUG
	prev_clock2 = local_clock();
#endif

	__enqueue_io_req(sqid, sq->cqid, sq_entry, nsecs_start, &ret);

#ifdef PERF_DEBUG
	prev_clock3 = local_clock();
#endif

	__reclaim_completed_reqs();

#ifdef PERF_DEBUG
	prev_clock4 = local_clock();

	clock1 += (prev_clock2 - prev_clock);
	clock2 += (prev_clock3 - prev_clock2);
	clock3 += (prev_clock4 - prev_clock3);
	counter++;

	if (counter > 1000) {
		NVMEV_DEBUG("LAT: %llu, ENQ: %llu, CLN: %llu\n",
				clock1 / counter, clock2 / counter, clock3 / counter);
		clock1 = 0;
		clock2 = 0;
		clock3 = 0;
		counter = 0;
	}
#endif
	return true;
}


int nvmev_proc_io_sq(int sqid, int new_db, int old_db)
{
	struct nvmev_submission_queue *sq = vdev->sqes[sqid];
	int num_proc = new_db - old_db;
	int seq;
	int sq_entry = old_db;
	int latest_db;

	if (unlikely(!sq)) return old_db;
	if (unlikely(num_proc < 0)) num_proc += sq->queue_size;

	for (seq = 0; seq < num_proc; seq++) {
		if (!__nvmev_proc_io(sqid, sq_entry))
			break;

		if (++sq_entry == sq->queue_size) {
			sq_entry = 0;
		}
		sq->stat.nr_dispatched++;
		sq->stat.nr_in_flight++;
		//sq->stat.total_io += io_size;
	}
	sq->stat.nr_dispatch++;
	sq->stat.max_nr_in_flight =
		max_t(int, sq->stat.max_nr_in_flight, sq->stat.nr_in_flight);

	latest_db = (old_db + seq) % sq->queue_size;
	//latest_db = new_db;
	return latest_db;
}

void nvmev_proc_io_cq(int cqid, int new_db, int old_db)
{
	struct nvmev_completion_queue *cq = vdev->cqes[cqid];
	int i;
	for (i = old_db; i != new_db; i++) {
		if (i >= cq->queue_size) {
			i = -1;
			continue;
		}
		vdev->sqes[cq_entry(i).sq_id]->stat.nr_in_flight--;
	}

	cq->cq_tail = new_db - 1;
	if (new_db == -1) cq->cq_tail = cq->queue_size - 1;
}

static void __fill_cq_result(struct nvmev_proc_table * proc_entry)
{
	int sqid = proc_entry -> sqid;
	int cqid = proc_entry -> cqid;
	int sq_entry = proc_entry -> sq_entry;
	unsigned int command_id = proc_entry -> command_id;
	unsigned int status = proc_entry -> status;
	unsigned int result0 = proc_entry -> result0;
	unsigned int result1 = proc_entry -> result1;


	struct nvmev_completion_queue *cq = vdev->cqes[cqid];
	int cq_head = cq->cq_head;

	spin_lock(&cq->entry_lock);
	cq_entry(cq_head).command_id = command_id;
	cq_entry(cq_head).sq_id = sqid;
	cq_entry(cq_head).sq_head = sq_entry;
	cq_entry(cq_head).status = cq->phase | status << 1;
	cq_entry(cq_head).result0 = result0;
	cq_entry(cq_head).result1 = result1;

	if (++cq_head == cq->queue_size) {
		cq_head = 0;
		cq->phase = !cq->phase;
	}

	cq->cq_head = cq_head;
	cq->interrupt_ready = true;
	spin_unlock(&cq->entry_lock);
}

static int nvmev_kthread_io(void *data)
{
	struct nvmev_proc_info *pi = (struct nvmev_proc_info *)data;
	struct nvmev_ns *ns;

#ifdef PERF_DEBUG
	static unsigned long long intr_clock[NR_MAX_IO_QUEUE + 1];
	static unsigned long long intr_counter[NR_MAX_IO_QUEUE + 1];

	unsigned long long prev_clock;
#endif

	NVMEV_INFO("%s started on cpu %d (node %d)\n",
			pi->thread_name, smp_processor_id(), cpu_to_node(smp_processor_id()));

	while (!kthread_should_stop()) {
		unsigned long long curr_nsecs_wall = __get_wallclock();
		unsigned long long curr_nsecs_local = local_clock();
		long long delta = curr_nsecs_wall - curr_nsecs_local;

		volatile unsigned int curr = pi->io_seq;
		int qidx;

		while (curr != -1) {
			struct nvmev_proc_table *pe = &pi->proc_table[curr];
			unsigned long long curr_nsecs = local_clock() + delta;
			pi->proc_io_nsecs = curr_nsecs;

			if (pe->is_completed == true) {
				curr = pe->next;
				continue;
			}

			if (pe->is_copied == false) {
#ifdef PERF_DEBUG
				unsigned long long memcpy_time;
				pe->nsecs_copy_start = local_clock() + delta;
#endif
				if (pe->writeback_cmd) {
					;
				} else if (io_using_dma) {
					__do_perform_io_using_dma(pe->sqid, pe->sq_entry);
				} else {
#if (BASE_SSD == KV_PROTOTYPE)
					struct nvmev_submission_queue *sq = vdev->sqes[pe->sqid];
					ns = &vdev->ns[0];
					if (ns->identify_io_cmd(ns, sq_entry(pe->sq_entry))) {
						pe->result0 = ns->perform_io_cmd(ns, &sq_entry(pe->sq_entry), &(pe->status));
					} else {
						__do_perform_io(pe->sqid, pe->sq_entry);
					}
#endif
					__do_perform_io(pe->sqid, pe->sq_entry);
				}

#ifdef PERF_DEBUG
				pe->nsecs_copy_done = local_clock() + delta;
				memcpy_time = pe->nsecs_copy_done - pe->nsecs_copy_start;
#endif
				pe->is_copied = true;

				NVMEV_DEBUG("%s: copied %u, %d %d %d\n",
						pi->thread_name, curr,
						pe->sqid, pe->cqid, pe->sq_entry);
			}

			if (pe->nsecs_target <= curr_nsecs) {
				if (pe->writeback_cmd) {
#if (BASE_SSD == SAMSUNG_970PRO || BASE_SSD == ZNS_PROTOTYPE)
					buffer_release((struct buffer *)pe->write_buffer, pe->buffs_to_release);
#endif
				} else  {
					__fill_cq_result(pe);
				}

				NVMEV_DEBUG("%s: completed %u, %d %d %d\n",
						pi->thread_name, curr,
						pe->sqid, pe->cqid, pe->sq_entry);

#ifdef PERF_DEBUG
				pe->nsecs_cq_filled = local_clock() + delta;
				trace_printk("%llu %llu %llu %llu %llu %llu\n",
						pe->nsecs_start,
						pe->nsecs_enqueue - pe->nsecs_start,
						pe->nsecs_copy_start - pe->nsecs_start,
						pe->nsecs_copy_done - pe->nsecs_start,
						pe->nsecs_cq_filled - pe->nsecs_start,
						pe->nsecs_target - pe->nsecs_start);
#endif
				mb(); /* Reclaimer shall see after here */
				pe->is_completed = true;
			}

			curr = pe->next;
		}

		for (qidx = 1; qidx <= vdev->nr_cq; qidx++) {
			struct nvmev_completion_queue *cq = vdev->cqes[qidx];
#if SUPPORT_MULTI_IO_WORKER_BY_SQ
			if ((pi->id) != ((qidx - 1) % vdev->config.nr_io_cpu)) continue;
#endif
			if (cq == NULL || !cq->irq_enabled) continue;

			if (spin_trylock(&cq->irq_lock)) {
				if (cq->interrupt_ready == true) {

#ifdef PERF_DEBUG
					prev_clock = local_clock();
#endif
					cq->interrupt_ready = false;
					nvmev_signal_irq(cq->irq_vector);

#ifdef PERF_DEBUG
					intr_clock[qidx] += (local_clock() - prev_clock);
					intr_counter[qidx]++;

					if (intr_counter[qidx] > 1000) {
						NVMEV_DEBUG("Intr %d: %llu\n", qidx,
								intr_clock[qidx] / intr_counter[qidx]);
						intr_clock[qidx] = 0;
						intr_counter[qidx] = 0;
					}
#endif
				}
				spin_unlock(&cq->irq_lock);
			}
		}
		cond_resched();
	}

	return 0;
}

void NVMEV_IO_PROC_INIT(struct nvmev_dev *vdev)
{
	unsigned int i, proc_idx;

	vdev->proc_info = kcalloc(sizeof(struct nvmev_proc_info), vdev->config.nr_io_cpu, GFP_KERNEL);
	vdev->proc_turn = 0;

	for (proc_idx = 0; proc_idx < vdev->config.nr_io_cpu; proc_idx++) {
		struct nvmev_proc_info *pi = &vdev->proc_info[proc_idx];

		pi->proc_table = kzalloc(sizeof(struct nvmev_proc_table) * NR_MAX_PARALLEL_IO, GFP_KERNEL);
		for (i = 0; i < NR_MAX_PARALLEL_IO; i++) {
			pi->proc_table[i].next = i + 1;
			pi->proc_table[i].prev = i - 1;
		}
		pi->proc_table[NR_MAX_PARALLEL_IO - 1].next = -1;
#if SUPPORT_MULTI_IO_WORKER_BY_SQ
		pi->id = proc_idx;
#endif
		pi->free_seq = 0;
		pi->free_seq_end = NR_MAX_PARALLEL_IO - 1;
		pi->io_seq = -1;
		pi->io_seq_end = -1;

		snprintf(pi->thread_name, sizeof(pi->thread_name), "nvmev_proc_io_%d", proc_idx);

		pi->nvmev_io_worker = kthread_create(nvmev_kthread_io, pi, pi->thread_name);

		kthread_bind(pi->nvmev_io_worker, vdev->config.cpu_nr_proc_io[proc_idx]);
		wake_up_process(pi->nvmev_io_worker);
	}
}

void NVMEV_IO_PROC_FINAL(struct nvmev_dev *vdev)
{
	unsigned int i;

	for (i = 0; i < vdev->config.nr_io_cpu; i++) {
		struct nvmev_proc_info *pi = &vdev->proc_info[i];

		if (!IS_ERR_OR_NULL(pi->nvmev_io_worker)) {
			kthread_stop(pi->nvmev_io_worker);
		}

		kfree(pi->proc_table);
	}

	kfree(vdev->proc_info);
}
