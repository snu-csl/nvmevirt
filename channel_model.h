// SPDX-License-Identifier: GPL-2.0-only

#ifndef _CHANNEL_MODEL_H
#define _CHANNEL_MODEL_H

/* Macros for channel model */
#define NR_CREDIT_ENTRIES (1024 * 96)
#define UNIT_TIME_INTERVAL (4000ULL) //ns
#define UNIT_XFER_SIZE (128ULL) //bytes
#define UNIT_XFER_CREDITS (1) //credits needed to transfer data(UNIT_XFER_SIZE)

#define SIZE_OF_CREDIT_T 1

#if (SIZE_OF_CREDIT_T == 1)
typedef uint8_t credit_t;
#define MEMSET(dest, value, length) memset(dest, value, length)

#elif (SIZE_OF_CREDIT_T == 2)
typedef uint16_t credit_t;
#define MEMSET(dest, value, length) memset16(dest, value, length)

#elif (SIZE_OF_CREDIT_T == 4)
typedef uint32_t credit_t;
#define MEMSET(dest, value, length) memset32(dest, value, length)
#else
#error "Invalid credit size"
#endif

struct channel_model {
	uint64_t cur_time;
	uint32_t head;
	uint32_t valid_len;
	uint32_t max_credits;
	uint32_t command_credits;
	uint32_t xfer_lat; /*XKB NAND CH transfer time in nanoseconds*/

	credit_t avail_credits[NR_CREDIT_ENTRIES];
};

#define BANDWIDTH_TO_TX_TIME(MB_S) (((UNIT_XFER_SIZE)*NS_PER_SEC(1)) / (MB(MB_S)))
#define BANDWIDTH_TO_MAX_CREDITS(MB_S) \
	(MB(MB_S) * UNIT_TIME_INTERVAL / NS_PER_SEC(1) / UNIT_XFER_SIZE * UNIT_XFER_CREDITS)

uint64_t chmodel_request(struct channel_model *ch, uint64_t request_time, uint64_t length);
void chmodel_init(struct channel_model *ch, uint64_t bandwidth /*MB/s*/);
#endif
