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

#ifndef __SOUND_SOC_SST_DSP_H
#define __SOUND_SOC_SST_DSP_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>

struct sst_dsp;

/* SST PCI device IDs */
#define SST_MRST_PCI_ID 0x080A
#define SST_MFLD_PCI_ID 0x082F
#define SST_CLV_PCI_ID	0x08E7
#define SST_MRFLD_PCI_ID  0x119A


/* SST register map */
#define SST_CSR			0x00
#define SST_PISR		0x08
#define SST_PIMR		0x10
#define SST_ISRX		0x18
#define SST_ISRD		0x20
#define SST_IMRX		0x28
#define SST_IMRD		0x30
#define SST_IPCX		0x38 /* IPC IA -> SST */
#define SST_IPCD		0x40 /* IPC SST -> IA */
#define SST_ISRSC		0x48
#define SST_ISRLPESC		0x50
#define SST_IMRSC		0x58
#define SST_IMRLPESC		0x60
#define SST_IPCSC		0x68
#define SST_IPCLPESC		0x70
#define SST_CLKCTL		0x78
#define SST_CSR2		0x80
#define SST_LTRC		0xE0
#define SST_HDMC		0xE8
#define SST_DBGO		0xF0

#define SST_SHIM_SIZE		0x100
#define SST_PWMCTRL             0x1000

/* SST Register bits
 * The register/bit naming can differ between products. Some products also
 * contain extra fuctionality.
 */

/* CSR / CS */
#define SST_CSR_RST		(0x1 << 1)
#define SST_CSR_SBCS0		(0x1 << 2)
#define SST_CSR_SBCS1		(0x1 << 3)
#define SST_CSR_DCS(x)		(x << 4)
#define SST_CSR_DCS_MASK	(0x7 << 4)
#define SST_CSR_STALL		(0x1 << 10)
#define SST_CSR_S0IOCS		(0x1 << 21)
#define SST_CSR_S1IOCS		(0x1 << 23)
#define SST_CSR_LPCS		(0x1 << 31)

/*  ISRX / ISC */
#define SST_ISRX_BUSY		(0x1 << 1)
#define SST_ISRX_DONE		(0x1 << 0)

/*  ISRD / ISD */
#define SST_ISRD_BUSY		(0x1 << 1)
#define SST_ISRD_DONE		(0x1 << 0)

/* IMRX / IMC */
#define SST_IMRX_BUSY		(0x1 << 1)
#define SST_IMRX_DONE		(0x1 << 0)

/*  IPCX / IPCC */
#define	SST_IPCX_DONE		(0x1 << 30)
#define	SST_IPCX_BUSY		(0x1 << 31)

/*  IPCD */
#define	SST_IPCD_DONE		(0x1 << 30)
#define	SST_IPCD_BUSY		(0x1 << 31)

/* CLKCTL */
#define SST_CLKCTL_SMOS(x)	(x << 24)
#define SST_CLKCTL_MASK		(3 << 24)
#define SST_CLKCTL_DCPLCG	(1 << 18)
#define SST_CLKCTL_SCOE1	(1 << 17)
#define SST_CLKCTL_SCOE0	(1 << 16)

/* CSR2 / CS2 */
#define SST_CSR2_SDFD_SSP0	(1 << 1)
#define SST_CSR2_SDFD_SSP1	(1 << 2)

/* LTRC */
#define SST_LTRC_VAL(x)		(x << 0)

/* HDMC */
#define SST_HDMC_HDDA0(x)	(x << 0)
#define SST_HDMC_HDDA1(x)	(x << 7)

/*
 * SST Device.
 *
 * This structure is populated by the SST core driver. 
 */
struct sst_dsp_device {
	/* Mandatory fields */
	u32 id;
	irqreturn_t (*thread)(int irq, void *context);
	void *thread_context;
};

