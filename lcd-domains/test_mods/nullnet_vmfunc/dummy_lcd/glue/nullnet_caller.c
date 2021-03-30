#include <lcd_config/pre_hook.h>

#include <libcap.h>
#include <liblcd/liblcd.h>
#include <liblcd/sync_ipc_poll.h>
#include <liblcd/glue_cspace.h>
#include <liblcd/skbuff.h>
#include "../../glue_helper.h"
#include "../nullnet_caller.h"

#include <linux/hashtable.h>
#include "../../rdtsc_helper.h"
#include <lcd_config/post_hook.h>

struct cptr sync_ep;
static struct glue_cspace *c_cspace;
extern struct thc_channel *net_async;
extern struct thc_channel_group ch_grp;

struct rtnl_link_ops *g_rtnl_link_ops;
void *data_pool;
uint64_t con_skb_sum = 0;
struct kmem_cache *skb_c_cache;
struct kmem_cache *skbuff_cache;
struct kmem_cache *skb2_cache;

extern bool tdiff_valid;
extern u64 tdiff_disp;
//#define LCD_MEASUREMENT
#define LCD_SKB_CONTAINER
#define NOLOOKUP
#define STATIC_SKB

/* XXX: How to determine this? */
#define CPTR_HASH_BITS      5

#if defined(CONFIG_RUN_DUMMY_2)
#define HASHING
#endif

static DEFINE_HASHTABLE(cptr_table, CPTR_HASH_BITS);

struct lcd_sk_buff_container {
	struct cptr my_ref, other_ref;
	struct sk_buff skbuff;
	uint64_t tid;
	void *chnl;
	unsigned int cookie;
};

int glue_nullnet_init(void)
{
	int ret;
	ret = glue_cap_init();
	if (ret) {
		LIBLCD_ERR("cap init");
		goto fail1;
	}
	ret = glue_cap_create(&c_cspace);
	if (ret) {
		LIBLCD_ERR("cap create");
		goto fail2;
	}
	hash_init(cptr_table);
	skb_c_cache = kmem_cache_create("skb_c_cache",
#ifdef LCD_SKB_CONTAINER
				sizeof(struct lcd_sk_buff_container),
#else
				sizeof(struct sk_buff_container),
#endif
				0,
				SLAB_HWCACHE_ALIGN|SLAB_PANIC,
				NULL);
	if (!skb_c_cache)
		printk("WARN: skb_container cache not created\n");

	skbuff_cache = kmem_cache_create("skbuff_cache",
				sizeof(struct sk_buff),
				0,
				SLAB_HWCACHE_ALIGN|SLAB_PANIC,
				NULL);
	if (!skbuff_cache)
		printk("WARN: skbuff cache not created\n");

	skb2_cache = kmem_cache_create("skb2_cache",
				sizeof(struct sk_buff_container_2),
				0,
				SLAB_HWCACHE_ALIGN|SLAB_PANIC,
				NULL);
	if (!skb2_cache) {
		LIBLCD_ERR("skb_container cache not created");
		goto fail2;
	}

	return 0;
fail2:
	glue_cap_exit();
fail1:
	return ret;

}

void glue_nullnet_exit()
{
	glue_cap_destroy(c_cspace);
	glue_cap_exit();

	if (skb2_cache)
		kmem_cache_destroy(skb2_cache);
}

int glue_insert_skbuff(struct hlist_head *htable, struct sk_buff_container_hash *skb_c)
{
        BUG_ON(!skb_c->skb);

        skb_c->my_ref = __cptr((unsigned long)skb_c->skb);

        hash_add(cptr_table, &skb_c->hentry, (unsigned long) skb_c->skb);
        return 0;
}

int glue_lookup_skbuff(struct hlist_head *htable, struct cptr c, struct sk_buff_container_hash **skb_cout)
{
        struct sk_buff_container_hash *skb_c;

        hash_for_each_possible(cptr_table, skb_c, hentry, (unsigned long) cptr_val(c)) {
		if (skb_c->skb == (struct sk_buff*) c.cptr) {
	                *skb_cout = skb_c;
		}
        }
        return 0;
}

void glue_remove_skbuff(struct sk_buff_container_hash *skb_c)
{
	hash_del(&skb_c->hentry);
}

