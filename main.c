// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include <linux/debugfs.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/fs.h>

#ifdef CONFIG_X86
#include <asm/e820/types.h>
#include <asm/e820/api.h>
#endif

#include "nvmev.h"
#include "conv_ftl.h"
#include "zns_ftl.h"
#include "simple_ftl.h"
#include "kv_ftl.h"
#include "dma.h"

/****************************************************************
 * Memory Layout
 ****************************************************************
 * virtDev
 *  - PCI header
 *    -> BAR at 1MiB area
 *  - PCI capability descriptors
 *
 * +--- memmap_start
 * |
 * v
 * +--------------+------------------------------------------+
 * | <---1MiB---> | <---------- Storage Area --------------> |
 * +--------------+------------------------------------------+
 *
 * 1MiB area for metadata
 *  - BAR : 1 page
 *	- DBS : 1 page
 *	- MSI-x table: 16 bytes/entry * 32
 *
 * Storage area
 *
 ****************************************************************/

LIST_HEAD(devices);
struct nvmev *nvmev = NULL;

int io_using_dma = false;

char *cmd;

struct params {
	unsigned long memmap_start;
	unsigned long memmap_size;

	unsigned int read_time;
	unsigned int read_delay;
	unsigned int read_trailing;

	unsigned int write_time;
	unsigned int write_delay;
	unsigned int write_trailing;

	unsigned int nr_io_units;
	unsigned int io_unit_shift;

	char *cpus;
	char *name;
	unsigned int debug;

	unsigned int ftl;
};

/*
static int set_parse_mem_param(const char *val, const struct kernel_param *kp)
{
	unsigned long *arg = (unsigned long *)kp->arg;
	*arg = memparse(val, NULL);
	return 0;
}

static struct kernel_param_ops ops_parse_mem_param = {
	.set = set_parse_mem_param,
	.get = param_get_ulong,
};
*/

static void nvmev_proc_dbs(struct nvmev_dev *nvmev_vdev)
{
	int qid;
	int dbs_idx;
	int new_db;
	int old_db;

	// Admin queue
	new_db = nvmev_vdev->dbs[0];
	if (new_db != nvmev_vdev->old_dbs[0]) {
		nvmev_proc_admin_sq(new_db, nvmev_vdev->old_dbs[0],nvmev_vdev);
		nvmev_vdev->old_dbs[0] = new_db;
	}
	new_db = nvmev_vdev->dbs[1];
	if (new_db != nvmev_vdev->old_dbs[1]) {
		nvmev_proc_admin_cq(new_db, nvmev_vdev->old_dbs[1]);
		nvmev_vdev->old_dbs[1] = new_db;
	}

	// Submission queues
	for (qid = 1; qid <= nvmev_vdev->nr_sq; qid++) {
		if (nvmev_vdev->sqes[qid] == NULL)
			continue;
		dbs_idx = qid * 2;
		new_db = nvmev_vdev->dbs[dbs_idx];
		old_db = nvmev_vdev->old_dbs[dbs_idx];
		if (new_db != old_db) {
			nvmev_vdev->old_dbs[dbs_idx] =
				nvmev_proc_io_sq(qid, new_db, old_db, nvmev_vdev);
		}
	}

	// Completion queues
	for (qid = 1; qid <= nvmev_vdev->nr_cq; qid++) {
		if (nvmev_vdev->cqes[qid] == NULL)
			continue;
		dbs_idx = qid * 2 + 1;
		new_db = nvmev_vdev->dbs[dbs_idx];
		old_db = nvmev_vdev->old_dbs[dbs_idx];
		if (new_db != old_db) {
			nvmev_proc_io_cq(qid, new_db, old_db, nvmev_vdev);
			nvmev_vdev->old_dbs[dbs_idx] = new_db;
		}
	}
}

