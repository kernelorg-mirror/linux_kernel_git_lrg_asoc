/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __SOF_INTEL_H
#define __SOF_INTEL_H


/*
 * SHIM registers for BYT, BSW, CHT HSW, BDW
 */

#define SHIM_CSR		0x00
#define SHIM_PISR		0x08
#define SHIM_PIMR		0x10
#define SHIM_ISRX		0x18
#define SHIM_ISRD		0x20
#define SHIM_IMRX		0x28
#define SHIM_IMRD		0x30
#define SHIM_IPCX		0x38
#define SHIM_IPCD		0x40
#define SHIM_ISRSC		0x48
#define SHIM_ISRLPESC		0x50
#define SHIM_IMRSC		0x58
#define SHIM_IMRLPESC		0x60
#define SHIM_IPCSC		0x68
#define SHIM_IPCLPESC		0x70
#define SHIM_CLKCTL		0x78
#define SHIM_CSR2		0x80
#define SHIM_LTRC		0xE0
#define SHIM_HMDC		0xE8


#define SHIM_SIZE		0x100
#define SHIM_PWMCTRL		0x1000


/*
 * SST SHIM register bits for BYT, BSW, CHT HSW, BDW
 * Register bit naming and functionaility can differ between devices.
 */

/* CSR / CS */
#define SHIM_CSR_RST		(0x1 << 1)
#define SHIM_CSR_SBCS0		(0x1 << 2)
#define SHIM_CSR_SBCS1		(0x1 << 3)
#define SHIM_CSR_DCS(x)		(x << 4)
#define SHIM_CSR_DCS_MASK	(0x7 << 4)
#define SHIM_CSR_STALL		(0x1 << 10)
#define SHIM_CSR_S0IOCS		(0x1 << 21)
#define SHIM_CSR_S1IOCS		(0x1 << 23)
#define SHIM_CSR_LPCS		(0x1 << 31)
#define SHIM_CSR_24MHZ_LPCS	(SHIM_CSR_SBCS0 | SHIM_CSR_SBCS1 | SHIM_CSR_LPCS)
#define SHIM_CSR_24MHZ_NO_LPCS	(SHIM_CSR_SBCS0 | SHIM_CSR_SBCS1)
#define SHIM_BYT_CSR_RST	(0x1 << 0)
#define SHIM_BYT_CSR_VECTOR_SEL	(0x1 << 1)
#define SHIM_BYT_CSR_STALL	(0x1 << 2)
#define SHIM_BYT_CSR_PWAITMODE	(0x1 << 3)

/*  ISRX / ISC */
#define SHIM_ISRX_BUSY		(0x1 << 1)
#define SHIM_ISRX_DONE		(0x1 << 0)
#define SHIM_BYT_ISRX_REQUEST	(0x1 << 1)

/*  ISRD / ISD */
#define SHIM_ISRD_BUSY		(0x1 << 1)
#define SHIM_ISRD_DONE		(0x1 << 0)

/* IMRX / IMC */
#define SHIM_IMRX_BUSY		(0x1 << 1)
#define SHIM_IMRX_DONE		(0x1 << 0)
#define SHIM_BYT_IMRX_REQUEST	(0x1 << 1)

/* IMRD / IMD */
#define SHIM_IMRD_DONE		(0x1 << 0)
#define SHIM_IMRD_BUSY		(0x1 << 1)
#define SHIM_IMRD_SSP0		(0x1 << 16)
#define SHIM_IMRD_DMAC0		(0x1 << 21)
#define SHIM_IMRD_DMAC1		(0x1 << 22)
#define SHIM_IMRD_DMAC		(SHIM_IMRD_DMAC0 | SHIM_IMRD_DMAC1)

/*  IPCX / IPCC */
#define	SHIM_IPCX_DONE		(0x1 << 30)
#define	SHIM_IPCX_BUSY		(0x1 << 31)
#define SHIM_BYT_IPCX_DONE	((u64)0x1 << 62)
#define SHIM_BYT_IPCX_BUSY	((u64)0x1 << 63)

