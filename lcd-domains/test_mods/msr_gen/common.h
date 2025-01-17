#ifndef COMMON_H
#define COMMON_H

#include <liblcd/trampoline.h>
#include <libfipc.h>
#include <liblcd/boot_info.h>
#include <asm/cacheflush.h>
#include <lcd_domains/microkernel.h>
#include <liblcd/liblcd.h>

#include "glue_user.h"

#define DEFAULT_GFP_FLAGS  (GFP_KERNEL)
#define verbose_debug 1
#define glue_pack(pos, msg, ext, value) glue_pack_impl((pos), (msg), (ext), (uint64_t)(value))
#define glue_pack_shadow(pos, msg, ext, value) glue_pack_shadow_impl((pos), (msg), (ext), (value))
#define glue_unpack(pos, msg, ext, type) (type)glue_unpack_impl((pos), (msg), (ext))
#define glue_unpack_shadow(pos, msg, ext, type) ({ \
	if (verbose_debug) \
		printk("%s:%d, unpack shadow for type %s\n", __func__, __LINE__, __stringify(type)); \
	(type)glue_unpack_shadow_impl(glue_unpack(pos, msg, ext, void*)); })

#define glue_unpack_new_shadow(pos, msg, ext, type, size, flags) ({ \
	if (verbose_debug) \
		printk("%s:%d, unpack new shadow for type %s | size %llu\n", __func__, __LINE__, __stringify(type), (uint64_t) size); \
	(type)glue_unpack_new_shadow_impl(glue_unpack(pos, msg, ext, void*), size, flags); })

#define glue_unpack_bind_or_new_shadow(pos, msg, ext, type, size, flags) ({ \
	if (verbose_debug) \
		printk("%s:%d, unpack or bind new shadow for type %s | size %llu\n", __func__, __LINE__, __stringify(type), (uint64_t) size); \
	(type)glue_unpack_bind_or_new_shadow_impl(glue_unpack(pos, msg, ext, void*), size, flags); })