extern cptr_t nullnet_sync_endpoint;
extern cptr_t nullnet_register_channel;

struct thc_channel_group_item *ptrs[32];

extern struct thc_channel_group ch_grp;

void rtnl_lock()
{
	struct fipc_message r;
	struct fipc_message *request = &r;

	async_msg_set_fn_type(request, RTNL_LOCK);

	vmfunc_wrapper(request);

	return;
}

void rtnl_unlock()
{
	struct fipc_message r;
	struct fipc_message *request = &r;

	async_msg_set_fn_type(request, RTNL_UNLOCK);

	vmfunc_wrapper(request);

	return;
}

int __rtnl_link_register(struct rtnl_link_ops *ops)
{
	struct rtnl_link_ops_container *ops_container;
	struct fipc_message r;
	struct fipc_message *request = &r;

	int ret;

	ops_container = container_of(ops,
			struct rtnl_link_ops_container,
			rtnl_link_ops);
	ret = glue_cap_insert_rtnl_link_ops_type(c_cspace,
			ops_container,
			&ops_container->my_ref);
	if (ret) {
		LIBLCD_ERR("lcd insert");
		goto fail1;
	}

	async_msg_set_fn_type(request, __RTNL_LINK_REGISTER);

	fipc_set_reg0(request, cptr_val(ops_container->my_ref));

	g_rtnl_link_ops = ops;

	vmfunc_wrapper(request);

	ret = fipc_get_reg0(request);

	printk("%s: Got %d\n", __func__, ret);
        if (ret < 0) {
                LIBLCD_ERR("remote register fs failed");
                goto fail2;
        }

	ops_container->other_ref.cptr = fipc_get_reg1(request);
	return ret;

fail2:
fail1:
	return ret;
}

void __rtnl_link_unregister(struct rtnl_link_ops *ops)
{
	int err;
	struct fipc_message r;
	struct fipc_message *request = &r;
	struct rtnl_link_ops_container *ops_container;

	ops_container = container_of(ops, struct rtnl_link_ops_container,
			rtnl_link_ops);
	async_msg_set_fn_type(request, __RTNL_LINK_UNREGISTER);

	fipc_set_reg0(request, ops_container->other_ref.cptr);

	err = vmfunc_wrapper(request);
	if (err) {
		LIBLCD_ERR("thc_ipc_call");
		lcd_exit(-1);
	}
	return;
}

int register_netdevice(struct net_device *dev)
{
	struct net_device_container *dev_container;
	struct net_device_ops_container *netdev_ops_container;
	struct rtnl_link_ops_container *rtnl_link_ops_container;
	int err;
	struct fipc_message r;
	struct fipc_message *request = &r;
	int ret;

	dev_container = container_of(dev, struct net_device_container, net_device);

	netdev_ops_container = container_of(dev->netdev_ops, struct
			net_device_ops_container, net_device_ops);

	rtnl_link_ops_container = container_of(dev->rtnl_link_ops, struct
			rtnl_link_ops_container, rtnl_link_ops);

	async_msg_set_fn_type(request, REGISTER_NETDEVICE);
	fipc_set_reg1(request, dev_container->other_ref.cptr);
	err = vmfunc_wrapper(request);
	if (err) {
		LIBLCD_ERR("thc_ipc_call");
		lcd_exit(-1);
	}
	ret = fipc_get_reg0(request);
	return ret;
}

void ether_setup(struct net_device *dev)
{
	int err;
	struct fipc_message r;
	struct fipc_message *request = &r;
	struct net_device_container *netdev_container;

	async_msg_set_fn_type(request, ETHER_SETUP);

	netdev_container = container_of(dev, struct net_device_container, net_device);
	fipc_set_reg1(request, netdev_container->other_ref.cptr);
	LIBLCD_MSG("ndev other ref %lu\n", netdev_container->other_ref.cptr);
	err = vmfunc_wrapper(request);
	if (err) {
		LIBLCD_ERR("thc_ipc_call");
		goto fail_ipc;
	}
fail_ipc:
	return;
}

int sync_prep_data(void *data, unsigned long *sz, unsigned long *off, cptr_t *data_cptr)
{
    int ret;
	ret = lcd_virt_to_cptr(__gva((unsigned long)data), data_cptr, sz, off);
	if (ret) {
		LIBLCD_ERR("virt to cptr failed");
	}
	return ret;
}


