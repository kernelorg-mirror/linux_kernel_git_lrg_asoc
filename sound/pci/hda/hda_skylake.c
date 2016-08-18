/*
 *
 * Implementation of primary ALSA driver code base for Intel Skylake and Broxton.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/clk.h>
#include <linux/clocksource.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/time.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/dsp_bus.h>

#include "hda_codec.h"
#include "hda_controller.h"
#include "hda_priv.h"


struct hda_skl {
	struct azx chip;
	struct device *dev;

	void __iomem *regs;
};

/* PCI register access. */
static void pci_azx_writel(u32 value, u32 __iomem *addr)
{
	writel(value, addr);
}

static u32 pci_azx_readl(u32 __iomem *addr)
{
	return readl(addr);
}

static void pci_azx_writew(u16 value, u16 __iomem *addr)
{
	writew(value, addr);
}

static u16 pci_azx_readw(u16 __iomem *addr)
{
	return readw(addr);
}

static void pci_azx_writeb(u8 value, u8 __iomem *addr)
{
	writeb(value, addr);
}

static u8 pci_azx_readb(u8 __iomem *addr)
{
	return readb(addr);
}

static int disable_msi_reset_irq(struct azx *chip)
{
	int err;

	free_irq(chip->irq, chip);
	chip->irq = -1;
	pci_disable_msi(chip->pci);
	chip->msi = 0;
	err = azx_acquire_irq(chip, 1);
	if (err < 0)
		return err;

	return 0;
}

/* DMA page allocation helpers.  */
static int dma_alloc_pages(struct azx *chip,
			   int type,
			   size_t size,
			   struct snd_dma_buffer *buf)
{
	int err;

	err = snd_dma_alloc_pages(type,
				  chip->dev,
				  size, buf);
	if (err < 0)
		return err;
	mark_pages_wc(chip, buf, true);
	return 0;
}

static void dma_free_pages(struct azx *chip, struct snd_dma_buffer *buf)
{
	mark_pages_wc(chip, buf, false);
	snd_dma_free_pages(buf);
}

static int substream_alloc_pages(struct azx *chip,
				 struct snd_pcm_substream *substream,
				 size_t size)
{
	struct azx_dev *azx_dev = get_azx_dev(substream);
	int ret;

	mark_runtime_wc(chip, azx_dev, substream, false);
	azx_dev->bufsize = 0;
	azx_dev->period_bytes = 0;
	azx_dev->format_val = 0;
	ret = snd_pcm_lib_malloc_pages(substream, size);
	if (ret < 0)
		return ret;
	mark_runtime_wc(chip, azx_dev, substream, true);
	return 0;
}

static int substream_free_pages(struct azx *chip,
				struct snd_pcm_substream *substream)
{
	struct azx_dev *azx_dev = get_azx_dev(substream);
	mark_runtime_wc(chip, azx_dev, substream, false);
	return snd_pcm_lib_free_pages(substream);
}

static const struct hda_controller_ops hda_skylake_ops = {
	.reg_writel = pci_azx_writel,
	.reg_readl = pci_azx_readl,
	.reg_writew = pci_azx_writew,
	.reg_readw = pci_azx_readw,
	.reg_writeb = pci_azx_writeb,
	.reg_readb = pci_azx_readb,
	.disable_msi_reset_irq = disable_msi_reset_irq,
	.dma_alloc_pages = dma_alloc_pages,
	.dma_free_pages = dma_free_pages,
	.substream_alloc_pages = substream_alloc_pages,
	.substream_free_pages = substream_free_pages,
	.position_check = azx_position_check,
};

