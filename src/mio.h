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

#ifndef __MIO_H__
#define __MIO_H__

#include "mio_internal.h"
#include "logger.h"

typedef void (*mio_callback)(struct mio_op *op);
struct mio_kvs_id;

/**
 * Definitions of MIO operations:
 *   - Data structure (mio_op) to interact with MIO applications.
 *   - Operation codes for object and key-value store (kvs).
 *   - mio_op_state defines operation states.
 *   - operations on ops such as initialisation and finalisation.
 */
enum mio_obj_opcode {
	MIO_OBJ_INVALID,
	MIO_OBJ_CREATE,
	MIO_OBJ_DELETE,
	MIO_OBJ_OPEN,
	MIO_OBJ_CLOSE,
	MIO_OBJ_SYNC,
	MIO_OBJ_ATTRS_SET,
	MIO_OBJ_ATTRS_GET,
	MIO_OBJ_READ,
	MIO_OBJ_WRITE,
	MIO_OBJ_OP_NR
};

enum mio_kvs_opcode {
	MIO_KVS_CREATE_SET = MIO_OBJ_OP_NR + 1,
	MIO_KVS_DELETE_SET,
	/** Check a kvs for an existence. */
	MIO_KVS_LOOKUP_SET,
	/** Lookup a value with the given key. */
	MIO_KVS_GET,
	/** Insert or update the value, given a key. */
	MIO_KVS_PUT,
	/** Delete the value, if any, for the given key. */
	MIO_KVS_DEL,
	MIO_KVS_OP_NR
};

enum mio_composite_obj_opcode {
	MIO_COMP_OBJ_CREATE = MIO_KVS_OP_NR + 1,
	MIO_COMP_OBJ_DELETE,
	MIO_COMP_OBJ_ADD_LAYERS,
	MIO_COMP_OBJ_DEL_LAYERS,
	MIO_COMP_OBJ_LIST_LAYERS,
	MIO_COMP_OBJ_ADD_EXTENTS,
	MIO_COMP_OBJ_DEL_EXTENTS,
	MIO_COMP_OBJ_GET_EXTENTS,
	MIO_COMP_OBJ_OP_NR
};

enum mio_op_state {
	MIO_OP_ONFLY = 0,
	MIO_OP_COMPLETED,
	MIO_OP_FAILED
};

/**
 * Callback functions set by applications which are called when
 * all actions of an operation are done.
 */
struct mio_op_app_cbs {
	mio_callback moc_cb_complete;
	mio_callback moc_cb_failed;
	void         *moc_cb_data;
};

struct mio_op {
	unsigned int mop_opcode; /* Type of operation. */
	union {                  /* Which object or key-value set. */
		struct mio_obj *obj;
		struct mio_kvs_id *kvs_id; 
	} mop_who;
	int mop_state;           /* Current state of operation. */
	int mop_rc;              /* == 0 when operation successes,
				  *  < 0 the error code if the
				  *      operation fails. */

	struct mio_op_app_cbs mop_app_cbs;	

	/* See mio_drv_op_chain in mio_inernal for explanation. */
	struct mio_driver_op_chain mop_drv_op_chain;

	struct mio_op_ops *mop_op_ops;
};
/**
 * Initialise and finalise an operation. Applications have to allocate
 * memory for the operation before calling mio_op_init().
 */
void mio_op_init(struct mio_op *op);
void mio_op_fini(struct mio_op *op);
struct mio_op* mio_op_alloc_init();
void mio_op_fini_free(struct mio_op *op);

