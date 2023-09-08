#include "nvmev.h"
#include "ssd_config.h"

void load_simple_configs(struct ftl_configs *ftl_cfgs) 
{
	ftl_cfgs->NS_SSD_TYPE = SSD_TYPE_NVM;

	ftl_cfgs->MDTS = 5;
	ftl_cfgs->CELL_MODE = CELL_MODE_UNKNOWN;
}

void load_conv_configs(struct ftl_configs *ftl_cfgs) 
{
	ftl_cfgs->NS_SSD_TYPE = SSD_TYPE_CONV;

	ftl_cfgs->MDTS = 6;
	ftl_cfgs->CELL_MODE = CELL_MODE_MLC;

	ftl_cfgs->SSD_PARTITIONS = 4;
	ftl_cfgs->NAND_CHANNELS = 8;
	ftl_cfgs->LUNS_PER_NAND_CH = 2;
	ftl_cfgs->PLNS_PER_LUN = 1;
	ftl_cfgs->FLASH_PAGE_SIZE = KB(32);
	ftl_cfgs->ONESHOT_PAGE_SIZE = ftl_cfgs->FLASH_PAGE_SIZE * 1;
	ftl_cfgs->BLKS_PER_PLN = 8192; /*BLKS_PER_PLN should not be 0 */
	ftl_cfgs->BLK_SIZE = 0;

	NVMEV_ASSERT((ftl_cfgs->ONESHOT_PAGE_SIZE % ftl_cfgs->FLASH_PAGE_SIZE) == 0);

	ftl_cfgs->MAX_CH_XFER_SIZE = KB(16); /* to overlap with pcie transfer */
	ftl_cfgs->WRITE_UNIT_SIZE = 512;
	
	ftl_cfgs->NAND_CHANNEL_BANDWIDTH = 800ull; //MB/s
	ftl_cfgs->PCIE_BANDWIDTH = 3360ull; //MB/s

	ftl_cfgs->NAND_4KB_READ_LATENCY_LSB = 35760 - 6000; //ns
	ftl_cfgs->NAND_4KB_READ_LATENCY_MSB = 35760 + 6000; //ns
	ftl_cfgs->NAND_4KB_READ_LATENCY_CSB = 0; //not used
	ftl_cfgs->NAND_READ_LATENCY_LSB = 36013 - 6000;
	ftl_cfgs->NAND_READ_LATENCY_MSB = 36013 + 6000;
	ftl_cfgs->NAND_READ_LATENCY_CSB = 0; //not used
	ftl_cfgs->NAND_PROG_LATENCY = 185000;
	ftl_cfgs->NAND_ERASE_LATENCY = 0;

	ftl_cfgs->FW_4KB_READ_LATENCY = 21500;
	ftl_cfgs->FW_READ_LATENCY = 30490;
	ftl_cfgs->FW_WBUF_LATENCY0 = 4000;
	ftl_cfgs->FW_WBUF_LATENCY1 = 460;
	ftl_cfgs->FW_CH_XFER_LATENCY = 0;
	//ftl_cfgs->OP_AREA_PERCENT = 0.07;

	ftl_cfgs->GLOBAL_WB_SIZE = ftl_cfgs->NAND_CHANNELS * ftl_cfgs->LUNS_PER_NAND_CH * 
								ftl_cfgs->ONESHOT_PAGE_SIZE * 2;
	ftl_cfgs->WRITE_EARLY_COMPLETION = true;
}