#ifndef LCD_ISOLATE
#define glue_unpack_rpc_ptr(pos, msg, ext, name) \
	(fptr_##name)glue_unpack_rpc_ptr_impl(glue_unpack(pos, msg, ext, void*), LCD_DUP_TRAMPOLINE(trmp_##name), LCD_TRAMPOLINE_SIZE(trmp_##name))

#else
#define glue_unpack_rpc_ptr(pos, msg, ext, name) NULL; glue_user_panic("Trampolines cannot be used on LCD side")
#endif

#define glue_peek(pos, msg, ext) glue_peek_impl(pos, msg, ext)
#define glue_call_server(pos, msg, rpc_id) \
	msg->regs[0] = *pos; *pos = 0; glue_user_call_server(msg, rpc_id);

#define glue_remove_shadow(shadow) glue_user_remove_shadow(shadow)
#define glue_call_client(pos, msg, rpc_id) \
	msg->regs[0] = *pos; *pos = 0; glue_user_call_client(msg, rpc_id);

void glue_user_init(void);
void glue_user_panic(const char* msg);
void glue_user_trace(const char* msg);
void* glue_user_map_to_shadow(const void* obj, bool fail);
const void* glue_user_map_from_shadow(const void* shadow);
void glue_user_add_shadow(const void* ptr, void* shadow);
void* glue_user_alloc(size_t size, gfp_t flags);
void glue_user_free(void* ptr);
void glue_user_call_server(struct fipc_message* msg, size_t rpc_id);
void glue_user_call_client(struct fipc_message* msg, size_t rpc_id);
void glue_user_remove_shadow(void* shadow);

static inline void* glue_unpack_rpc_ptr_impl(void* target, struct lcd_trampoline_handle* handle, size_t size)
{
	if (!target)
		glue_user_panic("Target was NULL");

	if (!handle)
		glue_user_panic("Trmp was NULL");

	set_memory_x(((unsigned long)handle) & PAGE_MASK, ALIGN(size, PAGE_SIZE) >> PAGE_SHIFT);
	handle->hidden_args = target;
	return LCD_HANDLE_TO_TRAMPOLINE(handle);
}

static inline void
glue_pack_impl(size_t* pos, struct fipc_message* msg, struct ext_registers* ext, uint64_t value)
{
	if (*pos >= 512)
		glue_user_panic("Glue message was too large");
	if (*pos < 6)
		msg->regs[(*pos)++ + 1] = value;
	else
		ext->regs[(*pos)++ + 1] = value;
}

static inline uint64_t
glue_unpack_impl(size_t* pos, const struct fipc_message* msg, const struct ext_registers* ext)
{
	if (*pos >= msg->regs[0])
		glue_user_panic("Unpacked past end of glue message");
	if (*pos < 6)
		return msg->regs[(*pos)++ + 1];
	else
		return ext->regs[(*pos)++ + 1];
}

static inline uint64_t
glue_peek_impl(size_t* pos, const struct fipc_message* msg, const struct ext_registers* ext)
{
	if (*pos >= msg->regs[0])
		glue_user_panic("Peeked past end of glue message");
	if (*pos < 5)
		return msg->regs[*pos + 2];
	else
		return ext->regs[*pos + 2];
}

static inline void* glue_unpack_new_shadow_impl(const void* ptr, size_t size, gfp_t flags)
{
	void* shadow = 0;
	if (!ptr)
		return NULL;

	shadow = glue_user_alloc(size, flags);
	glue_user_add_shadow(ptr, shadow);
	return shadow;
}

static inline void* glue_unpack_bind_or_new_shadow_impl(const void* ptr, size_t size, gfp_t flags)
{
	void* shadow = 0;
	if (!ptr)
		return NULL;

	shadow = glue_user_map_to_shadow(ptr, false);
	if (!shadow) {
		shadow = glue_user_alloc(size, flags);
		glue_user_add_shadow(ptr, shadow);
	}
	return shadow;
}

static inline void* glue_unpack_shadow_impl(const void* ptr)
{
	return ptr ? glue_user_map_to_shadow(ptr, true) : NULL;
}

static inline void glue_pack_shadow_impl(size_t* pos, struct fipc_message* msg, struct ext_registers* ext, const void* ptr)
{
	glue_pack(pos, msg, ext, ptr ? glue_user_map_from_shadow(ptr) : NULL);
}

#ifdef LCD_ISOLATE
void shared_mem_init(void);
#else
void shared_mem_init_callee(struct fipc_message *msg, struct ext_registers* ext);
#endif	/* LCD_ISOLATE */


enum RPC_ID {
	MODULE_INIT,
	MODULE_EXIT,
	RPC_ID_shared_mem_init,
	RPC_ID___class_create,
	RPC_ID___register_chrdev,
	RPC_ID___unregister_chrdev,
	RPC_ID_capable,
	RPC_ID_class_destroy,
	RPC_ID_cpu_maps_update_begin,
	RPC_ID_cpu_maps_update_done,
	RPC_ID_devnode,
	RPC_ID_device_create,
	RPC_ID_device_destroy,
	RPC_ID_rdmsr_safe_on_cpu,
	RPC_ID_rdmsr_safe_regs_on_cpu,
	RPC_ID_wrmsr_safe_on_cpu,
	RPC_ID_wrmsr_safe_regs_on_cpu,
	RPC_ID_no_seek_end_llseek,
	RPC_ID_llseek,
	RPC_ID_msr_read,
	RPC_ID_msr_write,
	RPC_ID_msr_ioctl,
	RPC_ID_msr_open,
	RPC_ID_get_jiffies,
};

int try_dispatch(enum RPC_ID id, struct fipc_message* __msg, struct ext_registers* __ext);

typedef char* (*fptr_devnode)(struct device* dev, unsigned short* mode);
typedef char* (*fptr_impl_devnode)(fptr_devnode target, struct device* dev, unsigned short* mode);

LCD_TRAMPOLINE_DATA(trmp_devnode)
char* LCD_TRAMPOLINE_LINKAGE(trmp_devnode) trmp_devnode(struct device* dev, unsigned short* mode);

typedef long long (*fptr_llseek)(struct file* file, long long offset, int whence);
typedef long long (*fptr_impl_llseek)(fptr_llseek target, struct file* file, long long offset, int whence);

LCD_TRAMPOLINE_DATA(trmp_llseek)
long long LCD_TRAMPOLINE_LINKAGE(trmp_llseek) trmp_llseek(struct file* file, long long offset, int whence);

typedef unsigned long long (*fptr_msr_read)(struct file* file, char* buf, unsigned long long count, unsigned long long* ppos);
typedef unsigned long long (*fptr_impl_msr_read)(fptr_msr_read target, struct file* file, char* buf, unsigned long long count, unsigned long long* ppos);

LCD_TRAMPOLINE_DATA(trmp_msr_read)
unsigned long long LCD_TRAMPOLINE_LINKAGE(trmp_msr_read) trmp_msr_read(struct file* file, char* buf, unsigned long long count, unsigned long long* ppos);

typedef unsigned long long (*fptr_msr_write)(struct file* file, char* buf, unsigned long long count, unsigned long long* ppos);
typedef unsigned long long (*fptr_impl_msr_write)(fptr_msr_write target, struct file* file, char* buf, unsigned long long count, unsigned long long* ppos);

LCD_TRAMPOLINE_DATA(trmp_msr_write)
unsigned long long LCD_TRAMPOLINE_LINKAGE(trmp_msr_write) trmp_msr_write(struct file* file, char* buf, unsigned long long count, unsigned long long* ppos);

typedef unsigned long long (*fptr_msr_ioctl)(struct file* file, unsigned int ioc, unsigned long long arg);
typedef unsigned long long (*fptr_impl_msr_ioctl)(fptr_msr_ioctl target, struct file* file, unsigned int ioc, unsigned long long arg);

LCD_TRAMPOLINE_DATA(trmp_msr_ioctl)
unsigned long long LCD_TRAMPOLINE_LINKAGE(trmp_msr_ioctl) trmp_msr_ioctl(struct file* file, unsigned int ioc, unsigned long long arg);

typedef int (*fptr_msr_open)(struct inode* inode, struct file* file);
typedef int (*fptr_impl_msr_open)(fptr_msr_open target, struct inode* inode, struct file* file);

LCD_TRAMPOLINE_DATA(trmp_msr_open)
int LCD_TRAMPOLINE_LINKAGE(trmp_msr_open) trmp_msr_open(struct inode* inode, struct file* file);

struct __class_create_call_ctx {
	struct module* owner;
	char const* name;
	struct lock_class_key* key;
};

struct __register_chrdev_call_ctx {
	unsigned int major;
	unsigned int baseminor;
	unsigned int count;
	char const* name;
	struct file_operations const* fops;
};

struct __unregister_chrdev_call_ctx {
	unsigned int major;
	unsigned int baseminor;
	unsigned int count;
	char const* name;
};

struct capable_call_ctx {
	int cap;
};

struct class_destroy_call_ctx {
	struct class* cls;
};

struct cpu_maps_update_begin_call_ctx {
};

struct cpu_maps_update_done_call_ctx {
};

struct devnode_call_ctx {
	struct device* dev;
	unsigned short* mode;
};

struct device_create_call_ctx {
	struct class* class;
	struct device* parent;
	unsigned int devt;
	void* drvdata;
	char const* fmt;
	unsigned int cpu;
};

struct device_destroy_call_ctx {
	struct class* class;
	unsigned int devt;
};

struct rdmsr_safe_on_cpu_call_ctx {
	unsigned int cpu;
	unsigned int msr_no;
	unsigned int* l;
	unsigned int* h;
};

struct rdmsr_safe_regs_on_cpu_call_ctx {
	unsigned int cpu;
	unsigned int* regs;
};

struct wrmsr_safe_on_cpu_call_ctx {
	unsigned int cpu;
	unsigned int msr_no;
	unsigned int l;
	unsigned int h;
};

struct wrmsr_safe_regs_on_cpu_call_ctx {
	unsigned int cpu;
	unsigned int* regs;
};

struct no_seek_end_llseek_call_ctx {
	struct file* file;
	long long offset;
	int whence;
};

struct llseek_call_ctx {
	struct file* file;
	long long offset;
	int whence;
};

struct msr_read_call_ctx {
	struct file* file;
	char* buf;
	unsigned long long count;
	unsigned long long* ppos;
};

struct msr_write_call_ctx {
	struct file* file;
	char* buf;
	unsigned long long count;
	unsigned long long* ppos;
};

struct msr_ioctl_call_ctx {
	struct file* file;
	unsigned int ioc;
	unsigned long long arg;
};

struct msr_open_call_ctx {
	struct inode* inode;
	struct file* file;
};

struct get_jiffies_call_ctx {
};

void caller_marshal_kernel____class_create__ret_class__out(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct __class_create_call_ctx const* call_ctx,
	struct class const* ptr);

void callee_unmarshal_kernel____class_create__ret_class__out(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct __class_create_call_ctx const* call_ctx,
	struct class* ptr);

void callee_marshal_kernel____class_create__ret_class__out(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct __class_create_call_ctx const* call_ctx,
	struct class const* ptr);

void caller_unmarshal_kernel____class_create__ret_class__out(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct __class_create_call_ctx const* call_ctx,
	struct class* ptr);

void caller_marshal_kernel____class_create__module__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct __class_create_call_ctx const* call_ctx,
	struct module const* ptr);