/**
 * mio_op_poll() is defined similar to POSIX poll() system call
 * and it waits for one of a set of operations to reach COMPLETED
 * or FAILED state.
 *
 * The set of operations to be monitored is specified in the ops
 * argument, which is an array of structures of the following form:
 *
 *         struct mio_pollop {
 *             struct mio_op *mp_op;  //Operation to poll.
 *             int mp_retstate;       // Returned state.
 *         };
 * If none of the operations reach states requested (COMPLETED
 * or FAILED) and no error has occurred for any of the operations,
 * then mio_op_poll() blocks until one of the states occurs.
 * The field retstat is an output parameter, filled by MIO with the
 * state that actually occurred.
 *
 * The timeout argument specifies the number of milliseconds that
 * mio_op_poll()should block waiting for an operation to reach state
 * requested.  The call will block until either:
 *     - an operation reaches the state requested;
 *     - the timeout expires.
 *
 * Specifying a negative value in timeout means an infinite timeout.
 * Specifying a timeout of zero causes mio_op_poll() to
 * return immediately, even if no operations reach the states
 * requested.
 *
 * @param ops The pointer to an array of data structure mio_pollop,
 * containing the operations and states to query on.
 * @param nr_ops The number of members in array ops.
 * @param timeout. Timeout value in milliseconds.
 * @return 0 the operation is completed or failed, < 0 for error.
 */
struct mio_pollop {
	struct mio_op *mp_op;  /* Operation to poll. */
	int mp_retstate;       /* Returned state. */
};

int mio_op_poll(struct mio_pollop *ops, int nr_ops, uint64_t timeout);

#define MIO_TIME_NEVER (~0ULL)

/**
 * Callbacks provides an alternative way to handle operations
 * asynchronously. mio_op_set_callbacks() set callbacks for
 * an operation.
 * 
 * @param op The pointer to operation.
 * @cb_complete The callback function is triggered when
 * the operation reach COMPLETED state.
 * @cb_complete The callback function is triggered when
 * the operation turns into FAILED state.
 * @cb_cb The data passed to callback functions. 
 */
void mio_op_callbacks_set(struct mio_op *op,
			  mio_callback cb_complete,
			  mio_callback cb_failed,
			  void *cb_data);

/**
 * Hints set to this opened object. MIO differentiates
 * 2 types of hints: (a) session hints which are set only for
 * object that are openned and are kept alive only during
 * the period of openning session. Session hints will be
 * destroyed when an object is closed and the hints won't
 * be valid for next session. Example of session hints: cache
 * and pre-fetching hints. (b) Persistent hints which are persisted
 * and are retrieved and set when an object is openned. The
 * persistent hints are valid for the life time of the object.
 */
enum mio_hint_type {
	MIO_HINT_SESSION,
	MIO_HINT_PERSISTENT
};

enum mio_hint_key {
	MIO_HINT_OBJ_CACHE_FLUSH_SIZE,
	MIO_HINT_OBJ_LIFETIME,
	MIO_HINT_OBJ_WHERE,
	MIO_HINT_KEY_NUM
};

/**
 * Hints are stored and managed as a map between hint’s key and value.
 */
struct mio_hints {
    struct mio_hint_map mh_map; 
};

/**
 * Object identifier is defined as a byte array with lower-indexed/big-endian
 * ordering and the option of Maestro changing to 256 bit later,
 * with lower-bits-0 indicating legacy IDs (i.e., the ones at bytes[8..15]).
 */
enum {
	MIO_OBJ_ID_LEN = 16,
	MIO_KVS_ID_LEN = 16
};
struct mio_obj_id {
	uint8_t moi_bytes[MIO_OBJ_ID_LEN];
};

struct mio_kvs_id {
	uint8_t mki_bytes[MIO_KVS_ID_LEN];
};

struct mio_pool {
	uint64_t mp_hi;
	uint64_t mp_lo;
};

/**
 * mio_iovec specifies the base address and length of an area in memory
 * from/to which data should be written. It also specifies byte range of
 * an object.
 */
struct mio_iovec {
	char   *miov_base;  /* Base address. */
	off_t   miov_off;   /* Offset. */
	size_t  miov_len;   /* Length. */
};

enum {
	MIO_MAX_HINTS_PER_OBJ = 16,
};

/**
 * Object attributes stored to and loaded from storage backend.
 */
struct mio_obj_attrs {
	uint64_t moa_size;
	uint64_t moa_wtime;