// DONE
#if 0
int eth_mac_addr(struct net_device *dev, void *p)
{
	struct fipc_message r;
	struct fipc_message *request = &r;
	int sync_ret;
	unsigned 	long p_mem_sz;
	unsigned 	long p_offset;
	cptr_t p_cptr;
	int ret;
	struct net_device_container *dev_container;



	dev_container = container_of(dev, struct net_device_container, net_device);

	fipc_set_reg1(request, dev_container->other_ref.cptr);

	async_msg_set_fn_type(request, ETH_MAC_ADDR);

	sync_ret = sync_prep_data(p, &p_mem_sz, &p_offset, &p_cptr);
	if (sync_ret) {
		LIBLCD_ERR("virt to cptr failed");
		lcd_exit(-1);
	}

	ret = thc_ipc_send_request(net_async, request, &request_cookie);
	if (ret) {
		LIBLCD_ERR("thc_ipc_call");
		goto fail_ipc;
	}

	lcd_set_r0(p_mem_sz);
	lcd_set_r1(p_offset);
	lcd_set_cr0(p_cptr);
	sync_ret = lcd_sync_send(nullnet_sync_endpoint);
	lcd_set_cr0(CAP_CPTR_NULL);
	if (sync_ret) {
		LIBLCD_ERR("failed to send");
		goto fail_sync;
	}
#if 0
	ret = thc_ipc_recv_response(net_async,
				request_cookie,
				&response);
	if (ret) {
		LIBLCD_ERR("async recv failed");
		goto fail_ipc_recv;
	}
#endif
	ret = fipc_get_reg1(request);

fail_sync:
//fail_ipc_recv:
fail_ipc:
	return ret;
}
#endif
int eth_validate_addr(struct net_device *dev)
{
	int ret;
	struct fipc_message r;
	struct fipc_message *request = &r;
	struct net_device_container *dev_container;

	async_msg_set_fn_type(request, ETH_VALIDATE_ADDR);
	
	dev_container = container_of(dev, struct net_device_container, net_device);

	fipc_set_reg1(request, dev_container->other_ref.cptr);

	LIBLCD_MSG("%s, cptr lcd %lu", __func__, dev_container->other_ref.cptr);

	ret = vmfunc_wrapper(request);

	if (ret) {
		LIBLCD_ERR("thc_ipc_call");
		goto fail_ipc;
	}

	ret = fipc_get_reg1(request);

fail_ipc:
	return ret;
}

void free_netdev(struct net_device *dev)
{
	int ret;
	struct fipc_message r;
	struct fipc_message *request = &r;
	struct net_device_container *dev_container;

	async_msg_set_fn_type(request, FREE_NETDEV);

	dev_container = container_of(dev, struct net_device_container, net_device);

	fipc_set_reg1(request, dev_container->other_ref.cptr);

	ret = vmfunc_wrapper(request);
	if (ret) {
		LIBLCD_ERR("thc_ipc_call");
		goto fail_ipc;
	}

fail_ipc:
	return;
}

void netif_carrier_off(struct net_device *dev)
{
	int ret;
	struct fipc_message r;
	struct fipc_message *request = &r;
	struct net_device_container *dev_container;

	dev_container = container_of(dev, struct net_device_container, net_device);

	async_msg_set_fn_type(request, NETIF_CARRIER_OFF);
	fipc_set_reg1(request, dev_container->other_ref.cptr);
	
	ret = vmfunc_wrapper(request);
	if (ret) {
		LIBLCD_ERR("thc_ipc_call");
		goto fail_ipc;
	}
fail_ipc:
	return;
}

void netif_carrier_on(struct net_device *dev)
{
	int ret;
	struct fipc_message r;
	struct fipc_message *request = &r;
	struct net_device_container *dev_container;

	dev_container = container_of(dev, struct net_device_container, net_device);

	async_msg_set_fn_type(request, NETIF_CARRIER_ON);

	fipc_set_reg1(request, dev_container->other_ref.cptr);

	ret = vmfunc_wrapper(request);
	if (ret) {
		LIBLCD_ERR("thc_ipc_call");
		goto fail_ipc;
	}

fail_ipc:
	return;
}

