struct sst_dfsentry {
	struct dentry *dfsentry;
	size_t size;
	void *buf;
	struct sst_dsp *sst;
};

static int sst_dfsentry_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return 0;
}

static ssize_t sst_dfsentry_read(struct file *file, char __user *buffer,
				 size_t count, loff_t *ppos)
{
	struct sst_dfsentry *dfse = file->private_data;
	int size;
	u32 *buf;
	loff_t pos = *ppos;
	size_t ret;

	//dev_dbg(dfse->sst->dev, "pbuf: %p, *ppos: 0x%llx\n", buffer, *ppos);

	size = dfse->size;

	if (pos < 0)
		return -EINVAL;
	if (pos >= size || !count)
		return 0;
	if (count > size - pos)
		count = size - pos;

	size = (count + 3) & (~3);
	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pm_runtime_get(dfse->sst->dev);
	sst_memcpy_fromio_32(dfse->sst, buf, dfse->buf + pos, size);
	pm_runtime_put(dfse->sst->dev);

	ret = copy_to_user(buffer, buf, count);
	kfree(buf);

	if (ret == count)
		return -EFAULT;
	count -= ret;
	*ppos = pos + count;

	//dev_dbg(dfse->sst->dev, "*ppos: 0x%llx, count: %zu\n", *ppos, count);

	return count;
}

static const struct file_operations sst_dfs_fops = {
	.open = sst_dfsentry_open,
	.read = sst_dfsentry_read,
	.llseek = default_llseek,
};



static int hsw_debugfs_entry_create(struct sst_dsp *sst, void __iomem *base,
	size_t size, const char *name)
{
	struct sst_dfsentry *dfse;

	if (!sst)
		return -EINVAL;

	dfse = kzalloc(sizeof(*dfse), GFP_KERNEL);

	if (!dfse)
		return -ENOMEM;
	dfse->buf = base;
	dfse->size = size;//mbox size

	dfse->dfsentry = debugfs_create_file(name, 0444, sst->debugfs_root,
					     dfse, &sst_dfs_fops);
	if (!dfse->dfsentry) {
		dev_err(sst->dev, "cannot create debugfs entry.\n");
		kfree(dfse);
		return -ENODEV;
	}

	dfse->sst = sst;

	return 0;
}

struct sst_debugfs_map {
	const char *name;
	u32 offset;
	u32 size;
};

static int hsw_debugfs_init(struct sst_hsw *hsw)
{
	struct sst_dsp *sst = hsw->dsp;
	struct sst_pdata *pdata = sst->pdata;
	int err = 0, i;
	struct dentry *d;

	for (i = 0; i < ARRAY_SIZE(debugfs_byt); i++) {
		err = hsw_debugfs_entry_create(sst,
			(void __iomem *)((char *)sst->addr.lpe + debugfs_byt[i].offset),
			debugfs_byt[i].size, debugfs_byt[i].name);
		if (err < 0)
			dev_err(sst->dev, "cannot create debugfs for %s\n", debugfs_byt[i].name);
	}

	INIT_LIST_HEAD(&hsw->host_buffer.elem_list);
	/* 1. alloc host buffer */
	err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, pdata->dma_dev,
				  PAGE_SIZE, &hsw->trace_dma_descriptor);
	if (err < 0)
		return err;

	err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV_SG, pdata->dma_dev,
				  PAGE_SIZE * DMA_TRACE_PAGE_NUMBER,
				  &hsw->dtrace_buffer);
	if (err < 0) {
		snd_dma_free_pages(&hsw->trace_dma_descriptor);
		return err;
	}

	err = hsw_setup_dma_trace_page_table(hsw);
	if (err < 0) {
		snd_dma_free_pages(&hsw->trace_dma_descriptor);
		snd_dma_free_pages(&hsw->dtrace_buffer);
		return err;
	}

	hsw->dt_enable_request.ringinfo.ring_pt_address =
		hsw->trace_dma_descriptor.addr;
	hsw->dt_enable_request.ringinfo.num_pages = DMA_TRACE_PAGE_NUMBER;
	hsw->dt_enable_request.ringinfo.ring_offset = 0;

	/* 2. init debugfs */
	d = debugfs_create_file("dma_trace", 0444, sst->debugfs_root,
					hsw, &sst_dma_trace_fops);
	if (!d) {
		snd_dma_free_pages(&hsw->trace_dma_descriptor);
		snd_dma_free_pages(&hsw->dtrace_buffer);
		return -ENODEV;
	}

	return err;
}
