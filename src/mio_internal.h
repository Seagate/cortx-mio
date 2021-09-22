/* -*- C -*- */
/*
 * Copyright: (c) 2020 - 2021 Seagate Technology LLC and/or its its Affiliates,
 * All Rights Reserved
 *
 * This software is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#ifndef __MIO_INTERNAL__
#define __MIO_INTERNAL__

#include <stdint.h>

#include "motr/client.h"

struct mio;
struct mio_op;
struct mio_obj_id;
struct mio_kvs_id;
struct mio_pool_id;
struct mio_pool;
struct mio_obj;
struct mio_iovec;
struct mio_kv_pair;
struct mio_hints;
struct mio_thread;

#ifdef __cplusplus
enum mio_obj_opcode : int;
#else
enum mio_obj_opcode;
#endif
struct mio_obj_ext;
struct mio_comp_obj_layer;
struct mio_comp_obj_layout;

struct mio_driver_op;

/**
 * Statistics on object accesses.
 */
struct mio_obj_stats {
	/* Number of READ. */
	uint64_t mos_rcount;
	uint64_t mos_rbytes;
	uint64_t mos_rtime;

	/* Number of WRITE. */
	uint64_t mos_wcount;
	uint64_t mos_wbytes;
	uint64_t mos_wtime;
};

/**
 * MIO defines sets of operations a driver must implemnt:
 *   - mio_op_ops defines driver specific functions on operation,
 *     such as initialisation/finalisation and setting callbacks. (mandate)
 *   - mio_obj_ops defines object access operations (mandate).
 *   - mio_kvs_ops defines key-value operations (optional).
 *   - motr-inspired operations, such as composite object related
 *     operations (optional).
 *
 * Notes on writing driver specific functions to implement the sets of
 * operations mentioned above.
 *  - mio_driver_op_set() must be called and must be called after the op
 *    is created and before it being launched.
 */
typedef void (*mio_callback)(struct mio_op *op);
struct mio_op_ops {
	int  (*mopo_init)(struct mio_op *op);
	void (*mopo_fini)(struct mio_op *op);
	int  (*mopo_wait)(struct mio_op *op, uint64_t timeout, int *retstate);
	int  (*mopo_set_cbs)(struct mio_op *op);
};

struct mio_obj_ops {
	int (*moo_open)(struct mio_obj *obj, struct mio_op *op);
	int (*moo_close)(struct mio_obj *obj);
	int (*moo_create)(const struct mio_pool_id *pool_id,
			  struct mio_obj *obj, struct mio_op *op);
	int (*moo_delete)(const struct mio_obj_id *oid, struct mio_op *op);

	int (*moo_writev)(struct mio_obj *obj,
			  const struct mio_iovec *iov,
			  int iovcnt, struct mio_op *op);
	int (*moo_readv)(struct mio_obj *obj,
			 const struct mio_iovec *iov,
			 int iovcnt, struct mio_op *op);
	int (*moo_sync)(struct mio_obj *obj, struct mio_op *op);
	int (*moo_size)(struct mio_obj *obj, struct mio_op *op);
	int (*moo_pool_id)(const struct mio_obj *obj,
			   struct mio_pool_id *pool_id);

	/**
	 * Exclusive whole object (blocking) lock.
	 */
	int (*moo_lock)(struct mio_obj *obj);
	int (*moo_unlock)(struct mio_obj *obj);

	/**
	 * Load and store persistent hints. Unlike other operations,
	 * storing/loading hints are synchronous operations because
	 * not all hints are persistent hints, if applications use
	 * only session hints, no IO (to retrieve persistent hints)
	 * will be launched, thus no 'op' is needed. And as setting
	 * hints is rare comapred to object accesses, its overhead
	 * is considered small.
	 */
	int (*moo_hint_store)(struct mio_obj *obj);
	int (*moo_hint_load)(struct mio_obj *obj);
};

struct mio_kvs_ops {
	int (*mko_get)(struct mio_kvs_id *kvs_id,
		       int nr_kvps, struct mio_kv_pair *kvps,
		       int32_t *rcs, struct mio_op *op);

	int (*mko_next)(struct mio_kvs_id *kvs_id,
			int nr_kvps, struct mio_kv_pair *kvps,
			bool exclude_start_key, int32_t *rcs,
			struct mio_op *op);

	int (*mko_put)(struct mio_kvs_id *kvs_id,
		       int nr_kvps, struct mio_kv_pair *kvps,
		       int32_t *rcs, struct mio_op *op);

	int (*mko_del)(struct mio_kvs_id *kvs_id,
		       int nr_kvps, struct mio_kv_pair *kvps,
		       int32_t *rcs, struct mio_op *op);

	int (*mko_create_set)(struct mio_kvs_id *kvs_id, struct mio_op *op);
	int (*mko_del_set)(struct mio_kvs_id *kvs_id, struct mio_op *op);
};

