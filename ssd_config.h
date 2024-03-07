// SPDX-License-Identifier: GPL-2.0-only

#ifndef _NVMEVIRT_SSD_CONFIG_H
#define _NVMEVIRT_SSD_CONFIG_H

/* SSD Model */
#define INTEL_OPTANE 0
#define SAMSUNG_970PRO 1
#define ZNS_PROTOTYPE 2
#define KV_PROTOTYPE 3
#define WD_ZN540 4

/* SSD Type */
#define SSD_TYPE_NVM 0
#define SSD_TYPE_CONV 1
#define SSD_TYPE_ZNS 2
#define SSD_TYPE_KV 3

/* Cell Mode */
#define CELL_MODE_UNKNOWN 0
#define CELL_MODE_SLC 1
#define CELL_MODE_MLC 2
#define CELL_MODE_TLC 3
#define CELL_MODE_QLC 4

/* Must select one of INTEL_OPTANE, SAMSUNG_970PRO, or ZNS_PROTOTYPE
 * in Makefile */

#if (BASE_SSD == INTEL_OPTANE)
#define NR_NAMESPACES 1

#define NS_SSD_TYPE_0 SSD_TYPE_NVM
#define NS_CAPACITY_0 (0)
#define NS_SSD_TYPE_1 NS_SSD_TYPE_0
#define NS_CAPACITY_1 (0)
#define MDTS (5)
#define CELL_MODE (CELL_MODE_UNKNOWN)

#define LBA_BITS (9)
#define LBA_SIZE (1 << LBA_BITS)

#elif (BASE_SSD == KV_PROTOTYPE)
#define NR_NAMESPACES 1

#define NS_SSD_TYPE_0 SSD_TYPE_KV
#define NS_CAPACITY_0 (0)
#define NS_SSD_TYPE_1 NS_SSD_TYPE_0
#define NS_CAPACITY_1 (0)
#define MDTS (5)
#define CELL_MODE (CELL_MODE_MLC)

enum {
	ALLOCATOR_TYPE_BITMAP,
	ALLOCATOR_TYPE_APPEND_ONLY,
};

#define KV_MAPPING_TABLE_SIZE GB(1)
#define ALLOCATOR_TYPE ALLOCATOR_TYPE_APPEND_ONLY

#define LBA_BITS (9)
#define LBA_SIZE (1 << LBA_BITS)

#elif (BASE_SSD == SAMSUNG_970PRO)
#define NR_NAMESPACES 1

#define NS_SSD_TYPE_0 SSD_TYPE_CONV
#define NS_CAPACITY_0 (0)
#define NS_SSD_TYPE_1 NS_SSD_TYPE_0
#define NS_CAPACITY_1 (0)
#define MDTS (6)
#define CELL_MODE (CELL_MODE_MLC)

#define SSD_PARTITIONS (4)
#define NAND_CHANNELS (8)
#define LUNS_PER_NAND_CH (2)
#define PLNS_PER_LUN (1)
#define FLASH_PAGE_SIZE KB(32)
#define ONESHOT_PAGE_SIZE (FLASH_PAGE_SIZE * 1)
#define BLKS_PER_PLN (8192)
#define BLK_SIZE (0) /*BLKS_PER_PLN should not be 0 */
static_assert((ONESHOT_PAGE_SIZE % FLASH_PAGE_SIZE) == 0);

#define MAX_CH_XFER_SIZE KB(16) /* to overlap with pcie transfer */
#define WRITE_UNIT_SIZE (512)

#define NAND_CHANNEL_BANDWIDTH (800ull) //MB/s
#define PCIE_BANDWIDTH (3360ull) //MB/s

#define NAND_4KB_READ_LATENCY_LSB (35760 - 6000) //ns
#define NAND_4KB_READ_LATENCY_MSB (35760 + 6000) //ns
#define NAND_4KB_READ_LATENCY_CSB (0) //not used
#define NAND_READ_LATENCY_LSB (36013 - 6000)
#define NAND_READ_LATENCY_MSB (36013 + 6000)
#define NAND_READ_LATENCY_CSB (0) //not used
#define NAND_PROG_LATENCY (185000)
#define NAND_ERASE_LATENCY (0)

