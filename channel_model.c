// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/highmem.h>
#include <linux/sched/clock.h>

#include "nvmev.h"
#include "channel_model.h"

static inline unsigned long long __get_wallclock(void)
{
	return cpu_clock(nvmev_vdev->config.cpu_nr_dispatcher);
}

void chmodel_init(struct channel_model *ch, uint64_t bandwidth /*MB/s*/)
{
	ch->head = 0;
	ch->valid_len = 0;
	ch->cur_time = 0;
	ch->max_credits = BANDWIDTH_TO_MAX_CREDITS(bandwidth);
	ch->command_credits = 0;
	ch->xfer_lat = BANDWIDTH_TO_TX_TIME(bandwidth);

	MEMSET(&(ch->avail_credits[0]), ch->max_credits, NR_CREDIT_ENTRIES);

	NVMEV_INFO("[%s] bandwidth %llu max_credits %u tx_time %u\n", __func__, bandwidth,
		   ch->max_credits, ch->xfer_lat);
}

uint64_t chmodel_request(struct channel_model *ch, uint64_t request_time, uint64_t length)
{
	uint64_t cur_time = __get_wallclock();
	uint32_t pos, next_pos;
	uint32_t remaining_credits, consumed_credits;
	uint32_t default_delay, delay = 0;
	uint32_t valid_length;
	uint64_t total_latency;
	uint32_t units_to_xfer = DIV_ROUND_UP(length, UNIT_XFER_SIZE);
	uint32_t cur_time_offs, request_time_offs;

	// Search current time index and move head to it
	cur_time_offs = (cur_time / UNIT_TIME_INTERVAL) - (ch->cur_time / UNIT_TIME_INTERVAL);
	cur_time_offs = (cur_time_offs < ch->valid_len) ? cur_time_offs : ch->valid_len;

	if (ch->head + cur_time_offs >= NR_CREDIT_ENTRIES) {
		MEMSET(&(ch->avail_credits[ch->head]), ch->max_credits,
		       NR_CREDIT_ENTRIES - ch->head);
		MEMSET(&(ch->avail_credits[0]), ch->max_credits,
		       cur_time_offs - (NR_CREDIT_ENTRIES - ch->head));
	} else {
		MEMSET(&(ch->avail_credits[ch->head]), ch->max_credits, cur_time_offs);
	}

	ch->head = (ch->head + cur_time_offs) % NR_CREDIT_ENTRIES;
	ch->cur_time = cur_time;
	ch->valid_len = ch->valid_len - cur_time_offs;

	if (ch->valid_len > NR_CREDIT_ENTRIES) {
		NVMEV_ERROR("[%s] Invalid valid_len 0x%x\n", __func__, ch->valid_len);
		NVMEV_ASSERT(0);
	}

	if (request_time < cur_time) {
		NVMEV_DEBUG("[%s] Reqeust time is before the current time 0x%llx 0x%llx\n",
			    __func__, request_time, cur_time);
		return request_time; // return minimum delay
	}

	//Search request time index
	request_time_offs = (request_time / UNIT_TIME_INTERVAL) - (cur_time / UNIT_TIME_INTERVAL);

	if (request_time_offs >= NR_CREDIT_ENTRIES) {
		NVMEV_ERROR("[%s] Need to increase array size 0x%llx 0x%llx 0x%x\n", __func__,
			    request_time, cur_time, request_time_offs);
		return request_time; // return minimum delay
	}

	pos = (ch->head + request_time_offs) % NR_CREDIT_ENTRIES;
	remaining_credits = units_to_xfer * UNIT_XFER_CREDITS;
	remaining_credits += ch->command_credits;

	default_delay = remaining_credits / ch->max_credits;
	delay = 0;

	while (1) {
		consumed_credits = (remaining_credits <= ch->avail_credits[pos]) ?
						 remaining_credits :
						 ch->avail_credits[pos];
		ch->avail_credits[pos] -= consumed_credits;
		remaining_credits -= consumed_credits;

		if (remaining_credits) {
			next_pos = (pos + 1) % NR_CREDIT_ENTRIES;
			// If array is full
			if (next_pos != ch->head) {
				delay++;
				pos = next_pos;
			} else {
				NVMEV_ERROR("[%s] No free entry 0x%llx 0x%llx 0x%x\n", __func__,
					    request_time, cur_time, request_time_offs);
				break;
			}
		} else
			break;
	}

	valid_length = (pos >= ch->head) ? (pos - ch->head + 1) :
						 (NR_CREDIT_ENTRIES - (ch->head - pos - 1));

	if (valid_length > ch->valid_len)
		ch->valid_len = valid_length;

	// check if array is small..
	delay = (delay > default_delay) ? (delay - default_delay) : 0;

	total_latency = (ch->xfer_lat * units_to_xfer) + (delay * UNIT_TIME_INTERVAL);

	return request_time + total_latency;
}
