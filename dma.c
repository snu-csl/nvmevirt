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

#include <linux/prandom.h>
#include "dma.h"

// #define SHOW_DMA_TRACE

struct dmatest_thread dma_thread;

static bool dmatest_match_channel(struct dmatest_params *params,
		struct dma_chan *chan)
{
	if (params->channel[0] == '\0')
		return true;
	return strcmp(dma_chan_name(chan), params->channel) == 0;
}

static bool dmatest_match_device(struct dmatest_params *params,
		struct dma_device *device)
{
	if (params->device[0] == '\0')
		return true;
	return strcmp(dev_name(device->dev), params->device) == 0;
}

static unsigned long dmatest_random(void)
{
	unsigned long buf;

	prandom_bytes(&buf, sizeof(buf));
	return buf;
}

static inline u8 gen_inv_idx(u8 index, bool is_memset)
{
	u8 val = is_memset ? PATTERN_MEMSET_IDX : index;

	return ~val & PATTERN_COUNT_MASK;
}

static inline u8 gen_src_value(u8 index, bool is_memset)
{
	return PATTERN_SRC | gen_inv_idx(index, is_memset);
}

static inline u8 gen_dst_value(u8 index, bool is_memset)
{
	return PATTERN_DST | gen_inv_idx(index, is_memset);
}

static void dmatest_init_srcs(u8 **bufs, unsigned long start, unsigned int len,
		unsigned int buf_size, bool is_memset)
{
	unsigned int i;
	u8 *buf;

	for (; (buf = *bufs); bufs++) {
		for (i = 0; i < start; i++)
			buf[i] = gen_src_value(i, is_memset);
		for ( ; i < start + len; i++)
			buf[i] = gen_src_value(i, is_memset) | PATTERN_COPY;
		for ( ; i < buf_size; i++)
			buf[i] = gen_src_value(i, is_memset);
		buf++;
	}
}

static void dmatest_init_dsts(u8 **bufs, unsigned long start, unsigned int len,
		unsigned int buf_size, bool is_memset)
{
	unsigned int i;
	u8 *buf;

	for (; (buf = *bufs); bufs++) {
		for (i = 0; i < start; i++)
			buf[i] = gen_dst_value(i, is_memset);
		for ( ; i < start + len; i++)
			buf[i] = gen_dst_value(i, is_memset) |
						PATTERN_OVERWRITE;
		for ( ; i < buf_size; i++)
			buf[i] = gen_dst_value(i, is_memset);
	}
}

static void dmatest_mismatch(u8 actual, u8 pattern, unsigned int index,
		unsigned int counter, bool is_srcbuf, bool is_memset)
{
	u8		diff = actual ^ pattern;
	u8		expected = pattern | gen_inv_idx(counter, is_memset);
	const char	*thread_name = current->comm;

	if (is_srcbuf)
		pr_warn("%s: srcbuf[0x%x] overwritten! Expected %02x, got %02x\n",
			thread_name, index, expected, actual);
	else if ((pattern & PATTERN_COPY)
			&& (diff & (PATTERN_COPY | PATTERN_OVERWRITE)))
		pr_warn("%s: dstbuf[0x%x] not copied! Expected %02x, got %02x\n",
			thread_name, index, expected, actual);
	else if (diff & PATTERN_SRC)
		pr_warn("%s: dstbuf[0x%x] was copied! Expected %02x, got %02x\n",
			thread_name, index, expected, actual);
	else
		pr_warn("%s: dstbuf[0x%x] mismatch! Expected %02x, got %02x\n",
			thread_name, index, expected, actual);
}