	/* Persistent hints only. */
	struct mio_hints moa_phints;
};

/**
 * In-memory object handler.
 */
struct mio_obj {
	struct mio_obj_id mo_id;
	struct mio_obj_op *mo_op;

	/** Driver specific object ops. */
	struct mio_obj_ops *mo_drv_obj_ops;

	/** Hints (persistent + session hints) set for this object. */
	struct mio_hints mo_hints;

	/** Associated metadata key-vaule set. */
	struct mio_kvs *mo_md_kvs;

	struct mio_obj_attrs mo_attrs;

	/** Pointer to driver specific object structure. */
	void *mo_drv_obj;
};

/**
 * Open an object identified by object identifier oid.
 * Upon successful completion mio_obj_open() return a ‘obj’
 * pointer. Otherwise, NULL is returned with an error code.

 * @param oid The object identifier.
 * @param obj Pointer to the object handle which can be used can be
 * subsequently used to access the object until the object is closed.  
 * Note that the object handle must be pre-allocated by application.
 * @param op The open operation for progress query. See Table 5 for 
 * mio_op data structure definition
 * @return 0 for success, < 0 for error.
 */
int mio_obj_open(const struct mio_obj_id *oid,
		 struct mio_obj *obj, struct mio_op *op);

/**
 * Close an object handle and free resources held by the object.
 * @param obj Pointer to the object handle.
 */
void mio_obj_close(struct mio_obj *obj);

/**
 * Create an object with identifier ‘oid’. See mio_obj_open()
 * above for notes on object identifier. 
 * Note that an internal key-value set will be created when
 * creating an object, offering a convenient mechanism
 * for applications to store and query customized object attributes.

 *
 * @param oid The object identifier.
 * @param pool_id The pool where the object is stored to.
 * @param[out] obj Pointer to the object handle which can be
 * used can be subsequently used to access the object until
 * the object is closed. Note that the object handle must be
 * pre-allocated by application.
 * @return 0 for success, < 0 for error.
 *
 * TODO: hints to create an object such as which pool to store.
 * Should MIO expose pools to applications?
 */
int mio_obj_create(const struct mio_obj_id *oid,
                   const struct mio_pool *pool_id,
                   struct mio_obj *obj, struct mio_op *op);

/**
 * Delete an object.
 * @param oid The object identifier.
 * @return 0 for success, < 0 for error.
 */
int mio_obj_delete(const struct mio_obj_id *oid, struct mio_op *op);

/**
 * mio_obj_writev() writes from a set of buffers specified by
 * the members of the iov: iov[0], iov[1], ..., iov[iovcnt-1]
 * to object offsets specified by the members of the offsets:
 * offsets[0], offsets[1], ...,  offsets[iovcnt-1].
 *
 * mio_obj_readv() reads from object offsets specified by
 * the members of the offsets:
 * offsets[0], offsets[1], ..., offsets[iovcnt-1] into a set
 * of buffers of iov: iov[0], iov[1], ..., iov[iovcnt-1].
 * 
 * Upon successfully returning, mio_obj_writev() and mio_obj_readv()
 * return with a launched operation. op can be used to query
 * the process of the operation.
 *
 * @param obj Pointer to the object handle.
 * @param iov The data buffer array. Each member specifies an
 * area of memory to write to or read from and an object offset.
 * @param iovcnt The number of data buffers. The number of
 * offsets and iov must be equal to iovcnt.
 * @param op[out]. The returned operation data structure for
 * state querying.
 * @return 0 for success, < 0 for error.
 *
 */
int mio_obj_writev(struct mio_obj *obj,
                   const struct mio_iovec *iov,
                   int iovcnt, struct mio_op *op);

int mio_obj_readv(struct mio_obj *obj,
                  const struct mio_iovec *iov,
                  int iovcnt, struct mio_op *op);

/**
 * mio_obj_sync() flushes all previous writes to obj to be
 * persisted to the storage device.
 *
 * @param obj The object is going to be sync'ed.
 * @param op[out]. The returned operation data structure for
 * state querying.
 * @return 0 for success, anything else for an error.
 */