static void hda_skylake_init(struct hda_skylake *hda)
{
	u32 v;

	/* Enable PCI access */
	v = readl(hda->regs + HDA_IPFS_CONFIG);
	v |= HDA_IPFS_EN_FPCI;
	writel(v, hda->regs + HDA_IPFS_CONFIG);

	/* Enable MEM/IO space and bus master */
	v = readl(hda->regs + HDA_CFG_CMD);
	v &= ~HDA_DISABLE_INTR;
	v |= HDA_ENABLE_MEM_SPACE | HDA_ENABLE_IO_SPACE |
		HDA_ENABLE_BUS_MASTER | HDA_ENABLE_SERR;
	writel(v, hda->regs + HDA_CFG_CMD);

	writel(HDA_BAR0_INIT_PROGRAM, hda->regs + HDA_CFG_BAR0);
	writel(HDA_BAR0_FINAL_PROGRAM, hda->regs + HDA_CFG_BAR0);
	writel(HDA_FPCI_BAR0_START, hda->regs + HDA_IPFS_FPCI_BAR0);

	v = readl(hda->regs + HDA_IPFS_INTR_MASK);
	v |= HDA_IPFS_EN_INTR;
	writel(v, hda->regs + HDA_IPFS_INTR_MASK);
}

/*
 * destructor
 */
static int hda_skylake_dev_free(struct snd_device *device)
{
	int i;
	struct azx *chip = device->device_data;

	azx_notifier_unregister(chip);

	if (chip->initialized) {
		for (i = 0; i < chip->num_streams; i++)
			azx_stream_stop(chip, &chip->azx_dev[i]);
		azx_stop_chip(chip);
	}

	azx_free_stream_pages(chip);

	return 0;
}

static int hda_skylake_init_chip(struct azx *chip, struct platform_device *pdev)
{
	struct hda_skylake *hda = container_of(chip, struct hda_skylake, chip);
	struct device *dev = hda->dev;
	struct resource *res;
	int err;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hda->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(chip->remap_addr))
		return PTR_ERR(chip->remap_addr);

	chip->remap_addr = hda->regs + HDA_BAR0;
	chip->addr = res->start + HDA_BAR0;


	hda_skylake_init(hda);

	return 0;
}

/*
 * The codecs were powered up in snd_hda_codec_new().
 * Now all initialization done, so turn them down if possible
 */
static void power_down_all_codecs(struct azx *chip)
{
	struct hda_codec *codec;
	list_for_each_entry(codec, &chip->bus->codec_list, list)
		snd_hda_power_down(codec);
}

static int hda_skylake_first_init(struct azx *chip, struct platform_device *pdev)
{
	struct snd_card *card = chip->card;
	int err;
	unsigned short gcap;
	int irq_id = platform_get_irq(pdev, 0);

	err = hda_skylake_init_chip(chip, pdev);
	if (err)
		return err;

	err = devm_request_irq(chip->card->dev, irq_id, azx_interrupt,
			     IRQF_SHARED, KBUILD_MODNAME, chip);
	if (err) {
		dev_err(chip->card->dev,
			"unable to request IRQ %d, disabling device\n",
			irq_id);
		return err;
	}
	chip->irq = irq_id;

	synchronize_irq(chip->irq);

	gcap = azx_readw(chip, GCAP);
	dev_dbg(card->dev, "chipset global capabilities = 0x%x\n", gcap);

	/* read number of streams from GCAP register instead of using
	 * hardcoded value
	 */
	chip->capture_streams = (gcap >> 8) & 0x0f;
	chip->playback_streams = (gcap >> 12) & 0x0f;
	if (!chip->playback_streams && !chip->capture_streams) {
		/* gcap didn't give any info, switching to old method */
		chip->playback_streams = NUM_PLAYBACK_SD;
		chip->capture_streams = NUM_CAPTURE_SD;
	}
	chip->capture_index_offset = 0;
	chip->playback_index_offset = chip->capture_streams;
	chip->num_streams = chip->playback_streams + chip->capture_streams;
	chip->azx_dev = devm_kcalloc(card->dev, chip->num_streams,
				     sizeof(*chip->azx_dev), GFP_KERNEL);
	if (!chip->azx_dev)
		return -ENOMEM;

	err = azx_alloc_stream_pages(chip);
	if (err < 0)
		return err;

	/* initialize streams */
	azx_init_stream(chip);

	/* initialize chip */
	azx_init_chip(chip, 1);

	/* codec detection */
	if (!chip->codec_mask) {
		dev_err(card->dev, "no codecs found!\n");
		return -ENODEV;
	}

	strcpy(card->driver, "skylake-hda");
	strcpy(card->shortname, "skylake-hda");
	snprintf(card->longname, sizeof(card->longname),
		 "%s at 0x%lx irq %i",
		 card->shortname, chip->addr, chip->irq);

	return 0;
}

