// SPDX-License-Identifier: GPL-2.0-only

#ifndef _LIB_DMA_H
#define _LIB_DMA_H

// DMA Init, Final Function
int ioat_dma_chan_set(const char *val);
int ioat_dma_submit(dma_addr_t src_addr, dma_addr_t dst_addr, unsigned int size);
void ioat_dma_cleanup(void);

#endif /* _LIB_DMA_H */
