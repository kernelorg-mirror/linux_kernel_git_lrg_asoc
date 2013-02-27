/*
 * Intel Lynxpoint LPSS clocks.
 *
 * Copyright (C) 2013, Intel Corporation
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *	    Heikki Krogerus <heikki.krogerus@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>

static int lpss_clk_probe(struct platform_device *pdev)
{
	struct clk *clk;

	/* LPSS free running clock */
	clk = clk_register_fixed_rate(&pdev->dev, "lpss_clk", NULL, CLK_IS_ROOT,
				      100000000);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	/* Shared DMA clock */
	clk_register_clkdev(clk, "hclk", "INTL9C60:00");
	clk_register_clkdev(clk, "hclk", "INTL9C60:01");

	/* PWM clock */
	clk = clk_register_fixed_rate(NULL, "pwm_clk", "lpss_clk", 0, 25000000);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	clk_register_clkdev(clk, NULL, "80860F08:00");
	clk_register_clkdev(clk, NULL, "80860F09:00");
	return 0;
}

static struct platform_driver lpss_clk_driver = {
	.driver = {
		.name = "clk-lpss",
		.owner = THIS_MODULE,
	},
	.probe = lpss_clk_probe,
};

int __init lpss_clk_init(void)
{
	return platform_driver_register(&lpss_clk_driver);
}