void rtnl_link_unregister(struct rtnl_link_ops *ops)
{
	struct rtnl_link_ops_container *ops_container;
	int err;
	struct fipc_message r;
	struct fipc_message *request = &r;

	ops_container = container_of(ops, struct rtnl_link_ops_container,
			rtnl_link_ops);

	async_msg_set_fn_type(request, RTNL_LINK_UNREGISTER);
	fipc_set_reg0(request, ops_container->other_ref.cptr);

	err = vmfunc_wrapper(request);
	if (err) {
		LIBLCD_ERR("thc_ipc_call");
		goto fail2;
	}

	glue_cap_remove(c_cspace, ops_container->my_ref);

fail2:
	return;
}

struct net_device *alloc_netdev_mqs(int sizeof_priv, const char *name, unsigned
		char name_assign_type, void (*setup)(struct net_device* dev),
		unsigned int txqs, unsigned int rxqs)
{
	struct setup_container *setup_container;
	int ret;
	int err;
	struct fipc_message r;
	struct fipc_message *request = &r;
	struct net_device_container *ret1;

	ret1 = kzalloc(sizeof( struct net_device_container   ), GFP_KERNEL);
	if (!ret1) {
		LIBLCD_ERR("kzalloc");
		goto fail_alloc;
	}
	setup_container = kzalloc(sizeof(*setup_container), GFP_KERNEL);
	if (!setup_container) {
		LIBLCD_ERR("kzalloc");
		goto fail_alloc;
	}

	setup_container->setup = setup;

	ret = glue_cap_insert_setup_type(c_cspace, setup_container,
			&setup_container->my_ref);

	if (ret) {
		LIBLCD_ERR("lcd insert");
		goto fail_insert;
	}

	ret = glue_cap_insert_net_device_type(c_cspace, ret1, &ret1->my_ref);

	if (ret) {
		LIBLCD_ERR("lcd insert");
		goto fail_insert;
	}

	async_msg_set_fn_type(request, ALLOC_NETDEV_MQS);
	fipc_set_reg1(request, sizeof_priv);
	fipc_set_reg2(request, setup_container->my_ref.cptr);
	fipc_set_reg3(request, name_assign_type);
	fipc_set_reg4(request, txqs);
	fipc_set_reg5(request, rxqs);
	fipc_set_reg6(request, ret1->my_ref.cptr);
	printk("%s, netdevice lcd cptr : %lu", __func__, ret1->my_ref.cptr);
	err = vmfunc_wrapper(request);
	if (err) {
		LIBLCD_ERR("thc_ipc_call");
		goto fail_ipc;
	}

	printk("%s: returned", __func__);
	//ret1->other_ref.cptr = fipc_get_reg5(request);


fail_ipc:
fail_alloc:
fail_insert:
	return &ret1->net_device;
}

TS_DECL(ipc_send);
TS_DECL(hlookup);

void consume_skb(struct sk_buff *skb)
{
	struct fipc_message r;
	struct fipc_message *request = &r;
	struct lcd_sk_buff_container *skb_c;

	INIT_FIPC_MSG(&r);

	skb_c = container_of(skb,
			struct lcd_sk_buff_container, skbuff);

	async_msg_set_fn_type(request, CONSUME_SKB);

	fipc_set_reg0(request, cptr_val(skb_c->other_ref));

	vmfunc_wrapper(request);
	
#ifdef CONFIG_SKB_COPY
	//lcd_consume_skb(skb);
#endif

	return;
}

// DONE
int ndo_init_callee(struct fipc_message *request)
{
	struct net_device_container *net_dev_container;
	int ret;

	ret = glue_cap_lookup_net_device_type(c_cspace,
			__cptr(fipc_get_reg1(request)), &net_dev_container);
	if (ret) {
		LIBLCD_ERR("lookup");
		goto fail_lookup;
	}

	ret =
		net_dev_container->net_device.netdev_ops->ndo_init(&net_dev_container->net_device);

	fipc_set_reg1(request, ret);

fail_lookup:
	return ret;
}

