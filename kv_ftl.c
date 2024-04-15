// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ktime.h>
#include <linux/highmem.h>
#include <linux/sched/clock.h>

#include "nvmev.h"
#include "kv_ftl.h"

static const struct allocator_ops append_only_ops = {
	.init = append_only_allocator_init,
	.allocate = append_only_allocate,
	.kill = append_only_kill,
};

static const struct allocator_ops bitmap_ops = {
	.init = bitmap_allocator_init,
	.allocate = bitmap_allocate,
	.kill = bitmap_kill,
};

static inline unsigned long long __get_wallclock(void)
{
	return cpu_clock(nvmev_vdev->config.cpu_nr_dispatcher);
}

static size_t __cmd_io_size(struct nvme_rw_command *cmd)
{
	NVMEV_DEBUG("%d lba %llu length %d, %llx %llx\n", cmd->opcode, cmd->slba, cmd->length,
		    cmd->prp1, cmd->prp2);

	return (cmd->length + 1) << LBA_BITS;
}

static unsigned int cmd_key_length(struct nvme_kv_command cmd)
{
	if (cmd.common.opcode == nvme_cmd_kv_store) {
		return cmd.kv_store.key_len + 1;
	} else if (cmd.common.opcode == nvme_cmd_kv_retrieve) {
		return cmd.kv_retrieve.key_len + 1;
	} else if (cmd.common.opcode == nvme_cmd_kv_delete) {
		return cmd.kv_delete.key_len + 1;
	} else {
		return cmd.kv_store.key_len + 1;
	}
}

static unsigned int cmd_value_length(struct nvme_kv_command cmd)
{
	if (cmd.common.opcode == nvme_cmd_kv_store) {
		return cmd.kv_store.value_len << 2;
	} else if (cmd.common.opcode == nvme_cmd_kv_retrieve) {
		return cmd.kv_retrieve.value_len << 2;
	} else {
		return cmd.kv_store.value_len << 2;
	}
}

/* Return the time to complete */
static unsigned long long __schedule_io_units(int opcode, unsigned long lba, unsigned int length,
					      unsigned long long nsecs_start)
{
	unsigned int io_unit_size = 1 << nvmev_vdev->config.io_unit_shift;
	unsigned int io_unit =
		(lba >> (nvmev_vdev->config.io_unit_shift - LBA_BITS)) % nvmev_vdev->config.nr_io_units;
	int nr_io_units = min(nvmev_vdev->config.nr_io_units, DIV_ROUND_UP(length, io_unit_size));

	unsigned long long latest; /* Time of completion */
	unsigned int delay = 0;
	unsigned int latency = 0;
	unsigned int trailing = 0;

	if (opcode == nvme_cmd_write || opcode == nvme_cmd_kv_store ||
	    opcode == nvme_cmd_kv_batch) {
		delay = nvmev_vdev->config.write_delay;
		latency = nvmev_vdev->config.write_time;
		trailing = nvmev_vdev->config.write_trailing;
	} else if (opcode == nvme_cmd_read || opcode == nvme_cmd_kv_retrieve) {
		delay = nvmev_vdev->config.read_delay;
		latency = nvmev_vdev->config.read_time;
		trailing = nvmev_vdev->config.read_trailing;
	}

	latest = max(nsecs_start, nvmev_vdev->io_unit_stat[io_unit]) + delay;

	do {
		latest += latency;
		nvmev_vdev->io_unit_stat[io_unit] = latest;

		if (nr_io_units-- > 0) {
			nvmev_vdev->io_unit_stat[io_unit] += trailing;
		}

		length -= min(length, io_unit_size);
		if (++io_unit >= nvmev_vdev->config.nr_io_units)
			io_unit = 0;
	} while (length > 0);

	return latest;
}

static unsigned long long __schedule_flush(struct nvmev_request *req)
{
	unsigned long long latest = 0;
	int i;

	for (i = 0; i < nvmev_vdev->config.nr_io_units; i++) {
		latest = max(latest, nvmev_vdev->io_unit_stat[i]);
	}

	return latest;
}

/* KV-SSD Mapping Management */

static size_t allocate_mem_offset(struct kv_ftl *kv_ftl, struct nvme_kv_command cmd)
{
	if (cmd.common.opcode == nvme_cmd_kv_store) {
		u64 length_bytes = cmd_value_length(cmd);
		size_t offset;

		offset = kv_ftl->allocator_ops.allocate(length_bytes, NULL);

		if (offset == -1) {
			NVMEV_ERROR("mem alloc failed");
			return 0;
		} else {
			NVMEV_DEBUG("allocate memory offset %lu for %u %u\n", offset,
				    cmd_key_length(cmd), cmd_value_length(cmd));
			return offset;
		}
	} else {
		NVMEV_ERROR("Couldn't allocate mem offset %d", cmd.common.opcode);
		return 0;
	}
}

