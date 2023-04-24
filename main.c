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

/****************************************************************
 * Argument
 ****************************************************************
 * 1. Memmap start
 * 2. Memmap size
 ****************************************************************/

struct nvmev_dev *nvmev_vdev = NULL;

static unsigned long memmap_start = 0;
static unsigned long memmap_size = 0;

static unsigned int read_time = 1;
static unsigned int read_delay = 1;
static unsigned int read_trailing = 0;

static unsigned int write_time = 1;
static unsigned int write_delay = 1;
static unsigned int write_trailing = 0;

static unsigned int nr_io_units = 8;
static unsigned int io_unit_shift = 12;

static char *cpus;
static unsigned int debug = 0;

extern int io_using_dma;

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

module_param_cb(memmap_start, &ops_parse_mem_param, &memmap_start, 0444);
MODULE_PARM_DESC(memmap_start, "Reserved memory address");
module_param_cb(memmap_size, &ops_parse_mem_param, &memmap_size, 0444);
MODULE_PARM_DESC(memmap_size, "Reserved memory size");
module_param(read_time, uint, 0644);
MODULE_PARM_DESC(read_time, "Read time in nanoseconds");
module_param(read_delay, uint, 0644);
MODULE_PARM_DESC(read_delay, "Read delay in nanoseconds");
module_param(read_trailing, uint, 0644);
MODULE_PARM_DESC(read_trailing, "Read trailing in nanoseconds");
module_param(write_time, uint, 0644);
MODULE_PARM_DESC(write_time, "Write time in nanoseconds");
module_param(write_delay, uint, 0644);
MODULE_PARM_DESC(write_delay, "Write delay in nanoseconds");
module_param(write_trailing, uint, 0644);
MODULE_PARM_DESC(write_trailing, "Write trailing in nanoseconds");
module_param(nr_io_units, uint, 0444);
MODULE_PARM_DESC(nr_io_units, "Number of I/O units that operate in parallel");
module_param(io_unit_shift, uint, 0444);
MODULE_PARM_DESC(io_unit_shift, "Size of each I/O unit (2^)");
module_param(cpus, charp, 0444);
MODULE_PARM_DESC(cpus, "CPU list for process, completion(int.) threads, Seperated by Comma(,)");
module_param(debug, uint, 0644);

static void nvmev_proc_dbs(void)
{
	int qid;
	int dbs_idx;
	int new_db;
	int old_db;

	// Admin queue
	new_db = nvmev_vdev->dbs[0];
	if (new_db != nvmev_vdev->old_dbs[0]) {
		nvmev_proc_admin_sq(new_db, nvmev_vdev->old_dbs[0]);
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
			nvmev_vdev->old_dbs[dbs_idx] = nvmev_proc_io_sq(qid, new_db, old_db);
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
			nvmev_proc_io_cq(qid, new_db, old_db);
			nvmev_vdev->old_dbs[dbs_idx] = new_db;
		}
	}
}

static int nvmev_dispatcher(void *data)
{
	NVMEV_INFO("nvmev_dispatcher started on cpu %d (node %d)\n",
		   nvmev_vdev->config.cpu_nr_dispatcher,
		   cpu_to_node(nvmev_vdev->config.cpu_nr_dispatcher));

	while (!kthread_should_stop()) {
		nvmev_proc_bars();
		nvmev_proc_dbs();

		cond_resched();
	}

	return 0;
}

static void NVMEV_DISPATCHER_INIT(struct nvmev_dev *nvmev_vdev)
{
	nvmev_vdev->nvmev_manager = kthread_create(nvmev_dispatcher, NULL, "nvmev_dispatcher");
	if (nvmev_vdev->config.cpu_nr_dispatcher != -1)
		kthread_bind(nvmev_vdev->nvmev_manager, nvmev_vdev->config.cpu_nr_dispatcher);
	wake_up_process(nvmev_vdev->nvmev_manager);
}

static void NVMEV_REG_PROC_FINAL(struct nvmev_dev *nvmev_vdev)
{
	if (!IS_ERR_OR_NULL(nvmev_vdev->nvmev_manager)) {
		kthread_stop(nvmev_vdev->nvmev_manager);
		nvmev_vdev->nvmev_manager = NULL;
	}
}