// DONE
int ndo_uninit_callee(struct fipc_message *request)
{
	int ret;

	struct net_device_container *net_dev_container;
	cptr_t netdev_ref = __cptr(fipc_get_reg1(request));



	ret = glue_cap_lookup_net_device_type(c_cspace, netdev_ref, &net_dev_container);
	if (ret) {
		LIBLCD_ERR("lookup");
		goto fail_lookup;
	}
	printk("%s called, triggering rpc\n", __func__);

	net_dev_container->net_device.netdev_ops->ndo_uninit(&net_dev_container->net_device);

fail_lookup:
	return ret;
}
extern uint64_t st_disp_loop, en_disp_loop;

extern netdev_tx_t dummy_xmit(struct sk_buff *skb, struct net_device *dev);

/* This function is used for testing bare fipc, non-async mtu sized packets */
int ndo_start_xmit_bare_callee(struct fipc_message *_request)
{
#ifdef MARSHAL
	xmit_type_t xmit_type;
	unsigned long skbh_offset, skb_end;
	__be16 proto;
	u32 len;
	cptr_t skb_ref;

	xmit_type = fipc_get_reg0(_request);

	skb_ref = __cptr(fipc_get_reg2(_request));

	skbh_offset = fipc_get_reg3(_request);

	skb_end = fipc_get_reg4(_request);
	proto = fipc_get_reg5(_request);
	len = fipc_get_reg6(_request);
#endif

#ifdef MARSHAL
	fipc_set_reg1(_request, skb_end|skbh_offset| proto | len | cptr_val(skb_ref));
#endif

	return 0;
}

int ndo_start_xmit_noawe_callee(struct fipc_message *_request)
{
	struct lcd_sk_buff_container static_skb_c;
	struct lcd_sk_buff_container *skb_c = &static_skb_c;
	struct sk_buff *skb = &skb_c->skbuff;
	int ret;
#ifdef COPY
	struct skbuff_members *skb_lcd;
#endif

	unsigned long skbh_offset, skb_end;
	__be16 proto;
	u32 len;
	cptr_t skb_ref;

	skb_ref = __cptr(fipc_get_reg2(_request));

	skbh_offset = fipc_get_reg3(_request);

	skb_end = fipc_get_reg4(_request);
	proto = fipc_get_reg5(_request);
	len = fipc_get_reg6(_request);

	skb->head = (char*)data_pool + skbh_offset;
	skb->end = skb_end;
	skb->len = len;
	skb->private = true;

#ifdef COPY
	skb_lcd = SKB_LCD_MEMBERS(skb);

	P(len);
	P(data_len);
	P(queue_mapping);
	P(xmit_more);
	P(tail);
	P(truesize);
	P(ip_summed);
	P(csum_start);
	P(network_header);
	P(csum_offset);
	P(transport_header);

	skb->data = skb->head + skb_lcd->head_data_off;
#endif

	ret = dummy_xmit(skb, NULL);

	fipc_set_reg1(_request, ret);
	return ret;
}

char data[20][1514] = {0};

