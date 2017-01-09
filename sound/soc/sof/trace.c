struct dma_trace_buffer {
	u32 size; /* total sg elems size */
	int read_offset; /* the read ptr in rcurrent */
	int write_offset; /* the write ptr in wcurrent */
	int pending_size; /* the data size pending to copy to userspace */
	struct list_head elem_list;
	struct list_head *rcurrent;
	struct list_head *wcurrent;
	u32 current_end;
};

static int sst_dma_trace_open(struct inode *inode, struct file *file)
{
	struct sst_hsw *hsw = inode->i_private;
	struct sst_hsw_ipc_debug_log_enable_req *dtrace_req =
		&hsw->dt_enable_request;
	struct sst_hsw_ipc_debug_log_reply reply;
	struct dma_trace_buffer *hbuf = &hsw->host_buffer;
	u32 header;
	int ret;

	file->private_data = hsw;

	reply.log_buffer_size = 0;
	hbuf->read_offset = hbuf->write_offset = 0;
	hbuf->pending_size = 0;
	hbuf->rcurrent = hbuf->wcurrent = hbuf->elem_list.next;

	header = IPC_GLB_TYPE(IPC_GLB_DEBUG_LOG_MESSAGE);

	dtrace_req->config[0] = IPC_DEBUG_ENABLE_LOG;
	dtrace_req->config[1] = PAGE_SIZE/2;
	ret = sst_ipc_tx_message_wait(&hsw->ipc, header, dtrace_req,
			  sizeof(*dtrace_req), &reply, sizeof(reply));
	if (ret < 0) {
		dev_err(hsw->dev, "error: stream comint failed\n");
		return ret;
	}

	return 0;
}

static int sst_dma_trace_release(struct inode *inode, struct file *file)
{
	struct sst_hsw *hsw = inode->i_private;
	struct sst_hsw_ipc_debug_log_enable_req *dtrace_req =
		&hsw->dt_enable_request;
	struct sst_hsw_ipc_debug_log_reply reply;
	u32 header;
	int ret;

	header = IPC_GLB_TYPE(IPC_GLB_DEBUG_LOG_MESSAGE);

	dtrace_req->config[0] = IPC_DEBUG_DISABLE_LOG;
	dtrace_req->config[1] = PAGE_SIZE/2;
	ret = sst_ipc_tx_message_wait(&hsw->ipc, header, dtrace_req,
			  sizeof(*dtrace_req), &reply, sizeof(reply));
	if (ret < 0) {
		dev_err(hsw->dev, "error: stream comint failed\n");
		return ret;
	}

	return 0;
}

static inline struct list_head *next_dma_trace_sg(struct sst_hsw *hsw,
						  struct list_head *list)
{
	if (list_is_last(list, &hsw->host_buffer.elem_list))
		return hsw->host_buffer.elem_list.next;
	else
		return list->next;
}

static ssize_t sst_dma_trace_read(struct file *file, char __user *buffer,
					  size_t count, loff_t *ppos)
{
	struct sst_hsw *hsw = file->private_data;
	struct dma_trace_buffer *hbuf = &hsw->host_buffer;
	struct dma_trace_sg *sg_elem;
	int sg_size, msg_size, cnt = 0, ret;

	memset(buffer, 0, count);
	while (hbuf->pending_size > 0) {
		sg_elem = list_entry(hbuf->rcurrent, struct dma_trace_sg, list);
		sg_size = sg_elem->size;
		/* the dma trace buffer is a ring sg buffer
		 * msg_size should be:
		 * 1. if rcurrent != wcurrent, msg_size is sg_size - read_offset
		 * 2. if rcurrent == wcurrent
		 *  1) if pending_size > (sg_size - read_offset),
		 *     the ring is rolled over
		 *     (sg_size - read_offset) data should be transferred
		 *  2) if pending_size <= (sg_size - read_offset),
		 *     transfer all the pending_size data
		 */
		msg_size = min(hbuf->pending_size, sg_size - hbuf->read_offset);
		ret = copy_to_user(buffer, sg_elem->buf + hbuf->read_offset,
				   msg_size);
		ret = msg_size - ret;
		cnt += ret;
		hbuf->read_offset += ret;
		if (hbuf->read_offset == sg_size) {
			hbuf->rcurrent = next_dma_trace_sg(hsw, hbuf->rcurrent);
			hbuf->read_offset = 0;
		}
		hbuf->pending_size -= ret;
	}

	msleep(100);
	return cnt + 1;
}

static const struct file_operations sst_dma_trace_fops = {
	.open = sst_dma_trace_open,
	.read = sst_dma_trace_read,
	.llseek = default_llseek,
	.release = sst_dma_trace_release,
};
