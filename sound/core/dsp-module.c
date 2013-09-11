/*
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/export.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/dsp-module-drv.h>
#include <uapi/sound/dsp-module.h>

#define SNDRV_DSP_MODULE_VERSION	1

struct snd_dsp_module {
	const char *name;
	struct device *dev;
	void *private_data;
	struct snd_card *card;
	struct mutex lock;
	int device;
};

static int dsp_module_open(struct inode *inode, struct file *f)
{
	return 0;
}

static int dsp_module_free(struct inode *inode, struct file *f)
{
	return 0;
}

// TODO add all IOCTL handler funcs here
static int dsp_module_load(struct snd_dsp_module *mod, unsigned long arg)
{
	return 0;
}

// TODO add all IOCTLs to switch stamement
static long dsp_module_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct snd_dsp_module *mod = f->private_data;
	int retval = -ENOTTY;
	
	mutex_lock(&mod->lock);

	switch (_IOC_NR(cmd)) {
	case _IOC_NR(SNDRV_MOD_IOCTL_GET_INFO):
		retval = put_user(SNDRV_DSP_MODULE_VERSION,
				(int __user *)arg) ? -EFAULT : 0;
		break;
	case _IOC_NR(SNDRV_MOD_IOCTL_LOAD_MODULE):
		retval = dsp_module_load(mod, arg);
		break;

	}

	mutex_unlock(&mod->lock);
	return retval;
}

static const struct file_operations snd_dsp_module_file_ops = {
		.owner =	THIS_MODULE,
		.open =		dsp_module_open,
		.release =	dsp_module_free,
		.unlocked_ioctl = dsp_module_ioctl,
};

static int snd_dsp_module_dev_register(struct snd_device *device)
{
	struct snd_dsp_module *mod;
	int ret = -EINVAL;

	if (snd_BUG_ON(!device || !device->device_data))
		return -EBADFD;
	mod = device->device_data;

	/* register module device */
	ret = snd_register_device(SNDRV_DEVICE_TYPE_DSP_MODULE, mod->card,
			mod->device, &snd_dsp_module_file_ops, mod, "mod");
	if (ret < 0) {
		pr_err("snd_register_device failed\n %d", ret);
		return ret;
	}
	return ret;

}

static int snd_dsp_module_dev_disconnect(struct snd_device *device)
{
	struct snd_dsp_module *mod;

	mod = device->device_data;
	snd_unregister_device(SNDRV_DEVICE_TYPE_DSP_MODULE, mod->card, mod->device);
	return 0;
}

/*
 * snd_dsp_module_new: create new module device
 * @card: sound card pointer
 * @device: device number
 * @dirn: device direction, should be of type enum snd_dsp_module_direction
 * @compr: module device pointer
 */
int snd_dsp_module_new(struct snd_card *card, int device,
			struct snd_dsp_module *mod)
{
	static struct snd_device_ops ops = {
		.dev_free = NULL,
		.dev_register = snd_dsp_module_dev_register,
		.dev_disconnect = snd_dsp_module_dev_disconnect,
	};

	mod->card = card;
	mod->device = device;

	return snd_device_new(card, SNDRV_DEV_MODULE, mod, &ops);
}
EXPORT_SYMBOL_GPL(snd_dsp_module_new);

/*
 * snd_dsp_module_free: free module device
 * @card: sound card pointer
 * @compr: module device pointer
 */
void snd_dsp_module_free(struct snd_card *card, struct snd_dsp_module *mod)
{
	snd_device_free(card, mod);
}
EXPORT_SYMBOL_GPL(snd_dsp_module_free);

static int __init snd_dsp_module_init(void)
{
	return 0;
}

static void __exit snd_dsp_module_exit(void)
{
}

module_init(snd_dsp_module_init);
module_exit(snd_dsp_module_exit);

MODULE_DESCRIPTION("ALSA moduleed offload framework");
MODULE_AUTHOR("Vinod Koul <vinod.koul@linux.intel.com>");
MODULE_LICENSE("GPL v2");