struct mio_comp_obj_ops {
	int (*mcoo_create)(struct mio_obj *obj, struct mio_op *op);
	int (*mcoo_del)(const struct mio_obj_id *oid, struct mio_op *op);

	int (*mcoo_add_layers)(struct mio_obj *obj,
			       int nr_layers,
			       struct mio_comp_obj_layer *layers,
			       struct mio_op *op);
	int (*mcoo_del_layers)(struct mio_obj *obj,
			       int nr_layers_to_del,
			       struct mio_comp_obj_layer *layers_to_del,
			       struct mio_op *op);
	int (*mcoo_list_layers)(struct mio_obj *obj,
				struct mio_comp_obj_layout *ret_layout,
				struct mio_op *op);

	int (*mcoo_add_extents)(struct mio_obj *obj,
			        struct mio_obj_id *layer_id,
				int nr_exts, struct mio_obj_ext *exts,
				struct mio_op *op);
	int (*mcoo_del_extents)(struct mio_obj *obj,
			        struct mio_obj_id *layer_id,
				int nr_exts, struct mio_obj_ext *exts,
				struct mio_op *op);

	int (*mcoo_get_extents)(struct mio_obj *obj,
			        struct mio_obj_id *layer_id, off_t offset,
				int nr_exts, struct mio_obj_ext *exts,
				int *nr_ret_exts, struct mio_op *op);
};

/**
 * Driver's pool operations.
 */
struct mio_pool_ops {
	/* Synchronous GET operation. */
	int (*mpo_get)(const struct mio_pool_id *pool_id, struct mio_pool *pool);
};

/**
 * MIO defines a generic driver interface to use different object stores
 * as backend. It includes:
 *   - Types of interface driver. Currently only Motr object store is
 *     supported. Ceph driver will be implemented in future.
 *   - Operations to initialise and finalise a driver.
 *   - Operation set that a driver is to implement. Each driver must
 *     implement an object store interface and key-value set interface.
 *     motr-inspired interface is optional.
 */
enum mio_driver_id {
	MIO_DRIVER_INVALID = -1,
	MIO_MOTR,
	MIO_DRIVER_NUM
};

/**
 * Initialisation and finalisation functions for an interface driver.
 */
struct mio_driver_sys_ops {
	int (*mdo_init) (struct mio *mio_inst);
	void (*mdo_fini) (void);

	int (*mdo_user_perm)(struct mio *mio_inst);

	int (*mdo_thread_init)(struct mio_thread *thread);
	void (*mdo_thread_fini)(struct mio_thread *thread);
};

/**
 * Each Maestro IO driver is defined by a set of operations. Driver
 * initialisaton/finalisation and object and key-value set accessing operations
 * must be implemented for a driver, while motr-inspired operations
 * are optional.
 */
struct mio_driver {
	/** Driver's operations, such as initialisation and finalisation. */
	struct mio_driver_sys_ops *md_sys_ops;

	struct mio_op_ops *md_op_ops;

	/** Pool operations. */
	struct mio_pool_ops *md_pool_ops;

	/** Object and key-value set operations. */
	struct mio_obj_ops *md_obj_ops;
	struct mio_kvs_ops *md_kvs_ops;

	/** Motr-inspired APIs. */
	struct mio_comp_obj_ops *md_comp_obj_ops;
};

/**
 * mio_driver_op contains a pointer to the driver specific operation and
 * a function pointer which is called to do post processing or to create
 * and move to next op. In this way, MIO is able to chain and execute a
 * number of dependent ops. Examples of mio_driver_op::mdo_func are:
 *   - Post-processing of GET operation to copy returned values from
 *     the key-value store.
 *   - Object WRITE which includes writing data (to data store) and
 *     updating object attributes (such as size) to metadata store.
 *
 * Beaware that when chaining multiple driver operations to accomplish
 * a task, it is up to the driver to handle any driver operation's
 * failure, some driver will roll back to a consistent state if proper
 * transcation mechanism is implemented. Current version of Motr driver
 * simply reports the error back to application. This will be re-visited
 * after DTM is in place.
 *
 * An implementation of mio_driver_op::mdo_post_process() should follow the
 * general rules below:
 *   - Return value: if it doesn't create a new op,  MIO_DRV_OP_FINAL
 *     is returned, otherwise a new next function is set and
 *     MIO_DRV_OP_NEXT is returned.
 *
 * mio_driver_op::mdo_post_process is invoked when:
 *   - mio_op_poll() checks if post processing action is set for an op
 *     when mio_op_ops::mopo_wait() returns. If post processing is set
 *     and called and new driver's op is created, mio_op_poll() will
 *     keep polling if not yet timed out.
 *  - if an application has set the callback functions, the callback
 *    functions must check and call the post processing action if set.
 *  - So, an application can't poll an op whose callback functions have
 *    been set to avoid double entries to the post processing action.
 */
