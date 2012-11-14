/*
 * acpi_uart.c - ACPI UART enumeration support
 *
 * Copyright (c) 2013, Intel Corporation
 * Author: Lv Zheng <lv.zheng@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 */

#include <linux/init.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/acpi.h>

#include "internal.h"

struct acpi_uart_buf {
	char *buf;
	size_t size;
	int *len;
};

struct acpi_uart_attr {
	struct ktermios *termios;
	unsigned int *mctrl;
};

struct acpi_uart_enum {
	acpi_status (*procfunc)(struct acpi_device *,
				struct acpi_resource_uart_serialbus *,
				void *);
	void *procdata;
	bool handle_resources;
	acpi_status status;

	struct acpi_device *adev;
};

static int acpi_uart_enum_resources(struct acpi_resource *ares,
				    void *context)
{
	struct acpi_uart_enum *ctx = context;
	struct acpi_resource_uart_serialbus *sb;
	int ret = ctx->handle_resources ? 0 : 1;

	if (ares->type != ACPI_RESOURCE_TYPE_SERIAL_BUS)
		return ret;
	sb = &ares->data.uart_serial_bus;
	if (sb->type != ACPI_RESOURCE_SERIAL_TYPE_UART)
		return ret;

	ctx->status = ctx->procfunc(ctx->adev, sb, ctx->procdata);
	return ret;
}

static acpi_status acpi_uart_enum_devices(acpi_handle handle, u32 level,
					  void *context,
					  void **return_value)
{
	struct acpi_uart_enum *ctx = context;
	struct acpi_device *adev;
	struct list_head resource_list;

	if (acpi_bus_get_device(handle, &adev))
		return AE_OK;
	if (acpi_bus_get_status(adev) || !adev->status.present)
		return AE_OK;
	if (!ctx->procfunc)
		return AE_OK;

	/* Enumerate resources. */
	ctx->adev = adev;
	ctx->status = AE_OK;
	INIT_LIST_HEAD(&resource_list);
	acpi_dev_get_resources(adev, &resource_list,
			       acpi_uart_enum_resources, ctx);
	acpi_dev_free_resource_list(&resource_list);

	return ctx->status;
}

static int
acpi_uart_walk_port(struct device *parent,
		    bool handle_resources,
		    acpi_status (*procfunc)(struct acpi_device *,
					    struct acpi_resource_uart_serialbus *,
					    void *),
		    void *procdata)
{
	acpi_handle handle;
	acpi_status status;
	struct acpi_uart_enum ctx;

	handle = parent ? ACPI_HANDLE(parent) : NULL;
	if (!handle)
		return -ENODEV;

	ctx.handle_resources = handle_resources;
	ctx.procfunc = procfunc;
	ctx.procdata = procdata;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, 1,
				     acpi_uart_enum_devices, NULL,
				     &ctx, NULL);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	return 0;
}

static acpi_status
acpi_uart_enum_pnpids(struct acpi_device *adev,
		      struct acpi_resource_uart_serialbus *sb,
		      void *context)
{
	int len;
	struct acpi_uart_buf *ctx = context;

	len = acpi_device_create_modalias(adev, ctx->buf, ctx->size);
	*ctx->len = len;

	return len < 0 ? AE_CTRL_DEPTH : AE_OK;
}

/**
 * acpi_uart_get_peripheral_type - get peripheral HID/CIDs
 * @dev: the tty class device
 * @buf: the string buffer containing HID/CIDs
 * @size: the size of the string buffer
 *
 * Obtain UART peripheral HID/CIDs.  Return buffer size on success,
 * -errno on failure.
 */
int acpi_uart_get_peripheral_type(struct device *dev,
				  char *buf, size_t size)
{
	int ret;
	int len = 0;
	struct acpi_uart_buf ctx = { buf, size, &len };

	ret = acpi_uart_walk_port(dev->parent, false,
				  acpi_uart_enum_pnpids, &ctx);
	if (ret < 0)
		return ret;

	return len;
}
EXPORT_SYMBOL_GPL(acpi_uart_get_peripheral_type);

static acpi_status
acpi_uart_enum_termios(struct acpi_device *adev,
		       struct acpi_resource_uart_serialbus *sb,
		       void *context)
{
	struct acpi_uart_attr *ctx = context;
	struct ktermios *termios = ctx->termios;
	unsigned int mctrl;

	memset(termios, 0, sizeof(struct ktermios));
	mctrl = 0;

	/* data bits */
	switch (sb->data_bits) {
	case ACPI_UART_5_DATA_BITS:
		termios->c_cflag |= CS5;
		break;
	case ACPI_UART_6_DATA_BITS:
		termios->c_cflag |= CS6;
		break;
	case ACPI_UART_7_DATA_BITS:
		termios->c_cflag |= CS7;
		break;
	case ACPI_UART_8_DATA_BITS:
	default:
		termios->c_cflag |= CS8;
		break;
	}
	/* parity */
	if (sb->parity == ACPI_UART_PARITY_EVEN)
		termios->c_cflag |= PARENB;
	else if (sb->parity == ACPI_UART_PARITY_ODD)
		termios->c_cflag |= (PARENB | PARODD);
	/* baud rate */
	tty_termios_encode_baud_rate(termios,
				     sb->default_baud_rate,
				     sb->default_baud_rate);
	/* stop bits */
	if (sb->stop_bits == ACPI_UART_2_STOP_BITS)
		termios->c_cflag |= CSTOPB;
	/* HW control */
	if (sb->flow_control & ACPI_UART_FLOW_CONTROL_HW)
		termios->c_cflag |= CRTSCTS;
	/* SW control */
	if (sb->flow_control & ACPI_UART_FLOW_CONTROL_XON_XOFF)
		termios->c_iflag |= (IXON | IXOFF);

	/* endianess */
	if (sb->endian == ACPI_UART_LITTLE_ENDIAN)
		mctrl |= TIOCM_LE;
	/* terminal lines */
	if (sb->lines_enabled & ACPI_UART_DATA_TERMINAL_READY)
		mctrl |= TIOCM_DTR;
	if (sb->lines_enabled & ACPI_UART_REQUEST_TO_SEND)
		mctrl |= TIOCM_RTS;
	/* modem lines */
	if (sb->lines_enabled & ACPI_UART_CLEAR_TO_SEND)
		mctrl |= TIOCM_CTS;
	if (sb->lines_enabled & ACPI_UART_CARRIER_DETECT)
		mctrl |= TIOCM_CAR;
	if (sb->lines_enabled & ACPI_UART_RING_INDICATOR)
		mctrl |= TIOCM_RNG;
	if (sb->lines_enabled & ACPI_UART_DATA_SET_READY)
		mctrl |= TIOCM_DSR;
	*ctx->mctrl = mctrl;

	return AE_OK;
}

/**
 * acpi_uart_get_peripheral_attr - get peripheral attributes
 * @dev: the tty class device
 * @termios: the termios settings
 * @mctrl: the mctrl settings
 *
 * Obtain UART peripheral attributes.  Return 0 on success, -errno on
 * failure.
 */
int acpi_uart_get_peripheral_attr(struct device *dev,
				  struct ktermios *termios,
				  unsigned int *mctrl)
{
	struct acpi_uart_attr ctx = { termios, mctrl };

	return acpi_uart_walk_port(dev->parent, false,
				   acpi_uart_enum_termios, &ctx);
}
EXPORT_SYMBOL_GPL(acpi_uart_get_peripheral_attr);
