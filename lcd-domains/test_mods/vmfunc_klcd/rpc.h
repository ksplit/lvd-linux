/*
 * rpc.h
 *
 * Internal defs
 *
 * Copyright: University of Utah
 */
#ifndef LCD_ASYNC_RPC_TEST_H
#define LCD_ASYNC_RPC_TEST_H

#include <libfipc.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <asm/lcd_domains/libvmfunc.h>
#include <linux/acpi.h>

#define TRANSACTIONS 1000000
#define NUM_ITERATIONS	TRANSACTIONS

#define INIT_FIPC_MSG(msg)             memset(msg, 0x0, sizeof(*msg))

#define verbose_debug 1
enum fn_type {
	MODULE_INIT,
	NULL_INVOCATION, 
	CALLEE,
	FOO,
	BAR,
	MARSHAL_ONE,
	MEMTEST,
	IRQ_HANDLER,
	DUMMY,
	REGISTER_WIDE_HW_BREAKPOINT,
	UNREGISTER_WIDE_HW_BREAKPOINT,
  MARSHAL_NONE,
	MARSHAL_INT,
	MARSHAL_ARRAY,
	MARSHAL_STRING,
	MARSHAL_VOIDP,
	MARSHAL_UNION,
	RPC_ID_get_jiffies,
};
int callee(struct fipc_message *);

static inline void
async_msg_set_fn_type(struct fipc_message *msg, int type)
{
	msg->vmfunc_id = VMFUNC_RPC_CALL;
	msg->rpc_id = type;
}

struct get_jiffies_call_ctx {
};
#endif /* LCD_ASYNC_RPC_TEST_H */