static size_t allocate_mem_offset_by_length(struct kv_ftl *kv_ftl, int val_len)
{
	u64 length_bytes = val_len;
	size_t offset;

	offset = kv_ftl->allocator_ops.allocate(length_bytes, NULL);

	if (offset == -1) {
		NVMEV_ERROR("mem alloc failed");
		return 0;
	} else {
		NVMEV_DEBUG("allocate memory offset %lu for %u\n", offset, val_len);
		return offset;
	}
}

static unsigned int get_hash_slot(struct kv_ftl *kv_ftl, char *key, u32 key_len)
{
	return hash_function(key, key_len) % kv_ftl->hash_slots;
}

static void chain_mapping(struct kv_ftl *kv_ftl, unsigned int prev, unsigned int slot)
{
	kv_ftl->kv_mapping_table[prev].next_slot = slot;
}

static unsigned int find_next_slot(struct kv_ftl *kv_ftl, int original_slot, int *prev_slot)
{
	unsigned int ret_slot = original_slot;

	// 1. Find the tail of the link.
	unsigned int tail = original_slot;
	unsigned int prevs = -1;
	while (kv_ftl->kv_mapping_table[tail].mem_offset != -1) {	
		prevs = tail;
		tail = kv_ftl->kv_mapping_table[tail].next_slot;
		if (tail == -1) break;
	}

	ret_slot = prevs;
	*prev_slot = prevs;

	// 2. Search the next available slots starting from the tail.
	while (kv_ftl->kv_mapping_table[ret_slot].mem_offset != -1) {
		ret_slot++;
		if (ret_slot >= kv_ftl->hash_slots)
			ret_slot = 0;
	}

	// *prev_slot = original_slot;

	if (prev_slot < 0) {
		NVMEV_ERROR("Prev slot less than 0\n");
	}

	NVMEV_DEBUG("Collision at slot %d, found new slot %u\n", original_slot, ret_slot);
	if (ret_slot - original_slot > 3)
		NVMEV_DEBUG("Slot difference: %d\n", ret_slot - original_slot);

	return ret_slot;
}

static unsigned int new_mapping_entry(struct kv_ftl *kv_ftl, struct nvme_kv_command cmd,
				      size_t val_offset)
{
	unsigned int slot = -1;
	unsigned int prev_slot;
	BUG_ON(val_offset < 0 || val_offset >= nvmev_vdev->config.storage_size);

	slot = get_hash_slot(kv_ftl, cmd.kv_store.key, cmd_key_length(cmd));

	prev_slot = -1;
	if (kv_ftl->kv_mapping_table[slot].mem_offset != -1) {
		NVMEV_DEBUG("Collision\n");
		slot = find_next_slot(kv_ftl, slot, &prev_slot);
	}

	if (slot < 0 || slot >= kv_ftl->hash_slots) {
		NVMEV_ERROR("slot < 0 || slot >= kv_ftl->hash_slots\n");
	}

	memcpy(kv_ftl->kv_mapping_table[slot].key, cmd.kv_store.key, cmd.kv_store.key_len + 1);
	kv_ftl->kv_mapping_table[slot].mem_offset = val_offset;
	kv_ftl->kv_mapping_table[slot].length = cmd_value_length(cmd);
	/* hash chaining */
	if (prev_slot != -1) {
		NVMEV_DEBUG("Linking slot %d to new slot %d", prev_slot, slot);
		chain_mapping(kv_ftl, prev_slot, slot);
	}

	NVMEV_DEBUG("New mapping entry key %s offset %lu length %u slot %u\n", cmd.kv_store.key,
		    val_offset, cmd_value_length(cmd), slot);

	return 0;
}

static unsigned int new_mapping_entry_by_key(struct kv_ftl *kv_ftl, unsigned char *key, int key_len,
					     int val_len, size_t val_offset)
{
	unsigned int slot = -1;
	unsigned int prev_slot;
	BUG_ON(val_offset < 0 || val_offset >= nvmev_vdev->config.storage_size);

	slot = get_hash_slot(kv_ftl, key, key_len);

	prev_slot = -1;
	if (kv_ftl->kv_mapping_table[slot].mem_offset != -1) {
		NVMEV_DEBUG("Collision\n");
		slot = find_next_slot(kv_ftl, slot, &prev_slot);
	}

	if (slot < 0 || slot >= kv_ftl->hash_slots) {
		NVMEV_ERROR("slot < 0 || slot >= kv_ftl->hash_slots\n");
	}

	memcpy(kv_ftl->kv_mapping_table[slot].key, key, key_len);
	kv_ftl->kv_mapping_table[slot].mem_offset = val_offset;
	kv_ftl->kv_mapping_table[slot].length = val_len;
	/* hash chaining */
	if (prev_slot != -1) {
		NVMEV_DEBUG("Linking slot %d to new slot %d", prev_slot, slot);
		chain_mapping(kv_ftl, prev_slot, slot);
	}

	NVMEV_DEBUG("New mapping entry key %s offset %lu length %u slot %u\n", key, val_offset,
		    val_len, slot);

	return 0;
}