static unsigned int dmatest_verify(u8 **bufs, unsigned long start,
		unsigned long end, unsigned long counter, u8 pattern,
		bool is_srcbuf, bool is_memset)
{
#ifdef SHOW_DMA_TRACE
		printk("[DMA_TRACE] %s\n", __func__);
#endif
	unsigned int i;
	unsigned int error_count = 0;
	u8 actual;
	u8 expected;
	u8 *buf;
	unsigned int counter_orig = counter;

	for (; (buf = *bufs); bufs++) {
		counter = counter_orig;
		for (i = start; i < end; i++) {
			actual = buf[i];
			expected = pattern | gen_inv_idx(counter, is_memset);
			if (actual != expected) {
				if (error_count < MAX_ERROR_COUNT)
					dmatest_mismatch(actual, pattern, i,
							 counter, is_srcbuf,
							 is_memset);
				error_count++;
			}
			counter++;
		}
	}

	if (error_count > MAX_ERROR_COUNT)
		pr_warn("%s: %u errors suppressed\n",
			current->comm, error_count - MAX_ERROR_COUNT);

	return error_count;
}

static void result(const char *err, unsigned int n, unsigned long src_off,
		   unsigned long dst_off, unsigned int len, unsigned long data)
{
#ifdef SHOW_DMA_TRACE
	if (IS_ERR_VALUE(data)) {
		pr_info("%s: result #%u: '%s' with src_off=0x%lx dst_off=0x%lx len=0x%x (%ld)\n",
			current->comm, n, err, src_off, dst_off, len, data);
	} else {
		pr_info("%s: result #%u: '%s' with src_off=0x%lx dst_off=0x%lx len=0x%x (%lu)\n",
			current->comm, n, err, src_off, dst_off, len, data);
	}
#else
	if (IS_ERR_VALUE(data)) {
		pr_debug("%s: result #%u: '%s' with src_off=0x%lx dst_off=0x%lx len=0x%x (%ld)\n",
			current->comm, n, err, src_off, dst_off, len, data);
	} else {
		pr_debug("%s: result #%u: '%s' with src_off=0x%lx dst_off=0x%lx len=0x%x (%lu)\n",
			current->comm, n, err, src_off, dst_off, len, data);
	}
#endif
}

static unsigned long long dmatest_persec(s64 runtime, unsigned int val)
{
	unsigned long long per_sec = 1000000;

	if (runtime <= 0)
		return 0;

	/* drop precision until runtime is 32-bits */
	while (runtime > UINT_MAX) {
		runtime >>= 1;
		per_sec <<= 1;
	}

	per_sec *= val;
	per_sec = INT_TO_FIXPT(per_sec);
	do_div(per_sec, runtime);

	return per_sec;
}

static unsigned long long dmatest_KBs(s64 runtime, unsigned long long len)
{
	return FIXPT_TO_INT(dmatest_persec(runtime, len >> 10));
}

static void __dmatest_free_test_data(struct dmatest_data *d, unsigned int cnt)
{
#ifdef SHOW_DMA_TRACE
		printk("[DMA_TRACE] %s\n", __func__);
#endif

	unsigned int i;

	if (! d->is_phys) {
		for (i = 0; i < cnt; i++)
			kfree(d->raw[i]);
	}

	kfree(d->aligned);
	kfree(d->raw);
}

static void dmatest_free_test_data(struct dmatest_data *d)
{
#ifdef SHOW_DMA_TRACE
		printk("[DMA_TRACE] %s\n", __func__);
#endif

	__dmatest_free_test_data(d, d->cnt);
}

static int dmatest_alloc_test_data(struct dmatest_data *d,
		unsigned int buf_size, u8 align)
{
#ifdef SHOW_DMA_TRACE
		printk("[DMA_TRACE] %s\n", __func__);
#endif

	unsigned int i = 0;

	d->raw = kcalloc(d->cnt + 1, sizeof(u8 *), GFP_KERNEL);
	if (!d->raw)
		return -ENOMEM;

	d->aligned = kcalloc(d->cnt + 1, sizeof(u8 *), GFP_KERNEL);
	if (!d->aligned)
		goto err;

	if (d->is_phys) {
		pr_info("Skip allocating test data\n");
		return 0;
	}

	for (i = 0; i < d->cnt; i++) {
		d->raw[i] = kmalloc(buf_size + align, GFP_KERNEL);
		if (!d->raw[i])
			goto err;

		/* align to alignment restriction */
		if (align)
			d->aligned[i] = PTR_ALIGN(d->raw[i], align);
		else
			d->aligned[i] = d->raw[i];
	}

	return 0;
err:
	__dmatest_free_test_data(d, i);
	return -ENOMEM;
}

