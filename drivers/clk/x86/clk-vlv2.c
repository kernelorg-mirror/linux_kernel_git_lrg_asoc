/*
 * Intel ValleyView2 LPSS clocks.
 *
 * Copyright (C) 2013, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
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

#include <asm/processor.h>

#include "clk-lpss.h"

#define PRV_CLOCK_PARAMS_SPI	0x400
#define PRV_CLOCK_PARAMS	0x800

static int vlv2_clk_probe(struct platform_device *pdev)
{
	struct clk *clk;

	/* SCC / LPSS free running clock */
	clk = clk_register_fixed_rate(NULL, "lpss_clk", NULL, CLK_IS_ROOT,
				      100000000);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	/* Shared DMA controllers */
	clk_register_clkdev(clk, "hclk", "INTL9C60.0.auto");
	clk_register_clkdev(clk, "hclk", "INTL9C60.1.auto");

	/* SPI clock */
	clk = clk_register_fixed_rate(NULL, "spi_clk", "lpss_clk", 0, 20000000);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	clk = clk_register_lpss_gate("spi0_clk", "spi_clk", "80860F0E", NULL,
				     PRV_CLOCK_PARAMS_SPI);
	if (!IS_ERR(clk)) {
		clk_register_clkdev(clk, NULL, "80860F0E:00");
	} else {
		clk = clk_register_lpss_gate("spi0_clk", "spi_clk", "INT33B0",
					     NULL, PRV_CLOCK_PARAMS_SPI);
		if (!IS_ERR(clk))
			clk_register_clkdev(clk, NULL, "INT33B0:00");
	}

	/* UART clock */
	clk = clk_register_fixed_rate(NULL, "uart_clk", "lpss_clk", 0, 44236800);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	clk = clk_register_lpss_gate("uart0_clk", "uart_clk", "80860F0A", "1",
				     PRV_CLOCK_PARAMS);
	if (!IS_ERR(clk)) {
		clk_register_clkdev(clk, NULL, "80860F0A:00");
	} else {
		clk = clk_register_lpss_gate("uart0_clk", "uart_clk",
					     "INT33BC", "1", PRV_CLOCK_PARAMS);
		if (!IS_ERR(clk))
			clk_register_clkdev(clk, NULL, "INT33BC:00");
	}

	clk = clk_register_lpss_gate("uart1_clk", "uart_clk", "80860F0A", "2",
				     PRV_CLOCK_PARAMS);
	if (!IS_ERR(clk))  {
		clk_register_clkdev(clk, NULL, "80860F0A:01");
	} else {
		clk = clk_register_lpss_gate("uart1_clk", "uart_clk",
					     "INT33BC", "2", PRV_CLOCK_PARAMS);
		if (!IS_ERR(clk))
			clk_register_clkdev(clk, NULL, "INT33BC:01");
	}

	/* PWM clock */
	clk = clk_register_fixed_rate(NULL, "pwm_clk", "lpss_clk", 0, 25000000);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	clk_register_clkdev(clk, NULL, "80860F08:00");
	clk_register_clkdev(clk, NULL, "80860F09:00");

	/* I2C clocks */
	clk = clk_register_fixed_rate(NULL, "i2c_100_clk", "lpss_clk", 0,
				      100000000);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	if (boot_cpu_data.x86_mask >= 2) {
		static const char *i2c_parents[] = {
			"i2c_100_clk",
			"i2c_133_clk",
		};

		/*
		 * I2C clocks are fixed rate but depending on the bit 0 in
		 * the PRV_CLOCK_PARAMS register, the source clock will be
		 * 133MHz or 100MHz. This selection started from stepping 2
		 * of the CPU.
		 */
		clk = clk_register_fixed_rate(NULL, "i2c_133_clk", "lpss_clk",
					      0, 133000000);
		if (IS_ERR(clk))
			return PTR_ERR(clk);

		clk = clk_register_lpss_mux("i2c_mux", i2c_parents,
					    ARRAY_SIZE(i2c_parents), "80860F41",
					    PRV_CLOCK_PARAMS);
		if (IS_ERR(clk)) {
			clk = clk_register_lpss_mux("i2c_mux", i2c_parents,
						    ARRAY_SIZE(i2c_parents),
						    "INT33B1",
						    PRV_CLOCK_PARAMS);
			if (IS_ERR(clk))
				return PTR_ERR(clk);
		}
	}

	clk_register_clkdev(clk, NULL, "80860F41:00");
	clk_register_clkdev(clk, NULL, "80860F41:01");
	clk_register_clkdev(clk, NULL, "80860F41:02");
	clk_register_clkdev(clk, NULL, "80860F41:03");
	clk_register_clkdev(clk, NULL, "80860F41:04");
	clk_register_clkdev(clk, NULL, "80860F41:05");
	clk_register_clkdev(clk, NULL, "80860F41:06");

	clk_register_clkdev(clk, NULL, "INT33B1:00");
	clk_register_clkdev(clk, NULL, "INT33B1:01");
	clk_register_clkdev(clk, NULL, "INT33B1:02");
	clk_register_clkdev(clk, NULL, "INT33B1:03");
	clk_register_clkdev(clk, NULL, "INT33B1:04");
	clk_register_clkdev(clk, NULL, "INT33B1:05");
	clk_register_clkdev(clk, NULL, "INT33B1:06");

	return 0;
}

static struct platform_driver vlv2_clk_driver = {
	.driver = {
		.name = "clk-vlv2",
		.owner = THIS_MODULE,
	},
	.probe = vlv2_clk_probe,
};

static int __init vlv2_clk_init(void)
{
	return platform_driver_register(&vlv2_clk_driver);
}
arch_initcall(vlv2_clk_init);