enum {
	MIO_DRV_OP_NEXT = 0,
	MIO_DRV_OP_FINAL,
};

typedef int (*mio_driver_op_postprocess)(struct mio_op *op);
typedef int (*mio_driver_op_fini)(struct mio_driver_op *dop);
struct mio_driver_op {
	/**
	 * The function and data for post-processing.
	 */
	mio_driver_op_postprocess mdo_post_proc;
	void *mdo_post_proc_data;

	/** Pointers to driver specific op. */
	void *mdo_op;
	void *mdo_op_args;
	mio_driver_op_fini mdo_op_fini;

	struct mio_driver_op *mdo_next;
};

struct mio_driver_op_chain {
	struct mio_driver_op *mdoc_head;
};

int mio_driver_op_add(struct mio_op *op,
		      mio_driver_op_postprocess post_proc,
		      void *post_proc_data,
		      mio_driver_op_fini op_fini,
		      void *drv_op,void *drv_op_args);

void mio_driver_op_invoke_real_cb(struct mio_op *op, int rc);

struct mio_driver* mio_driver_get(enum mio_driver_id driver_id);

/** Register a driver. */
void mio_driver_register(enum mio_driver_id driver_id,
			 struct mio_driver_sys_ops *drv_ops,
			 struct mio_pool_ops *pool_ops,
			 struct mio_op_ops *op_ops,
			 struct mio_obj_ops *obj_ops,
			 struct mio_kvs_ops *kvs_ops,
			 struct mio_comp_obj_ops *comp_obj_ops);

/** Register all drivers when initialising MIO instance. */
void mio_drivers_register();

void mio_motr_driver_register();

/**
 * mio_motr_config contains configuration parameters to setup an
 * Motr instance.
 */
enum {
	MIO_MOTR_MAX_POOL_CNT = 16,
};

struct mio_motr_config {
	/** oostore mode is set when 'is_oostore' is TRUE. */
	bool mc_is_oostore;
	/**
	 * Flag for verify-on-read. Parity is checked when doing
	 * READ's if this flag is set.
	 */
	bool mc_is_read_verify;

	/** Local endpoint.*/
	char *mc_motr_local_addr;
	/** HA service's endpoint.*/
	char *mc_ha_addr;
	/** Process fid for rmservice@motr. */
	char *mc_process_fid;
	char *mc_profile;

	/**
	 * The minimum length of the 'tm' receive queue,
         * use M0_NET_TM_RECV_QUEUE_DEF_LEN if unsure.
         */
	uint32_t mc_tm_recv_queue_min_len;
	/**
	 * The maximum rpc message size, use M0_RPC_DEF_MAX_RPC_MSG_SIZE
	 * if unsure.
	 */
	uint32_t mc_max_rpc_msg_size;

	/**
	 * Default parity group data unit size of an object. As Motr 
	 * internally uses layout id instead of unit size, the layout id
	 * will be computed from unit size.
	 */
	int mc_unit_size;
	int mc_default_layout_id;

	/**
 	 * Due to motr's BE limitation, if a request send too much data to
 	 * service, an -E2BIG is returned. Set this parameter to limit
 	 * the IO size sent to each device.
 	 */
	uint64_t mc_max_iosize_per_dev;

	/**
 	 * Motr user group.
 	 */
	char *mc_motr_group;

	/** Is ADDB on? */
	bool mc_is_addb_on;
};

/**
 * A simple map implementation for hints in which key is of type `int`
 * and value is of type `uint64_t`. As there are usually small number
 * of hints for each object, arrays are used to store keys and values.
 * The overhead of looking up entries in arrays is low.
 */
struct mio_hint_map {
	int mhm_nr_entries;
	int mhm_nr_set;
	int *mhm_keys;
	uint64_t *mhm_values;
};

enum {
	MIO_OBJ_HINT_NUM = 32
};

int mio_hint_map_init(struct mio_hint_map *map, int nr_entries);
void mio_hint_map_fini(struct mio_hint_map *map);
int mio_hint_map_copy(struct mio_hint_map *, struct mio_hint_map *from);
int mio_hint_map_set(struct mio_hint_map *map, int key, uint64_t value);
int mio_hint_map_get(struct mio_hint_map *map, int key, uint64_t *value);

bool mio_hint_is_set(struct mio_hints *hints, int hint_key);

int mio_obj_hotness_to_pool_idx(uint64_t hotness);

int mio_conf_init(const char *config_file);
void mio_conf_fini();
bool mio_conf_default_pool_has_set();

int mio_instance_check();

int mio_obj_op_init(struct mio_op *op, struct mio_obj *obj,
		    enum mio_obj_opcode opcode);

void mio_op_cb_failed(struct mio_op *op);
void mio_op_cb_complete(struct mio_op *op);
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
