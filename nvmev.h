// SPDX-License-Identifier: GPL-2.0-only

#ifndef _LIB_NVMEV_H
#define _LIB_NVMEV_H

#include <linux/pci.h>
#include <linux/msi.h>
#include <asm/apic.h>

#include "nvme.h"

#define CONFIG_NVMEV_IO_WORKER_BY_SQ
#undef CONFIG_NVMEV_FAST_X86_IRQ_HANDLING

#undef CONFIG_NVMEV_VERBOSE
#undef CONFIG_NVMEV_DEBUG
#undef CONFIG_NVMEV_DEBUG_VERBOSE

/*************************/
#define NVMEV_DRV_NAME "NVMeVirt"
#define NVMEV_VERSION 0x0110
#define NVMEV_DEVICE_ID	NVMEV_VERSION
#define NVMEV_VENDOR_ID 0x0c51
#define NVMEV_SUBSYSTEM_ID	0x370d
#define NVMEV_SUBSYSTEM_VENDOR_ID NVMEV_VENDOR_ID

#define NVMEV_INFO(string, args...) printk(KERN_INFO "%s: " string, NVMEV_DRV_NAME, ##args)
#define NVMEV_ERROR(string, args...) printk(KERN_ERR "%s: " string, NVMEV_DRV_NAME, ##args)
#define NVMEV_ASSERT(x) BUG_ON((!(x)))

#ifdef CONFIG_NVMEV_DEBUG
#define  NVMEV_DEBUG(string, args...) printk(KERN_INFO "%s: " string, NVMEV_DRV_NAME, ##args)
#ifdef CONFIG_NVMEV_DEBUG_VERBOSE
#define  NVMEV_DEBUG_VERBOSE(string, args...) printk(KERN_INFO "%s: " string, NVMEV_DRV_NAME, ##args)
#else
#define  NVMEV_DEBUG_VERBOSE(string, args...)
#endif
#else
#define NVMEV_DEBUG(string, args...)
#define NVMEV_DEBUG_VERBOSE(string, args...)
#endif


#define NR_MAX_IO_QUEUE 72
#define NR_MAX_PARALLEL_IO 16384

#define NVMEV_INTX_IRQ 15

#define PAGE_OFFSET_MASK (PAGE_SIZE - 1)
#define PRP_PFN(x) ((unsigned long)((x) >> PAGE_SHIFT))

#define KB(k) ((k) << 10)
#define MB(m) ((m) << 20)
#define GB(g) ((g) << 30)

#define BYTE_TO_KB(b) ((b) >> 10)
#define BYTE_TO_MB(b) ((b) >> 20)
#define BYTE_TO_GB(b) ((b) >> 30)

#define MS_PER_SEC(s) ((s)*1000)
#define US_PER_SEC(s) (MS_PER_SEC(s) * 1000)
#define NS_PER_SEC(s) (US_PER_SEC(s) * 1000)

#define LBA_TO_BYTE(lba) ((lba) << 9)
#define BYTE_TO_LBA(byte) ((byte) >> 9)

#define BITMASK32_ALL (0xFFFFFFFF)
#define BITMASK64_ALL (0xFFFFFFFFFFFFFFFF)
#define ASSERT(X)

#include "ssd_config.h"

struct nvmev_sq_stat {
	unsigned int nr_dispatched;
	unsigned int nr_dispatch;
	unsigned int nr_in_flight;
	unsigned int max_nr_in_flight;
	unsigned long long total_io;
};

struct nvmev_submission_queue {
	int qid;
	int cqid;
	int priority;
	bool phys_contig;

	int queue_size;

	struct nvmev_sq_stat stat;

	struct nvme_command __iomem **sq;
};

struct nvmev_completion_queue {
	int qid;
	int irq_vector;
	bool irq_enabled;
	bool interrupt_ready;
	bool phys_contig;

	spinlock_t entry_lock;
	spinlock_t irq_lock;

	int queue_size;

	int phase;
	int cq_head;
	int cq_tail;

	struct nvme_completion __iomem **cq;
};

struct nvmev_admin_queue {
	int phase;

	int sq_depth;
	int cq_depth;

	int cq_head;

	struct nvme_command __iomem **nvme_sq;
	struct nvme_completion __iomem **nvme_cq;
};

#define NR_SQE_PER_PAGE (PAGE_SIZE / sizeof(struct nvme_command))
#define NR_CQE_PER_PAGE (PAGE_SIZE / sizeof(struct nvme_completion))

#define SQ_ENTRY_TO_PAGE_NUM(entry_id) (entry_id / NR_SQE_PER_PAGE)
#define CQ_ENTRY_TO_PAGE_NUM(entry_id) (entry_id / NR_CQE_PER_PAGE)

#define SQ_ENTRY_TO_PAGE_OFFSET(entry_id) (entry_id % NR_SQE_PER_PAGE)
#define CQ_ENTRY_TO_PAGE_OFFSET(entry_id) (entry_id % NR_CQE_PER_PAGE)

struct nvmev_config {
	unsigned long memmap_start; // byte
	unsigned long memmap_size; // byte

	unsigned long storage_start; //byte
	unsigned long storage_size; // byte

	unsigned int cpu_nr_dispatcher;
	unsigned int nr_io_workers;
	unsigned int cpu_nr_io_workers[32];

	/* TODO Refactoring storage configurations */
	unsigned int nr_io_units;
	unsigned int io_unit_shift; // 2^