int dmatest_func(void)
{
#ifdef SHOW_DMA_TRACE
		pr_info("[DMA_TRACE] %s\n", __func__);
#endif

	struct dmatest_thread	*thread = &dma_thread;
	struct dmatest_info	*info;
	struct dmatest_params	*params;
	struct dma_chan		*chan;
	struct dma_device	*dev;
	unsigned int		error_count;
	unsigned int		failed_tests = 0;
	unsigned int		total_tests = 0;
	dma_cookie_t		cookie;
	enum dma_status		status;
	enum dma_ctrl_flags 	flags;
	u8			*pq_coefs = NULL;
	int			ret;
	unsigned int 		buf_size;
	struct dmatest_data	*src;
	struct dmatest_data	*dst;
	int			i;
	ktime_t			ktime, start, diff;
	ktime_t			filltime = 0;
	ktime_t			comparetime = 0;
	s64			runtime = 0;
	unsigned long long	total_len = 0;
	unsigned long long	iops = 0;
	u8			align = 0;
	bool			is_memset = false;
	dma_addr_t		*srcs;
	dma_addr_t		*dma_pq;
	unsigned int iterations = 1;
	unsigned long gpu_start; // byte
	unsigned long gpu_size; // byte
	void *gpu_mapped;

	set_freezable();

	ret = -ENOMEM;

	smp_rmb();
	info = thread->info;
	params = &info->params;
	chan = thread->chan;
	dev = chan->device;
	src = &thread->src;
	dst = &thread->dst;

	// thread->type is always DMA_MEMCPY
	pr_info("thread type: DMA_MEMCPY, align: %d\n", align);
	src->cnt = dst->cnt = 1;

	/* Check if buffer count fits into map count variable (u8) */
	if ((src->cnt + dst->cnt) >= 255) {
		pr_err("too many buffers (%d of 255 supported)\n",
		       src->cnt + dst->cnt);
		goto err_free_coefs;
	}

	buf_size = params->buf_size;

	src->is_phys = false;
	dst->is_phys = false;

	if (dmatest_alloc_test_data(src, buf_size, align) < 0)
		goto err_free_coefs;

	if (dmatest_alloc_test_data(dst, buf_size, align) < 0)
		goto err_src;


	if (src->is_phys) {
		src->raw[0] = gpu_mapped;
		src->aligned[0] = gpu_mapped;
		pr_info("SRC is using physical address, 0x%lx, 0x%lx\n", (unsigned long)src->raw[0], (unsigned long)dst->raw[0]);
	}
	if (dst->is_phys) {
		dst->raw[0] = gpu_mapped;
		dst->aligned[0] = gpu_mapped;
		pr_info("DST is using physical address, 0x%lx, 0x%lx\n", (unsigned long)src->raw[0], (unsigned long)dst->raw[0]);
	}

	srcs = kcalloc(src->cnt, sizeof(dma_addr_t), GFP_KERNEL);
	if (!srcs)
		goto err_dst;

	dma_pq = kcalloc(dst->cnt, sizeof(dma_addr_t), GFP_KERNEL);
	if (!dma_pq)
		goto err_srcs_array;

	/*
	 * src and dst buffers are freed by ourselves below
	 */
	/* Always use polled mode */
	flags = DMA_CTRL_ACK;

	ktime = ktime_get();
	while (total_tests < iterations) {
		struct dma_async_tx_descriptor *tx = NULL;
		struct dmaengine_unmap_data *um;
		dma_addr_t *dsts;
		unsigned int len;

		total_tests++;

		if (params->transfer_size) {
			if (params->transfer_size >= buf_size) {
				pr_err("%u-byte transfer size must be lower than %u-buffer size\n",
				       params->transfer_size, buf_size);
				break;
			} else
				pr_debug("%u-byte transfer size is used for %u-buffer size\n",
					   params->transfer_size, buf_size);
			len = params->transfer_size;
		} else {
			len = dmatest_random() % buf_size + 1;
		}

		/* Do not alter transfer size explicitly defined by user */
		if (!params->transfer_size) {
			len = (len >> align) << align;
			if (!len)
				len = 1 << align;
		}
		total_len += len;

		/* Set random offset */
		src->off = dmatest_random() % (buf_size - len + 1);
		dst->off = dmatest_random() % (buf_size - len + 1);

		src->off = (src->off >> align) << align;
		dst->off = (dst->off >> align) << align;

		/* Init data to verify */
		start = ktime_get();
		dmatest_init_srcs(src->aligned, src->off, len,
					buf_size, is_memset);
		dmatest_init_dsts(dst->aligned, dst->off, len,
					buf_size, is_memset);

		diff = ktime_sub(ktime_get(), start);
		filltime = ktime_add(filltime, diff);

		um = dmaengine_get_unmap_data(dev->dev, src->cnt + dst->cnt,
					      GFP_KERNEL);
		if (!um) {
			failed_tests++;
			result("unmap data NULL", total_tests,
			       src->off, dst->off, len, ret);
			continue;
		}

		um->len = buf_size;
		for (i = 0; i < src->cnt; i++) {
			void *buf = src->aligned[i];
			struct page *pg = virt_to_page(buf);
			unsigned long pg_off = offset_in_page(buf);

			if (src->is_phys) {
				srcs[i] = gpu_start + src->off;
			} else {
				um->addr[i] = dma_map_page(dev->dev, pg, pg_off,
							um->len, DMA_TO_DEVICE);
				srcs[i] = um->addr[i] + src->off;
				ret = dma_mapping_error(dev->dev, um->addr[i]);
				if (ret) {
					result("src mapping error", total_tests,
						src->off, dst->off, len, ret);
					goto error_unmap_continue;
				}
			}
			um->to_cnt++;
		}
		/* map with DMA_BIDIRECTIONAL to force writeback/invalidate */
		dsts = &um->addr[src->cnt];
		for (i = 0; i < dst->cnt; i++) {
			void *buf = dst->aligned[i];
			struct page *pg = virt_to_page(buf);
			unsigned long pg_off = offset_in_page(buf);

			if (dst->is_phys) {
				dsts[i] = gpu_start;
			} else {
				dsts[i] = dma_map_page(dev->dev, pg, pg_off, um->len,
							DMA_BIDIRECTIONAL);
				ret = dma_mapping_error(dev->dev, dsts[i]);
				if (ret) {
					result("dst mapping error", total_tests,
						src->off, dst->off, len, ret);
					goto error_unmap_continue;
				}
			}
			um->bidi_cnt++;
		}

		/* thread->type is always DMA_MEMCPY */
		tx = dev->device_prep_dma_memcpy(chan,
							dsts[0] + dst->off,
							srcs[0], len, flags);

		if (!tx) {
			result("prep error", total_tests, src->off,
			       dst->off, len, ret);
			msleep(100);
			goto error_unmap_continue;
		}

		cookie = tx->tx_submit(tx);

		if (dma_submit_error(cookie)) {
			result("submit error", total_tests, src->off,
			       dst->off, len, ret);
			msleep(100);
			goto error_unmap_continue;
		}

		/* Always use polled mode */
		status = dma_sync_wait(chan, cookie);
		dmaengine_terminate_sync(chan);

		if (status != DMA_COMPLETE &&
			   !(dma_has_cap(DMA_COMPLETION_NO_ORDER,
					 dev->cap_mask) &&
			     status == DMA_OUT_OF_ORDER)) {
			result(status == DMA_ERROR ?
			       "completion error status" :
			       "completion busy status", total_tests, src->off,
			       dst->off, len, ret);
			goto error_unmap_continue;
		}

		dmaengine_unmap_put(um);

		start = ktime_get();
		pr_info("%s: verifying source buffer...\n", current->comm);
		error_count = dmatest_verify(src->aligned, 0, src->off,
				0, PATTERN_SRC, true, is_memset);
		error_count += dmatest_verify(src->aligned, src->off,
				src->off + len, src->off,
				PATTERN_SRC | PATTERN_COPY, true, is_memset);
		error_count += dmatest_verify(src->aligned, src->off + len,
				buf_size, src->off + len,
				PATTERN_SRC, true, is_memset);

		pr_info("%s: verifying dest buffer...\n", current->comm);
		error_count += dmatest_verify(dst->aligned, 0, dst->off,
				0, PATTERN_DST, false, is_memset);
		error_count += dmatest_verify(dst->aligned, dst->off,
				dst->off + len, src->off,
				PATTERN_SRC | PATTERN_COPY, false, is_memset);
		error_count += dmatest_verify(dst->aligned, dst->off + len,
				buf_size, dst->off + len,
				PATTERN_DST, false, is_memset);

		diff = ktime_sub(ktime_get(), start);
		comparetime = ktime_add(comparetime, diff);

		if (error_count) {
			result("data error", total_tests, src->off, dst->off,
			       len, error_count);
			failed_tests++;
		} else {
			result("test passed", total_tests, src->off,
				       dst->off, len, 0);
		}

		continue;

error_unmap_continue:
		dmaengine_unmap_put(um);
		failed_tests++;
	}
	ktime = ktime_sub(ktime_get(), ktime);
	ktime = ktime_sub(ktime, comparetime);
	ktime = ktime_sub(ktime, filltime);
	runtime = ktime_to_us(ktime);

	ret = 0;
	kfree(dma_pq);
err_srcs_array:
	kfree(srcs);
err_dst:
	dmatest_free_test_data(dst);
err_src:
	dmatest_free_test_data(src);
err_free_coefs:
	kfree(pq_coefs);

	iops = dmatest_persec(runtime, total_tests);
	pr_info("%s: summary %u tests, %u failures %llu.%02llu iops %llu KB/s (%d)\n",
		current->comm, total_tests, failed_tests,
		FIXPT_TO_INT(iops), FIXPT_GET_FRAC(iops),
		dmatest_KBs(runtime, total_len), ret);

	/* terminate all transfers on specified channels */
	if (ret || failed_tests)
		dmaengine_terminate_sync(chan);

	return ret;
}