void callee_unmarshal_kernel____class_create__module__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct __class_create_call_ctx const* call_ctx,
	struct module* ptr);

void callee_marshal_kernel____class_create__module__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct __class_create_call_ctx const* call_ctx,
	struct module const* ptr);

void caller_unmarshal_kernel____class_create__module__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct __class_create_call_ctx const* call_ctx,
	struct module* ptr);

void caller_marshal_kernel____class_create__lock_class_key__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct __class_create_call_ctx const* call_ctx,
	struct lock_class_key const* ptr);

void callee_unmarshal_kernel____class_create__lock_class_key__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct __class_create_call_ctx const* call_ctx,
	struct lock_class_key* ptr);

void callee_marshal_kernel____class_create__lock_class_key__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct __class_create_call_ctx const* call_ctx,
	struct lock_class_key const* ptr);

void caller_unmarshal_kernel____class_create__lock_class_key__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct __class_create_call_ctx const* call_ctx,
	struct lock_class_key* ptr);

void caller_marshal_kernel___global_file_operations__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct file_operations const* ptr);

void callee_unmarshal_kernel___global_file_operations__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct file_operations* ptr);

void callee_marshal_kernel___global_file_operations__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct file_operations const* ptr);

void caller_unmarshal_kernel___global_file_operations__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct file_operations* ptr);

