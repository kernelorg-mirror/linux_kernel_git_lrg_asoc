/*
 * sst_dsp.h - Intel Smart Sound Technology
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __SOUND_SOC_SST_DSP_PRIV_H
#define __SOUND_SOC_SST_DSP_PRIV_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>

struct sst_ops {
	/* DSP core boot / reset */
	void (*boot)(struct sst_dsp *);
	void (*reset)(struct sst_dsp *);

	/* Shim IO */
	void (*write)(void __iomem *addr, u32 offset, u32 value);
	u32 (*read)(void __iomem *addr, u32 offset);
	void (*write64)(void __iomem *addr, u32 offset, u64 value);
	u64 (*read64)(void __iomem *addr, u32 offset);

	/* DSP I/DRAM IO */
	void (*dram_read)(struct sst_dsp *sst, void *dest, void *src, size_t bytes);
	void (*dram_write)(struct sst_dsp *sst, void *dest, void *src, size_t bytes);
	void (*iram_read)(struct sst_dsp *sst, void *dest, void *src, size_t bytes);
	void (*iram_write)(struct sst_dsp *sst, void *dest, void *src, size_t bytes);

	void (*dump)(struct sst_dsp *);

	/* IRQ handlers */
	irqreturn_t (*irq_handler)(int irq, void *context);

	/* SST init and free */
	int (*init)(struct sst_dsp *sst, struct sst_pdata *pdata);
	void (*free)(struct sst_dsp *sst);
};

struct sst_addr {
	u32 iram_base;
	u32 dram_base;
	u32 iram_end;
	u32 dram_end;
	u32 ddr_end;
	u32 ddr_base;
	void __iomem *shim;
	void __iomem *iram;
	void __iomem *dram;
	void __iomem *pci_cfg;
};

struct sst_mailbox {
	void __iomem *in_base;
	void __iomem *out_base;
	size_t in_size;
	size_t out_size;
};

/*
 * Generic SST Shim Interface.
 */
struct sst_dsp {

	struct sst_dsp_device *sst_dev;
	spinlock_t spinlock;
	struct device *dev;
	void *thread_context;
	int irq;

	/* operations */
	struct sst_ops *ops;

	/* runtime */
	bool validate_memcpy;
	bool dsp_ram32;

	/* Firmware */
	const struct firmware *fw;
	void *fw_in_mem;

	/* debug FS */
	struct dentry *debugfs_root;

	/* base addresses */
	struct sst_addr addr;

	/* mailbox */
	struct sst_mailbox mailbox;
};

/* Core specific ops for internal use only */
extern struct sst_ops hswult_ops;

void shim_write(void __iomem *addr, u32 offset, u32 value);
u32 shim_read(void __iomem *addr, u32 offset);
void shim_write64(void __iomem *addr, u32 offset, u64 value);
u64 shim_read64(void __iomem *addr, u32 offset);
void sst_memcpy_toio_32(struct sst_dsp *sst,
	void __iomem *dest, void *src, size_t bytes);
void sst_memcpy_fromio_32(struct sst_dsp *sst, void *dest,
	void __iomem *src, size_t bytes);
void sst_memcpy_toio_64(struct sst_dsp *sst, void *dest, void *src,
	size_t bytes);
void sst_memcpy_fromio_64(struct sst_dsp *sst, void *dest, void *src,
	size_t bytes);
#endif