#define FW_4KB_READ_LATENCY (21500)
#define FW_READ_LATENCY (30490)
#define FW_WBUF_LATENCY0 (4000)
#define FW_WBUF_LATENCY1 (460)
#define FW_CH_XFER_LATENCY (0)
#define OP_AREA_PERCENT (0.07)

#define GLOBAL_WB_SIZE (NAND_CHANNELS * LUNS_PER_NAND_CH * ONESHOT_PAGE_SIZE * 2)
#define WRITE_EARLY_COMPLETION 1

#define LBA_BITS (9)
#define LBA_SIZE (1 << LBA_BITS)

#elif (BASE_SSD == ZNS_PROTOTYPE)
#define NR_NAMESPACES 1

#define NS_SSD_TYPE_0 SSD_TYPE_ZNS
#define NS_CAPACITY_0 (0)
#define NS_SSD_TYPE_1 NS_SSD_TYPE_0
#define NS_CAPACITY_1 (0)
#define MDTS (6)
#define CELL_MODE (CELL_MODE_TLC)

#define SSD_PARTITIONS (1)
#define NAND_CHANNELS (8)
#define LUNS_PER_NAND_CH (16)
#define FLASH_PAGE_SIZE KB(64)
#define PLNS_PER_LUN (1) /* not used*/
#define DIES_PER_ZONE (1)

#if 0
/* Real device configuration. Need to modify kernel to support zone size which is not power of 2*/
#define ONESHOT_PAGE_SIZE (FLASH_PAGE_SIZE * 3)
#define ZONE_SIZE MB(96) /* kernal only support zone size which is power of 2 */
#else /* If kernel is not modified, use this config for just testing ZNS*/
#define ONESHOT_PAGE_SIZE (FLASH_PAGE_SIZE * 2)
#define ZONE_SIZE MB(32)
#endif
static_assert((ONESHOT_PAGE_SIZE % FLASH_PAGE_SIZE) == 0);

#define MAX_CH_XFER_SIZE (FLASH_PAGE_SIZE) /* to overlap with pcie transfer */
#define WRITE_UNIT_SIZE (ONESHOT_PAGE_SIZE)

#define NAND_CHANNEL_BANDWIDTH (800ull) //MB/s
#define PCIE_BANDWIDTH (3200ull) //MB/s

#define NAND_4KB_READ_LATENCY_LSB (25485)
#define NAND_4KB_READ_LATENCY_MSB (25485)
#define NAND_4KB_READ_LATENCY_CSB (25485)
#define NAND_READ_LATENCY_LSB (40950)
#define NAND_READ_LATENCY_MSB (40950)
#define NAND_READ_LATENCY_CSB (40950)
#define NAND_PROG_LATENCY (1913640)
#define NAND_ERASE_LATENCY (0)

#define FW_4KB_READ_LATENCY (37540 - 7390 + 2000)
#define FW_READ_LATENCY (37540 - 7390 + 2000)
#define FW_WBUF_LATENCY0 (0)
#define FW_WBUF_LATENCY1 (0)
#define FW_CH_XFER_LATENCY (413)
#define OP_AREA_PERCENT (0)

#define GLOBAL_WB_SIZE (NAND_CHANNELS * LUNS_PER_NAND_CH * ONESHOT_PAGE_SIZE * 2)
#define ZONE_WB_SIZE (0)
#define WRITE_EARLY_COMPLETION 0

/* Don't modify followings. BLK_SIZE is caculated from ZONE_SIZE and DIES_PER_ZONE */
#define BLKS_PER_PLN 0 /* BLK_SIZE should not be 0 */
#define BLK_SIZE (ZONE_SIZE / DIES_PER_ZONE)
static_assert((ZONE_SIZE % DIES_PER_ZONE) == 0);

/* For ZRWA */
#define MAX_ZRWA_ZONES (0)
#define ZRWAFG_SIZE (0)
#define ZRWA_SIZE (0)
#define ZRWA_BUFFER_SIZE (0)

