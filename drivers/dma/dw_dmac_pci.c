/*
 * PCI driver for the Synopsys DesignWare DMA Controller
 *
 * Copyright (C) 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/idr.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/dw_dmac.h>

static struct dw_dma_platform_data dw_pci_pdata = {
	.is_private = 1,
	.chan_allocation_order = CHAN_ALLOCATION_ASCENDING,
	.chan_priority = CHAN_PRIORITY_ASCENDING,
};

static DEFINE_IDA(dw_pci_ida);

static int dw_pci_probe(struct pci_dev *pdev, const struct pci_device_id *pid)
{
	struct platform_device *pd;
	struct resource r[2];
	struct dw_dma_platform_data *driver = (void *)pid->driver_data;
	int id, ret;

	id = ida_simple_get(&dw_pci_ida, 0, 0, GFP_KERNEL);
	if (id < 0)
		return id;

	ret = pci_enable_device(pdev);
	if (ret)
		goto put_id;

	pci_set_power_state(pdev, PCI_D0);
	pci_set_master(pdev);
	pci_try_set_mwi(pdev);

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret)
		goto err0;

	ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret)
		goto err0;

	pd = platform_device_alloc("dw_dmac", id);
	if (!pd) {
		dev_err(&pdev->dev, "can't allocate dw_dmac platform device\n");
		ret = -ENOMEM;
		goto err0;
	}

	memset(r, 0, sizeof(r));

	r[0].start = pci_resource_start(pdev, 0);
	r[0].end = pci_resource_end(pdev, 0);
	r[0].flags = IORESOURCE_MEM;

	r[1].start = pdev->irq;
	r[1].flags = IORESOURCE_IRQ;

	ret = platform_device_add_resources(pd, r, ARRAY_SIZE(r));
	if (ret) {
		dev_err(&pdev->dev, "can't add resources to platform device\n");
		goto err1;
	}

	ret = platform_device_add_data(pd, driver, sizeof(*driver));
	if (ret)
		goto err1;

	dma_set_coherent_mask(&pd->dev, pdev->dev.coherent_dma_mask);
	pd->dev.dma_mask = pdev->dev.dma_mask;
	pd->dev.dma_parms = pdev->dev.dma_parms;
	pd->dev.parent = &pdev->dev;

	pci_set_drvdata(pdev, pd);

	ret = platform_device_add(pd);
	if (ret) {
		dev_err(&pdev->dev, "platform_device_add failed\n");
		goto err1;
	}

	return 0;

err1:
	platform_device_put(pd);
err0:
	pci_disable_device(pdev);
put_id:
	ida_simple_remove(&dw_pci_ida, id);
	return ret;
}

static void dw_pci_remove(struct pci_dev *pdev)
{
	struct platform_device *pd = pci_get_drvdata(pdev);

	platform_device_unregister(pd);
	ida_simple_remove(&dw_pci_ida, pd->id);
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
}

static DEFINE_PCI_DEVICE_TABLE(dw_pci_id_table) = {
	{ PCI_VDEVICE(INTEL, 0x0827), (kernel_ulong_t)&dw_pci_pdata },
	{ PCI_VDEVICE(INTEL, 0x0830), (kernel_ulong_t)&dw_pci_pdata },
	{ PCI_VDEVICE(INTEL, 0x0f06), (kernel_ulong_t)&dw_pci_pdata },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, dw_pci_id_table);

static struct pci_driver dw_pci_driver = {
	.name		= "dw_dmac_pci",
	.id_table	= dw_pci_id_table,
	.probe		= dw_pci_probe,
	.remove		= dw_pci_remove,
};

module_pci_driver(dw_pci_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare DMAC PCI driver");
MODULE_AUTHOR("Heikki Krogerus <heikki.krogerus@linux.intel.com>");
MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