int dmatest_submit(unsigned long src_off, unsigned long dst_off, unsigned int size)
{
#ifdef SHOW_DMA_TRACE
		pr_info("[DMA_TRACE] %s\n", __func__);
#endif

	struct dmatest_thread	*thread = &dma_thread;
	struct dmatest_info	*info;
	struct dmatest_params	*params;
	struct dma_chan		*chan;
	struct dma_device	*dev;
	dma_cookie_t		cookie;
	enum dma_status		status;
	enum dma_ctrl_flags 	flags;
	u8			*pq_coefs = NULL;
	dma_addr_t src_addr, dst_addr;
	int			ret;
	int			i;
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
		result("prep error", 1, src_off,
				dst_off, size, ret);
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
			!(dma_has_cap(DMA_COMPLETION_NO_ORDER,
					dev->cap_mask) &&
				status == DMA_OUT_OF_ORDER)) {
		result(status == DMA_ERROR ?
				"completion error status" :
				"completion busy status", 1, src_off,
				dst_off, size, ret);
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

static int dmatest_add_channel(struct dmatest_info *info,
		struct dma_chan *chan)
{
#ifdef SHOW_DMA_TRACE
		pr_info("[DMA_TRACE] %s\n", __func__);
#endif
	struct dmatest_chan	*dtc;
	struct dma_device	*dma_dev = chan->device;
	unsigned int		thread_count = 0;

	dtc = kmalloc(sizeof(struct dmatest_chan), GFP_KERNEL);
	if (!dtc) {
		pr_warn("No memory for %s\n", dma_chan_name(chan));
		return -ENOMEM;
	}

	dtc->chan = chan;
	INIT_LIST_HEAD(&dtc->threads);

	if (dma_has_cap(DMA_COMPLETION_NO_ORDER, dma_dev->cap_mask) &&
	    info->params.polled) {
		info->params.polled = false;
		pr_warn("DMA_COMPLETION_NO_ORDER, polled disabled\n");
	}

	if (dma_has_cap(DMA_MEMCPY, dma_dev->cap_mask)) {
		pr_info("dmatest_add_threads\n");
		dma_thread.info = info;
		dma_thread.chan = dtc->chan;
		dma_thread.type = DMA_MEMCPY;
	}

	pr_info("Added %u threads using %s\n",
		thread_count, dma_chan_name(chan));

	list_add_tail(&dtc->node, &info->channels);
	info->nr_channels++;

	return 0;
}

static bool filter(struct dma_chan *chan, void *param)
{
	return dmatest_match_channel(param, chan) && dmatest_match_device(param, chan->device);
}

static void request_channels(struct dmatest_info *info,
			     enum dma_transaction_type type)
{
#ifdef SHOW_DMA_TRACE
		pr_info("[DMA_TRACE] %s\n", __func__);
#endif
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(type, mask);
	for (;;) {
		struct dmatest_params *params = &info->params;
		struct dma_chan *chan;

		chan = dma_request_channel(mask, filter, params);
		if (chan) {
			if (dmatest_add_channel(info, chan)) {
				dma_release_channel(chan);
				break; /* add_channel failed, punt */
			}
		} else
			break; /* no more channels available */
		if (params->max_channels &&
		    info->nr_channels >= params->max_channels)
			break; /* we have all we need */
	}
}

static void add_threaded_test(struct dmatest_info *info)
{
#ifdef SHOW_DMA_TRACE
		pr_info("[DMA_TRACE] %s\n", __func__);
#endif
	struct dmatest_params *params = &info->params;

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

int dmatest_chan_set(const char *val)
{
#ifdef SHOW_DMA_TRACE
		pr_info("[DMA_TRACE] %s\n", __func__);
#endif
	struct dmatest_info *info = &test_info;
	struct dmatest_chan *dtc;
	char chan_reset_val[20];
	int ret;

	mutex_lock(&info->lock);
	memcpy(test_channel, val, 20);

	/* Reject channels that are already registered */
	list_for_each_entry(dtc, &info->channels, node) {
		if (strcmp(dma_chan_name(dtc->chan),
				strim(test_channel)) == 0) {
			dtc = list_last_entry(&info->channels,
							struct dmatest_chan,
							node);
			strlcpy(chan_reset_val,
				dma_chan_name(dtc->chan),
				sizeof(chan_reset_val));
			ret = -EBUSY;
			goto add_chan_err;
		}
	}

	add_threaded_test(info);

	/* Check if channel was added successfully */
	if (!list_empty(&info->channels)) {
		/*
		 * if new channel was not successfully added, revert the
		 * "test_channel" string to the name of the last successfully
		 * added channel. exception for when users issues empty string
		 * to channel parameter.
		 */
		dtc = list_last_entry(&info->channels, struct dmatest_chan, node);
		if ((strcmp(dma_chan_name(dtc->chan), strim(test_channel)) != 0)
		    && (strcmp("", strim(test_channel)) != 0)) {
			ret = -EINVAL;
			strlcpy(chan_reset_val, dma_chan_name(dtc->chan),
				sizeof(chan_reset_val));
			pr_err("ERR %d\n", __LINE__);
			goto add_chan_err;
		}

	} else {
		/* Clear test_channel if no channels were added successfully */
		strlcpy(chan_reset_val, "", sizeof(chan_reset_val));
		ret = -EBUSY;
		pr_err("ERR %d\n", __LINE__);
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
