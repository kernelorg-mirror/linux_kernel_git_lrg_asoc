/*
 * Intel Smart Sound Technology
 *
 * Copyright (C) 2013, Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __SOUND_SOC_SST_DSP_PRIV_H
#define __SOUND_SOC_SST_DSP_PRIV_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>

struct sst_mem_block;
struct sst_fw;

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
	void (*ram_read)(struct sst_dsp *sst, void *dest, void *src, size_t bytes);
	void (*ram_write)(struct sst_dsp *sst, void *dest, void *src, size_t bytes);

	void (*dump)(struct sst_dsp *);

	/* IRQ handlers */
	irqreturn_t (*irq_handler)(int irq, void *context);

	/* SST init and free */
	int (*init)(struct sst_dsp *sst, struct sst_pdata *pdata);
	void (*free)(struct sst_dsp *sst);

	/* FW module parser/loader */
	int (*parse_fw)(struct sst_fw *sst_fw);
};

/*
 * Memory offsets and addresses for Audio DSP.
 */ 
struct sst_addr {
	u32 ram_base;
	u32 shim_offset;
	u32 iram_offset;
	void __iomem *shim;
	void __iomem *ram;
	void __iomem *pci_cfg;
};

/*
 * Mailbox configuration.
 */
struct sst_mailbox {
	void __iomem *in_base;
	void __iomem *out_base;
	size_t in_size;
	size_t out_size;
};

/*
 * Firmware data types. 
 */
enum sst_data_type {
	SST_DATA_M	= 0, /* module block data */
	SST_DATA_P	= 1, /* peristant data (text, data) */
	SST_DATA_S	= 2, /* scratch data (usually buffers) */
};

/*
 * Audio DSP memory block types.
 */
enum sst_mem_type {
	SST_MEM_IRAM = 0,
	SST_MEM_DRAM = 1,
	SST_MEM_ANY  = 2,
};

/* 
 * SST Firmware File - each FW file can support N modules
 */
struct sst_fw {
	struct sst_dsp *dsp;

	dma_addr_t dmable_fw_paddr;
	struct list_head list;		/* DSP list of FW */
	struct list_head module_list;	/* FW list of modules */

	void *dma_buf;
	u32 size;

	void *private;
};

/*
 * SST Firmware File Module data.
 */
struct sst_module_data {
	u32 size;
	u32 offset;
	enum sst_mem_type type;
	enum sst_data_type data_type;
	u32 data_offset;
	void *data;
};


struct sst_module_template {
	u32 id;
	u32 entry;
	struct sst_module_data s;	/* scratch data */
	struct sst_module_data p;	/* peristant data */
};

/* SST Module - module can span multiple blocks */
struct sst_module {
	struct sst_dsp *dsp;
	struct sst_fw *sst_fw;		/* parent FW we belong too */

	/* module configuration */
	u32 id;
	u32 entry;
	u32 offset;
	u32 size;
	struct sst_module_data s;	/* scratch data */
	struct sst_module_data p;	/* peristant data */

	/* runtime */
	u32 usage_count;
	void *private;

	struct list_head block_list;	/* Module list of blocks in use */
	struct list_head list;		/* DSP list of modules */
	struct list_head list_fw;	/* FW list of modules */
};

struct sst_block_ops {
	int (*enable)(struct sst_mem_block *block);
	int (*disable)(struct sst_mem_block *block);
};

/* SST Memory Block - SST memory has multiple IRAM and DRAM blocks */
struct sst_mem_block {
	struct sst_dsp *dsp;
	struct sst_module *module;

	/* block config */
	u32 offset;
	u32 size;
	u32 index;
	enum sst_mem_type type;		/* block memory type IRAM/DRAM */
	struct sst_block_ops *ops;

	/* block status */
	u32 in_use;
	void *private;			/* generic core does not touch this */

	/* block lists */
	struct list_head module_list;	/* Module list of I blocks */
	struct list_head list;		/* Map list of free/used blocks */
	enum sst_data_type data_type;
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
	u32 id;

	/* memory blocks */
	struct list_head used_block_list;
	struct list_head free_block_list;

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

	/* SST FW modules */
	struct list_head module_list;
	struct list_head fw_list;

	/* platform data */
	struct sst_pdata *pdata;

	/* dma FW loading */
	struct sst_dma *dma;
	bool fw_use_dma;
};

/* Create/Free FW files - can contain multiple modules */
struct sst_fw *sst_fw_new(struct sst_dsp *dsp,
	const struct firmware *fw, void *private);
void sst_fw_free(struct sst_fw *sst_fw);
void sst_fw_free_all(struct sst_dsp *dsp);

/* Create/Free firmware modules */
struct sst_module *sst_module_new(struct sst_fw *sst_fw,
	struct sst_module_template *template, void *private);
void sst_module_free(struct sst_module *sst_module);
int sst_module_insert(struct sst_module *sst_module);
int sst_module_remove(struct sst_module *sst_module);
int sst_module_insert_fixed_block(struct sst_module *module,
	struct sst_module_data *data);
struct sst_module *sst_module_get_config(struct sst_dsp *dsp, u32 id);

/* allocate/free pesistent/scratch memory regions managed by drv */
int sst_mem_block_drv_managed_alloc(struct sst_dsp *dsp);
void sst_mem_block_drv_managed_free(struct sst_dsp *dsp);

/* Register the DSPs memory blocks - would be nice to read from ACPI */
struct sst_mem_block *sst_mem_block_register(struct sst_dsp *dsp, u32 offset,
	u32 size, enum sst_mem_type type, struct sst_block_ops *ops, u32 index,
	void *private);
void sst_mem_block_unregister_all(struct sst_dsp *dsp);
int sst_memcpy32_transport(struct sst_dsp *sst,
		void *dstn, u32 dstn_pa, void *src, size_t size);
int sst_dsp_dma_new(struct sst_dsp *sst);
void sst_dma_free(struct sst_dma *dma);

#endif