static int nvmev_dispatcher(void *data)
{
	struct nvmev_dev *nvmev_vdev = (struct nvmev_dev *)data;
	printk("dispatcher's nvmev :%p\n",nvmev_vdev);
	NVMEV_INFO("nvmev_dispatcher started on cpu %d (node %d)\n",
		   nvmev_vdev->config.cpu_nr_dispatcher,
		   cpu_to_node(nvmev_vdev->config.cpu_nr_dispatcher));

	while (!kthread_should_stop()) {
		nvmev_proc_bars(nvmev_vdev);
		nvmev_proc_dbs(nvmev_vdev);
		cond_resched();
	}

	return 0;
}

static void NVMEV_DISPATCHER_INIT(struct nvmev_dev *nvmev_vdev)
{
	char thread_name[32];
	printk("nvmev_vdev : %p\n",nvmev_vdev);
	snprintf(thread_name, sizeof(thread_name), "nvmev_%d_disp", nvmev_vdev->dev_id);

	nvmev_vdev->nvmev_dispatcher =
		kthread_create(nvmev_dispatcher, nvmev_vdev, thread_name);
	if (nvmev_vdev->config.cpu_nr_dispatcher != -1)
		kthread_bind(nvmev_vdev->nvmev_dispatcher, nvmev_vdev->config.cpu_nr_dispatcher);
	wake_up_process(nvmev_vdev->nvmev_dispatcher);
}

static void NVMEV_DISPATCHER_FINAL(struct nvmev_dev *nvmev_vdev)
{
	if (!IS_ERR_OR_NULL(nvmev_vdev->nvmev_dispatcher)) {
		kthread_stop(nvmev_vdev->nvmev_dispatcher);
		nvmev_vdev->nvmev_dispatcher = NULL;
	}
}

#ifdef CONFIG_X86
static int __validate_configs_arch(struct params *p)
{
	unsigned long resv_start_bytes;
	unsigned long resv_end_bytes;

	resv_start_bytes = p->memmap_start;
	resv_end_bytes = resv_start_bytes + p->memmap_size - 1;

	if (e820__mapped_any(resv_start_bytes, resv_end_bytes, E820_TYPE_RAM) ||
	    e820__mapped_any(resv_start_bytes, resv_end_bytes, E820_TYPE_RESERVED_KERN)) {
		NVMEV_ERROR("[mem %#010lx-%#010lx] is usable, not reseved region\n",
			    (unsigned long)resv_start_bytes, (unsigned long)resv_end_bytes);
		return -EPERM;
	}

	if (!e820__mapped_any(resv_start_bytes, resv_end_bytes, E820_TYPE_RESERVED)) {
		NVMEV_ERROR("[mem %#010lx-%#010lx] is not reseved region\n",
			    (unsigned long)resv_start_bytes, (unsigned long)resv_end_bytes);
		return -EPERM;
	}
	return 0;
}
#else
static int __validate_configs_arch(void)
{
	/* TODO: Validate architecture-specific configurations */
	return 0;
}
#endif

static int __validate_configs(struct params *p)
{
	if (!p->memmap_start) {
		NVMEV_ERROR("[memmap_start] should be specified\n");
		return -EINVAL;
	}

	if (!p->memmap_size) {
		NVMEV_ERROR("[memmap_size] should be specified\n");
		return -EINVAL;
	} else if (p->memmap_size <= MB(1)) {
		NVMEV_ERROR("[memmap_size] should be bigger than 1 MiB\n");
		return -EINVAL;
	}

	if (__validate_configs_arch(p)) {
		return -EPERM;
	}

	if (p->nr_io_units == 0 || p->io_unit_shift == 0) {
		NVMEV_ERROR("Need non-zero IO unit size and at least one IO unit\n");
		return -EINVAL;
	}
	if (p->read_time == 0) {
		NVMEV_ERROR("Need non-zero read time\n");
		return -EINVAL;
	}
	if (p->write_time == 0) {
		NVMEV_ERROR("Need non-zero write time\n");
		return -EINVAL;
	}

	return 0;
}

