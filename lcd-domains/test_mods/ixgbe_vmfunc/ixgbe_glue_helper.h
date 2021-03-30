#ifndef __IXGBE_GLUE_HELPER_H__
#define __IXGBE_GLUE_HELPER_H__

#include <liblcd/skbuff.h>

struct device_container {
	struct device device;
	struct device* dev;
	struct cptr other_ref;
	struct cptr my_ref;
	struct hlist_node hentry;
};
struct module_container {
	struct module module;
	struct cptr other_ref;
	struct cptr my_ref;
};
struct net_device_container {
	struct net_device net_device;
	struct cptr other_ref;
	struct cptr my_ref;
};
struct net_device_ops_container {
	struct net_device_ops net_device_ops;
	struct cptr other_ref;
	struct cptr my_ref;
};
struct ethtool_ops_container {
	struct ethtool_ops ethtool_ops;
	struct cptr other_ref;
	struct cptr my_ref;
};
struct pci_bus_container {
	struct pci_bus pci_bus;
	struct cptr other_ref;
	struct cptr my_ref;
};
struct pci_dev_container {
	struct pci_dev pci_dev;
	struct pci_dev *pdev;
	struct cptr other_ref;
	struct cptr my_ref;
	struct hlist_node hentry;
};
struct pci_device_id_container {
	struct pci_device_id pci_device_id;
	struct cptr other_ref;
	struct cptr my_ref;
};
struct pci_driver_container {
	struct pci_driver pci_driver;
	struct cptr other_ref;
	struct cptr my_ref;
};
struct rtnl_link_stats64_container {
	struct rtnl_link_stats64 rtnl_link_stats64;
	struct cptr other_ref;
	struct cptr my_ref;
};

struct sk_buff_container_2 {
	/* just store the pointer */
	struct sk_buff skb;
	/* for hashtable insertion */
	struct cptr other_ref;
	struct cptr my_ref;
};

struct sk_buff_container_hash {
	/* just store the pointer */
	struct sk_buff *skb;
	/* store the order when volunteered. comes handy during unmap */
	unsigned int skb_ord, skbd_ord;
	cptr_t skb_cptr, skbh_cptr;
	/*
	 * as head, data pointer is different in LCD and KLCD, store it
	 * while crossing the boundary
	 */
	unsigned char *head, *data;
	/* for hashtable insertion */
	struct hlist_node hentry;
	struct cptr other_ref;
	struct cptr my_ref;
	struct task_struct *tsk;
	void *channel;
};

struct trampoline_hidden_args {
	void *struct_container;
	struct glue_cspace *cspace;
	struct lcd_trampoline_handle *t_handle;
	struct thc_channel *async_chnl;
	struct cptr sync_ep;
};
struct sync_container {
	int ( *sync )(struct net_device *, const unsigned char*);
	cptr_t my_ref;
	cptr_t other_ref;
};

struct unsync_container {
	int ( *unsync )(struct net_device *, const unsigned char*);
	cptr_t my_ref;
	cptr_t other_ref;
};

struct poll_container {
	int ( *poll )(struct napi_struct *, int);
	cptr_t my_ref;
	cptr_t other_ref;
};

struct irqhandler_t_container {
	irqreturn_t (*irqhandler)(int, void *);
	void *data;
	int napi_idx;
	cptr_t my_ref;
	cptr_t other_ref;
};

struct napi_struct_container {
	struct napi_struct *napi;
	struct napi_struct napi_struct;
	struct cptr other_ref;
	struct cptr my_ref;
	/* for hashtable insertion */
	struct hlist_node hentry;
};

int glue_cap_insert_device_type(struct glue_cspace *cspace,
		struct device_container *device_container,
		struct cptr *c_out);
int glue_cap_insert_module_type(struct glue_cspace *cspace,
		struct module_container *module_container,
		struct cptr *c_out);
