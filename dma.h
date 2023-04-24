// SPDX-License-Identifier: GPL-2.0-only

#ifndef _LIB_DMA_H
#define _LIB_DMA_H

#include <linux/err.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/freezer.h>
#include <linux/init.h>
#include <linux/sched/task.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/wait.h>

// Size of the memcpy test buffer
static unsigned int test_buf_size = 4096;

// Bus ID of the DMA Engine to test (default: any)
static char test_device[32];

// Maximum number of channels to use (default: all)
static unsigned int max_channels;

// Transfer Timeout in msec (default: 3000), Pass -1 for infinite timeout
static int timeout = 3000;

// Optional custom transfer size in bytes (default: not used (0))
static unsigned int transfer_size = 1024;

// Use polling for completion instead of interrupts
static bool polled = true;

#define CHANNEL_NAME_LEN 20

/**
 * struct ioat_dma_params - test parameters.
 * @buf_size:		size of the memcpy test buffer
 * @channel:		bus ID of the channel to test
 * @device:		bus ID of the DMA Engine to test
 * @max_channels:	maximum number of channels to use
 * @timeout:		transfer timeout in msec, -1 for infinite timeout
 * @transfer_size:	custom transfer size in bytes
 * @polled:		use polling for completion instead of interrupts
 */
struct ioat_dma_params {
	unsigned int buf_size;
	char channel[CHANNEL_NAME_LEN];
	char device[32];
	unsigned int max_channels;
	int timeout;
	unsigned int transfer_size;
	bool polled;
};

/**
 * struct ioat_dma_info - test information.
 * @params:		test parameters
 * @channels:		channels under test
 * @nr_channels:	number of channels under test
 * @lock:		access protection to the fields of this structure
 * @did_init:		module has been initialized completely
 * @last_error:		test has faced configuration issues
 */
static struct ioat_dma_info {
	/* Test parameters */
	struct ioat_dma_params params;

	/* Internal state */
	struct list_head channels;
	unsigned int nr_channels;
	int last_error;
	struct mutex lock;
	bool did_init;
} test_info = {
	.channels = LIST_HEAD_INIT(test_info.channels),
	.lock = __MUTEX_INITIALIZER(test_info.lock),
};

struct ioat_dma_done {
	bool done;
	wait_queue_head_t *wait;
};

struct ioat_dma_data {
	u8 **raw;
	u8 **aligned;
	unsigned int cnt;
	unsigned long off;
	bool is_phys;
};

struct ioat_dma_thread {
	struct list_head node;
	struct ioat_dma_info *info;
	struct task_struct *task;
	struct dma_chan *chan;
	struct ioat_dma_data src;
	struct ioat_dma_data dst;
	enum dma_transaction_type type;
};

struct ioat_dma_chan {
	struct list_head node;
	struct dma_chan *chan;
	struct list_head threads;
};

static char test_channel[CHANNEL_NAME_LEN];

/* Maximum amount of mismatched bytes in buffer to print */
#define MAX_ERROR_COUNT 32

// DMA Init, Final Function
int ioat_dma_chan_set(const char *val);
int ioat_dma_submit(unsigned long src_off, unsigned long dst_off, unsigned int size);

#endif /* _LIB_DMA_H */