static void __print_perf_configs(struct nvmev_dev *nvmev_vdev)
{
#ifdef CONFIG_NVMEV_VERBOSE
	unsigned long unit_perf_kb =
			nvmev_vdev->config.nr_io_units << (nvmev_vdev->config.io_unit_shift - 10);
	struct nvmev_config *cfg = &nvmev_vdev->config;

	NVMEV_INFO("=============== Configurations ===============\n");
	NVMEV_INFO("* IO units : %d x %d\n",
			cfg->nr_io_units, 1 << cfg->io_unit_shift);
	NVMEV_INFO("* I/O times\n");
	NVMEV_INFO("  Read     : %u + %u x + %u ns\n",
				cfg->read_delay, cfg->read_time, cfg->read_trailing);
	NVMEV_INFO("  Write    : %u + %u x + %u ns\n",
				cfg->write_delay, cfg->write_time, cfg->write_trailing);
	NVMEV_INFO("* Bandwidth\n");
	NVMEV_INFO("  Read     : %lu MiB/s\n",
			(1000000000UL / (cfg->read_time + cfg->read_delay + cfg->read_trailing)) * unit_perf_kb >> 10);
	NVMEV_INFO("  Write    : %lu MiB/s\n",
			(1000000000UL / (cfg->write_time + cfg->write_delay + cfg->write_trailing)) * unit_perf_kb >> 10);
#endif
}

static int __get_nr_entries(int dbs_idx, int queue_size)
{
	int diff = nvmev_vdev->dbs[dbs_idx] - nvmev_vdev->old_dbs[dbs_idx];
	if (diff < 0) {
		diff += queue_size;
	}
	return diff;
}

static struct nvmev_dev *__get_nvmev(const char *dev_name)
{
	struct nvmev_dev *cursor, *next;
	struct nvmev_dev *result = NULL;

	list_for_each_entry_safe(cursor, next, &nvmev->dev_list, list_elem) {
		if (result == NULL && strcmp(dev_name, cursor->dev_name) == 0) {
			result = cursor;
			break;
		}
	}

	return result;
}

static ssize_t __sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	/* TODO: Need a function that search "nvnev-vdev" from file name. */
	/* TODO: Print to file from nvmev_config data. */
	ssize_t len = 0;
	const char *dev_name = kobj->name;
	const char *file_name = attr->attr.name;

	struct nvmev_dev *nvmev_vdev = __get_nvmev(dev_name);
	struct nvmev_config *cfg = NULL;

	if (nvmev_vdev == NULL) {
		printk("No dev");
		return 0;
	}

	cfg = &nvmev_vdev->config;

	if (strcmp(file_name, "read_times") == 0) {
		len = sprintf(buf, "%u + %u x + %u", cfg->read_delay, cfg->read_time, cfg->read_trailing);
	} else if (strcmp(file_name, "write_times") == 0) {
		len = sprintf(buf, "%u + %u x + %u", cfg->write_delay, cfg->write_time, cfg->write_trailing);
	} else if (strcmp(file_name, "io_units") == 0) {
		len = sprintf(buf, "%u x %u", cfg->nr_io_units, cfg->io_unit_shift);
	} else if (strcmp(file_name, "stat") == 0) {
	} else if (strcmp(file_name, "stat") == 0) {
		int i;
		unsigned int nr_in_flight = 0;
		unsigned int nr_dispatch = 0;
		unsigned int nr_dispatched = 0;
		unsigned long long total_io = 0;
		for (i = 1; i <= nvmev_vdev->nr_sq; i++) {
			struct nvmev_submission_queue *sq = nvmev_vdev->sqes[i];
			if (!sq)
				continue;

			len += sprintf(buf, "%2d: %2u %4u %4u %4u %4u %llu\n", i,
				       __get_nr_entries(i * 2, sq->queue_size),
				       sq->stat.nr_in_flight, sq->stat.max_nr_in_flight,
				       sq->stat.nr_dispatch, sq->stat.nr_dispatched,
				       sq->stat.total_io);

			nr_in_flight += sq->stat.nr_in_flight;
			nr_dispatch += sq->stat.nr_dispatch;
			nr_dispatched += sq->stat.nr_dispatched;
			total_io += sq->stat.total_io;

			barrier();
			sq->stat.max_nr_in_flight = 0;
		}
		len += sprintf(buf, "total: %u %u %u %llu\n", nr_in_flight, nr_dispatch,
			       nr_dispatched, total_io);
	} else if (strcmp(file_name, "debug") == 0) {
		/* Left for later use */
	}

	return len;
}

