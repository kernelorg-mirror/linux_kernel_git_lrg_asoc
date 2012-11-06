/*
 * acpi/acpica_sys.c - ACPICA debugfs support
 *
 * Copyright (c) 2012, Intel Corporation
 * Author: Lv Zheng <lv.zheng@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#define pr_fmt(fmt)	"ACPI: " KBUILD_MODNAME ": " fmt

#include <linux/export.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/time.h>
#include <linux/acpi.h>
#include "internal.h"

struct acpi_chrono {
	const char *name;
	u64 ts_nsec;		/* Execution time */
	u64 clock_enter;	/* Clock of the entrance */
};

#define ACPI_CHRONO_INIT(step)	\
	[ACPI_CHRONO_##step] = { .name = __stringify(step), }

static struct acpi_chrono acpi_chronos[ACPI_MAX_CHRONOS] = {
	ACPI_CHRONO_INIT(initialize_tables),
	ACPI_CHRONO_INIT(initialize_subsystem),
	ACPI_CHRONO_INIT(load_tables),
	ACPI_CHRONO_INIT(enable_subsystem),
	ACPI_CHRONO_INIT(initialize_objects),
	ACPI_CHRONO_INIT(bus_scan),
	ACPI_CHRONO_INIT(bus_scan_fixed),
	ACPI_CHRONO_INIT(update_all_gpes),
};

/**
 * acpi_chrono_log_enter - Mark the enter point of the initialization step.
 * @step: ACPI initialization step.
 *
 * Check and record the 'enter' time for the given initialization step.
 */
void acpi_chrono_log_enter(int step)
{
	acpi_chronos[step].clock_enter = local_clock();
}
EXPORT_SYMBOL(acpi_chrono_log_enter);

/**
 * acpi_chrono_log_exit - Mark the exit point of the initialization step.
 * @step: ACPI initialization step.
 *
 * Check the 'exit' time and record the sum of the time in nano seconds for
 * the given initialization step.
 */
void acpi_chrono_log_exit(int step)
{
	acpi_chronos[step].ts_nsec +=
		local_clock() - acpi_chronos[step].clock_enter;
}
EXPORT_SYMBOL(acpi_chrono_log_exit);

static int acpica_init_time_show(struct seq_file *m, void *v)
{
	int i;

	for (i = 0; i < ACPI_MAX_CHRONOS; i++) {
		u64 ts = acpi_chronos[i].ts_nsec;
		unsigned long rem_nsec;

		rem_nsec = do_div(ts, 1000000000);
		seq_printf(m, "%5lu.%06lu acpi_%s\n",
			   (unsigned long)ts, rem_nsec / 1000,
			   acpi_chronos[i].name ? : "unknown");
	}

	return 0;
}

static int acpica_init_time_open(struct inode *inode, struct file *file)
{
	return single_open(file, acpica_init_time_show, NULL);
}

static const struct file_operations acpica_init_time_fops = {
	.open		= acpica_init_time_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int __init acpica_debugfs_init(void)
{
	struct dentry *fentry;

	fentry = debugfs_create_file("acpica_init_time", S_IRUSR,
				     acpi_debugfs_dir, NULL,
				     &acpica_init_time_fops);
	if (fentry) {
		pr_debug("ACPI boot time measurement started.\n");
		return 0;
	}

	return -EINVAL;
}