int mio_obj_sync(struct mio_obj *obj, struct mio_op *op);

/**
 * Send a request to retrieve object size.
 *
 * @param obj The object in question.
 * @param op[out]. The returned operation data structure for
 * state querying.
 * @return 0 for success, anything else for an error.
 */
int mio_obj_size(struct mio_obj *obj, struct mio_op *op);

/**
 * Data structure represents a record stored in a key-value set.
 * MIO key-value set accepts variable lengths of keys and values.
 */
struct mio_kv_pair {
    void *mkp_key;
    size_t mkp_klen;
    void *mkp_val;
    size_t mkp_vlen;
};

struct mio_kvs {
	struct mio_kvs_id mk_id;
	/** Driver specific key-value set data structure. */
	void *mk_drv_kvs;
	struct mio_kvs_ops *mk_ops;
};

extern struct mio_kvs mio_obj_attrs_kvs;

/**
 *
 * For mio_kvs_pair_get() and mio_kvs_pair_del() arguments should be
 * as follows:
 * - 'kvps' key-value pairs should set mio_kv_pair::key’s for
 *   records being requested and set mio_kv_pair::val’s to NULLs.
 *   At least one key should be specified.
 * - After successful operation completion retrieved record
 *   values are stored in mio_kv_pair::mkp_val. If some value
 *   retrieval has failed, then corresponding element in 'rcs' array
 *   is set to a suitable error code.
 *
 * For mio_kvs_pair_put() arguments should be as follows:
 * - 'kvps' key-value pairs should set mio_kv_pair::key’s
 *   for records being requested and set mio_kv_pair::val’s
 *   to corresponding values.
 *
 * Error handling:
 * 'rcs' holds an array of per-item return codes for the operation.
 * It should be allocated by user with at least `nr_kvps` elements.
 * For example, 6 records with keys k0...k5 were requested through
 * mio_kvs_get(), k3 being absent in the key-value set.
 * After operation completion rcs[3] will be set to –ENOENT
 * and rcs[0,1,2,4,5] will be 0.
 *
 * Per-item return codes are more fine-grained than global operation
 * return code (mio_op::mo_rc). On operation completion the global
 * return code is set to negative value if it's impossible
 * to process any item .
 * - If the global return code is 0, then the user should check
 *   per-item return codes.
 * - If the global return code is not 0, then per-item return codes
 *   are undefined.
 *
 * @param kvs_id The key-value set identifier. The format of
 * key-value set identifier is driver specific and must explicitly
 * stated in documentation.
 * @param nr_kvps The number of key-value pairs.
 * @param rcs. Array for per-item return code.
 * @param op[out]. The operation pointer for progress query.
 * @return 0 for success, < 0 for error.
 */
int mio_kvs_pair_get(struct mio_kvs_id *kvs_id,
                     int nr_kvps, struct mio_kv_pair *kvps,
                     int32_t *rcs, struct mio_op *op);

int mio_kvs_pair_put(struct mio_kvs_id *kvs_id,
                     int nr_kvps, struct mio_kv_pair *kvps,
                     int32_t *rcs, struct mio_op *op);

int mio_kvs_pair_del(struct mio_kvs_id *kvs_id,
                     int nr_kvps, struct mio_kv_pair *kvps,
                     int32_t *rcs, struct mio_op *op); 

/**
 * mio_kvs_create_set() creates a key-value set,
 * while mio_kvs_del_set() deletes a key-value set.
 *
 * @param kvs_id The key-value set identifier.
 * @return 0 for success, < 0 for error.
 */
int mio_kvs_create_set(struct mio_kvs_id *kvs_id, struct mio_op *op);
int mio_kvs_del_set(struct mio_kvs_id *kvs_id, struct mio_op *op);