static unsigned int update_mapping_entry(struct kv_ftl *kv_ftl, struct nvme_kv_command cmd)
{
	unsigned int slot = 0;
	bool found = false;
	// u64 t0, t1;

	u32 count = 0;

	// t0 = ktime_get_ns();
	slot = get_hash_slot(kv_ftl, cmd.kv_store.key, cmd_key_length(cmd));
	// t1 = ktime_get_ns();
	// printk("Hashing took %llu\n", t1-t0);

	while (kv_ftl->kv_mapping_table[slot].mem_offset != -1) {
		NVMEV_DEBUG("Comparing %s | %.*s\n", cmd.kv_store.key, cmd_key_length(cmd),
			    kv_ftl->kv_mapping_table[slot].key);
		count++;

		if (count > 10) {
			NVMEV_ERROR("Searched %u times", count);
			// break;
		}

		if (memcmp(cmd.kv_store.key, kv_ftl->kv_mapping_table[slot].key,
			   cmd_key_length(cmd)) == 0) {
			NVMEV_DEBUG("1 Found\n");
			found = true;
			break;
		}

		slot = kv_ftl->kv_mapping_table[slot].next_slot;
		if (slot == -1)
			break;
		// t1 = ktime_get_ns();
		// printk("Comparison took %llu", t1-t0);
	}

	if (found) {
		NVMEV_DEBUG("Updating mapping length %lu to %u for key %s\n",
			    kv_ftl->kv_mapping_table[slot].length, cmd_value_length(cmd),
			    cmd.kv_store.key);
		kv_ftl->kv_mapping_table[slot].length = cmd_value_length(cmd);
	}

	if (!found) {
		NVMEV_ERROR("No mapping found for key %s\n", cmd.kv_store.key);
		return 1;
	}

	return 0;
}

static struct mapping_entry get_mapping_entry(struct kv_ftl *kv_ftl, struct nvme_kv_command cmd)
{
	struct mapping_entry mapping;
	// char *key = NULL;
	unsigned int slot = 0;
	bool found = false;
	// u64 t0, t1;

	u32 count = 0;

	memset(&mapping, -1, sizeof(struct mapping_entry)); // init mapping

	// t0 = ktime_get_ns();
	slot = get_hash_slot(kv_ftl, cmd.kv_store.key, cmd_key_length(cmd));
	// t1 = ktime_get_ns();
	// printk("Hashing took %llu\n", t1-t0);

	while (kv_ftl->kv_mapping_table[slot].mem_offset != -1) {
		NVMEV_DEBUG("Comparing %s | %.*s\n", cmd.kv_store.key, cmd_key_length(cmd),
			    kv_ftl->kv_mapping_table[slot].key);
		count++;

		if (count > 10) {
			NVMEV_DEBUG("Searched %u times", count);
			// break;
		}

		if (memcmp(cmd.kv_store.key, kv_ftl->kv_mapping_table[slot].key,
			   cmd_key_length(cmd)) == 0) {
			NVMEV_DEBUG("1 Found\n");
			found = true;
			break;
		}

		slot = kv_ftl->kv_mapping_table[slot].next_slot;
		if (slot == -1)
			break;
		NVMEV_DEBUG("Next slot %d", slot);
		// t1 = ktime_get_ns();
		// printk("Comparison took %llu", t1-t0);
	}

	if (found) {
		NVMEV_DEBUG("2 Found\n");
		memcpy(mapping.key, kv_ftl->kv_mapping_table[slot].key, cmd_key_length(cmd));
		mapping.mem_offset = kv_ftl->kv_mapping_table[slot].mem_offset;
		mapping.next_slot = kv_ftl->kv_mapping_table[slot].next_slot;
		mapping.length = kv_ftl->kv_mapping_table[slot].length;
	}

	if (!found)
		NVMEV_DEBUG("No mapping found for key %s\n", cmd.kv_store.key);
	else
		NVMEV_DEBUG("Returning mapping %lu length %lu for key %s\n", mapping.mem_offset,
			    mapping.length, cmd.kv_store.key);

	return mapping;
}

static struct mapping_entry get_mapping_entry_by_key(struct kv_ftl *kv_ftl, unsigned char *key,
						     int key_len)
{
	struct mapping_entry mapping;
	// char *key = NULL;
	unsigned int slot = 0;
	bool found = false;
	// u64 t0, t1;

	u32 count = 0;

	memset(&mapping, -1, sizeof(struct mapping_entry)); // init mapping

	// t0 = ktime_get_ns();
	slot = get_hash_slot(kv_ftl, key, key_len);
	// t1 = ktime_get_ns();
	// printk("Hashing took %llu\n", t1-t0);

	while (kv_ftl->kv_mapping_table[slot].mem_offset != -1) {
		NVMEV_DEBUG("Comparing %s | %.*s\n", key, key_len,
			    kv_ftl->kv_mapping_table[slot].key);
		count++;

		if (count > 10) {
			NVMEV_DEBUG("Searched %u times", count);
			// break;
		}

		if (memcmp(key, kv_ftl->kv_mapping_table[slot].key, key_len) == 0) {
			NVMEV_DEBUG("1 Found\n");
			found = true;
			break;
		}

		slot = kv_ftl->kv_mapping_table[slot].next_slot;
		if (slot == -1)
			break;
		NVMEV_DEBUG("Next slot %d", slot);
		// t1 = ktime_get_ns();
		// printk("Comparison took %llu", t1-t0);
	}

	if (found) {
		NVMEV_DEBUG("2 Found\n");
		memcpy(mapping.key, kv_ftl->kv_mapping_table[slot].key, key_len);
		mapping.mem_offset = kv_ftl->kv_mapping_table[slot].mem_offset;
		mapping.next_slot = kv_ftl->kv_mapping_table[slot].next_slot;
		mapping.length = kv_ftl->kv_mapping_table[slot].length;
	}

	if (!found)
		NVMEV_DEBUG("No mapping found for key %s\n", key);
	else
		NVMEV_DEBUG("Returning mapping %lu length %lu for key %s\n", mapping.mem_offset,
			    mapping.length, key);

	return mapping;
}

