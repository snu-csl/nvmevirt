// SPDX-License-Identifier: GPL-2.0-only

#include <linux/err.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/freezer.h>
#include <linux/init.h>
#include <linux/sched/task.h>
#include <linux/slab.h>

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

struct ioat_dma_thread {
	struct ioat_dma_info *info;
	struct dma_chan *chan;
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

static struct ioat_dma_thread dma_thread;

static bool ioat_dma_match_channel(struct ioat_dma_params *params, struct dma_chan *chan)
{
	if (params->channel[0] == '\0')
		return true;
	return strcmp(dma_chan_name(chan), params->channel) == 0;
}

static bool ioat_dma_match_device(struct ioat_dma_params *params, struct dma_device *device)
{
	if (params->device[0] == '\0')
		return true;
	return strcmp(dev_name(device->dev), params->device) == 0;
}

static void result(const char *err, unsigned int n, dma_addr_t src_addr, dma_addr_t dst_addr,
		   unsigned int len, unsigned long data)
{
	pr_debug("%s: result #%u: '%s' with src_addr=0x%llx dst_addr=0x%llx len=0x%x (%ld)\n",
		 current->comm, n, err, src_addr, dst_addr, len, data);
}

int ioat_dma_submit(dma_addr_t src_addr, dma_addr_t dst_addr, unsigned int size)
{
	struct ioat_dma_thread *thread = &dma_thread;
	struct ioat_dma_info *info;
	struct dma_chan *chan;
	struct dma_device *dev;
	dma_cookie_t cookie;
	enum dma_status status;
	enum dma_ctrl_flags flags = DMA_CTRL_ACK; /* Always use polled mode */
	int ret;
	int i;
	struct dma_async_tx_descriptor *tx = NULL;

	set_freezable();

	pr_debug("START: 0x%llx -> 0x%llx, len: %d\n", src_addr, dst_addr, size);

	ret = -ENOMEM;

	smp_rmb();
	info = thread->info;
	chan = thread->chan;
	dev = chan->device;

	/* thread->type is always DMA_MEMCPY */
	tx = dev->device_prep_dma_memcpy(chan, dst_addr, src_addr, size, flags);

	if (!tx) {
		result("prep error", 1, src_addr, dst_addr, size, ret);
		msleep(100);
		goto out;
	}

	cookie = tx->tx_submit(tx);

	if (dma_submit_error(cookie)) {
		result("submit error", 1, src_addr, dst_addr, size, ret);
		msleep(100);
		goto out;
	}

	/* Always use polled mode */
	status = dma_sync_wait(chan, cookie);
	dmaengine_terminate_sync(chan);

	if (status != DMA_COMPLETE &&
	    !(dma_has_cap(DMA_COMPLETION_NO_ORDER, dev->cap_mask) && status == DMA_OUT_OF_ORDER)) {
		result(status == DMA_ERROR ? "completion error status" : "completion busy status",
		       1, src_addr, dst_addr, size, ret);
		goto out;
	}

	ret = 0;

out:
	pr_debug("DONE: 0x%llx -> 0x%llx, len: %d\n", src_addr, dst_addr, size);

	/* terminate all transfers on specified channels */
	if (ret)
		dmaengine_terminate_sync(chan);

	return ret;
}

static int ioat_dma_add_channel(struct ioat_dma_info *info, struct dma_chan *chan)
{
	struct ioat_dma_chan *dtc;
	struct dma_device *dma_dev = chan->device;
	unsigned int thread_count = 0;

	dtc = kmalloc(sizeof(struct ioat_dma_chan), GFP_KERNEL);
	if (!dtc) {
		pr_warn("No memory for %s\n", dma_chan_name(chan));
		return -ENOMEM;
	}

	dtc->chan = chan;
	INIT_LIST_HEAD(&dtc->threads);

	if (dma_has_cap(DMA_COMPLETION_NO_ORDER, dma_dev->cap_mask) && info->params.polled) {
		info->params.polled = false;
		pr_warn("DMA_COMPLETION_NO_ORDER, polled disabled\n");
	}

	if (dma_has_cap(DMA_MEMCPY, dma_dev->cap_mask)) {
		pr_info("ioat_dma_add_threads\n");
		dma_thread.info = info;
		dma_thread.chan = dtc->chan;
		dma_thread.type = DMA_MEMCPY;
	}

	pr_info("Added %u threads using %s\n", thread_count, dma_chan_name(chan));

	list_add_tail(&dtc->node, &info->channels);
	info->nr_channels++;

	return 0;
}

static bool filter(struct dma_chan *chan, void *param)
{
	return ioat_dma_match_channel(param, chan) && ioat_dma_match_device(param, chan->device);
}

static void request_channels(struct ioat_dma_info *info, enum dma_transaction_type type)
{
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(type, mask);
	for (;;) {
		struct ioat_dma_params *params = &info->params;
		struct dma_chan *chan;

		chan = dma_request_channel(mask, filter, params);
		if (chan) {
			if (ioat_dma_add_channel(info, chan)) {
				dma_release_channel(chan);
				break; /* add_channel failed, punt */
			}
		} else
			break; /* no more channels available */
		if (params->max_channels && info->nr_channels >= params->max_channels)
			break; /* we have all we need */
	}
}

static void add_threaded_dma(struct ioat_dma_info *info)
{
	struct ioat_dma_params *params = &info->params;

	/* Copy test parameters */
	params->buf_size = test_buf_size;
	strlcpy(params->channel, strim(test_channel), sizeof(params->channel));
	strlcpy(params->device, strim(test_device), sizeof(params->device));
	params->max_channels = max_channels;
	params->timeout = timeout;
	params->transfer_size = transfer_size;
	params->polled = polled;

	request_channels(info, DMA_MEMCPY);
}

int ioat_dma_chan_set(const char *val)
{
	struct ioat_dma_info *info = &test_info;
	struct ioat_dma_chan *dtc;
	int ret = 0;

	BUG_ON(strlen(val) >= CHANNEL_NAME_LEN);

	mutex_lock(&info->lock);
	strcpy(test_channel, val);

	/* Reject channels that are already registered */
	list_for_each_entry(dtc, &info->channels, node) {
		if (strcmp(dma_chan_name(dtc->chan), strim(test_channel)) == 0) {
			dtc = list_last_entry(&info->channels, struct ioat_dma_chan, node);
			ret = -EBUSY;
			goto add_chan_err;
		}
	}

	add_threaded_dma(info);

	/* Check if channel was added successfully */
	if (!list_empty(&info->channels)) {
		/*
		 * if new channel was not successfully added, revert the
		 * "test_channel" string to the name of the last successfully
		 * added channel. exception for when users issues empty string
		 * to channel parameter.
		 */
		dtc = list_last_entry(&info->channels, struct ioat_dma_chan, node);
		if ((strcmp(dma_chan_name(dtc->chan), strim(test_channel)) != 0) &&
		    (strcmp("", strim(test_channel)) != 0)) {
			ret = -EINVAL;
			pr_err("ERROR on DMA engine %d\n", __LINE__);
			goto add_chan_err;
		}

	} else {
		/* Clear test_channel if no channels were added successfully */
		ret = -EBUSY;
		pr_err("ERROR on DMA engine %d\n", __LINE__);
		goto add_chan_err;
	}

	info->last_error = ret;
	mutex_unlock(&info->lock);

	return ret;

add_chan_err:
	info->last_error = ret;
	mutex_unlock(&info->lock);

	return ret;
}

static void ioat_dma_cleanup_channel(struct ioat_dma_chan *dtc)
{
	/* terminate all transfers on specified channels */
	dmaengine_terminate_sync(dtc->chan);

	kfree(dtc);
}

void ioat_dma_cleanup(void)
{
	struct ioat_dma_info *info = &test_info;

	struct ioat_dma_chan *dtc, *_dtc;
	struct dma_chan *chan;

	list_for_each_entry_safe(dtc, _dtc, &info->channels, node) {
		list_del(&dtc->node);
		chan = dtc->chan;
		ioat_dma_cleanup_channel(dtc);
		pr_debug("dropped channel %s\n", dma_chan_name(chan));
		dma_release_channel(chan);
	}

	info->nr_channels = 0;
}