/*
 * constructor
 */
static int hda_skylake_create(struct snd_card *card,
			    unsigned int driver_caps,
			    const struct hda_controller_ops *hda_ops,
			    struct hda_skylake *hda)
{
	static struct snd_device_ops ops = {
		.dev_free = hda_skylake_dev_free,
	};
	struct azx *chip;
	int err;

	chip = &hda->chip;

	spin_lock_init(&chip->reg_lock);
	mutex_init(&chip->open_mutex);
	chip->card = card;
	chip->ops = hda_ops;
	chip->irq = -1;
	chip->driver_caps = driver_caps;
	chip->driver_type = driver_caps & 0xff;
	chip->dev_index = 0;
	INIT_LIST_HEAD(&chip->pcm_list);

	chip->codec_probe_mask = -1;

	chip->single_cmd = false;
	chip->snoop = true;

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0) {
		dev_err(card->dev, "Error creating device\n");
		return err;
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP

/*
 * power management - these will all need to be ordered so that :-
 *
 * 1. All suspend calls are performed after DSP context save
 * 2. All resume calls are performed before DSP context restore.
 */
static int hda_skylake_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;
	struct azx_pcm *p;
	struct hda_skylake *hda = container_of(chip, struct hda_skylake, chip);

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	list_for_each_entry(p, &chip->pcm_list, list)
		snd_pcm_suspend_all(p->pcm);
	if (chip->initialized)
		snd_hda_suspend(chip->bus);

	azx_stop_chip(chip);
	azx_enter_link_reset(chip);

	return 0;
}

static int hda_skylake_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;
	struct hda_skylake *hda = container_of(chip, struct hda_skylake, chip);


	hda_skylake_init(hda);

	azx_init_chip(chip, 1);

	snd_hda_resume(chip->bus);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM_RUNTIME
static int skylake_runtime_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip;
	struct hda_intel *hda;

	if (!card)
		return 0;

	chip = card->private_data;
	hda = container_of(chip, struct hda_intel, chip);
	if (chip->disabled || hda->init_failed)
		return 0;


	/* enable controller wake up event */
	skylake_writew(chip, WAKEEN, skylake_readw(chip, WAKEEN) |
		  STATESTS_INT_MASK);

	skylake_stop_chip(chip);
	skylake_enter_link_reset(chip);
	skylake_clear_irq_pending(chip);


	return 0;
}

static int skylake_runtime_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip;
	struct hda_intel *hda;
	struct hda_bus *bus;
	struct hda_codec *codec;
	int status;

	if (!card)
		return 0;

	chip = card->private_data;
	hda = container_of(chip, struct hda_intel, chip);
	if (chip->disabled || hda->init_failed)
		return 0;


	/* Read STATESTS before controller reset */
	status = skylake_readw(chip, STATESTS);

	azx_init_pci(chip);
	azx_init_chip(chip, true);

	bus = chip->bus;
	if (status && bus) {
		list_for_each_entry(codec, &bus->codec_list, list)
			if (status & (1 << codec->addr))
				queue_delayed_work(codec->bus->workq,
						   &codec->jackpoll_work, codec->jackpoll_interval);
	}

	/* disable controller Wake Up event*/
	skylake_writew(chip, WAKEEN, skylake_readw(chip, WAKEEN) &
			~STATESTS_INT_MASK);

	return 0;
}

