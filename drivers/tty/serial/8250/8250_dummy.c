/*
 * 8250_dummy.c: Dummy 8250 UART target device enumerator
 *
 * Copyright (c) 2012-2013, Intel Corporation
 * Author: Lv Zheng <lv.zheng@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 */


#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/serial_8250.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* Keep the inclusion order same as 8250.c */

#include "8250.h"

#define CONFIG_HUB6	1
#include <asm/serial.h>

#ifndef SERIAL_PORT_DFNS
#define SERIAL_PORT_DFNS
#endif

struct dummy8250_data {
	int	line;
};

static const struct old_serial_port old_serial_port[] = {
	SERIAL_PORT_DFNS
};

static unsigned int share_irqs = SERIAL8250_SHARE_IRQS;

static int dummy8250_probe(struct platform_device *pdev)
{
	struct uart_8250_port uart = {};
	int irqflags = 0;
	struct dummy8250_data *data;

	if (share_irqs)
		irqflags = IRQF_SHARED;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	uart.port.private_data = data;

	spin_lock_init(&uart.port.lock);
	uart.port.iobase = old_serial_port[0].port;
	uart.port.membase = old_serial_port[0].iomem_base;
	uart.port.irq = irq_canonicalize(old_serial_port[0].irq);
	uart.port.irqflags = (old_serial_port[0].irqflags | irqflags);
	uart.port.uartclk = old_serial_port[0].baud_base * 16;
	uart.port.regshift = old_serial_port[0].iomem_reg_shift;
	uart.port.iotype = old_serial_port[0].io_type;
	uart.port.flags = old_serial_port[0].flags;
	uart.port.hub6 = old_serial_port[0].hub6;
	uart.port.dev = &pdev->dev;

	data->line = serial8250_register_8250_port(&uart);
	if (data->line < 0)
		return data->line;

	platform_set_drvdata(pdev, data);

	return 0;
}

static int dummy8250_remove(struct platform_device *pdev)
{
	struct dummy8250_data *data = platform_get_drvdata(pdev);

	serial8250_unregister_port(data->line);
	return 0;
}

static const struct acpi_device_id dummy8250_match[] = {
	{ .id = "IBMF000" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(acpi, dummy8250_match);

static struct platform_driver dummy8250_platform_driver = {
	.driver = {
		.name			= "dummy-uart",
		.owner			= THIS_MODULE,
		.acpi_match_table	= ACPI_PTR(dummy8250_match),
	},
	.probe				= dummy8250_probe,
	.remove				= dummy8250_remove,
};

module_platform_driver(dummy8250_platform_driver);

MODULE_AUTHOR("Lv Zheng");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Dummy 8250 serial port driver");