static ssize_t __sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			     size_t count)
{
	/* TODO: Need a function that search "nvnev-vdev" from file name. */
	/* TODO: Scan from file to nvmev_config data. */
	ssize_t len = count;
	unsigned int ret;
	unsigned long long *old_stat;

	const char *dev_name = kobj->name;
	const char *file_name = attr->attr.name;

	struct nvmev_dev *nvmev_vdev = __get_nvmev(dev_name);
	struct nvmev_config *cfg = NULL;

	if (nvmev_vdev == NULL)
		goto out;

	cfg = &nvmev_vdev->config;

	if (!strcmp(file_name, "read_times")) {
		ret = sscanf(buf, "%u %u %u", &cfg->read_delay, &cfg->read_time,
			     &cfg->read_trailing);
		//adjust_ftl_latency(0, cfg->read_time);
	} else if (!strcmp(file_name, "write_times")) {
		ret = sscanf(buf, "%u %u %u", &cfg->write_delay, &cfg->write_time,
			     &cfg->write_trailing);
		//adjust_ftl_latency(1, cfg->write_time);
	} else if (!strcmp(file_name, "io_units")) {
		ret = sscanf(buf, "%d %d", &cfg->nr_io_units, &cfg->io_unit_shift);
		if (ret < 1)
			goto out;

		old_stat = nvmev_vdev->io_unit_stat;
		nvmev_vdev->io_unit_stat =
			kzalloc(sizeof(*nvmev_vdev->io_unit_stat) * cfg->nr_io_units, GFP_KERNEL);

		mdelay(100); /* XXX: Delay the free of old stat so that outstanding
						 * requests accessing the unit_stat are all returned
						 */
		kfree(old_stat);
	} else if (!strcmp(file_name, "stat")) {
		int i;
		for (i = 1; i <= nvmev_vdev->nr_sq; i++) {
			struct nvmev_submission_queue *sq = nvmev_vdev->sqes[i];
			if (!sq)
				continue;

			memset(&sq->stat, 0x00, sizeof(sq->stat));
		}
	} else if (!strcmp(file_name, "debug")) {
		/* Left for later use */
	}

out:
	__print_perf_configs(nvmev_vdev);

	return count;
}

static struct kobj_attribute read_times_attr =
	__ATTR(read_times, 0644, __sysfs_show, __sysfs_store);
static struct kobj_attribute write_times_attr =
	__ATTR(write_times, 0644, __sysfs_show, __sysfs_store);
static struct kobj_attribute io_units_attr = __ATTR(io_units, 0644, __sysfs_show, __sysfs_store);
static struct kobj_attribute stat_attr = __ATTR(stat, 0644, __sysfs_show, __sysfs_store);
static struct kobj_attribute debug_attr = __ATTR(debug, 0644, __sysfs_show, __sysfs_store);

void NVMEV_STORAGE_INIT(struct nvmev_dev *nvmev_vdev)
{
	int ret = 0;

	NVMEV_INFO("Storage: %#010lx-%#010lx (%lu MiB)\n",
			nvmev_vdev->config.storage_start,
			nvmev_vdev->config.storage_start + nvmev_vdev->config.storage_size,
			BYTE_TO_MB(nvmev_vdev->config.storage_size));

	nvmev_vdev->io_unit_stat = kzalloc(
		sizeof(*nvmev_vdev->io_unit_stat) * nvmev_vdev->config.nr_io_units, GFP_KERNEL);

	nvmev_vdev->storage_mapped = memremap(nvmev_vdev->config.storage_start,
					      nvmev_vdev->config.storage_size, MEMREMAP_WB);

	if (nvmev_vdev->storage_mapped == NULL)
		NVMEV_ERROR("Failed to map storage memory.\n");

	printk("addr : %p\n", nvmev_vdev->storage_mapped);

	nvmev_vdev->sysfs_root = kobject_create_and_add(nvmev_vdev->dev_name, nvmev->config_root);
	nvmev_vdev->sysfs_read_times = &read_times_attr;
	nvmev_vdev->sysfs_write_times = &write_times_attr;
	nvmev_vdev->sysfs_io_units = &io_units_attr;
	nvmev_vdev->sysfs_stat = &stat_attr;
	nvmev_vdev->sysfs_debug = &debug_attr;

	ret = sysfs_create_file(nvmev_vdev->sysfs_root, &nvmev_vdev->sysfs_read_times->attr);
	ret = sysfs_create_file(nvmev_vdev->sysfs_root, &nvmev_vdev->sysfs_write_times->attr);
	ret = sysfs_create_file(nvmev_vdev->sysfs_root, &nvmev_vdev->sysfs_io_units->attr);
	ret = sysfs_create_file(nvmev_vdev->sysfs_root, &nvmev_vdev->sysfs_stat->attr);
	ret = sysfs_create_file(nvmev_vdev->sysfs_root, &nvmev_vdev->sysfs_debug->attr);
}

