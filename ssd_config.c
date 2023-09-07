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
	ftl_cfgs->BLKS_PER_PLN = 8192;
	ftl_cfgs->BLK_SIZE = 0;

	ftl_cfgs->MAX_CH_XFER_SIZE = KB(16);
	ftl_cfgs->WRITE_UNIT_SIZE = 512;
	
	ftl_cfgs->NAND_CHANNEL_BANDWIDTH = 800ull;
	ftl_cfgs->PCIE_BANDWIDTH = 3360ull;

	ftl_cfgs->NAND_4KB_READ_LATENCY_LSB = 35760 - 6000;
	ftl_cfgs->NAND_4KB_READ_LATENCY_MSB = 35760 + 6000;
	ftl_cfgs->NAND_4KB_READ_LATENCY_CSB = 0;
	ftl_cfgs->NAND_READ_LATENCY_LSB = 36013 - 6000;
	ftl_cfgs->NAND_READ_LATENCY_MSB = 36013 + 6000;
	ftl_cfgs->NAND_READ_LATENCY_CSB = 0;
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