/**
 * mio_obj_hints_set() sets new values for the hints of the object
 * handler associated with object.
 * mio_obj_hints_get() retrieves object’s hints.
 *
 * As potentially there may be many hints for an object, the argument
 * hints allows us to set or get multiple hints in one single call.
 *
 * @param obj Pointer to the object handle.
 * @param hints Hints to set for an object. 
 * @return 0 for success, < 0 for error.
 */
int mio_obj_hints_set(struct mio_obj *obj, struct mio_hints *hints);
int mio_obj_hints_get(struct mio_obj *obj, struct mio_hints *hints);

/**
 * Helper functions to set or get individual hint.
 */
int mio_hints_init(struct mio_hints *hints);
void mio_hints_fini(struct mio_hints *hints);
int mio_hint_add(struct mio_hints *hints, int hint_key, uint64_t hint_value);
int mio_hint_lookup(struct mio_hints *hints, int hint_key, uint64_t *hint_value);

enum mio_hint_type mio_hint_type(enum mio_hint_key key);
char* mio_hint_name(enum mio_hint_key key);

/**
 * TODO: short description/definition of composite object.
 * 
 * Data structures and APIs for MIO composite object.
 *   - object extent is defined as a tuple (offset, size).
 *   - Composite layer (sub-object).
 *   - Composite object operations for MIO driver implementation.
 *   - APIs to create/delete/list layers.
 *   - APIs to create/delete/list extents. 
 */
struct mio_obj_ext {
	off_t  moe_off;
	size_t moe_size;
};

struct mio_comp_obj_layer {
	int mcol_priority;
	struct mio_obj_id mcol_oid;
};

struct mio_comp_obj_layout {
	int mlo_nr_layers;
	struct mio_comp_obj_layer *mlo_layers;
};

/**
 * mio_composite_obj_create() crates a composite object. If the
 * object of identifier `oid` exists this function returns –EEXIST
 * error code.
 * mio_composite_obj_del() deletes a composite object.
 *
 * @param oid The object identifier.
 * @return 0 for success, anything else for an error
 */
int mio_composite_obj_create(const struct mio_obj_id *oid,
                             struct mio_obj *obj, struct mio_op *op);
int mio_composite_obj_del(const struct mio_obj_id *oid, struct mio_op *op);

/**
 * mio_composite_obj_add_layer() adds a new layer (sub-object) to a
 * composite object. mio_composite_obj_del_layer() deletes a layer
 * from the composite object. All extents in the layer will
 * be removed as well.
 *
 * @param obj The object to add to.
 * @param layers  The layers to be added or deleted.
 * @param op The operation will be initialised when returned.
 * @return 0 for success, anything else for an error.
 */
int mio_composite_obj_add_layers(struct mio_obj *obj, int nr_layers,
				 struct mio_comp_obj_layer *layers,
				 struct mio_op *op);
int mio_composite_obj_del_layers(struct mio_obj *obj,
				 int nr_layers_to_del,
				 struct mio_comp_obj_layer *layers_to_del,
				 struct mio_op *op);

/**
 * List layers from highest priority to lowest one.
 * The memory to store layers and priorities are allocated
 * inside the function.
 * 
 * @param object The object to add to.
 * @param layout[out] The returned composite layout.
 * @param op The operation will be initialised when returned.
 * @return 0 for success, anything else for an error.
 */
int mio_composite_obj_list_layers(struct mio_obj *obj,
                                  struct mio_comp_obj_layout *ret_layout,
                   		  struct mio_op *op);

/**
 * Add/remove extents of the specified layer of a composite object.
 *
 * @param object The object to add to.
 * @param layer  The sub-object of the layer.
 * @param ext The extent in question.
 * @return 0 for success, anything else for an error.
 */
int mio_composite_obj_add_extents(struct mio_obj *obj,
				  struct mio_obj_id *layer_id,
				  int nr_exts, struct mio_obj_ext *exts,
				  struct mio_op *op);
int mio_composite_obj_del_extents(struct mio_obj *obj,
				  struct mio_obj_id *layer_id,
				  int nr_exts, struct mio_obj_ext *exts,
				  struct mio_op *op);