void NVMEV_STORAGE_FINAL(struct nvmev_dev *nvmev_vdev)
{
	sysfs_remove_file(nvmev_vdev->sysfs_root, &nvmev_vdev->sysfs_read_times->attr);
	sysfs_remove_file(nvmev_vdev->sysfs_root, &nvmev_vdev->sysfs_write_times->attr);
	sysfs_remove_file(nvmev_vdev->sysfs_root, &nvmev_vdev->sysfs_io_units->attr);
	sysfs_remove_file(nvmev_vdev->sysfs_root, &nvmev_vdev->sysfs_stat->attr);
	sysfs_remove_file(nvmev_vdev->sysfs_root, &nvmev_vdev->sysfs_debug->attr);

	kobject_put(nvmev_vdev->sysfs_root);

	if (nvmev_vdev->storage_mapped)
		memunmap(nvmev_vdev->storage_mapped);

	if (nvmev_vdev->io_unit_stat)
		kfree(nvmev_vdev->io_unit_stat);
}

static bool __load_configs(struct nvmev_config *config, struct params *p)
{
	bool first = true;
	unsigned int cpu_nr;
	char *cpu;

	if (__validate_configs(p) < 0) {
		return false;
	}

#if (BASE_SSD == KV_PROTOTYPE)
	p->memmap_size -= KV_MAPPING_TABLE_SIZE; // Reserve space for KV mapping table
#endif

	config->memmap_start = p->memmap_start;
	config->memmap_size = p->memmap_size;
	// storage space starts from 1M offset
	config->storage_start = p->memmap_start + MB(1);
	config->storage_size = p->memmap_size - MB(1);

	config->read_time = p->read_time;
	config->read_delay = p->read_delay;
	config->read_trailing = p->read_trailing;
	config->write_time = p->write_time;
	config->write_delay = p->write_delay;
	config->write_trailing = p->write_trailing;
	config->nr_io_units = p->nr_io_units;
	config->io_unit_shift = p->io_unit_shift;

	config->nr_io_workers = 0;
	config->cpu_nr_dispatcher = -1;

	while ((cpu = strsep(&p->cpus, ",")) != NULL) {
		cpu_nr = (unsigned int)simple_strtol(cpu, NULL, 10);
		if (first) {
			config->cpu_nr_dispatcher = cpu_nr;
		} else {
			config->cpu_nr_io_workers[config->nr_io_workers] = cpu_nr;
			config->nr_io_workers++;
		}
		first = false;
	}

	return true;
}