void caller_marshal_kernel__class_destroy__class__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct class_destroy_call_ctx const* call_ctx,
	struct class const* ptr);

void callee_unmarshal_kernel__class_destroy__class__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct class_destroy_call_ctx const* call_ctx,
	struct class* ptr);

void callee_marshal_kernel__class_destroy__class__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct class_destroy_call_ctx const* call_ctx,
	struct class const* ptr);

void caller_unmarshal_kernel__class_destroy__class__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct class_destroy_call_ctx const* call_ctx,
	struct class* ptr);

void caller_marshal_kernel__devnode__device__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct devnode_call_ctx const* call_ctx,
	struct device const* ptr);

void callee_unmarshal_kernel__devnode__device__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct devnode_call_ctx const* call_ctx,
	struct device* ptr);

void callee_marshal_kernel__devnode__device__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct devnode_call_ctx const* call_ctx,
	struct device const* ptr);

void caller_unmarshal_kernel__devnode__device__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct devnode_call_ctx const* call_ctx,
	struct device* ptr);

void caller_marshal_kernel__device_create__ret_device__out(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct device_create_call_ctx const* call_ctx,
	struct device const* ptr);

void callee_unmarshal_kernel__device_create__ret_device__out(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct device_create_call_ctx const* call_ctx,
	struct device* ptr);