/* WD_ZN540 */
void load_zns_configs(struct ftl_configs *ftl_cfgs)
{
	struct zns_configs *zns_cfgs = &ftl_cfgs->extra_configs.zns;
	
	ftl_cfgs->NS_SSD_TYPE = SSD_TYPE_ZNS;

	ftl_cfgs->MDTS = 6;
	ftl_cfgs->CELL_MODE = CELL_MODE_TLC;

	ftl_cfgs->SSD_PARTITIONS = 1;
	ftl_cfgs->NAND_CHANNELS = 8;
	ftl_cfgs->LUNS_PER_NAND_CH = 4;
	ftl_cfgs->PLNS_PER_LUN = 1; //not used
	zns_cfgs->DIES_PER_ZONE = ftl_cfgs->NAND_CHANNELS * ftl_cfgs->LUNS_PER_NAND_CH;

	ftl_cfgs->FLASH_PAGE_SIZE = KB(32);
	ftl_cfgs->ONESHOT_PAGE_SIZE = ftl_cfgs->FLASH_PAGE_SIZE * 3;
	/*In an emulator environment, it may be too large to run an application
  	  which requires a certain number of zones or more.
  	  So, adjust the zone size to fit your environment */
	zns_cfgs->ZONE_SIZE = MB(2047);

	NVMEV_ASSERT((ftl_cfgs->ONESHOT_PAGE_SIZE % ftl_cfgs->FLASH_PAGE_SIZE) == 0);

	ftl_cfgs->MAX_CH_XFER_SIZE = ftl_cfgs->FLASH_PAGE_SIZE; //to overlap with pcie transfer
	ftl_cfgs->WRITE_UNIT_SIZE = 512;
	
	ftl_cfgs->NAND_CHANNEL_BANDWIDTH = 450ull; //MB/s
	ftl_cfgs->PCIE_BANDWIDTH = 3050ull; //MB/s

	ftl_cfgs->NAND_4KB_READ_LATENCY_LSB = 50000;
	ftl_cfgs->NAND_4KB_READ_LATENCY_MSB = 50000;
	ftl_cfgs->NAND_4KB_READ_LATENCY_CSB = 50000;
	ftl_cfgs->NAND_READ_LATENCY_LSB = 58000;
	ftl_cfgs->NAND_READ_LATENCY_MSB = 58000;
	ftl_cfgs->NAND_READ_LATENCY_CSB = 58000;
	ftl_cfgs->NAND_PROG_LATENCY = 561000;
	ftl_cfgs->NAND_ERASE_LATENCY = 0;

	ftl_cfgs->FW_4KB_READ_LATENCY = 20000;
	ftl_cfgs->FW_READ_LATENCY = 13000;
	ftl_cfgs->FW_WBUF_LATENCY0 = 5600;
	ftl_cfgs->FW_WBUF_LATENCY1 = 600;
	ftl_cfgs->FW_CH_XFER_LATENCY = 0;
	//ftl_cfgs->OP_AREA_PERCENT = 0.07;

	zns_cfgs->ZONE_WB_SIZE = 10 * ftl_cfgs->ONESHOT_PAGE_SIZE;
	ftl_cfgs->GLOBAL_WB_SIZE = 0;
	ftl_cfgs->WRITE_EARLY_COMPLETION = true;

	/* Don't modify followings. BLK_SIZE is caculated from ZONE_SIZE and DIES_PER_ZONE */
	ftl_cfgs->BLKS_PER_PLN = 0; //BLK_SIZE should not be 0
	ftl_cfgs->BLK_SIZE = zns_cfgs->ZONE_SIZE / zns_cfgs->DIES_PER_ZONE;

	NVMEV_ASSERT((zns_cfgs->ZONE_SIZE % zns_cfgs->DIES_PER_ZONE) == 0);

	//For ZRWA
	zns_cfgs->MAX_ZRWA_ZONES = 0;
	zns_cfgs->ZRWAFG_SIZE = 0;
	zns_cfgs->ZRWA_SIZE = 0;
	zns_cfgs->ZRWA_BUFFER_SIZE = 0;
}

void load_kv_configs(struct ftl_configs *ftl_cfgs)
{
	struct kv_configs *kv_cfgs = &ftl_cfgs->extra_configs.kv;
	
	ftl_cfgs->NS_SSD_TYPE = SSD_TYPE_KV;

	ftl_cfgs->MDTS = 5;
	ftl_cfgs->CELL_MODE = CELL_MODE_MLC;

	kv_cfgs->KV_MAPPING_TABLE_SIZE = GB(1);
	kv_cfgs->ALLOCATOR_TYPE = ALLOCATOR_TYPE_APPEND_ONLY;
}
