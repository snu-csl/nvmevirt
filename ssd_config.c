#include "nvmev.h"
#include "ssd_config.h"

void load_simple_configs(struct ftl_configs *ftl_cfgs) 
{
	ftl_cfgs->ns_ssd_type = SSD_TYPE_NVM;

	ftl_cfgs->mdts = 5;
	ftl_cfgs->cell_mode = CELL_MODE_UNKNOWN;
}

void load_conv_configs(struct ftl_configs *ftl_cfgs) 
{
	struct conv_configs *conv_cfgs = &ftl_cfgs->extra_configs.conv;

	ftl_cfgs->ns_ssd_type = SSD_TYPE_CONV;

	ftl_cfgs->mdts = 6;
	ftl_cfgs->cell_mode = CELL_MODE_MLC;

	conv_cfgs->ssd_partitions = 4;
	conv_cfgs->nand_channels = 8;
	conv_cfgs->luns_per_nand_ch = 2;
	conv_cfgs->plns_per_lun = 1;
	conv_cfgs->flash_page_size = KB(32);
	conv_cfgs->oneshot_page_size = conv_cfgs->flash_page_size * 1;
	conv_cfgs->blks_per_pln = 8192;
	conv_cfgs->blk_size = 0;

	conv_cfgs->max_ch_xfer_size = KB(16);
	conv_cfgs->write_unit_size = 512;
	
	conv_cfgs->nand_channel_bandwidth = 800ull;
	conv_cfgs->pcie_bandwidth = 3360ull;

	conv_cfgs->nand_4kb_read_latency_lsb = 35760 - 6000;
	conv_cfgs->nand_4kb_read_latency_msb = 35760 + 6000;
	conv_cfgs->nand_4kb_read_latency_csb	= 0;
	conv_cfgs->nand_read_latency_lsb = 36013 - 6000;
	conv_cfgs->nand_read_latency_msb = 36013 + 6000;
	conv_cfgs->nand_read_latency_csb = 0;
	conv_cfgs->nand_prog_latency = 185000;
	conv_cfgs->nand_erase_latency = 0;

	conv_cfgs->fw_4kb_read_latency = 21500;
	conv_cfgs->fw_read_latency = 30490;
	conv_cfgs->fw_wbuf_latency0 = 4000;
	conv_cfgs->fw_wbuf_latency1 = 460;
	conv_cfgs->fw_ch_xfer_latency = 0;
	conv_cfgs->op_area_percent = 0.07;

	conv_cfgs->global_wb_size = conv_cfgs->nand_channels * conv_cfgs->luns_per_nand_ch * 
								conv_cfgs->oneshot_page_size * 2;
	conv_cfgs->write_early_completion = true;
}
