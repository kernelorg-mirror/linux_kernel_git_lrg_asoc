/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#ifndef __SOUND_DSP_MODULE_DRV_H
#define __SOUND_DSP_MODULE_DRV_H

struct snd_card;
struct snd_dsp_module;

int snd_dsp_module_new(struct snd_card *card, int device,
			struct snd_dsp_module *mod);
void snd_dsp_module_free(struct snd_card *card, struct snd_dsp_module *mod);

#endif