#ifdef CONFIG_SKB_COPY
int ndo_start_xmit_async_bare_callee(struct fipc_message *_request)
{
	//struct sk_buff *skb;
	//struct sk_buff_container *skb_c = NULL;

	struct lcd_sk_buff_container static_skb_c;
	struct lcd_sk_buff_container *skb_c = &static_skb_c;
	struct sk_buff *skb = &skb_c->skbuff;

	cptr_t skb_ref;
	unsigned long skb_end;
	__be16 proto;
	struct ext_registers *ext_regs = get_register_page(smp_processor_id());
	u64 *regs = &ext_regs->regs[0];
	u32 skb_data_off;
	int i = 0, len;
	netdev_tx_t ret = NETDEV_TX_OK;
	//struct skb_shared_info *skb_sh_b4, *skb_sh_af;

#ifdef LCD_MEASUREMENT
	TS_DECL(xmit);
#endif

#ifdef HASHING
	skb_ref = __cptr(fipc_get_reg2(_request));
	//printk("%s, skb_ref %lx", __func__, skb_ref.cptr);
#else
	skb_ref = __cptr(fipc_get_reg0(_request));
#endif
	len = fipc_get_reg3(_request);
	skb_end = fipc_get_reg4(_request);
	proto = fipc_get_reg5(_request);

	//printk("%s, allocating skb with len %d", __func__, len);

#if 0
	skb = __alloc_skb(len, GFP_KERNEL, 0, 0);

	if (!skb) {
		LIBLCD_MSG("couldn't allocate skb");
		goto fail_alloc;
	}

	if (skb_shinfo(skb)->nr_frags)
		printk("%s, nr_frags %d", __func__, skb_shinfo(skb)->nr_frags);

	skb_sh_b4 = skb_shinfo(skb);
	//printf("%s, before skb_shinfo(skb) %p", skb_shinfo(skb));
	//
	skb_c = container_of(skb, struct sk_buff_container, skb);

	//printk("%s, lcd: %lu got %p from kmem_cache", __func__, smp_processor_id(), skb_c);
	skb = &skb_c->skb;
#endif
	skb->len = len;
	//skb->end = skb_end;

	skb->protocol = proto;

	GET_EREG(data_len);
	GET_EREG(queue_mapping);
	GET_EREG(xmit_more);
	GET_EREG(tail);
	GET_EREG(truesize);
	GET_EREG(ip_summed);
	GET_EREG(csum_start);
	GET_EREG(network_header);
	GET_EREG(csum_offset);
	GET_EREG(transport_header);

	skb_data_off = regs[i++];

#if 0
	skb_sh_af = skb_shinfo(skb);

	if (skb_sh_b4 != skb_sh_af) {
		printk("%s, b4 %p | af %p", __func__, skb_sh_b4, skb_sh_af);
	}
       	if (skb_shinfo(skb) && skb_shinfo(skb)->nr_frags) {
		skb_frag_t *frag = skb_shinfo(skb)->frags;
		int j;
		for (j = 0; j < skb_shinfo(skb)->nr_frags; j++) {
			printk("frag [%d] page %p, offset %u size %u", j,
						frag->page.p, frag->page_offset, frag->size); 
		}
	}
#endif
	skb->head = skb->data = data[smp_processor_id()];
	memcpy(skb->head, &regs[i], skb->len - skb->data_len + skb_data_off);

	if (0)
	LIBLCD_MSG("lcd-> l: %d | dlen %d | qm %d"
		   " | xm %d | t %lu |ts %u",
		skb->len, skb->data_len, skb->queue_mapping,
		skb->xmit_more, skb->tail, skb->truesize);

	skb->data = skb->head + skb_data_off;

	skb_c->other_ref = skb_ref;

#ifdef LCD_MEASUREMENT
	TS_START_LCD(xmit);
#endif
	ret = dummy_xmit(skb, NULL);

	fipc_set_reg0(_request, ret);
#ifdef LCD_MEASUREMENT
	TS_STOP_LCD(xmit);
#endif

#ifdef LCD_MEASUREMENT
	fipc_set_reg2(_request,
			TS_DIFF(xmit));
#endif

//fail_alloc:
	return ret;
}
#else		/* CONFIG_SKB_COPY */
/* xmit_callee for async. This function receives the IPC and
 * sends back a response
 */
int ndo_start_xmit_async_bare_callee(struct fipc_message *_request)
{
	struct lcd_sk_buff_container static_skb_c;
	struct lcd_sk_buff_container *skb_c = &static_skb_c;
	struct sk_buff *skb = &skb_c->skbuff;
#ifdef COPY
	struct skbuff_members *skb_lcd;
#endif
	unsigned long skbh_offset, skb_end;
	__be16 proto;
	u32 len;
	cptr_t skb_ref;

	skb_ref = __cptr(fipc_get_reg2(_request));

	skbh_offset = fipc_get_reg3(_request);

	skb_end = fipc_get_reg4(_request);
	proto = fipc_get_reg5(_request);
	len = fipc_get_reg6(_request);

	skb->head = (char*)data_pool + skbh_offset;
	skb->end = skb_end;
	skb->len = len;
	skb->private = true;

#ifdef COPY
	skb_lcd = SKB_LCD_MEMBERS(skb);

	P(len);
	P(data_len);
	P(queue_mapping);
	P(xmit_more);
	P(tail);
	P(truesize);
	P(ip_summed);
	P(csum_start);
	P(network_header);
	P(csum_offset);
	P(transport_header);

	skb->data = skb->head + skb_lcd->head_data_off;
#endif

	skb_c->other_ref = skb_ref;
	dummy_xmit(skb, NULL);

	return 0;
}
#endif		/* CONFIG_SKB_COPY */