static struct mapping_entry delete_mapping_entry(struct kv_ftl *kv_ftl, struct nvme_kv_command cmd)
{
	struct mapping_entry mapping;
	// char *key = NULL;
	unsigned int slot = 0;
	bool found = false;
	// u64 t0, t1;

	u32 count = 0;

	memset(&mapping, -1, sizeof(struct mapping_entry)); // init mapping

	// t0 = ktime_get_ns();
	slot = get_hash_slot(kv_ftl, cmd.kv_store.key, cmd_key_length(cmd));
	// t1 = ktime_get_ns();
	// printk("Hashing took %llu\n", t1-t0);

	while (kv_ftl->kv_mapping_table[slot].mem_offset != -1) {
		NVMEV_DEBUG("Comparing %s | %.*s\n", cmd.kv_store.key, cmd_key_length(cmd),
			    kv_ftl->kv_mapping_table[slot].key);
		count++;

		if (count > 10) {
			NVMEV_DEBUG("Searched %u times", count);
			// break;
		}

		if (memcmp(cmd.kv_store.key, kv_ftl->kv_mapping_table[slot].key,
			   cmd_key_length(cmd)) == 0) {
			NVMEV_DEBUG("1 Found\n");
			found = true;
			break;
		}

		slot = kv_ftl->kv_mapping_table[slot].next_slot;
		if (slot == -1)
			break;
		NVMEV_DEBUG("Next slot %d", slot);
		// t1 = ktime_get_ns();
		// printk("Comparison took %llu", t1-t0);
	}

	if (found) {
		NVMEV_DEBUG("2 Found\n");
		memset(&(kv_ftl->kv_mapping_table[slot]), -1, sizeof(struct mapping_entry));
	}

	if (!found)
		NVMEV_DEBUG("No mapping found for key %s\n", cmd.kv_store.key);
	else
		NVMEV_DEBUG("Deleting mapping %lu length %lu for key %s\n", mapping.mem_offset,
			    mapping.length, cmd.kv_store.key);

	return mapping;
}

/* KV-SSD IO */

/*
 * 1. find mapping_entry
 * if kv_store
 *   if mapping_entry exist -> write to mem_offset
 *   else -> allocate mem_offset and write
 * else if kv_retrieve
 *   if mapping_entry exist -> read from mem_offset
 *   else -> key doesn't exist!
 */
