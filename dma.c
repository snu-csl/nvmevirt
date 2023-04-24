// SPDX-License-Identifier: GPL-2.0-only

#include "dma.h"

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

static void result(const char *err, unsigned int n, unsigned long src_off, unsigned long dst_off,
		   unsigned int len, unsigned long data)
{
	if (IS_ERR_VALUE(data)) {
		pr_debug("%s: result #%u: '%s' with src_off=0x%lx dst_off=0x%lx len=0x%x (%ld)\n",
			 current->comm, n, err, src_off, dst_off, len, data);
	} else {
		pr_debug("%s: result #%u: '%s' with src_off=0x%lx dst_off=0x%lx len=0x%x (%lu)\n",
			 current->comm, n, err, src_off, dst_off, len, data);
	}
}

int ioat_dma_submit(unsigned long src_off, unsigned long dst_off, unsigned int size)
{
	struct ioat_dma_thread *thread = &dma_thread;
	struct ioat_dma_info *info;
	struct ioat_dma_params *params;
	struct dma_chan *chan;
	struct dma_device *dev;
	dma_cookie_t cookie;
	enum dma_status status;
	enum dma_ctrl_flags flags;
	u8 *pq_coefs = NULL;
	dma_addr_t src_addr, dst_addr;
	int ret;
	int i;
	struct dma_async_tx_descriptor *tx = NULL;

	set_freezable();

	pr_debug("START: 0x%lx -> 0x%lx, len: %d\n", src_off, dst_off, size);

	ret = -ENOMEM;

	smp_rmb();
	info = thread->info;
	params = &info->params;
	chan = thread->chan;
	dev = chan->device;

	/* Always use polled mode */
	flags = DMA_CTRL_ACK;

	src_addr = src_off;
	dst_addr = dst_off;

	/* thread->type is always DMA_MEMCPY */
	tx = dev->device_prep_dma_memcpy(chan, dst_addr, src_addr, size, flags);

	if (!tx) {
		result("prep error", 1, src_off, dst_off, size, ret);
		msleep(100);
		goto err_out;
	}

	cookie = tx->tx_submit(tx);

	if (dma_submit_error(cookie)) {
		result("submit error", 1, src_off, dst_off, size, ret);
		msleep(100);
		goto err_out;
	}

	/* Always use polled mode */
	status = dma_sync_wait(chan, cookie);
	dmaengine_terminate_sync(chan);

	if (status != DMA_COMPLETE &&
	    !(dma_has_cap(DMA_COMPLETION_NO_ORDER, dev->cap_mask) && status == DMA_OUT_OF_ORDER)) {
		result(status == DMA_ERROR ? "completion error status" : "completion busy status",
		       1, src_off, dst_off, size, ret);
		goto err_out;
	}

	ret = 0;

err_out:
	pr_debug("DONE: 0x%lx -> 0x%lx, len: %d\n", src_off, dst_off, size);

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
	char chan_reset_val[CHANNEL_NAME_LEN];
	int ret = 0;

	mutex_lock(&info->lock);
	BUG_ON(strlen(val) >= CHANNEL_NAME_LEN);
	strcpy(test_channel, val);

	/* Reject channels that are already registered */
	list_for_each_entry(dtc, &info->channels, node) {
		if (strcmp(dma_chan_name(dtc->chan), strim(test_channel)) == 0) {
			dtc = list_last_entry(&info->channels, struct ioat_dma_chan, node);
			strlcpy(chan_reset_val, dma_chan_name(dtc->chan), sizeof(chan_reset_val));
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
			strlcpy(chan_reset_val, dma_chan_name(dtc->chan), sizeof(chan_reset_val));
			pr_err("ERROR on DMA engine %d\n", __LINE__);
			goto add_chan_err;
		}

	} else {
		/* Clear test_channel if no channels were added successfully */
		strlcpy(chan_reset_val, "", sizeof(chan_reset_val));
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