/**
 * Query `nr_exts` extents of the specified layer whose offsets are
 * larger than `offset`. The number of extents found is returned.
 *
 * @param object The object to add to.
 * @param layer  The sub-object of the layer.
 * @param offset The returned extents’ offset must be >= this
 * argument.
 * @param nr_exts. The number of members in the array `exts`.
 * @param exts[out] The output array for extents.
 * @param nr_ret_exts[out] The returned number of extents from query.
 * @return = 0 for success, anything else for an error.
 */
int
mio_composite_obj_get_extents(struct mio_obj *obj,
			      struct mio_obj_id *layer_id, off_t offset,
			      int nr_exts, struct mio_obj_ext *exts,
			      int *nr_ret_exts, struct mio_op *op);

/**
 * The structure 'mio' holds global information of underlying object store
 * and key-value set.  
 */
struct mio {
	enum mio_log_level m_log_level;
	char *m_log_file;

	enum mio_driver_id m_driver_id;
	struct mio_driver *m_driver;
	void *m_driver_confs;
};
extern struct mio *mio_instance;

/**
 * Initialises Maestro IO interface.
 *
 * @param yaml_conf cnfiguration file in Yaml format.
 * @return 0 for success, anything else for an error.
 */
int mio_init(const char *yaml_conf);

/**
 * Descriptor of a MIO memory layer.
 */
struct mio_layer {
        /** the name by which this layer is referenced by the user */
        char *name;

        /** This structure can be chained */
        struct mio_layer *next;

        /* @sining: If you want add an opaque internal identifier like
         * mio_layer_id id;
         */

        /* Characteristics of the layer */
        /** Capacity of the layer (not necessarily constant or current) */
        size_t capacity;

        /* Note that the 'available space' is not a slot here, because that
         * would imply that MIO needs to update it frequently. Instead, the free
         * space should be something that can be inquired by passing the layer
         * (name, id, or this structure's pointer ?) to a separate
         * mio_layer_freespace() function */

        /** preferred data alignment */
        size_t pref_alignment;

        /* Preferred block sizes */
        /** Number of preferred block size entries */
        size_t num_pref_block_sizes;

        /** Preferred block sizes for read and write operations, ordered in
         * decreasing order of performance */
        size_t pref_block_sizes[];
};

/**
 * MIO configuration information structure.
 */
struct mio_configinfo {
        /** The instance name */
        char *name;

        /** @sining: consider adding an opaque internal identifier like
         * mio_instance_id id
         */

        /** The number of memory layers supported */
        size_t num_layers;

        /** The layer identifiers; roughly ordered by performance */
        struct mio_layer *layers;
};

/**
 * Return the available space in layer @arg layer.
 *
 * Returns a (reasonable approximation to) the free capacity of the given layer in @arg *freespace.
 *
 */
int mio_layer_freespace(const struct mio_later *layer,
                        size_t *freespace);

/**
 * Return information about the current Maestro IO instance
 *
 * This function can only be called after mio_init() returned successfully.
 *
 * The return value is an internal object reference to the MIO instance, and will remain valid until the mio_fini() call.
 *
 * User code is not permitted to modify the content of the @arg config result.
 *
 * @arg *config_p will not be NULL on a successful return. If no memory layers
 * are configured, this will be visible inside the configinfo structure itself.
 *
 * @arg config_p may not be NULL on call.
 *
 * @param[out] config_p A linked list of configuration entries
 * @return 0 for success
 */
int mio_configinfo_get(const struct mio_configinfo **config_p);


/**
 * Finalises Maestro IO interface. 
 */
void mio_fini();

/**
 * Some object stores such as Mero supports thread-wise functionalities
 * (such as ADDB) which requires thread to be initialised in a certain way.
 * Call mio_thread_init() at the beginning of a thread and mio_thread_fini()
 * at the end of it.
 */
struct mio_thread {
	void *mt_drv_thread;
};

int mio_thread_init(struct mio_thread *thread);
void mio_thread_fini(struct mio_thread *thread);

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