void NVMEV_NAMESPACE_INIT(struct nvmev_dev *nvmev_vdev)
{
	unsigned long long remaining_capacity = nvmev_vdev->config.storage_size;
	void *ns_addr = nvmev_vdev->storage_mapped;
	const int nr_ns = NR_NAMESPACES; // XXX: allow for dynamic nr_ns
	const unsigned int disp_no = nvmev_vdev->config.cpu_nr_dispatcher;
	int i;
	unsigned long long size;

	struct nvmev_ns *ns = kmalloc(sizeof(struct nvmev_ns) * nr_ns, GFP_KERNEL);
	struct ftl_configs *ftl_cfgs = kmalloc(sizeof(struct ftl_configs), GFP_KERNEL);

	for (i = 0; i < nr_ns; i++) {
		if (NS_CAPACITY(i) == 0)
			size = remaining_capacity;
		else
			size = min(NS_CAPACITY(i), remaining_capacity);

		//if (NS_SSD_TYPE(i) == SSD_TYPE_NVM)
		if (nvmev_vdev->ftl == SSD_TYPE_NVM) {
			load_simple_configs(ftl_cfgs);
			simple_init_namespace(&ns[i], i, size, ns_addr, disp_no, ftl_cfgs);
		}
		//else if (NS_SSD_TYPE(i) == SSD_TYPE_CONV)
		else if (nvmev_vdev->ftl == SSD_TYPE_CONV) {
			load_conv_configs(ftl_cfgs);
			conv_init_namespace(&ns[i], i, size, ns_addr, disp_no, ftl_cfgs);
		}
		//else if (NS_SSD_TYPE(i) == SSD_TYPE_ZNS)
		else if (nvmev_vdev->ftl == SSD_TYPE_ZNS) {
			load_zns_configs(ftl_cfgs);
			zns_init_namespace(&ns[i], i, size, ns_addr, disp_no, ftl_cfgs);
		}
		//else if (NS_SSD_TYPE(i) == SSD_TYPE_KV)
		else if (nvmev_vdev->ftl == SSD_TYPE_KV) {
			load_kv_configs(ftl_cfgs);
			kv_init_namespace(&ns[i], i, size, ns_addr, disp_no, ftl_cfgs);
		}
		else
			BUG_ON(1);

		remaining_capacity -= size;
		ns_addr += size;
		NVMEV_INFO("ns %d/%d: size %lld MiB\n", i, nr_ns, BYTE_TO_MB(ns[i].size));

		ns[i].p_dev = nvmev_vdev;
	}

	nvmev_vdev->ns = ns;
	nvmev_vdev->nr_ns = nr_ns;
	nvmev_vdev->mdts = ftl_cfgs->MDTS;

}

void NVMEV_NAMESPACE_FINAL(struct nvmev_dev *nvmev_vdev)
{
	struct nvmev_ns *ns = nvmev_vdev->ns;
	const int nr_ns = NR_NAMESPACES; // XXX: allow for dynamic nvmev_vdev->nr_ns
	int i;

	for (i = 0; i < nr_ns; i++) {
		if (NS_SSD_TYPE(i) == SSD_TYPE_NVM)
			simple_remove_namespace(&ns[i]);
		else if (NS_SSD_TYPE(i) == SSD_TYPE_CONV)
			conv_remove_namespace(&ns[i]);
		else if (NS_SSD_TYPE(i) == SSD_TYPE_ZNS)
			zns_remove_namespace(&ns[i]);
		else if (NS_SSD_TYPE(i) == SSD_TYPE_KV)
			kv_remove_namespace(&ns[i]);
		else
			BUG_ON(1);
	}

	kfree(ns);
	nvmev_vdev->ns = NULL;
}

static char *parse_to_cmd(char *cmd_line)
{
	char *command;

	if (cmd_line == NULL)
		return NULL;

	command = strsep(&cmd_line, " ");

	return command;
}

static void parse_command(char *cmd_line, struct params *p)
{
	char *arg;
	char *param, *val;

	if (cmd_line == NULL) {
		NVMEV_ERROR("cmd_line is NULL");
		return;
	}

	while ((arg = strsep(&cmd_line, " ")) != NULL) {
		next_arg(arg, &param, &val);

		if (strcmp(param, "memmap_start") == 0)
			p->memmap_start = memparse(val, NULL);

		else if (strcmp(param, "memmap_size") == 0)
			p->memmap_size = memparse(val, NULL);

		else if (strcmp(param, "cpus") == 0)
			p->cpus = val;

		else if (strcmp(param, "name") == 0)
			p->name = val;

		else if (strcmp(param, "ftl") == 0) {
			if (strcmp(val, "simple") == 0)
				p->ftl = SSD_TYPE_NVM;
			else if (strcmp(val, "conv") == 0)
				p->ftl = SSD_TYPE_CONV;
			else if (strcmp(val, "kv") == 0)
				p->ftl = SSD_TYPE_KV;
			else if (strcmp(val, "zns") == 0)
				p->ftl = SSD_TYPE_ZNS;
		}
	}
}

