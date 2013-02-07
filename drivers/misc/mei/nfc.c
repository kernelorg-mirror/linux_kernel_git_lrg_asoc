/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2012, Intel Corporation.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/mei.h>
#include <linux/mei_bus.h>

#include "mei_dev.h"
#include "client.h"
#include "nfc.h"

/** mei_nfc_dev - NFC mei device
 *
 * @cl_info: NFC info host client
 * @init_work: perform connection to the info client
 * @fw_ivn: NFC Intervace Version Number
 * @vendor_id: NFC manufacturer ID
 * @radio_type: NFC radio type
 */
struct mei_nfc_dev {
	struct mei_cl *cl_info;
	struct work_struct init_work;
	u8 fw_ivn;
	u8 vendor_id;
	u8 radio_type;
};

static struct mei_nfc_dev nfc_dev;

/* UUIDs for NFC F/W clients */
const uuid_le mei_nfc_guid = UUID_LE(0x0bb17a78, 0x2a8e, 0x4c50,
				     0x94, 0xd4, 0x50, 0x26,
				     0x67, 0x23, 0x77, 0x5c);

static const uuid_le mei_nfc_info_guid = UUID_LE(0xd2de1625, 0x382d, 0x417d,
					0x48, 0xa4, 0xef, 0xab,
					0xba, 0x8a, 0x12, 0x06);

static void mei_nfc_free(struct mei_nfc_dev *ndev)
{
	if (ndev->cl_info) {
		list_del(&ndev->cl_info->device_link);
		mei_cl_unlink(ndev->cl_info);
		kfree(ndev->cl_info);
	}
}

static int mei_nfc_if_version(struct mei_nfc_dev *ndev)
{
	struct mei_host *dev;
	struct mei_cl *cl;

	struct mei_nfc_cmd cmd;
	struct mei_nfc_reply *reply = NULL;
	struct mei_nfc_if_version *version;
	size_t if_version_length;
	int bytes_recv, ret;

	cl = ndev->cl_info;
	dev = cl->dev;

	memset(&cmd, 0, sizeof(struct mei_nfc_cmd));
	cmd.command = MEI_NFC_CMD_MAINTENANCE;
	cmd.data_size = 1;
	cmd.sub_command = MEI_NFC_SUBCMD_IF_VERSION;

	ret = __mei_send(cl, (u8 *)&cmd, sizeof(struct mei_nfc_cmd));
	if (ret < 0) {
		dev_err(&dev->pdev->dev, "Could not send IF version cmd\n");
		return ret;
	}

	/* to be sure on the stack we alloc memory */
	if_version_length = sizeof(struct mei_nfc_reply) +
		sizeof(struct mei_nfc_if_version);

	reply = kzalloc(if_version_length, GFP_KERNEL);
	if (!reply)
		return -ENOMEM;

	bytes_recv = __mei_recv(cl, (u8 *)reply, if_version_length);
	if (bytes_recv < 0 || bytes_recv < sizeof(struct mei_nfc_reply)) {
		dev_err(&dev->pdev->dev, "Could not read IF version\n");
		ret = -EIO;
		goto err;
	}

	version = (struct mei_nfc_if_version *)reply->data;

	ndev->fw_ivn = version->fw_ivn;
	ndev->vendor_id = version->vendor_id;
	ndev->radio_type = version->radio_type;

err:
	kfree(reply);
	return ret;
}

static void mei_nfc_init(struct work_struct *work)
{
	struct mei_host *dev;
	struct mei_nfc_dev *ndev;
	struct mei_cl *cl_info;
	int ret;

	ndev = container_of(work, struct mei_nfc_dev, init_work);

	cl_info = ndev->cl_info;
	dev = cl_info->dev;

	mutex_lock(&dev->device_lock);

	if (mei_cl_connect(cl_info, NULL) < 0) {
		mutex_unlock(&dev->device_lock);
		dev_err(&dev->pdev->dev,
			"Could not connect to the NFC INFO ME client");

		goto err;
	}

	mutex_unlock(&dev->device_lock);

	ret = mei_nfc_if_version(ndev);
	if (ret < 0) {
		dev_err(&dev->pdev->dev, "Could not get the NFC interfave version");

		goto err;
	}

	dev_info(&dev->pdev->dev,
		"NFC MEI VERSION: IVN 0x%x Vendor ID 0x%x Type 0x%x\n",
		ndev->fw_ivn, ndev->vendor_id, ndev->radio_type);

	return;

err:
	mei_nfc_free(ndev);

	return;
}


int mei_nfc_host_init(struct mei_host *dev)
{
	struct mei_nfc_dev *ndev = &nfc_dev;
	struct mei_cl *cl_info;
	int i, ret;

	/* already initialzed */
	if (ndev->cl_info)
		return 0;

	cl_info = mei_cl_allocate(dev);
	if (!cl_info)
		return -ENOMEM;

	/* check for valid client id */
	i = mei_me_cl_by_uuid(dev, &mei_nfc_info_guid);
	if (i < 0) {
		dev_info(&dev->pdev->dev, "nfc: failed to find the client\n");
		return -ENOENT;
	}

	cl_info->me_client_id = dev->me_clients[i].client_id;

	ret = mei_cl_link(cl_info, MEI_HOST_CLIENT_ID_ANY);
	if (ret)
		goto err;

	cl_info->device_uuid = mei_nfc_info_guid;

	list_add_tail(&cl_info->device_link, &dev->device_list);

	ndev->cl_info = cl_info;

	INIT_WORK(&ndev->init_work, mei_nfc_init);
	schedule_work(&ndev->init_work);

	return 0;

err:
	mei_nfc_free(ndev);

	return ret;
}

void mei_nfc_host_exit(void)
{
	struct mei_nfc_dev *ndev = &nfc_dev;

	mei_nfc_free(ndev);
}