/* SST Device IDs - can be PCI or ACPI ID */
/* TODO: Add other ID numbers, they dont have to be PCI or ACPI ID, but can be
 * any unique number to identify device.
 */
#define SST_DEV_ID_HSWULT	0x33C8

#define SST_MAX_MEM_REGIONS	8

/*
 * SST Platform Data
 * This data can be read from ACPI bus, PCI or ACPI platform device data.
 */
struct sst_pdata {
	u32 address[SST_MAX_MEM_REGIONS];
	u32 length[SST_MAX_MEM_REGIONS];
	int num_regions;
	int irq;
};

/* Initialization */
struct sst_dsp *sst_dsp_new(struct device *dev,
	struct sst_dsp_device *sst_dev, struct sst_pdata *pdata);
void sst_dsp_free(struct sst_dsp *sst);

/* Firmware loading */
int sst_fw_load(struct sst_dsp *dsp, const char *fw, int use_dma);
void sst_fw_free(struct sst_dsp *dsp);

/* SHIM Read / Write */
void sst_dsp_shim_write(struct sst_dsp *sst, u32 offset, u32 value);
u32 sst_dsp_shim_read(struct sst_dsp *sst, u32 offset);
int sst_dsp_shim_update_bits(struct sst_dsp *sst, u32 offset,
				u32 mask, u32 value);
void sst_dsp_shim_write64(struct sst_dsp *sst, u32 offset, u64 value);
u64 sst_dsp_shim_read64(struct sst_dsp *sst, u32 offset);
int sst_dsp_shim_update_bits64(struct sst_dsp *sst, u32 offset,
				u64 mask, u64 value);

/* SHIM Read / Write Unlocked */
void sst_dsp_shim_write_unlocked(struct sst_dsp *sst, u32 offset, u32 value);
u32 sst_dsp_shim_read_unlocked(struct sst_dsp *sst, u32 offset);
int sst_dsp_shim_update_bits_unlocked(struct sst_dsp *sst, u32 offset,
				u32 mask, u32 value);
void sst_dsp_shim_write64_unlocked(struct sst_dsp *sst, u32 offset, u64 value);
u64 sst_dsp_shim_read64_unlocked(struct sst_dsp *sst, u32 offset);

/* Size optimised DRAM/IRAM memcpy */
void sst_dsp_iram_write(struct sst_dsp *sst, void *src, u32 dest_offset,
	size_t bytes);
void sst_dsp_dram_write(struct sst_dsp *sst, void *src, u32 dest_offset,
	size_t bytes);
void sst_dsp_dram_clear(struct sst_dsp *sst, u32 src_offset, size_t bytes);
void sst_dsp_dram_read(struct sst_dsp *sst, void *dest, u32 src_offset,
	size_t bytes);

/* DSP reset & boot */
void sst_dsp_reset(struct sst_dsp *sst);
int sst_dsp_boot(struct sst_dsp *sst);

/* Msg IO */
void sst_dsp_ipc_msg_tx(struct sst_dsp *dsp, u32 msg);
u32 sst_dsp_ipc_msg_rx(struct sst_dsp *dsp);
void *sst_dsp_get_thread_context(struct sst_dsp *sst);

/* Mailbox management */
int sst_dsp_mailbox_init(struct sst_dsp *dsp, u32 inbox_offset, size_t inbox_size,
	u32 outbox_offset, size_t outbox_size);
void sst_dsp_inbox_write(struct sst_dsp *dsp, void *message, size_t bytes);
void sst_dsp_inbox_read(struct sst_dsp *dsp, void *message, size_t bytes);
void sst_dsp_outbox_write(struct sst_dsp *dsp, void *message, size_t bytes);
void sst_dsp_outbox_read(struct sst_dsp *dsp, void *message, size_t bytes);
void sst_dsp_mailbox_dump(struct sst_dsp *dsp, size_t bytes);

/* Debug */
void sst_dsp_dump(struct sst_dsp *sst);


#endif