void callee_marshal_kernel__device_create__ret_device__out(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct device_create_call_ctx const* call_ctx,
	struct device const* ptr);

void caller_unmarshal_kernel__device_create__ret_device__out(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct device_create_call_ctx const* call_ctx,
	struct device* ptr);

void caller_marshal_kernel__device_create__class__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct device_create_call_ctx const* call_ctx,
	struct class const* ptr);

void callee_unmarshal_kernel__device_create__class__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct device_create_call_ctx const* call_ctx,
	struct class* ptr);

void callee_marshal_kernel__device_create__class__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct device_create_call_ctx const* call_ctx,
	struct class const* ptr);

void caller_unmarshal_kernel__device_create__class__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct device_create_call_ctx const* call_ctx,
	struct class* ptr);

void caller_marshal_kernel__device_create__device__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct device_create_call_ctx const* call_ctx,
	struct device const* ptr);

void callee_unmarshal_kernel__device_create__device__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct device_create_call_ctx const* call_ctx,
	struct device* ptr);

void callee_marshal_kernel__device_create__device__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct device_create_call_ctx const* call_ctx,
	struct device const* ptr);

void caller_unmarshal_kernel__device_create__device__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct device_create_call_ctx const* call_ctx,
	struct device* ptr);

void caller_marshal_kernel__device_destroy__class__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct device_destroy_call_ctx const* call_ctx,
	struct class const* ptr);

void callee_unmarshal_kernel__device_destroy__class__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct device_destroy_call_ctx const* call_ctx,
	struct class* ptr);

void callee_marshal_kernel__device_destroy__class__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct device_destroy_call_ctx const* call_ctx,
	struct class const* ptr);

void caller_unmarshal_kernel__device_destroy__class__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct device_destroy_call_ctx const* call_ctx,
	struct class* ptr);

void caller_marshal_kernel__no_seek_end_llseek__file__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct no_seek_end_llseek_call_ctx const* call_ctx,
	struct file const* ptr);

void callee_unmarshal_kernel__no_seek_end_llseek__file__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct no_seek_end_llseek_call_ctx const* call_ctx,
	struct file* ptr);

void callee_marshal_kernel__no_seek_end_llseek__file__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct no_seek_end_llseek_call_ctx const* call_ctx,
	struct file const* ptr);

void caller_unmarshal_kernel__no_seek_end_llseek__file__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct no_seek_end_llseek_call_ctx const* call_ctx,
	struct file* ptr);

void caller_marshal_kernel__llseek__file__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct llseek_call_ctx const* call_ctx,
	struct file const* ptr);

void callee_unmarshal_kernel__llseek__file__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct llseek_call_ctx const* call_ctx,
	struct file* ptr);

void callee_marshal_kernel__llseek__file__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct llseek_call_ctx const* call_ctx,
	struct file const* ptr);

void caller_unmarshal_kernel__llseek__file__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct llseek_call_ctx const* call_ctx,
	struct file* ptr);

void caller_marshal_kernel__msr_read__file__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_read_call_ctx const* call_ctx,
	struct file const* ptr);

void callee_unmarshal_kernel__msr_read__file__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_read_call_ctx const* call_ctx,
	struct file* ptr);

void callee_marshal_kernel__msr_read__file__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_read_call_ctx const* call_ctx,
	struct file const* ptr);

void caller_unmarshal_kernel__msr_read__file__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_read_call_ctx const* call_ctx,
	struct file* ptr);

void caller_marshal_kernel__msr_read__file_inode__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_read_call_ctx const* call_ctx,
	struct inode const* ptr);

void callee_unmarshal_kernel__msr_read__file_inode__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_read_call_ctx const* call_ctx,
	struct inode* ptr);

void callee_marshal_kernel__msr_read__file_inode__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_read_call_ctx const* call_ctx,
	struct inode const* ptr);