/*  IPCD */
#define	SHIM_IPCD_DONE		(0x1 << 30)
#define	SHIM_IPCD_BUSY		(0x1 << 31)
#define SHIM_BYT_IPCD_DONE	((u64)0x1 << 62)
#define SHIM_BYT_IPCD_BUSY	((u64)0x1 << 63)

/* CLKCTL */
#define SHIM_CLKCTL_SMOS(x)	(x << 24)
#define SHIM_CLKCTL_MASK	(3 << 24)
#define SHIM_CLKCTL_DCPLCG	(1 << 18)
#define SHIM_CLKCTL_SCOE1	(1 << 17)
#define SHIM_CLKCTL_SCOE0	(1 << 16)

/* CSR2 / CS2 */
#define SHIM_CSR2_SDFD_SSP0	(1 << 1)
#define SHIM_CSR2_SDFD_SSP1	(1 << 2)

/* LTRC */
#define SHIM_LTRC_VAL(x)	(x << 0)

/* HMDC */
#define SHIM_HMDC_HDDA0(x)	(x << 0)
#define SHIM_HMDC_HDDA1(x)	(x << 7)
#define SHIM_HMDC_HDDA_E0_CH0	1
#define SHIM_HMDC_HDDA_E0_CH1	2
#define SHIM_HMDC_HDDA_E0_CH2	4
#define SHIM_HMDC_HDDA_E0_CH3	8
#define SHIM_HMDC_HDDA_E1_CH0	SHIM_HMDC_HDDA1(SHIM_HMDC_HDDA_E0_CH0)
#define SHIM_HMDC_HDDA_E1_CH1	SHIM_HMDC_HDDA1(SHIM_HMDC_HDDA_E0_CH1)
#define SHIM_HMDC_HDDA_E1_CH2	SHIM_HMDC_HDDA1(SHIM_HMDC_HDDA_E0_CH2)
#define SHIM_HMDC_HDDA_E1_CH3	SHIM_HMDC_HDDA1(SHIM_HMDC_HDDA_E0_CH3)
#define SHIM_HMDC_HDDA_E0_ALLCH	(SHIM_HMDC_HDDA_E0_CH0 | SHIM_HMDC_HDDA_E0_CH1 | \
				 SHIM_HMDC_HDDA_E0_CH2 | SHIM_HMDC_HDDA_E0_CH3)
#define SHIM_HMDC_HDDA_E1_ALLCH	(SHIM_HMDC_HDDA_E1_CH0 | SHIM_HMDC_HDDA_E1_CH1 | \
				 SHIM_HMDC_HDDA_E1_CH2 | SHIM_HMDC_HDDA_E1_CH3)




/* Audio DSP PCI registers */
#define PCI_VDRTCTL0		0xa0
#define PCI_VDRTCTL1		0xa4
#define PCI_VDRTCTL2		0xa8
#define PCI_VDRTCTL3		0xaC

/* VDRTCTL0 */
#define PCI_VDRTCL0_D3PGD		(1 << 0)
#define PCI_VDRTCL0_D3SRAMPGD		(1 << 1)
#define PCI_VDRTCL0_DSRAMPGE_SHIFT	12
#define PCI_VDRTCL0_DSRAMPGE_MASK	(0xfffff << PCI_VDRTCL0_DSRAMPGE_SHIFT)
#define PCI_VDRTCL0_ISRAMPGE_SHIFT	2
#define PCI_VDRTCL0_ISRAMPGE_MASK	(0x3ff << PCI_VDRTCL0_ISRAMPGE_SHIFT)

/* VDRTCTL2 */
#define PCI_VDRTCL2_DCLCGE		(1 << 1)
#define PCI_VDRTCL2_DTCGE		(1 << 10)
#define PCI_VDRTCL2_APLLSE_MASK		(1 << 31)

/* PMCS */
#define PCI_PMCS		0x84
#define PCI_PMCS_PS_MASK	0x3

#endif
