/* Mixer Controls */
int sst_hsw_stream_get_volume(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 stage_id, u32 channel, u32 *volume)
{
	if (channel > 1)
		return -EINVAL;

	sst_dsp_read(hsw->dsp, volume,
		stream->reply.volume_register_address[channel],
		sizeof(*volume));

	return 0;
}

/* stream volume */
int sst_hsw_stream_set_volume(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, u32 stage_id, u32 channel, u32 volume)
{
	struct sst_hsw_ipc_volume_req *req;
	u32 header;
	int ret;

	trace_ipc_request("set stream volume", stream->reply.stream_hw_id);

	if (channel >= 2 && channel != SST_HSW_CHANNELS_ALL)
		return -EINVAL;

	header = IPC_GLB_TYPE(IPC_GLB_STREAM_MESSAGE) |
		IPC_STR_TYPE(IPC_STR_STAGE_MESSAGE);
	header |= (stream->reply.stream_hw_id << IPC_STR_ID_SHIFT);
	header |= (IPC_STG_SET_VOLUME << IPC_STG_TYPE_SHIFT);
	header |= (stage_id << IPC_STG_ID_SHIFT);

	req = &stream->vol_req;
	req->target_volume = volume;

	/* set both at same time ? */
	if (channel == SST_HSW_CHANNELS_ALL) {
		if (hsw->mute[0] && hsw->mute[1]) {
			hsw->mute_volume[0] = hsw->mute_volume[1] = volume;
			return 0;
		} else if (hsw->mute[0])
			req->channel = 1;
		else if (hsw->mute[1])
			req->channel = 0;
		else
			req->channel = SST_HSW_CHANNELS_ALL;
	} else {
		/* set only 1 channel */
		if (hsw->mute[channel]) {
			hsw->mute_volume[channel] = volume;
			return 0;
		}
		req->channel = channel;
	}

	ret = sst_ipc_tx_message_wait(&hsw->ipc, header, req,
		sizeof(*req), NULL, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "error: set stream volume failed\n");
		return ret;
	}

	return 0;
}