static unsigned int __do_perform_kv_io(struct kv_ftl *kv_ftl, struct nvme_kv_command cmd,
				       unsigned int *status)
{
	size_t offset;
	size_t length, remaining;
	int prp_offs = 0;
	int prp2_offs = 0;
	u64 paddr;
	u64 *paddr_list = NULL;
	size_t mem_offs = 0;
	size_t new_offset = 0;
	struct mapping_entry entry;
	int is_insert = 0;

	entry = get_mapping_entry(kv_ftl, cmd);
	offset = entry.mem_offset;
	length = cmd_value_length(cmd);

	if (cmd.common.opcode == nvme_cmd_kv_store) {
		if (entry.mem_offset == -1) { // entry doesn't exist -> is insert
			new_offset = allocate_mem_offset(kv_ftl, cmd);
			offset = new_offset;
			is_insert = 1; // is insert

			NVMEV_DEBUG("kv_store insert %s %lu\n", cmd.kv_store.key, offset);
		} else {
			NVMEV_DEBUG("kv_store update %s %lu\n", cmd.kv_store.key, offset);

			if (length != entry.length) {
				if (length <= SMALL_LENGTH && entry.length <= SMALL_LENGTH) {
					is_insert = 2; // is update with different length;
				} else {
					NVMEV_ERROR("Length size invalid!!");
				}
			}
		}
	} else if (cmd.common.opcode == nvme_cmd_kv_retrieve) {
		if (entry.mem_offset == -1) { // kv pair doesn't exist
			NVMEV_DEBUG("kv_retrieve %s no exist\n", cmd.kv_store.key);

			*status = KV_ERR_KEY_NOT_EXIST;
			return 0; // dev_status_code for KVS_ERR_KEY_NOT_EXIST
		} else {
			length = min(entry.length, length);

			NVMEV_DEBUG("kv_retrieve %s exist - length %ld, offset %lu\n",
				    cmd.kv_store.key, length, offset);
		}
	} else if (cmd.common.opcode == nvme_cmd_kv_exist) {
		if (entry.mem_offset == -1) { // kv pair doesn't exist
			NVMEV_DEBUG("kv_exist %s no exist\n", cmd.kv_store.key);

			*status = KV_ERR_KEY_NOT_EXIST;
			return 0; // dev_status_code for KVS_ERR_KEY_NOT_EXIST
		} else {
			NVMEV_DEBUG("kv_exist %s exist\n", cmd.kv_store.key);

			return 0;
		}
	} else if (cmd.common.opcode == nvme_cmd_kv_delete) {
		if (entry.mem_offset == -1) { // kv pair doesn't exist
			NVMEV_DEBUG("kv_delete %s no exist\n", cmd.kv_store.key);

			*status = KV_ERR_KEY_NOT_EXIST;
			return 0; // dev_status_code for KVS_ERR_KEY_NOT_EXIST
		} else {
			NVMEV_DEBUG("kv_delete %s exist - length %ld, offset %lu\n",
				    cmd.kv_store.key, length, offset);

			delete_mapping_entry(kv_ftl, cmd);
			return 0;
		}
	} else {
		NVMEV_ERROR("Cmd type %d, for key %s but not store or retrieve. return 0\n",
			    cmd.common.opcode, cmd.kv_store.key);

		return 0;
	}
	remaining = length;

	while (remaining) {
		size_t io_size;
		void *vaddr;

		mem_offs = 0;
		prp_offs++;
		if (prp_offs == 1) {
			paddr = kv_io_cmd_value_prp(cmd, 1);
		} else if (prp_offs == 2) {
			paddr = kv_io_cmd_value_prp(cmd, 2);
			if (remaining > PAGE_SIZE) {
				paddr_list = kmap_atomic_pfn(PRP_PFN(paddr)) +
					     (paddr & PAGE_OFFSET_MASK);
				paddr = paddr_list[prp2_offs++];
			}
		} else {
			paddr = paddr_list[prp2_offs++];
		}

		vaddr = kmap_atomic_pfn(PRP_PFN(paddr));

		io_size = min_t(size_t, remaining, PAGE_SIZE);

		if (paddr & PAGE_OFFSET_MASK) { // 일반 block io면 언제 여기에 해당?
			mem_offs = paddr & PAGE_OFFSET_MASK;
			if (io_size + mem_offs > PAGE_SIZE)
				io_size = PAGE_SIZE - mem_offs;
		}
		if (cmd.common.opcode == nvme_cmd_kv_store) {
			memcpy(nvmev_vdev->storage_mapped + offset, vaddr + mem_offs, io_size);
		} else if (cmd.common.opcode == nvme_cmd_kv_retrieve) {
			memcpy(vaddr + mem_offs, nvmev_vdev->storage_mapped + offset, io_size);
		} else {
			NVMEV_ERROR("Wrong KV Command passed to NVMeVirt!!\n");
		}

		kunmap_atomic(vaddr);

		remaining -= io_size;
		offset += io_size;
	}

	if (paddr_list != NULL)
		kunmap_atomic(paddr_list);

	if (is_insert == 1) { // need to make new mapping
		new_mapping_entry(kv_ftl, cmd, new_offset);
	} else if (is_insert == 2) {
		update_mapping_entry(kv_ftl, cmd);
	}

	if (cmd.common.opcode == nvme_cmd_kv_retrieve)
		return length;

	return 0;
}

static unsigned int __do_perform_kv_batched_io(struct kv_ftl *kv_ftl, int opcode, char *key,
					       int key_len, char *value, int val_len)
{
	size_t offset;
	size_t new_offset = 0;
	struct mapping_entry entry;
	int is_insert = 0;

	entry = get_mapping_entry_by_key(kv_ftl, key, key_len);
	offset = entry.mem_offset;

	if (opcode == nvme_cmd_kv_store) {
		if (entry.mem_offset == -1) { // entry doesn't exist -> is insert
			NVMEV_DEBUG("kv_store insert %s\n", key);

			new_offset = allocate_mem_offset_by_length(kv_ftl, val_len);
			offset = new_offset;
			is_insert = 1; // is insert
		} else {
			NVMEV_DEBUG("kv_store update %s %lu\n", key, offset);

			if (val_len != entry.length) {
				if (val_len <= SMALL_LENGTH && entry.length <= SMALL_LENGTH) {
					is_insert = 2; // is update with different length;
				} else {
					NVMEV_ERROR("Length size invalid!!");
				}
			}
		}
	} else {
		NVMEV_ERROR("Cmd type %d, for key %s but not store or retrieve. return 0\n", opcode,
			    key);

		return 0;
	}

	NVMEV_DEBUG("Value write length %d to position %lu %s\n", val_len, offset, value);
	memcpy(nvmev_vdev->storage_mapped + offset, value, val_len);

	if (is_insert == 1) { // need to make new mapping
		new_mapping_entry_by_key(kv_ftl, key, key_len, val_len, new_offset);
	}
	// else if (is_insert == 2) {
	// 	update_mapping_entry(cmd);
	// }

	return 0;
}

