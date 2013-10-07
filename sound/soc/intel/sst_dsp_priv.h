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
#include <linux/firmware.h>

struct sst_mem_block;

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

	/* FW module parser/loader */
	int (*parse_fw)(struct sst_dsp *dsp, const struct firmware *fw);
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

/* SST Firmware - each FW file can support N modules */
struct sst_fw {
	struct sst_dsp *dsp;
	const struct firmware *fw;
	struct list_head list;		/* DSP list of FW */
	struct list_head module_list;	/* FW list of modules */

	void *private;
};

struct sst_module_template {
	u32 id;
	u32 text_size;
	u32 data_size;
	u32 text_offset;
	u32 data_offset;
	u32 text_fixed:1;
	u32 data_fixed:1;
	void *text;
	void *data;
};

/* SST Module - module can span multiple blocks */
struct sst_module {
	struct sst_dsp *dsp;

	u32 id;
	u32 text_size;
	u32 data_size;
	u32 text_offset;
	u32 data_offset;
	u32 text_fixed:1;
	u32 data_fixed:1;
	u32 resident:1;
	u32 usage_count;

	struct sst_fw *sst_fw;
	void *text;
	void *data;

	void *private;

	struct list_head tblock_list;	/* Module list of text blocks */
	struct list_head dblock_list;	/* Module list of data blocks */
	struct list_head list;		/* DSP list of modules */
	struct list_head list_fw;	/* FW list of modules */
};

enum sst_mem_type {
	SST_MEM_IRAM = 0,
	SST_MEM_DRAM = 1,
};

struct sst_block_ops {
	int (*enable)(struct sst_mem_block *block);
	int (*disable)(struct sst_mem_block *block);
};

/* SST memory Block - SST memory has multiple IRAM and DRAM regions */
struct sst_mem_block {
	struct sst_dsp *dsp;

	u32 offset;
	u32 size;
	enum sst_mem_type type;
	u32 in_use:1;
	u32 flags;			/* generic core does not touch this */
	u32 power;
	u32 clock;
	struct sst_block_ops *ops;
	void *private;

	struct sst_module *module;
	struct list_head module_tlist;	/* Module list of text blocks */
	struct list_head module_dlist;	/* Module list of data blocks */
	struct list_head map_list;	/* Block Map list of used/free blocks */
	struct list_head list; 		/* DSP list of blocks */
};

/* SST block memory map */
struct sst_bmap {
	struct sst_dsp *dsp;

	/* IRAM */
	u32 iram_base;
	u32 iram_size;

	/* DRAM */
	u32 dram_base;
	u32 dram_size;

	struct list_head used_block_list;
	struct list_head free_block_list;
};

/*
 * Generic SST Shim Interface.
 */
struct sst_dsp {

	struct sst_dsp_device *sst_dev;
	spinlock_t spinlock;	/* IPC locking */
	struct mutex mutex;	/* DSP FW lock */ 
	struct device *dev;
	void *thread_context;
	int irq;
	struct sst_bmap bmap;

	/* operations */
	struct sst_ops *ops;

	/* runtime */
	bool validate_memcpy;
	bool dsp_ram32;

	/* debug FS */
	struct dentry *debugfs_root;

	/* base addresses */
	struct sst_addr addr;

	/* mailbox */
	struct sst_mailbox mailbox;

	/* modules */
	struct list_head module_list;
	struct list_head fw_list;
	struct sst_bmap block_map;
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

/* Create/Free FW files - can contain multiple modules */
struct sst_fw *sst_fw_new(struct sst_dsp *dsp,
	const struct firmware *fw, void *private);
void sst_fw_free(struct sst_fw *sst_fw);

/* Create/Free firmware modules */
struct sst_module *sst_module_new(struct sst_fw *sst_fw,
	struct sst_module_template *template, void *private);
void sst_module_free(struct sst_module *sst_module);
int sst_module_insert(struct sst_module *sst_module, struct sst_bmap *bmap);
int sst_module_remove(struct sst_module *sst_module, struct sst_bmap *bmap);
int sst_module_insert_section(struct sst_module *module, struct sst_bmap *bmap,
	u32 offset, u32 size, void *data);

/* Register the DSPs memory blocks - would be nice to read from ACPI */
struct sst_mem_block *sst_mem_block_register(struct sst_bmap *bmap, u32 offset,
	u32 size, enum sst_mem_type type, struct sst_block_ops *ops,
	void *private);
void sst_mem_block_unregister_all(struct sst_bmap *bmap);

#endif
