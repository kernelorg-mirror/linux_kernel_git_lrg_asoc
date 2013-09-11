/*
 * DSP module control API definition
 *
 * Copyright (C) 2013 Intel Corp.
 *
 * Authors: Rafal Sztejna <rafal.sztejna@intel.com>
 *
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

#ifndef _UAPI__SOUND_MODULE_CTL_H
#define _UAPI__SOUND_MODULE_CTL_H
	
#include <linux/types.h>

/* snd_mod_header: interface header.
 *
 * @module_id: unique ID of given module
 * @instance_id: instance number of given module
 * @reserved: future use
 */
struct snd_mod_header {
	u32 module_id;
	u32 instance_id;
	u64 reserved;
};

/* SNDRV_MOD_IOCTL_IF_VERSION: Obtain interface version.
 *
 * @u32: interface version value
 */
#define SNDRV_MOD_IOCTL_IF_VERSION	_IOR('D', 0x00, u32)


#define SND_MOD_MAX_INFO_SIZE 64

/* snd_mod_get_info: module get information data.
 *
 * @snd_mod_header: module information
 * @name: Vendor specific module name string 
 * @name: Vendor specific module version string 
 */
struct snd_mod_get_info {
	struct snd_mod_header module;
	char name[SND_MOD_MAX_INFO_SIZE];
	char version[SND_MOD_MAX_INFO_SIZE];
	u64 vendor;
};

#define SND_MOD_MAX_MODULE_NAME 64

/* snd_mod_load_module: request module load data.
 *
 * @snd_mod_header: module information
 * @name: module SST filename 
 */
struct snd_mod_load_module {
	struct snd_mod_header module;
	char name[SND_MOD_MAX_MODULE_NAME];
};

/* snd_mod_set_get_state: module state data.
 *
 * @snd_mod_header: module information
 * @state: current state of given module in DSP
 */
struct snd_mod_set_get_state {
	struct snd_mod_header header;
	u32 state;
};

/* snd_mod_param: module param data.
 *
 * @snd_mod_header: module information
 * @param_id: vendor defined ID of param
 * @param_size: param size
 * @buffer: pointer do param data
 */
struct snd_mod_param {
	struct snd_mod_header module;
	u32 param_id;
	u32 param_size;
	char buffer[0];
};

/* snd_mod_register_event_notify: register for module event data.
 *
 * @snd_mod_header: module information
 * @status: status of notification
 * @event_type: type of notification
 * @size: size of notification data
 * @data: data coming with notification
 */
struct snd_mod_register_event_notify {
	struct snd_mod_header module;
	u32 status;
	u32 event_type;
	u32 size;
	char data[0];
};


/* SNDRV_MOD_IOCTL_GET_INFO: query AudioDSP FW for module information.
 *
 * @snd_mod_get_info: name string, version string and vendor specific data
 */
#define SNDRV_MOD_IOCTL_GET_INFO	_IOR('D', 0x01, \
	struct snd_mod_get_info)

/* SNDRV_MOD_IOCTL_LOAD_MODULE: load a module to DSP memory.
 *
 * @snd_mod_load_module: module specific data
 */
#define SNDRV_MOD_IOCTL_LOAD_MODULE	_IOW('D', 0x02, \
	struct snd_mod_load_module)

/* SNDRV_MOD_IOCTL_SET_STATE: Change module state.
 *
 * @snd_mod_set_get_state: module status specific data
 */
#define SNDRV_MOD_IOCTL_SET_STATE	_IOW('D', 0x03, \
	struct snd_mod_set_get_state)

/* SNDRV_MOD_IOCTL_GET_STATE: Obtain module state.
 *
 * @snd_mod_set_get_state: module status specific data
 */
#define SNDRV_MOD_IOCTL_GET_STATE	_IOR('D', 0x04, \
	struct snd_mod_set_get_state)

/* SNDRV_MOD_IOCTL_SET_STATE: Send passthrough param to module in DSP
 *
 * @snd_mod_param: module parameter specific data
 */
#define SNDRV_MOD_IOCTL_SET_PARAM	_IOW('D', 0x05, \
	struct snd_mod_param)

/* SNDRV_MOD_IOCTL_GET_STATE: Recieve param from module in DSP
 *
 * @snd_mod_param: module parameter specific data
 */
#define SNDRV_MOD_IOCTL_GET_PARAM	_IOR('D', 0x06, \
	struct snd_mod_param)

/* SNDRV_MOD_IOCTL_GET_STATE: Register for event notification from DSP
 *
 * @snd_mod_register_event_notify: notification specific data
 */
#define SNDRV_MOD_IOCTL_REGISTER_EVENT_NOTIFY	_IOR('D', 0x07, \
	struct snd_mod_register_event_notify)

#endif