static unsigned int __do_perform_kv_batch(struct kv_ftl *kv_ftl, struct nvme_kv_command cmd,
					  unsigned int *status)
{
	size_t offset;
	size_t length, remaining;
	int prp_offs = 0;
	int prp2_offs = 0;
	u64 paddr;
	u64 *paddr_list = NULL;
	size_t mem_offs = 0;
	int i;
	struct payload_format *payload;
	char *buffer = NULL;
	char key[20];
	char *value;
	int sub_cmd_cnt;
	int opcode, sub_len, key_len, val_len, payload_offset = 0;

	sub_cmd_cnt = cmd.kv_batch.rsvd4;
	length = cmd_value_length(cmd);

	value = kmalloc(4097, GFP_KERNEL);
	buffer = kmalloc(length, GFP_KERNEL);

	//printk("kv_batch %d %d", sub_cmd_cnt, length);

	remaining = length;
	offset = 0;

	while (remaining) {
		size_t io_size;
		void *vaddr;

		mem_offs = 0;
		prp_offs++;
		if (prp_offs == 1) {
			paddr = kv_io_cmd_value_prp(cmd, 1);
		} else if (prp_offs == 2) {
			paddr = kv_io_cmd_value_prp(cmd, 2);
			if (remaining > PAGE_SIZE) {
				paddr_list = kmap_atomic_pfn(PRP_PFN(paddr)) +
					     (paddr & PAGE_OFFSET_MASK);
				paddr = paddr_list[prp2_offs++];
			}
		} else {
			paddr = paddr_list[prp2_offs++];
		}

		vaddr = kmap_atomic_pfn(PRP_PFN(paddr));

		io_size = min_t(size_t, remaining, PAGE_SIZE);

		if (paddr & PAGE_OFFSET_MASK) { // 일반 block io면 언제 여기에 해당?
			mem_offs = paddr & PAGE_OFFSET_MASK;
			if (io_size + mem_offs > PAGE_SIZE)
				io_size = PAGE_SIZE - mem_offs;
		}

		NVMEV_DEBUG("Value write length %lu to position %lu, io size: %ld, mem_off: %lu\n",
			    remaining, offset, io_size, mem_offs);
		memcpy(buffer + offset, vaddr + mem_offs, io_size);

		kunmap_atomic(vaddr);

		remaining -= io_size;
		offset += io_size;
	}

	/* perform KV IO for sub-payload */
	payload = (struct payload_format *)buffer;
	payload_offset = ALIGN_LEN;
	for (i = 0; i < sub_cmd_cnt; i++) {
		memset(key, 0, 20);
		memset(value, 0, 4097);
		sub_len = 0;
		opcode = payload->batch_head.attr[i].opcode;
		key_len = payload->batch_head.attr[i].keySize;
		val_len = payload->batch_head.attr[i].valueSize;
		sub_len += ((key_len - 1) / ALIGN_LEN + 1) * ALIGN_LEN;
		sub_len += ((val_len - 1) / ALIGN_LEN + 1) * ALIGN_LEN;
		sub_len += ALIGN_LEN;

		memcpy(key, payload->sub_payload + payload_offset, key_len);
		memcpy(value,
		       payload->sub_payload + payload_offset +
			       ((key_len - 1) / ALIGN_LEN + 1) * ALIGN_LEN,
		       val_len);
		payload_offset += sub_len;
		NVMEV_DEBUG("sub-payload %d %d %d %d %s %s", payload->batch_head.attr[i].opcode,
			    key_len, val_len, sub_len, key, value);

		__do_perform_kv_batched_io(kv_ftl, opcode, key, key_len, value, val_len);
	}

	NVMEV_DEBUG("finished kv_batch with %d sub-commands", sub_cmd_cnt);

	if (paddr_list != NULL)
		kunmap_atomic(paddr_list);

	if (value != NULL)
		kfree(value);

	if (buffer != NULL)
		kfree(buffer);

	return 0;
}

static unsigned int kv_iter_open(struct kv_ftl *kv_ftl, struct nvme_kv_command cmd, unsigned int *status)
{
	int iter = 0;
	bool flag = false;

	for (iter = 1; iter <= 16; iter++) {
		if (kv_ftl->iter_handle[iter] == NULL) {
			flag = true;
			break;
		}
	}

	if (!flag)
		return 1;

	kv_ftl->iter_handle[iter] = kmalloc(sizeof(struct kv_iter_context), GFP_KERNEL);
	kv_ftl->iter_handle[iter]->buf = kmalloc(32768, GFP_KERNEL);
	kv_ftl->iter_handle[iter]->end = 0;
	kv_ftl->iter_handle[iter]->byteswritten = 0;
	kv_ftl->iter_handle[iter]->bufoffset = 0;
	kv_ftl->iter_handle[iter]->current_pos = 0;
	kv_ftl->iter_handle[iter]->bitmask = cmd.kv_iter_req.iter_bitmask;
	kv_ftl->iter_handle[iter]->prefix = cmd.kv_iter_req.iter_val;

	*status = 0;
	return iter;
}