#define LBA_BITS (9)
#define LBA_SIZE (1 << LBA_BITS)

#elif (BASE_SSD == WD_ZN540)
#define NR_NAMESPACES 1

#define NS_SSD_TYPE_0 SSD_TYPE_ZNS
#define NS_CAPACITY_0 (0)
#define NS_SSD_TYPE_1 NS_SSD_TYPE_0
#define NS_CAPACITY_1 (0)
#define MDTS (6)
#define CELL_MODE (CELL_MODE_TLC)

#define SSD_PARTITIONS (1)
#define NAND_CHANNELS (8)
#define LUNS_PER_NAND_CH (4)
#define PLNS_PER_LUN (1) /* not used*/
#define DIES_PER_ZONE (NAND_CHANNELS * LUNS_PER_NAND_CH)

#define FLASH_PAGE_SIZE KB(32)
#define ONESHOT_PAGE_SIZE (FLASH_PAGE_SIZE * 3)
/*In an emulator environment, it may be too large to run an application
  which requires a certain number of zones or more.
  So, adjust the zone size to fit your environment */
#define ZONE_SIZE GB(2ULL)

static_assert((ONESHOT_PAGE_SIZE % FLASH_PAGE_SIZE) == 0);

#define MAX_CH_XFER_SIZE (FLASH_PAGE_SIZE) /* to overlap with pcie transfer */
#define WRITE_UNIT_SIZE (512)

#define NAND_CHANNEL_BANDWIDTH (450ull) //MB/s
#define PCIE_BANDWIDTH (3050ull) //MB/s

#define NAND_4KB_READ_LATENCY_LSB (50000)
#define NAND_4KB_READ_LATENCY_MSB (50000)
#define NAND_4KB_READ_LATENCY_CSB (50000)
#define NAND_READ_LATENCY_LSB (58000)
#define NAND_READ_LATENCY_MSB (58000)
#define NAND_READ_LATENCY_CSB (58000)
#define NAND_PROG_LATENCY (561000)
#define NAND_ERASE_LATENCY (0)

#define FW_4KB_READ_LATENCY (20000)
#define FW_READ_LATENCY (13000)
#define FW_WBUF_LATENCY0 (5600)
#define FW_WBUF_LATENCY1 (600)
#define FW_CH_XFER_LATENCY (0)
#define OP_AREA_PERCENT (0)

#define ZONE_WB_SIZE (10 * ONESHOT_PAGE_SIZE)
#define GLOBAL_WB_SIZE (0)
#define WRITE_EARLY_COMPLETION 1

/* Don't modify followings. BLK_SIZE is caculated from ZONE_SIZE and DIES_PER_ZONE */
#define BLKS_PER_PLN 0 /* BLK_SIZE should not be 0 */
#define BLK_SIZE (ZONE_SIZE / DIES_PER_ZONE)
static_assert((ZONE_SIZE % DIES_PER_ZONE) == 0);

/* For ZRWA */
#define MAX_ZRWA_ZONES (0)
#define ZRWAFG_SIZE (0)
#define ZRWA_SIZE (0)
#define ZRWA_BUFFER_SIZE (0)

#define LBA_BITS (9)
#define LBA_SIZE (1 << LBA_BITS)
#endif
///////////////////////////////////////////////////////////////////////////

static const uint32_t ns_ssd_type[] = { NS_SSD_TYPE_0, NS_SSD_TYPE_1 };
static const uint64_t ns_capacity[] = { NS_CAPACITY_0, NS_CAPACITY_1 };

#define NS_SSD_TYPE(ns) (ns_ssd_type[ns])
#define NS_CAPACITY(ns) (ns_capacity[ns])

/* Still only support NR_NAMESPACES <= 2 */
static_assert(NR_NAMESPACES <= 2);

#define SUPPORTED_SSD_TYPE(type) \
	(NS_SSD_TYPE_0 == SSD_TYPE_##type || NS_SSD_TYPE_1 == SSD_TYPE_##type)

#endif