void caller_unmarshal_kernel__msr_read__file_inode__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_read_call_ctx const* call_ctx,
	struct inode* ptr);

void caller_marshal_kernel__msr_write__file__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_write_call_ctx const* call_ctx,
	struct file const* ptr);

void callee_unmarshal_kernel__msr_write__file__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_write_call_ctx const* call_ctx,
	struct file* ptr);

void callee_marshal_kernel__msr_write__file__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_write_call_ctx const* call_ctx,
	struct file const* ptr);

void caller_unmarshal_kernel__msr_write__file__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_write_call_ctx const* call_ctx,
	struct file* ptr);

void caller_marshal_kernel__msr_write__file_inode__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_write_call_ctx const* call_ctx,
	struct inode const* ptr);

void callee_unmarshal_kernel__msr_write__file_inode__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_write_call_ctx const* call_ctx,
	struct inode* ptr);

void callee_marshal_kernel__msr_write__file_inode__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_write_call_ctx const* call_ctx,
	struct inode const* ptr);

void caller_unmarshal_kernel__msr_write__file_inode__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_write_call_ctx const* call_ctx,
	struct inode* ptr);

void caller_marshal_kernel__msr_ioctl__file__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_ioctl_call_ctx const* call_ctx,
	struct file const* ptr);

void callee_unmarshal_kernel__msr_ioctl__file__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_ioctl_call_ctx const* call_ctx,
	struct file* ptr);

void callee_marshal_kernel__msr_ioctl__file__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_ioctl_call_ctx const* call_ctx,
	struct file const* ptr);

void caller_unmarshal_kernel__msr_ioctl__file__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_ioctl_call_ctx const* call_ctx,
	struct file* ptr);

void caller_marshal_kernel__msr_ioctl__file_inode__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_ioctl_call_ctx const* call_ctx,
	struct inode const* ptr);

void callee_unmarshal_kernel__msr_ioctl__file_inode__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_ioctl_call_ctx const* call_ctx,
	struct inode* ptr);

void callee_marshal_kernel__msr_ioctl__file_inode__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_ioctl_call_ctx const* call_ctx,
	struct inode const* ptr);

void caller_unmarshal_kernel__msr_ioctl__file_inode__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_ioctl_call_ctx const* call_ctx,
	struct inode* ptr);

void caller_marshal_kernel__msr_open__inode__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_open_call_ctx const* call_ctx,
	struct inode const* ptr);

void callee_unmarshal_kernel__msr_open__inode__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_open_call_ctx const* call_ctx,
	struct inode* ptr);

void callee_marshal_kernel__msr_open__inode__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_open_call_ctx const* call_ctx,
	struct inode const* ptr);

void caller_unmarshal_kernel__msr_open__inode__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_open_call_ctx const* call_ctx,
	struct inode* ptr);

void caller_marshal_kernel__msr_open__file__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_open_call_ctx const* call_ctx,
	struct file const* ptr);

void callee_unmarshal_kernel__msr_open__file__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_open_call_ctx const* call_ctx,
	struct file* ptr);

void callee_marshal_kernel__msr_open__file__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_open_call_ctx const* call_ctx,
	struct file const* ptr);

void caller_unmarshal_kernel__msr_open__file__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_open_call_ctx const* call_ctx,
	struct file* ptr);

void caller_marshal_kernel__msr_open__file_inode__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_open_call_ctx const* call_ctx,
	struct inode const* ptr);

void callee_unmarshal_kernel__msr_open__file_inode__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_open_call_ctx const* call_ctx,
	struct inode* ptr);

void callee_marshal_kernel__msr_open__file_inode__in(
	size_t* __pos,
	struct fipc_message* __msg,
	struct ext_registers* __ext,
	struct msr_open_call_ctx const* call_ctx,
	struct inode const* ptr);

void caller_unmarshal_kernel__msr_open__file_inode__in(
	size_t* __pos,
	const struct fipc_message* __msg,
	const struct ext_registers* __ext,
	struct msr_open_call_ctx const* call_ctx,
	struct inode* ptr);


#endif