#ifdef CONFIG_X86
static int __validate_configs_arch(void)
{
	unsigned long resv_start_bytes;
	unsigned long resv_end_bytes;

	resv_start_bytes = memmap_start;
	resv_end_bytes = resv_start_bytes + memmap_size - 1;

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

static int __validate_configs(void)
{
	if (!memmap_start) {
		NVMEV_ERROR("[memmap_start] should be specified\n");
		return -EINVAL;
	}

	if (!memmap_size) {
		NVMEV_ERROR("[memmap_size] should be specified\n");
		return -EINVAL;
	} else if (memmap_size <= MB(1)) {
		NVMEV_ERROR("[memmap_size] should be bigger than 1 MiB\n");
		return -EINVAL;
	}

	if (__validate_configs_arch()) {
		return -EPERM;
	}

	if (nr_io_units == 0 || io_unit_shift == 0) {
		NVMEV_ERROR("Need non-zero IO unit size and at least one IO unit\n");
		return -EINVAL;
	}
	if (read_time == 0) {
		NVMEV_ERROR("Need non-zero read time\n");
		return -EINVAL;
	}
	if (write_time == 0) {
		NVMEV_ERROR("Need non-zero write time\n");
		return -EINVAL;
	}

	return 0;
}

static void __print_perf_configs(void)
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

static int __proc_file_read(struct seq_file *m, void *data)
{
	const char *filename = m->private;
	struct nvmev_config *cfg = &nvmev_vdev->config;

	if (strcmp(filename, "read_times") == 0) {
		seq_printf(m, "%u + %u x + %u", cfg->read_delay, cfg->read_time,
			   cfg->read_trailing);
	} else if (strcmp(filename, "write_times") == 0) {
		seq_printf(m, "%u + %u x + %u", cfg->write_delay, cfg->write_time,
			   cfg->write_trailing);
	} else if (strcmp(filename, "io_units") == 0) {
		seq_printf(m, "%u x %u", cfg->nr_io_units, cfg->io_unit_shift);
	} else if (strcmp(filename, "stat") == 0) {
		int i;
		unsigned int nr_in_flight = 0;
		unsigned int nr_dispatch = 0;
		unsigned int nr_dispatched = 0;
		unsigned long long total_io = 0;
		for (i = 1; i <= nvmev_vdev->nr_sq; i++) {
			struct nvmev_submission_queue *sq = nvmev_vdev->sqes[i];
			if (!sq)
				continue;

			seq_printf(m, "%2d: %2u %4u %4u %4u %4u %llu\n", i,
				   __get_nr_entries(i * 2, sq->queue_size), sq->stat.nr_in_flight,
				   sq->stat.max_nr_in_flight, sq->stat.nr_dispatch,
				   sq->stat.nr_dispatched, sq->stat.total_io);

			nr_in_flight += sq->stat.nr_in_flight;
			nr_dispatch += sq->stat.nr_dispatch;
			nr_dispatched += sq->stat.nr_dispatched;
			total_io += sq->stat.total_io;

			barrier();
			sq->stat.max_nr_in_flight = 0;
		}
		seq_printf(m, "total: %u %u %u %llu\n", nr_in_flight, nr_dispatch, nr_dispatched,
			   total_io);
	} else if (strcmp(filename, "debug") == 0) {
		/* Left for later use */
	}

	return 0;
}

static ssize_t __proc_file_write(struct file *file, const char __user *buf, size_t len,
				 loff_t *offp)
{
	ssize_t count = len;
	const char *filename = file->f_path.dentry->d_name.name;
	char input[128];
	unsigned int ret;
	unsigned long long *old_stat;
	struct nvmev_config *cfg = &nvmev_vdev->config;
	size_t nr_copied;

	nr_copied = copy_from_user(input, buf, min(len, sizeof(input)));

	if (!strcmp(filename, "read_times")) {
		ret = sscanf(input, "%u %u %u", &cfg->read_delay, &cfg->read_time,
			     &cfg->read_trailing);
		//adjust_ftl_latency(0, cfg->read_time);
	} else if (!strcmp(filename, "write_times")) {
		ret = sscanf(input, "%u %u %u", &cfg->write_delay, &cfg->write_time,
			     &cfg->write_trailing);
		//adjust_ftl_latency(1, cfg->write_time);
	} else if (!strcmp(filename, "io_units")) {
		ret = sscanf(input, "%d %d", &cfg->nr_io_units, &cfg->io_unit_shift);
		if (ret < 1)
			goto out;

		old_stat = nvmev_vdev->io_unit_stat;
		nvmev_vdev->io_unit_stat =
			kzalloc(sizeof(*nvmev_vdev->io_unit_stat) * cfg->nr_io_units, GFP_KERNEL);

		mdelay(100); /* XXX: Delay the free of old stat so that outstanding
						 * requests accessing the unit_stat are all returned
						 */
		kfree(old_stat);
	} else if (!strcmp(filename, "stat")) {
		int i;
		for (i = 1; i <= nvmev_vdev->nr_sq; i++) {
			struct nvmev_submission_queue *sq = nvmev_vdev->sqes[i];
			if (!sq)
				continue;

			memset(&sq->stat, 0x00, sizeof(sq->stat));
		}
	} else if (!strcmp(filename, "debug")) {
		/* Left for later use */
	}

out:
	__print_perf_configs();

	return count;
}

static int __proc_file_open(struct inode *inode, struct file *file)
{
	return single_open(file, __proc_file_read, (char *)file->f_path.dentry->d_name.name);
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 0, 0)
static const struct proc_ops proc_file_fops = {
	.proc_open = __proc_file_open,
	.proc_write = __proc_file_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#else
static const struct file_operations proc_file_fops = {
	.open = __proc_file_open,
	.write = __proc_file_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

void NVMEV_STORAGE_INIT(struct nvmev_dev *nvmev_vdev)
{
	NVMEV_INFO("Storage : %lx + %lx\n", nvmev_vdev->config.storage_start,
		   nvmev_vdev->config.storage_size);

	nvmev_vdev->io_unit_stat = kzalloc(
		sizeof(*nvmev_vdev->io_unit_stat) * nvmev_vdev->config.nr_io_units, GFP_KERNEL);

	nvmev_vdev->storage_mapped = memremap(nvmev_vdev->config.storage_start,
					      nvmev_vdev->config.storage_size, MEMREMAP_WB);

	if (nvmev_vdev->storage_mapped == NULL)
		NVMEV_ERROR("Failed to map storage memory.\n");

	nvmev_vdev->proc_root = proc_mkdir("nvmev", NULL);
	nvmev_vdev->proc_read_times =
		proc_create("read_times", 0664, nvmev_vdev->proc_root, &proc_file_fops);
	nvmev_vdev->proc_write_times =
		proc_create("write_times", 0664, nvmev_vdev->proc_root, &proc_file_fops);
	nvmev_vdev->proc_io_units =
		proc_create("io_units", 0664, nvmev_vdev->proc_root, &proc_file_fops);
	nvmev_vdev->proc_stat = proc_create("stat", 0444, nvmev_vdev->proc_root, &proc_file_fops);
	nvmev_vdev->proc_stat = proc_create("debug", 0444, nvmev_vdev->proc_root, &proc_file_fops);
}

void NVMEV_STORAGE_FINAL(struct nvmev_dev *nvmev_vdev)
{
	remove_proc_entry("read_times", nvmev_vdev->proc_root);
	remove_proc_entry("write_times", nvmev_vdev->proc_root);
	remove_proc_entry("io_units", nvmev_vdev->proc_root);
	remove_proc_entry("stat", nvmev_vdev->proc_root);
	remove_proc_entry("debug", nvmev_vdev->proc_root);

	remove_proc_entry("nvmev", NULL);

	if (nvmev_vdev->storage_mapped)
		memunmap(nvmev_vdev->storage_mapped);

	if (nvmev_vdev->io_unit_stat)
		kfree(nvmev_vdev->io_unit_stat);
}

static bool __load_configs(struct nvmev_config *config)
{
	bool first = true;
	unsigned int cpu_nr;
	char *cpu;

	if (__validate_configs() < 0) {
		return false;
	}

#if (BASE_SSD == KV_PROTOTYPE)
	memmap_size -= KV_MAPPING_TABLE_SIZE; // Reserve space for KV mapping table
#endif

	config->memmap_start = memmap_start;
	config->memmap_size = memmap_size;
	// storage space starts from 1M offset
	config->storage_start = memmap_start + MB(1);
	config->storage_size = memmap_size - MB(1);

	config->read_time = read_time;
	config->read_delay = read_delay;
	config->read_trailing = read_trailing;
	config->write_time = write_time;
	config->write_delay = write_delay;
	config->write_trailing = write_trailing;
	config->nr_io_units = nr_io_units;
	config->io_unit_shift = io_unit_shift;

	config->nr_io_cpu = 0;
	config->cpu_nr_dispatcher = -1;

	while ((cpu = strsep(&cpus, ",")) != NULL) {
		cpu_nr = (unsigned int)simple_strtol(cpu, NULL, 10);
		if (first) {
			config->cpu_nr_dispatcher = cpu_nr;
		} else {
			config->cpu_nr_io_workers[config->nr_io_cpu] = cpu_nr;
			config->nr_io_cpu++;
		}
		first = false;
	}

	return true;
}

void NVMEV_NAMESPACE_INIT(struct nvmev_dev *nvmev_vdev)
{
	unsigned long long remaining_capacity = nvmev_vdev->config.storage_size;
	void *ns_addr = nvmev_vdev->storage_mapped;
	const int nr_ns = NR_NAMESPACES;
	const unsigned int disp_no = nvmev_vdev->config.cpu_nr_dispatcher;
	int i;
	unsigned long long size;

	struct nvmev_ns *ns = kmalloc(sizeof(struct nvmev_ns) * nr_ns, GFP_KERNEL);

	for (i = 0; i < nr_ns; i++) {
		if (NS_CAPACITY(i) == 0)
			size = remaining_capacity;
		else
			size = min(NS_CAPACITY(i), remaining_capacity);

		if (NS_SSD_TYPE(i) == SSD_TYPE_NVM)
			simple_init_namespace(&ns[i], i, size, ns_addr, disp_no);
		else if (NS_SSD_TYPE(i) == SSD_TYPE_CONV)
			conv_init_namespace(&ns[i], i, size, ns_addr, disp_no);
		else if (NS_SSD_TYPE(i) == SSD_TYPE_ZNS)
			zns_init_namespace(&ns[i], i, size, ns_addr, disp_no);
		else if (NS_SSD_TYPE(i) == SSD_TYPE_KV)
			kv_init_namespace(&ns[i], i, size, ns_addr, disp_no);
		else
			NVMEV_ASSERT(0);

		remaining_capacity -= size;
		ns_addr += size;
		NVMEV_INFO("[%s] ns=%d ns_addr=%p ns_size=%lld(MiB) \n", __FUNCTION__, i,
			   ns[i].mapped, BYTE_TO_MB(ns[i].size));
	}

	nvmev_vdev->ns = ns;
	nvmev_vdev->nr_ns = nr_ns;
	nvmev_vdev->mdts = MDTS;
}

void NVMEV_NAMESPACE_FINAL(struct nvmev_dev *nvmev_vdev)
{
	//TODO : should free memory allocated in ssd_init, zns_init, kv_init
}

static int NVMeV_init(void)
{
	int ret = 0;
	
	nvmev_vdev = VDEV_INIT();
	if (!nvmev_vdev)
		return -EINVAL;

	if (!__load_configs(&nvmev_vdev->config)) {
		goto ret_err;
	}

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

	__print_perf_configs();

	NVMEV_IO_PROC_INIT(nvmev_vdev);
	NVMEV_DISPATCHER_INIT(nvmev_vdev);

	pci_bus_add_devices(nvmev_vdev->virt_bus);

	NVMEV_INFO("Successfully created Virtual NVMe deivce\n");

	return 0;

ret_err:
	VDEV_FINALIZE(nvmev_vdev);
	return -EIO;
}

static void NVMeV_exit(void)
{
	NVMEV_REG_PROC_FINAL(nvmev_vdev);
	NVMEV_IO_PROC_FINAL(nvmev_vdev);

	NVMEV_NAMESPACE_FINAL(nvmev_vdev);
	NVMEV_STORAGE_FINAL(nvmev_vdev);

	if (nvmev_vdev->virt_bus != NULL) {
		int i;
		pci_remove_root_bus(nvmev_vdev->virt_bus);

		for (i = 0; i < nvmev_vdev->nr_sq; i++) {
			kfree(nvmev_vdev->sqes[i]);
		}

		for (i = 0; i < nvmev_vdev->nr_cq; i++) {
			kfree(nvmev_vdev->cqes[i]);
		}
	}
	VDEV_FINALIZE(nvmev_vdev);

	NVMEV_INFO("Virtual NVMe device closed\n");
}

MODULE_LICENSE("GPL v2");
module_init(NVMeV_init);
module_exit(NVMeV_exit);
