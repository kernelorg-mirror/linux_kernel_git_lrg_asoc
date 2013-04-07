#ifndef LINUX_INTEL_MID_EARLY_H
#define LINUX_INTEL_MID_EARLY_H

#include <linux/console.h>

extern struct console mid_spi_early_console;

extern void mid_spi_early_console_init(u32 spi_paddr);
extern int mid_spi_early_console_reset(void);

#endif /* LINUX_INTEL_MID_EARLY_H */
