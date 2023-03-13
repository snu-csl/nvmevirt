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

/**
 * struct dmatest_params - test parameters.
 * @buf_size:		size of the memcpy test buffer
 * @channel:		bus ID of the channel to test
 * @device:		bus ID of the DMA Engine to test
 * @max_channels:	maximum number of channels to use
 * @timeout:		transfer timeout in msec, -1 for infinite timeout
 * @transfer_size:	custom transfer size in bytes
 * @polled:		use polling for completion instead of interrupts
 */
struct dmatest_params {
	unsigned int buf_size;
	char channel[20];
	char device[32];
	unsigned int max_channels;
	int timeout;
	unsigned int transfer_size;
	bool polled;
};

/**
 * struct dmatest_info - test information.
 * @params:		test parameters
 * @channels:		channels under test
 * @nr_channels:	number of channels under test
 * @lock:		access protection to the fields of this structure
 * @did_init:		module has been initialized completely
 * @last_error:		test has faced configuration issues
 */
static struct dmatest_info {
	/* Test parameters */
	struct dmatest_params params;

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

struct dmatest_done {
	bool done;
	wait_queue_head_t *wait;
};

struct dmatest_data {
	u8 **raw;
	u8 **aligned;
	unsigned int cnt;
	unsigned long off;
	bool is_phys;
};

struct dmatest_thread {
	struct list_head node;
	struct dmatest_info *info;
	struct task_struct *task;
	struct dma_chan *chan;
	struct dmatest_data src;
	struct dmatest_data dst;
	enum dma_transaction_type type;
};

struct dmatest_chan {
	struct list_head node;
	struct dma_chan *chan;
	struct list_head threads;
};

static char test_channel[20];

/* Maximum amount of mismatched bytes in buffer to print */
#define MAX_ERROR_COUNT 32

/*
 * Initialization patterns. All bytes in the source buffer has bit 7
 * set, all bytes in the destination buffer has bit 7 cleared.
 *
 * Bit 6 is set for all bytes which are to be copied by the DMA
 * engine. Bit 5 is set for all bytes which are to be overwritten by
 * the DMA engine.
 *
 * The remaining bits are the inverse of a counter which increments by
 * one for each byte address.
 */
#define PATTERN_SRC 0x80
#define PATTERN_DST 0x00
#define PATTERN_COPY 0x40
#define PATTERN_OVERWRITE 0x20
#define PATTERN_COUNT_MASK 0x1f
#define PATTERN_MEMSET_IDX 0x01

/* Fixed point arithmetic ops */
#define FIXPT_SHIFT 8
#define FIXPNT_MASK 0xFF
#define FIXPT_TO_INT(a) ((a) >> FIXPT_SHIFT)
#define INT_TO_FIXPT(a) ((a) << FIXPT_SHIFT)
#define FIXPT_GET_FRAC(a) ((((a)&FIXPNT_MASK) * 100) >> FIXPT_SHIFT)

// DMA Init, Final Function
int dmatest_chan_set(const char *val);
int dmatest_func(void);
int dmatest_submit(unsigned long src_off, unsigned long dst_off, unsigned int size);

#endif /* _LIB_DMA_H */