int glue_cap_insert_net_device_type(struct glue_cspace *cspace,
		struct net_device_container *net_device_container,
		struct cptr *c_out);
int glue_cap_insert_net_device_ops_type(struct glue_cspace *cspace,
		struct net_device_ops_container *net_device_ops_container,
		struct cptr *c_out);
int glue_cap_insert_ethtool_ops_type(struct glue_cspace *cspace,
		struct ethtool_ops_container *ethtool_ops_container,
		struct cptr *c_out);
int glue_cap_insert_pci_bus_type(struct glue_cspace *cspace,
		struct pci_bus_container *pci_bus_container,
		struct cptr *c_out);
int glue_cap_insert_pci_dev_type(struct glue_cspace *cspace,
		struct pci_dev_container *pci_dev_container,
		struct cptr *c_out);
int glue_cap_insert_pci_device_id_type(struct glue_cspace *cspace,
		struct pci_device_id_container *pci_device_id_container,
		struct cptr *c_out);
int glue_cap_insert_pci_driver_type(struct glue_cspace *cspace,
		struct pci_driver_container *pci_driver_container,
		struct cptr *c_out);
int glue_cap_insert_rtnl_link_stats64_type(struct glue_cspace *cspace,
		struct rtnl_link_stats64_container *rtnl_link_stats64_container,
		struct cptr *c_out);
int glue_cap_insert_sk_buff_type(struct glue_cspace *cspace,
		struct sk_buff_container *sk_buff_container,
		struct cptr *c_out);
int glue_cap_insert_napi_struct_type(struct glue_cspace *cspace,
		struct napi_struct_container *napi_struct_container,
		struct cptr *c_out);

int glue_cap_lookup_device_type(struct glue_cspace *cspace,
		struct cptr c,
		struct device_container **device_container);
int glue_cap_lookup_module_type(struct glue_cspace *cspace,
		struct cptr c,
		struct module_container **module_container);
int glue_cap_lookup_net_device_type(struct glue_cspace *cspace,
		struct cptr c,
		struct net_device_container **net_device_container);
int glue_cap_lookup_net_device_ops_type(struct glue_cspace *cspace,
		struct cptr c,
		struct net_device_ops_container **net_device_ops_container);
int glue_cap_lookup_ethtool_ops_type(struct glue_cspace *cspace,
		struct cptr c,
		struct ethtool_ops_container **ethtool_ops_container);
int glue_cap_lookup_pci_bus_type(struct glue_cspace *cspace,
		struct cptr c,
		struct pci_bus_container **pci_bus_container);
int glue_cap_lookup_pci_dev_type(struct glue_cspace *cspace,
		struct cptr c,
		struct pci_dev_container **pci_dev_container);
int glue_cap_lookup_pci_device_id_type(struct glue_cspace *cspace,
		struct cptr c,
		struct pci_device_id_container **pci_device_id_container);
int glue_cap_lookup_pci_driver_type(struct glue_cspace *cspace,
		struct cptr c,
		struct pci_driver_container **pci_driver_container);
int glue_cap_lookup_rtnl_link_stats64_type(struct glue_cspace *cspace,
		struct cptr c,
		struct rtnl_link_stats64_container **rtnl_link_stats64_container);
int glue_cap_lookup_sk_buff_type(struct glue_cspace *cspace,
		struct cptr c,
		struct sk_buff_container **sk_buff_container);
int glue_cap_lookup_napi_struct_type(struct glue_cspace *cspace,
		struct cptr c,
		struct napi_struct_container **napi_struct_container);

int glue_cap_insert_irqhandler_type(struct glue_cspace *cspace,
		struct irqhandler_t_container *irqhandler_container,
		struct cptr *c_out);
int glue_cap_lookup_irqhandler_type(struct glue_cspace *cspace,
		struct cptr c,
		struct irqhandler_t_container **irqhandler_container);

#endif /* __IXGBE_GLUE_HELPER_H__ */