static unsigned int kv_iter_close(struct kv_ftl *kv_ftl, struct nvme_kv_command cmd, unsigned int *status)
{
	int iter = cmd.kv_iter_req.iter_handle;

	if (kv_ftl->iter_handle[iter]) {
		kfree(kv_ftl->iter_handle[iter]->buf);
		kfree(kv_ftl->iter_handle[iter]);

		kv_ftl->iter_handle[iter] = NULL;
	}

	*status = 0;
	return 0;
}

static unsigned int kv_iter_read(struct kv_ftl *kv_ftl, struct nvme_kv_command cmd,
				 unsigned int *status)
{
	int iter = cmd.kv_iter_req.iter_handle;
	struct kv_iter_context *handle = kv_ftl->iter_handle[iter];
	int pos = 0, keylen = 16, buf_offset = 4, nr_keys = 0;
	unsigned int key;
	bool full = false, end = false;
	size_t remaining, mem_offs = 0, offset;
	int prp_offs = 0, prp2_offs = 0;
	u64 paddr;
	u64 *paddr_list = NULL;

	if (handle == NULL) {
		NVMEV_ERROR("Invalid Iterator Handle");
		return 0;
	}

	pos = handle->current_pos;

	while (pos < kv_ftl->hash_slots) {
		if (kv_ftl->kv_mapping_table[pos].mem_offset != -1) {
			memcpy(&key, kv_ftl->kv_mapping_table[pos].key, 4);
			if ((key & handle->bitmask) == (handle->prefix & handle->bitmask)) {
				NVMEV_DEBUG("found %s at %d", kv_ftl->kv_mapping_table[pos].key,
					    pos);

				if ((buf_offset + 4 + keylen) > 1024) {
					full = true;
					break;
				}

				memcpy(handle->buf + buf_offset, &keylen, 4);
				buf_offset += 4;
				memcpy(handle->buf + buf_offset, kv_ftl->kv_mapping_table[pos].key,
				       keylen);
				buf_offset += (keylen + 3) & (~3);

				nr_keys++;
			}
		}

		pos++;
		if (pos == kv_ftl->hash_slots) {
			end = true;
			break;
		}
	}
	memcpy(handle->buf, &nr_keys, 4);

	NVMEV_DEBUG("Iterator read done, buf_offset %d, pos %d", buf_offset, pos);
	handle->current_pos = pos;

	/* Writing buffer to PRP */
	remaining = buf_offset;
	offset = 0;

	while (remaining) {
		size_t io_size;
		void *vaddr;

		mem_offs = 0;
		prp_offs++;
		if (prp_offs == 1) {
			paddr = kv_io_cmd_value_prp(cmd, 1);
		} else if (prp_offs == 2) {
			paddr = kv_io_cmd_value_prp(cmd, 2);
			if (remaining > PAGE_SIZE) {
				paddr_list = kmap_atomic_pfn(PRP_PFN(paddr)) +
					     (paddr & PAGE_OFFSET_MASK);
				paddr = paddr_list[prp2_offs++];
			}
		} else {
			paddr = paddr_list[prp2_offs++];
		}

		vaddr = kmap_atomic_pfn(PRP_PFN(paddr));

		io_size = min_t(size_t, remaining, PAGE_SIZE);

		if (paddr & PAGE_OFFSET_MASK) {
			mem_offs = paddr & PAGE_OFFSET_MASK;
			if (io_size + mem_offs > PAGE_SIZE)
				io_size = PAGE_SIZE - mem_offs;
		}

		NVMEV_DEBUG(
			"Buffer transfer, length %lu from position %lu, io size: %ld, mem_off: %lu\n",
			remaining, offset, io_size, mem_offs);
		memcpy(vaddr + mem_offs, handle->buf + offset, io_size);

		kunmap_atomic(vaddr);

		remaining -= io_size;
		offset += io_size;
	}

	if (paddr_list != NULL)
		kunmap_atomic(paddr_list);

	*status = 0;
	if (end) {
		*status = 0x393;
	}

	return buf_offset;
}

static unsigned int __do_perform_kv_iter_io(struct kv_ftl *kv_ftl, struct nvme_kv_command cmd,
					    unsigned int *status)
{
	if (is_kv_iter_req_cmd(cmd.common.opcode)) {
		if (cmd.kv_iter_req.option & ITER_OPTION_OPEN) {
			return kv_iter_open(kv_ftl, cmd, status);
		} else if (cmd.kv_iter_req.option & ITER_OPTION_CLOSE) {
			return kv_iter_close(kv_ftl, cmd, status);
		}
	} else if (is_kv_iter_read_cmd(cmd.common.opcode)) {
		return kv_iter_read(kv_ftl, cmd, status);
	}

	return 0;
}