int ndo_validate_addr_callee(struct fipc_message *request)
{
	struct net_device_container *net_dev_container;
	int ret;
	cptr_t netdev_ref = __cptr(fipc_get_reg1(request));

	ret = glue_cap_lookup_net_device_type(c_cspace, netdev_ref, &net_dev_container);
	if (ret) {
		LIBLCD_ERR("lookup");
		goto fail_lookup;
	}

	LIBLCD_MSG("%s, cptr lcd %lu", __func__, netdev_ref);
	LIBLCD_MSG("%s, looked up cptr lcd %p |  %lu", __func__, net_dev_container, net_dev_container->other_ref.cptr);

	ret = net_dev_container->net_device.netdev_ops->ndo_validate_addr(&net_dev_container->net_device);

	fipc_set_reg1(request, ret);

fail_lookup:
	return ret;
}

int ndo_set_rx_mode_callee(struct fipc_message *request)
{
	int ret;
	struct net_device_container *net_dev_container;

	ret = glue_cap_lookup_net_device_type(c_cspace, __cptr(fipc_get_reg1(request)), &net_dev_container);
	if (ret) {
		LIBLCD_ERR("lookup");
		goto fail_lookup;
	}

	net_dev_container->net_device.netdev_ops->ndo_set_rx_mode(&net_dev_container->net_device);

fail_lookup:
	return ret;
}

int ndo_set_mac_address_callee(struct fipc_message *request)
{
	struct net_device_container *net_dev_container;

	int ret;
	int sync_ret;
	unsigned 	long mem_order;
	unsigned 	long addr_offset;
	cptr_t addr_cptr;
	gva_t addr_gva;


	ret = glue_cap_lookup_net_device_type(c_cspace, __cptr(fipc_get_reg1(request)), &net_dev_container);
	if (ret) {
		LIBLCD_ERR("lookup");
		goto fail_lookup;
	}

	sync_ret = lcd_cptr_alloc(&addr_cptr);
	if (sync_ret) {
		LIBLCD_ERR("failed to get cptr");
		goto fail_sync;
	}


	fipc_set_reg0(request, cptr_val(addr_cptr));

	vmfunc_sync_call(request, SYNC_NDO_SET_MAC_ADDRESS);

	mem_order = fipc_get_reg0(request);
	addr_offset = fipc_get_reg1(request);

	LIBLCD_MSG("%s: cptr %lu | order %lu | offset %lu",
		__func__, addr_cptr.cptr, mem_order, addr_offset);

	sync_ret = lcd_map_virt(addr_cptr, mem_order, &addr_gva);
	if (sync_ret) {
		LIBLCD_ERR("failed to map void *addr");
		goto fail_sync;
	}

	ret = net_dev_container->net_device.netdev_ops->ndo_set_mac_address(&net_dev_container->net_device, (void*)(gva_val(addr_gva) + addr_offset));

	fipc_set_reg1(request, ret);
fail_lookup:
fail_sync:
	return 0;
}

int ndo_get_stats64_callee(struct fipc_message *request)
{

	struct rtnl_link_stats64 stats;
	int ret;
	struct net_device_container *net_dev_container;
	cptr_t netdev_ref = __cptr(fipc_get_reg1(request));



	ret = glue_cap_lookup_net_device_type(c_cspace, netdev_ref,
			&net_dev_container);
	if (ret) {
		LIBLCD_ERR("lookup");
		goto fail_lookup;
	}

	net_dev_container->net_device.
		netdev_ops->ndo_get_stats64(
			&net_dev_container->net_device,
			&stats);


	fipc_set_reg1(request, stats.tx_packets);
	fipc_set_reg2(request, stats.tx_bytes);

fail_lookup:
	return ret;
}

int ndo_change_carrier_callee(struct fipc_message *request)
{

	int ret;
	struct net_device_container *net_dev_container;
	bool new_carrier = fipc_get_reg2(request);
	
	ret = glue_cap_lookup_net_device_type(c_cspace, __cptr(fipc_get_reg1(request)), &net_dev_container);
	if (ret) {
		LIBLCD_ERR("lookup");
		goto fail_lookup;
	}



	ret = net_dev_container->net_device.netdev_ops->ndo_change_carrier(&net_dev_container->net_device, new_carrier);

	fipc_set_reg1(request, ret);

fail_lookup:
	return ret;
}

