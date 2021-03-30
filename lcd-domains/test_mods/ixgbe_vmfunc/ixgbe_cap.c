#include <lcd_config/pre_hook.h>

#include <libcap.h>
#include <liblcd/liblcd.h>
#include <linux/slab.h>
#include "ixgbe_common.h"

#include <lcd_config/post_hook.h>

/* ------------------------------------------------------------ */

static struct cap_type_system *glue_libcap_type_system;

struct type_ops_id {
	struct cap_type_ops ops;
	cap_type_t libcap_type;
};

static int dummy_func(struct cspace *cspace, struct cnode *cnode,
		void *object)
{
	return 0;
}

enum glue_type {
	GLUE_TYPE_DEVICE_CONTAINER,
	GLUE_TYPE_MODULE_CONTAINER,
	GLUE_TYPE_NET_DEVICE_CONTAINER,
	GLUE_TYPE_NET_DEVICE_OPS_CONTAINER,
	GLUE_TYPE_ETHTOOL_OPS_CONTAINER,
	GLUE_TYPE_PCI_BUS_CONTAINER,
	GLUE_TYPE_PCI_DEV_CONTAINER,
	GLUE_TYPE_PCI_DEVICE_ID_CONTAINER,
	GLUE_TYPE_PCI_DRIVER_CONTAINER,
	GLUE_TYPE_RTNL_LINK_STATS64_CONTAINER,
	GLUE_TYPE_SK_BUFF_CONTAINER,
	GLUE_TYPE_NAPI_STRUCT_CONTAINER,
	GLUE_TYPE_IRQ_HANDLER_CONTAINER,
	GLUE_NR_TYPES,
};

static struct type_ops_id glue_libcap_type_ops[GLUE_NR_TYPES] = {
	{
		{
			.name = "struct device",
			.delete = dummy_func,
			.revoke = dummy_func,
		}
	},
	{
		{
			.name = "struct module",
			.delete = dummy_func,
			.revoke = dummy_func,
		}
	},
	{
		{
			.name = "struct net_device",
			.delete = dummy_func,
			.revoke = dummy_func,
		}
	},
	{
		{
			.name = "struct net_device_ops",
			.delete = dummy_func,
			.revoke = dummy_func,
		}
	},
	{
		{
			.name = "struct ethtool_ops",
			.delete = dummy_func,
			.revoke = dummy_func,
		}
	},

	{
		{
			.name = "struct pci_bus",
			.delete = dummy_func,
			.revoke = dummy_func,
		}
	},
	{
		{
			.name = "struct pci_dev",
			.delete = dummy_func,
			.revoke = dummy_func,
		}
	},
	{
		{
			.name = "struct pci_device_id",
			.delete = dummy_func,
			.revoke = dummy_func,
		}
	},
	{
		{
			.name = "struct pci_driver",
			.delete = dummy_func,
			.revoke = dummy_func,
		}
	},
	{
		{
			.name = "struct rtnl linkstats",
			.delete = dummy_func,
			.revoke = dummy_func,
		}
	},

	{
		{
			.name = "struct sk_buff",
			.delete = dummy_func,
			.revoke = dummy_func,
		}
	},
	{
		{
			.name = "struct napi_struct",
			.delete = dummy_func,
			.revoke = dummy_func,
		}
	},
	{
		{
			.name = "struct irq_handler_container",
			.delete = dummy_func,
			.revoke = dummy_func,
		}
	},
};

int glue_cap_init(void)
{
	int ret;
	int i;
	cap_type_t libcap_type;
	/*
	 * Alloc and init microkernel type system
	 */
	ret = cap_type_system_alloc(&glue_libcap_type_system);
	if (ret) {
		LIBLCD_ERR("alloc glue type system failed");
		goto fail1;
	}
	ret = cap_type_system_init(glue_libcap_type_system);
	if (ret) {
		LIBLCD_ERR("init glue type system failed");
		goto fail2;
	}
	/*
	 * Add types
	 */
	for (i = 0; i < GLUE_NR_TYPES; i++) {

		libcap_type = cap_register_private_type(
			glue_libcap_type_system,
			0,
			&glue_libcap_type_ops[i].ops);
		if (libcap_type == CAP_TYPE_ERR) {
			LIBLCD_ERR("failed to register glue cap type %s",
				glue_libcap_type_ops[i].ops.name);
			ret = -EIO;
			goto fail3;
		}
		glue_libcap_type_ops[i].libcap_type = libcap_type;
	}

	return 0;

fail3:
	cap_type_system_destroy(glue_libcap_type_system);
fail2:
	cap_type_system_free(glue_libcap_type_system);
	glue_libcap_type_system = NULL;
fail1:
	return ret;
}

int glue_cap_create(struct glue_cspace **cspace_out)
{
	return glue_cspace_alloc_init(glue_libcap_type_system, cspace_out);
}

void glue_cap_destroy(struct glue_cspace *cspace)
{
	glue_cspace_destroy_free(cspace);
}

void glue_cap_remove(
	struct glue_cspace *cspace,
	cptr_t c)
{
	glue_cspace_remove(cspace, c);
}

void glue_cap_exit(void)
{
	/*
	 * Destroy and free type system if necessary
	 */
	if (glue_libcap_type_system) {
		cap_type_system_destroy(glue_libcap_type_system);
		cap_type_system_free(glue_libcap_type_system);
		glue_libcap_type_system = NULL;
	}
}

#include "ixgbe_glue_helper.c"