bool kv_proc_nvme_io_cmd(struct nvmev_ns *ns, struct nvmev_request *req, struct nvmev_result *ret)
{
	struct nvme_command *cmd = req->cmd;

	switch (cmd->common.opcode) {
	case nvme_cmd_write:
	case nvme_cmd_read:
		ret->nsecs_target = __schedule_io_units(
			cmd->common.opcode, cmd->rw.slba,
			__cmd_io_size((struct nvme_rw_command *)cmd), __get_wallclock());
		break;
	case nvme_cmd_flush:
		ret->nsecs_target = __schedule_flush(req);
		break;
	case nvme_cmd_kv_store:
	case nvme_cmd_kv_retrieve:
	case nvme_cmd_kv_batch:
		ret->nsecs_target = __schedule_io_units(
			cmd->common.opcode, 0, cmd_value_length(*((struct nvme_kv_command *)cmd)),
			__get_wallclock());
		NVMEV_INFO("%d, %llu, %llu\n", cmd_value_length(*((struct nvme_kv_command *)cmd)),
			   __get_wallclock(), ret->nsecs_target);
		break;
	default:
		NVMEV_ERROR("%s: command not implemented: %s (0x%x)\n", __func__,
				nvme_opcode_string(cmd->common.opcode), cmd->common.opcode);
		break;
	}

	return true;
}

bool kv_identify_nvme_io_cmd(struct nvmev_ns *ns, struct nvme_command cmd)
{
	return is_kv_cmd(cmd.common.opcode);
}

unsigned int kv_perform_nvme_io_cmd(struct nvmev_ns *ns, struct nvme_command *cmd, uint32_t *status)
{
	struct kv_ftl *kv_ftl = (struct kv_ftl *)ns->ftls;
	struct nvme_kv_command *kv_cmd = (struct nvme_kv_command *)cmd;

	if (is_kv_batch_cmd(cmd->common.opcode))
		return __do_perform_kv_batch(kv_ftl, *kv_cmd, status);
	else if (is_kv_iter_cmd(cmd->common.opcode))
		return __do_perform_kv_iter_io(kv_ftl, *kv_cmd, status);
	else
		return __do_perform_kv_io(kv_ftl, *kv_cmd, status);
}

void kv_init_namespace(struct nvmev_ns *ns, uint32_t id, uint64_t size, void *mapped_addr,
		       uint32_t cpu_nr_dispatcher)
{
	struct kv_ftl *kv_ftl;
	int i;

	kv_ftl = kmalloc(sizeof(struct kv_ftl), GFP_KERNEL);

	NVMEV_INFO("KV mapping table: %#010lx-%#010x\n",
		   nvmev_vdev->config.storage_start + nvmev_vdev->config.storage_size,
		   KV_MAPPING_TABLE_SIZE);

	kv_ftl->kv_mapping_table =
		memremap(nvmev_vdev->config.storage_start + nvmev_vdev->config.storage_size,
			 KV_MAPPING_TABLE_SIZE, MEMREMAP_WB);

	if (kv_ftl->kv_mapping_table == NULL)
		NVMEV_ERROR("Failed to map kv mapping table.\n");
	else
		memset(kv_ftl->kv_mapping_table, 0x0, KV_MAPPING_TABLE_SIZE);

	if (ALLOCATOR_TYPE == ALLOCATOR_TYPE_BITMAP) {
		kv_ftl->allocator_ops = bitmap_ops;
	} else if (ALLOCATOR_TYPE == ALLOCATOR_TYPE_APPEND_ONLY) {
		kv_ftl->allocator_ops = append_only_ops;
	} else {
		kv_ftl->allocator_ops = append_only_ops;
	}

	if (!kv_ftl->allocator_ops.init(nvmev_vdev->config.storage_size)) {
		NVMEV_ERROR("Allocator init failed\n");
	}

	kv_ftl->hash_slots = KV_MAPPING_TABLE_SIZE / KV_MAPPING_ENTRY_SIZE;
	NVMEV_INFO("Hash slots: %ld\n", kv_ftl->hash_slots);

	for (i = 0; i < kv_ftl->hash_slots; i++) {
		kv_ftl->kv_mapping_table[i].mem_offset = -1;
		kv_ftl->kv_mapping_table[i].next_slot = -1;
		kv_ftl->kv_mapping_table[i].length = -1;
	}

	for (i = 0; i < 16; i++)
		kv_ftl->iter_handle[i] = NULL;

	ns->id = id;
	ns->csi = NVME_CSI_NVM; // Not specifying to KV. Need to support NVM commands too.
	ns->ftls = (void *)kv_ftl;
	ns->size = size;
	ns->mapped = mapped_addr;
	/*register io command handler*/
	ns->proc_io_cmd = kv_proc_nvme_io_cmd;
	/*register CSS specific io command functions*/
	ns->identify_io_cmd = kv_identify_nvme_io_cmd;
	ns->perform_io_cmd = kv_perform_nvme_io_cmd;

	return;
}

void kv_remove_namespace(struct nvmev_ns *ns)
{
	kfree(ns->ftls);
	ns->ftls = NULL;
}
