/*
 * sst_hsw_pcm.h - Intel Smart Sound Technology
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

#ifndef __SOUND_SOC_SST_HSW_PCM_H
#define __SOUND_SOC_SST_HSW_PCM_H

struct sst_dsp;

struct sst_hsw_pcm {
	struct sst_dsp *sst;
	struct sst_hsw *hsw;
};

#endif
