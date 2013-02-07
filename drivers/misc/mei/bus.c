/*
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2012, Intel Corporation.
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
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/mei_bus.h>

#include "mei_dev.h"

#define to_mei_driver(d) container_of(d, struct mei_driver, driver)
#define to_mei_device(d) container_of(d, struct mei_device, dev)

static int mei_device_match(struct device *dev, struct device_driver *drv)
{
	struct mei_device *device = to_mei_device(dev);
	struct mei_driver *driver = to_mei_driver(drv);
	const struct mei_id *id;

	if (!device)
		return 0;

	if (!driver || !driver->id_table)
		return 0;

	id = driver->id_table;

	while (id->name[0]) {
		if (!uuid_le_cmp(device->uuid, id->uuid) &&
		    !strcmp(dev_name(dev), id->name))
			return 1;

		id++;
	}

	return 0;
}

static int mei_device_probe(struct device *dev)
{
	struct mei_device *device = to_mei_device(dev);
	struct mei_driver *driver;
	struct mei_id id;

	if (!device)
		return 0;

	driver = to_mei_driver(dev->driver);
	if (!driver || !driver->probe)
		return -ENODEV;

	dev_dbg(dev, "Device probe\n");

	id.uuid = device->uuid;
	strncpy(id.name, dev_name(dev), MEI_NAME_SIZE);

	return driver->probe(device, &id);
}

static int mei_device_remove(struct device *dev)
{
	struct mei_device *device = to_mei_device(dev);
	struct mei_driver *driver;

	if (!device || !dev->driver)
		return 0;

	driver = to_mei_driver(dev->driver);
	if (!driver->remove) {
		dev->driver = NULL;

		return 0;
	}

	return driver->remove(device);
}

static struct bus_type mei_bus_type = {
	.name		= "mei",
	.match		= mei_device_match,
	.probe		= mei_device_probe,
	.remove		= mei_device_remove,
};

static void mei_client_dev_release(struct device *dev)
{
	kfree(to_mei_device(dev));
}

static struct device_type mei_client_type = {
	.release	= mei_client_dev_release,
};

struct mei_device *mei_add_device(struct mei_host *mei_host,
				  uuid_le uuid, char *name)
{
	struct mei_device *device;
	int status;

	device = kzalloc(sizeof(struct mei_device), GFP_KERNEL);
	if (!device)
		return NULL;

	device->uuid = uuid;

	device->dev.parent = &mei_host->pdev->dev;
	device->dev.bus = &mei_bus_type;
	device->dev.type = &mei_client_type;

	dev_set_name(&device->dev, "%s", name);

	status = device_register(&device->dev);
	if (status)
		goto out_err;

	dev_dbg(&device->dev, "client %s registered\n", name);

	return device;

out_err:
	dev_err(device->dev.parent, "Failed to register MEI client\n");

	kfree(device);

	return NULL;
}
EXPORT_SYMBOL_GPL(mei_add_device);

void mei_remove_device(struct mei_device *device)
{
	device_unregister(&device->dev);
}
EXPORT_SYMBOL_GPL(mei_remove_device);