	unsigned int read_delay; // ns
	unsigned int read_time; // ns
	unsigned int read_trailing; // ns
	unsigned int write_delay; // ns
	unsigned int write_time; // ns
	unsigned int write_trailing; // ns
};

struct nvmev_io_work {
	int sqid;
	int cqid;

	int sq_entry;
	unsigned int command_id;

	unsigned long long nsecs_start;
	unsigned long long nsecs_target;

	unsigned long long nsecs_enqueue;
	unsigned long long nsecs_copy_start;
	unsigned long long nsecs_copy_done;
	unsigned long long nsecs_cq_filled;

	bool is_copied;
	bool is_completed;

	unsigned int status;
	unsigned int result0;
	unsigned int result1;

	bool is_internal;
	void *write_buffer;
	size_t buffs_to_release;

	unsigned int next, prev;
};

struct nvmev_io_worker {
	struct nvmev_dev *parent_dev;
	struct nvmev_io_work *work_queue;

	unsigned int free_seq; /* free io req head index */
	unsigned int free_seq_end; /* free io req tail index */
	unsigned int io_seq; /* io req head index */
	unsigned int io_seq_end; /* io req tail index */

	unsigned long long latest_nsecs;

	unsigned int id;
	struct task_struct *task_struct;
	char thread_name[32];
};

struct nvmev_dev {
	struct pci_bus *virt_bus;
	void *virtDev;
	struct pci_header *pcihdr;
	struct pci_pm_cap *pmcap;
	struct pci_msix_cap *msixcap;
	struct pcie_cap *pciecap;
	struct pci_ext_cap *extcap;

	struct pci_dev *pdev;
	struct pci_sysdata pci_sysdata;

	struct nvmev_config config;
	struct task_struct *nvmev_dispatcher;

	void *storage_mapped;

	struct nvmev_io_worker *io_workers;
	unsigned int io_worker_turn;

	void __iomem *msix_table;

	bool intx_disabled;

	struct __nvme_bar *old_bar;
	struct nvme_ctrl_regs __iomem *bar;

	u32 *old_dbs;
	u32 __iomem *dbs;

	struct nvmev_ns *ns;
	unsigned int nr_ns;
	unsigned int nr_sq;
	unsigned int nr_cq;

	struct nvmev_admin_queue *admin_q;
	struct nvmev_submission_queue *sqes[NR_MAX_IO_QUEUE + 1];
	struct nvmev_completion_queue *cqes[NR_MAX_IO_QUEUE + 1];

	unsigned int mdts;

	struct dentry *debug_root;
	struct dentry *debug_read_times;
	struct dentry *debug_write_times;
	struct dentry *debug_io_units;
	struct dentry *debug_stat;

	struct kobject *sysfs_root;
	struct kobj_attribute *sysfs_read_times;
	struct kobj_attribute *sysfs_write_times;
	struct kobj_attribute *sysfs_io_units;
	struct kobj_attribute *sysfs_stat;
	struct kobj_attribute *sysfs_debug;

	unsigned long long *io_unit_stat;

	struct list_head list_elem;
	char dev_name[30];

	unsigned int dev_id;
	
	unsigned int ftl;
};

struct nvmev_request {
	struct nvme_command *cmd;
	uint32_t sq_id;
	uint64_t nsecs_start;
};

struct nvmev_result {
	uint32_t status;
	uint64_t nsecs_target;
};

struct nvmev_ns {
	uint32_t id;
	uint32_t csi;
	uint64_t size;
	void *mapped;

	/*conv ftl or zns or kv*/
	uint32_t nr_parts; // partitions
	void *ftls; // ftl instances. one ftl per partition

	/*io command handler*/
	bool (*proc_io_cmd)(struct nvmev_ns *ns, struct nvmev_request *req,
			    struct nvmev_result *ret,struct nvmev_dev *nvmev_vdev);

	/*specific CSS io command identifier*/
	bool (*identify_io_cmd)(struct nvmev_ns *ns, struct nvme_command cmd);
	/*specific CSS io command processor*/
	unsigned int (*perform_io_cmd)(struct nvmev_ns *ns, struct nvme_command *cmd,
				       uint32_t *status);
};

struct nvmev {
	struct list_head dev_list;
	unsigned int nr_dev;

	struct kobject *config_root;
	struct kobj_attribute *config_attr;
};

// VDEV Init, Final Function
//extern struct nvmev_dev *nvmev_vdev;
extern struct nvmev *nvmev;
struct nvmev_dev *VDEV_INIT(void);
void VDEV_FINALIZE(struct nvmev_dev *nvmev_vdev);

// OPS_PCI
void nvmev_proc_bars(struct nvmev_dev *nvmev_vdev);
bool NVMEV_PCI_INIT(struct nvmev_dev *dev);
void nvmev_signal_irq(struct nvmev_dev *nvmev_vdev,int msi_index);

// OPS ADMIN QUEUE
void nvmev_proc_admin_sq(int new_db, int old_db,struct nvmev_dev *nvmev_vdev);
void nvmev_proc_admin_cq(int new_db, int old_db);

// OPS I/O QUEUE
void NVMEV_IO_WORKER_INIT(struct nvmev_dev *nvmev_vdev);
void NVMEV_IO_WORKER_FINAL(struct nvmev_dev *nvmev_vdev);
int nvmev_proc_io_sq(int qid, int new_db, int old_db,struct nvmev_dev *nvmev_vdev);
void nvmev_proc_io_cq(int qid, int new_db, int old_db,struct nvmev_dev *nvmev_vdev);

#endif /* _LIB_NVMEV_H */