static struct params *PARAM_INIT(void)
{
	struct params *params;

	params = kzalloc(sizeof(struct params), GFP_KERNEL);

	params->memmap_start = 0;
	params->memmap_size = 0;

	params->read_time = 1;
	params->read_delay = 1;
	params->read_trailing = 0;

	params->write_time = 1;
	params->write_delay = 1;
	params->write_trailing = 0;

	params->nr_io_units = 8;
	params->io_unit_shift = 12;

	params->debug = 0;

	params->name = NULL;
	params->cpus = NULL;

	params->ftl = SSD_TYPE_NVM;

	return params;
}

static void __print_base_config(void)
{
	const char *type = "unknown";
	switch (BASE_SSD) {
	case INTEL_OPTANE:
		type = "NVM SSD";
		printk("delete implementation please");
		break;
	case SAMSUNG_970PRO:
		type = "Samsung 970 Pro SSD";
		break;
	case ZNS_PROTOTYPE:
		type = "ZNS SSD Prototype";
		break;
	case KV_PROTOTYPE:
		type = "KVSSD Prototype";
		break;
	case WD_ZN540:
		type = "WD ZN540 ZNS SSD";
		break;
	}

	NVMEV_INFO("Version %x.%x for >> %s <<\n",
			(NVMEV_VERSION & 0xff00) >> 8, (NVMEV_VERSION & 0x00ff), type);
}
int dev_open(struct inode *inode,struct file *filp){
	printk("open dev\n");
	return 0;
}

struct file_operations dev_fops =
{
	.open = dev_open,
};

static int create_device(struct params *p)
{
	struct nvmev_dev *nvmev_vdev;
	nvmev_vdev = VDEV_INIT();

	if (!nvmev_vdev)
		return -EINVAL;

	if (!__load_configs(&nvmev_vdev->config, p)) {
		goto ret_err;
	}

	/* Put the list of devices for managing. */
	INIT_LIST_HEAD(&nvmev_vdev->list_elem);
	list_add(&nvmev_vdev->list_elem, &nvmev->dev_list);

	/* Alloc dev ID from number of device. */
	nvmev_vdev->dev_id = nvmev->nr_dev++;

	/* Load ftl. */
	nvmev_vdev->ftl = p->ftl;

	/* Load name. */
	if (p->name != NULL)
		strncpy(nvmev_vdev->dev_name, p->name, sizeof(nvmev_vdev->dev_name));

	else
		snprintf(nvmev_vdev->dev_name, sizeof(nvmev_vdev->dev_name), "nvmev_%d", nvmev_vdev->dev_id);
	
	nvmev_vdev->major = register_chrdev(0,nvmev_vdev->dev_name,&dev_fops);
	if(nvmev_vdev->major < 0)
		printk("fault major\n");
	else
		printk("success major\n");

	printk("storage\n");
	NVMEV_STORAGE_INIT(nvmev_vdev);

	NVMEV_NAMESPACE_INIT(nvmev_vdev);

	if (io_using_dma) {
		if (ioat_dma_chan_set("dma7chan0") != 0) {
			io_using_dma = false;
			NVMEV_ERROR("Cannot use DMA engine, Fall back to memcpy\n");
		}
	}

	if (!NVMEV_PCI_INIT(nvmev_vdev)) {
		goto ret_err;
	}

	__print_perf_configs(nvmev_vdev);

	NVMEV_IO_WORKER_INIT(nvmev_vdev);

	NVMEV_DISPATCHER_INIT(nvmev_vdev);
	
	pci_bus_add_devices(nvmev_vdev->virt_bus);	

	NVMEV_INFO("Virtual NVMe device created\n");

	return 0;

ret_err:
	printk("error......\n");
	
	nvmev->nr_dev--;
	list_del(&nvmev_vdev->list_elem);
	VDEV_FINALIZE(nvmev_vdev);
	return -EIO;
}