static int skylake_runtime_idle(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip;
	struct hda_intel *hda;

	if (!card)
		return 0;

	chip = card->private_data;
	hda = container_of(chip, struct hda_intel, chip);
	if (chip->disabled || hda->init_failed)
		return 0;

	return 0;
}
#endif

static const struct dev_pm_ops skylake_pm = {
#ifdef CONFIG_PM_RUNTIME
	SET_SYSTEM_SLEEP_PM_OPS(skylake_suspend, skylake_resume)
#endif
#if CONFIG_PM_SLEEP
	SET_RUNTIME_PM_OPS(skylake_runtime_suspend, skylake_runtime_resume, skylake_runtime_idle)
#endif
};

int snd_hda_skylake_probe(struct snd_dsp *dsp, struct snd_card *card)
{
	struct azx *chip;
	struct hda_skylake *skl;
	int err;
	const unsigned int driver_flags;

	skl = devm_kzalloc(&pci->dev, sizeof(*skl), GFP_KERNEL);
	if (!skl)
		return -ENOMEM;
	skl->dev = dsp->dev;
	chip = &skl->chip;

	err = hda_skylake_create(card, driver_flags, &hda_skylake_ops, skl);
	if (err < 0)
		goto out_free;
	card->private_data = chip;

	dev_set_drvdata(&pdev->dev, card);

	err = hda_skylake_first_init(chip, pdev);
	if (err < 0)
		goto out_free;

	/* create codec instances */
	err = azx_codec_create(chip, NULL, 0, &power_save);
	if (err < 0)
		goto out_free;

	err = azx_codec_configure(chip);
	if (err < 0)
		goto out_free;

	/* create PCM streams */
	err = snd_hda_build_pcms(chip->bus);
	if (err < 0)
		goto out_free;

	/* create mixer controls */
	err = azx_mixer_create(chip);
	if (err < 0)
		goto out_free;

	err = snd_card_register(chip->card);
	if (err < 0)
		goto out_free;

	chip->running = 1;
	power_down_all_codecs(chip);
	azx_notifier_register(chip);

	return 0;

out_free:
	snd_card_free(card);
	return err;
}


static void skylake_remove(struct pci_dev *pci)
{
	//struct snd_card *card = pci_get_drvdata(pci);
}

static void skylake_device_release(struct device *dev)
{
	kfree(to_dsp_t(dev));
}

static int skylake_probe(struct pci_dev *pci,
		     const struct pci_device_id *pci_id)
{
	struct snd_dsp *dsp;
	int ret;

	/* check for integrated controller DSP - use legacy driver if not found */
	if (1) {

#if 0
		/* check BIOS that ADSP is enabled */
		if (!bios_enabled)
			return azx_probe_do(pci, pci_id, NULL, false);
#endif

		dsp = kzalloc(sizeof(struct snd_hda), GFP_KERNEL);
		if (dsp == NULL)
			return -ENOMEM;

		dsp->dev.bus = &hda_bus_type;
		dsp->dev.parent = &pci->dev;
		dsp->dev.release = azx_dsp_device_release;

		/* use a better name */
		dev_set_name(&dsp->dev, "dsp-%d",dev);

		/* register device with HDA bus driver */
		ret = device_register(&dsp->dev);
		if (ret) {
			put_device(&dsp->dev);
			return ret;
		}
		return ret;

	} else
		return azx_probe_do(pci, pci_id, NULL, false);
}

/* PCI IDs  */
static const struct pci_device_id skylake_ids[] = {
	/* Sunrise Point-LP */
	{ PCI_DEVICE(0x8086, 0x9d70),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_SKYLAKE },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, skylake_ids);

/* pci_driver definition */
static struct pci_driver skylake_hda_driver = {
	.name = KBUILD_MODNAME,
	.id_table = skylake_ids,
	.probe = skylake_probe,
	.remove = skylake_remove,
	.driver = {
		.pm = &skylake_pm,
	},
};
module_pci_driver(skylake_hda_driver);

MODULE_DESCRIPTION("Skylake/Broxton HDA bus driver");
MODULE_LICENSE("GPL v2");
