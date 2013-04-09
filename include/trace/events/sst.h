#undef TRACE_SYSTEM
#define TRACE_SYSTEM sst

#if !defined(_TRACE_SST_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SST_H

#include <linux/types.h>
#include <linux/ktime.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(sst_ipc_msg,

	TP_PROTO(unsigned int val),

	TP_ARGS(val),

	TP_STRUCT__entry(
		__field(	unsigned int,	val		)
	),

	TP_fast_assign(
		__entry->val = val;
	),

	TP_printk("0x%8.8x", (unsigned int)__entry->val)
);

DEFINE_EVENT(sst_ipc_msg, sst_ipc_msg_tx,

	TP_PROTO(unsigned int val),

	TP_ARGS(val)

);

DEFINE_EVENT(sst_ipc_msg, sst_ipc_msg_rx,

	TP_PROTO(unsigned int val),

	TP_ARGS(val)

);

DECLARE_EVENT_CLASS(sst_ipc_mailbox,

	TP_PROTO(unsigned int offset, unsigned int val),

	TP_ARGS(offset, val),

	TP_STRUCT__entry(
		__field(	unsigned int,	offset		)
		__field(	unsigned int,	val		)
	),

	TP_fast_assign(
		__entry->offset = offset;
		__entry->val = val;
	),

	TP_printk(" 0x%4.4x = 0x%8.8x",
		(unsigned int)__entry->offset, (unsigned int)__entry->val)
);

DEFINE_EVENT(sst_ipc_mailbox, sst_ipc_inbox_rdata,

	TP_PROTO(unsigned int offset, unsigned int val),

	TP_ARGS(offset, val)

);

DEFINE_EVENT(sst_ipc_mailbox, sst_ipc_inbox_wdata,

	TP_PROTO(unsigned int offset, unsigned int val),

	TP_ARGS(offset, val)

);

DEFINE_EVENT(sst_ipc_mailbox, sst_ipc_outbox_rdata,

	TP_PROTO(unsigned int offset, unsigned int val),

	TP_ARGS(offset, val)

);

DEFINE_EVENT(sst_ipc_mailbox, sst_ipc_outbox_wdata,

	TP_PROTO(unsigned int offset, unsigned int val),

	TP_ARGS(offset, val)

);

DECLARE_EVENT_CLASS(sst_ipc_mailbox_info,

	TP_PROTO(unsigned int size),

	TP_ARGS(size),

	TP_STRUCT__entry(
		__field(	unsigned int,	size		)
	),

	TP_fast_assign(
		__entry->size = size;
	),

	TP_printk("Mailbox bytes 0x%8.8x", (unsigned int)__entry->size)
);

DEFINE_EVENT(sst_ipc_mailbox_info, sst_ipc_inbox_read,

	TP_PROTO(unsigned int size),

	TP_ARGS(size)

);

DEFINE_EVENT(sst_ipc_mailbox_info, sst_ipc_inbox_write,

	TP_PROTO(unsigned int size),

	TP_ARGS(size)

);

DEFINE_EVENT(sst_ipc_mailbox_info, sst_ipc_outbox_read,

	TP_PROTO(unsigned int size),

	TP_ARGS(size)

);

DEFINE_EVENT(sst_ipc_mailbox_info, sst_ipc_outbox_write,

	TP_PROTO(unsigned int size),

	TP_ARGS(size)

);

DECLARE_EVENT_CLASS(sst_irq,

	TP_PROTO(uint32_t status, uint32_t mask),

	TP_ARGS(status, mask),

	TP_STRUCT__entry(
		__field(	unsigned int,	status		)
		__field(	unsigned int,	mask		)
	),

	TP_fast_assign(
		__entry->status = status;
		__entry->mask = mask;
	),

	TP_printk("status 0x%8.8x mask 0x%8.8x",
		(unsigned int)__entry->status, (unsigned int)__entry->mask)
);

DEFINE_EVENT(sst_irq, sst_irq_busy,

	TP_PROTO(unsigned int status, unsigned int mask),

	TP_ARGS(status, mask)

);

DEFINE_EVENT(sst_irq, sst_irq_done,

	TP_PROTO(unsigned int status, unsigned int mask),

	TP_ARGS(status, mask)

);

#endif /* _TRACE_SST_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