int setup_callee(struct fipc_message *request)
{
	int ret;
	struct setup_container *setup_container;
	struct net_device_container *net_dev_container;
	struct net_device_ops_container *netdev_ops_container;
	const struct net_device_ops *netdev_ops;
	cptr_t netdev_ref = __cptr(fipc_get_reg1(request));
	cptr_t netdev_ops_ref = __cptr(fipc_get_reg4(request));
	cptr_t netdev_other_ref = __cptr(fipc_get_reg3(request));
	cptr_t pool_cptr;
	gva_t pool_addr;
	unsigned int pool_ord;

	ret = glue_cap_lookup_net_device_type(c_cspace, netdev_ref,
			&net_dev_container);
	if (ret) {
		LIBLCD_ERR("lookup");
		goto fail_lookup;
	}

	/* save other ref cptr */
	net_dev_container->other_ref = netdev_other_ref;

	ret = glue_cap_lookup_setup_type(c_cspace, __cptr(fipc_get_reg2(request)),
			&setup_container);
	if (ret) {
		LIBLCD_ERR("lookup");
		goto fail_lookup;
	}

	LIBLCD_MSG("%s, lcd other ref %p | %lu", __func__, net_dev_container,
			net_dev_container->other_ref.cptr);

	/* receive shared data pool */
	ret = lcd_cptr_alloc(&pool_cptr);
	if (ret) {
		LIBLCD_ERR("failed to get cptr");
		goto fail_cptr;
	}

	fipc_set_reg0(request, cptr_val(pool_cptr));
	vmfunc_sync_call(request, SYNC_SETUP);
	pool_ord = fipc_get_reg0(request);

	ret = lcd_map_virt(pool_cptr, pool_ord, &pool_addr);
	if (ret) {
		LIBLCD_ERR("failed to map pool");
		goto fail_pool;
	}

	LIBLCD_MSG("%s, mapping private pool %p | ord %d", __func__,
			gva_val(pool_addr), pool_ord);

	data_pool = (void*)gva_val(pool_addr);

	setup_container->setup(&net_dev_container->net_device);

	printk("%s, returned",__func__);

	netdev_ops = net_dev_container->net_device.netdev_ops;

	netdev_ops_container = container_of(netdev_ops, struct
			net_device_ops_container, net_device_ops);

	netdev_ops_container->other_ref = netdev_ops_ref;

	ret = glue_cap_insert_net_device_ops_type(c_cspace,
			netdev_ops_container, &netdev_ops_container->my_ref);
	if (ret) {
		LIBLCD_ERR("lcd insert");
		goto fail_insert;
	}

	fipc_set_reg1(request, net_dev_container->net_device.flags);
	fipc_set_reg2(request, net_dev_container->net_device.priv_flags);
	fipc_set_reg3(request, net_dev_container->net_device.features);
	fipc_set_reg4(request, net_dev_container->net_device.hw_features);
	fipc_set_reg5(request, net_dev_container->net_device.hw_enc_features);
	fipc_set_reg6(request, netdev_ops_container->my_ref.cptr);

fail_lookup:
fail_insert:
fail_pool:
fail_cptr:
	return ret;
}

// TODO:
int validate_callee(struct fipc_message *request)
{
	struct nlattr **tb;
	struct nlattr **data;

	int ret = 0;

	tb = kzalloc(sizeof( void  * ), GFP_KERNEL);
	if (!tb) {
		LIBLCD_ERR("kzalloc");
		lcd_exit(-1);
	}
	( *tb ) = kzalloc(sizeof( ( **tb ) ), GFP_KERNEL);
	if (!( *tb )) {
		LIBLCD_ERR("kzalloc");
		lcd_exit(-1);
	}
	data = kzalloc(sizeof( void  * ), GFP_KERNEL);
	if (!data) {
		LIBLCD_ERR("kzalloc");
		lcd_exit(-1);
	}
	( *data ) = kzalloc(sizeof( ( *data ) ), GFP_KERNEL);
	if (!( *data )) {
		LIBLCD_ERR("kzalloc");
		lcd_exit(-1);
	}
//	ret = validate(( *tb ), ( *data ));
	fipc_set_reg1(request, ret);
	return ret;
}