static int delete_device(struct nvmev_dev *nvmev_vdev)
{
	int i;

	nvmev->nr_dev--;

	unregister_chrdev(nvmev_vdev->major,nvmev_vdev->dev_name);

	if (nvmev_vdev->virt_bus != NULL) {
		pci_stop_root_bus(nvmev_vdev->virt_bus);
		pci_remove_root_bus(nvmev_vdev->virt_bus);
	}

	NVMEV_DISPATCHER_FINAL(nvmev_vdev);
	NVMEV_IO_WORKER_FINAL(nvmev_vdev);

	NVMEV_NAMESPACE_FINAL(nvmev_vdev);
	NVMEV_STORAGE_FINAL(nvmev_vdev);

	if (io_using_dma) {
		ioat_dma_cleanup();
	}

	for (i = 0; i < nvmev_vdev->nr_sq; i++) {
		kfree(nvmev_vdev->sqes[i]);
	}

	for (i = 0; i < nvmev_vdev->nr_cq; i++) {
		kfree(nvmev_vdev->cqes[i]);
	}

	list_del(&nvmev_vdev->list_elem);

	VDEV_FINALIZE(nvmev_vdev);

	printk("Virtual NVMe device deleted\n");

	return 0;
}

static int delete_all(void)
{
	struct nvmev_dev *cursor, *next;

	list_for_each_entry_safe(cursor, next, &nvmev->dev_list, list_elem)
		delete_device(cursor);

	return 0;
}

static ssize_t __config_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t __config_store(struct kobject *kobj, struct kobj_attribute *attr,
			      const char __user *buf, size_t count)
{
	ssize_t len = count;
	const char *filename = attr->attr.name;
	size_t nr_copied;
	char input[128];

	struct nvmev_dev *nvmev_vdev;
	struct params *p;

	strncpy(input, buf, min(count, sizeof(input)));
	printk("Command Start, Command is %s\n", input);

	p = PARAM_INIT();
	cmd = parse_to_cmd(input);
	parse_command(input + (sizeof(cmd) - 1), p);

	if (strcmp(cmd, "create") == 0) {
		printk("Memmap_start = %ld\n", p->memmap_start);
		printk("Memmap_size = %ld\n", p->memmap_size);
		printk("cpus = %s\n", p->cpus);
		printk("ftl = %u\n", p->ftl);

		if (p->name != NULL) {
			printk("name = %s\n", p->name);
		}

		printk("return = %d\n", create_device(p));
	}

	else if (strcmp(cmd, "delete") == 0) {
		
		if(p->name != NULL){
			printk("target device: %s\n", p->name);
		
			nvmev_vdev = __get_nvmev(p->name);
			delete_device(nvmev_vdev);
		}
		else{
			printk("Do not have target device\n");
		}
	} 

	else {
		NVMEV_ERROR("Doesn't not command.");
	}

	return len;
}

static struct kobj_attribute config_attr = __ATTR(config, 0664, __config_show, __config_store);

static int NVMeV_init(void)
{
	int ret = 0;
	nvmev_vdev = NULL;

	nvmev = kzalloc(sizeof(struct nvmev), GFP_KERNEL);

	INIT_LIST_HEAD(&nvmev->dev_list);
	nvmev->nr_dev = 0;

	nvmev->config_root = kobject_create_and_add("nvmevirt", NULL);
	nvmev->config_attr = &config_attr;

	if (sysfs_create_file(nvmev->config_root, &nvmev->config_attr->attr)) {
		printk("Cannot create sysfs file...\n");
		return -EIO;
	}

	NVMEV_INFO("Successfully load Virtual NVMe device module\n");
	return 0;
}

static void NVMeV_exit(void)
{
	delete_all();

	sysfs_remove_file(nvmev->config_root, &nvmev->config_attr->attr);
	kobject_put(nvmev->config_root);

	kfree(nvmev);

	NVMEV_INFO("Virtual NVMe device closed\n");
}

MODULE_LICENSE("GPL v2");
module_init(NVMeV_init);
module_exit(NVMeV_exit);
