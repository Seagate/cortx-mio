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

#include <pthread.h>
#include "mio_internal.h"
#include "mio_telemetry.h"
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
#ifdef __cplusplus
enum mio_obj_opcode : int {
#else
enum mio_obj_opcode {
#endif
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
	uint64_t mop_seqno;

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

	/* See mio_drv_op_chain in mio_inernal.h for explanation. */
	struct mio_driver_op_chain mop_drv_op_chain;

	struct mio_op_ops *mop_op_ops;
};

extern pthread_mutex_t mio_op_seqno_lock;
extern uint64_t mio_op_seqno;

/**
 * Initialise and finalise an operation. Applications have to allocate
 * memory for the operation before calling mio_op_init().
 */
int mio_op_init(struct mio_op *op);
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
 * Define the scope of a hint. A hint can be used for an object,
 * a key-value set or system level parameters.
 */
enum mio_hint_scope {
	MIO_HINT_SCOPE_OBJ,
	MIO_HINT_SCOPE_KVSET,
	MIO_HINT_SCOPE_SYS
};

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

/** Hints for individual object. */
enum mio_obj_hint_key {
	MIO_HINT_OBJ_LIFETIME,
	MIO_HINT_OBJ_WHERE,
	MIO_HINT_OBJ_HOT_INDEX,

	MIO_HINT_OBJ_KEY_NUM
};

/* System wide hints. */
enum mio_sys_hint_key {
	MIO_HINT_HOT_OBJ_THRESHOLD,
	MIO_HINT_COLD_OBJ_THRESHOLD,
};

enum mio_hint_value {
	MIO_HINT_VALUE_NULL
};

/**
 * Hints are stored and managed as a map between hint’s key and value.
 */
struct mio_hints {
    struct mio_hint_map mh_map;
};

extern struct mio_hints mio_sys_hints;

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

struct mio_pool_id {
	uint64_t mpi_hi;
	uint64_t mpi_lo;
};

/** Some marcos for pools. */
enum {
	MIO_POOL_ID_AUTO = 0x0,
	MIO_POOL_GOLD   = 0x100,
	MIO_POOL_SILVER = 0x101,
	MIO_POOL_BRONZE  = 0x102
};

/**
 * mio_iovec specifies the base address and length of an area in memory
 * from/to which data should be written. It also specifies byte range of
 * an object.
 */
struct mio_iovec {
	char *miov_base;    /* Base address. */
	uint64_t miov_off;  /* Offset. */
	size_t miov_len;    /* Length. */
};

enum {
	MIO_MAX_HINTS_PER_OBJ = 16,
};

/**
 * Object attributes stored to and loaded from storage backend.
 */
struct mio_obj_attrs {
	uint64_t moa_size;

	/**
	 * Object access statistics. MIO shows one usage of
	 * this information to calculate object hotness.
	 */
	struct mio_obj_stats moa_stats;

	/* Persistent hints only. */
	struct mio_hints moa_phints;
};

/**
 * In-memory object handler.
 */
struct mio_obj {
	struct mio_obj_id mo_id;
	struct mio_obj_op *mo_op;

	/**
	 * Sequence number of a opened object session. This sequence
	 * number can be used to associated all operations issued
	 * in this session, which can be used in telemetry data
	 * analysis.
	 */
	uint64_t mo_sess_seqno;

	/** Driver specific object ops. */
	struct mio_obj_ops *mo_drv_obj_ops;

	/** Hints (persistent + session hints) set for this object. */
	struct mio_hints mo_hints;

	/** Associated metadata key-vaule set. */
	struct mio_kvs *mo_md_kvs;

	/** If the object's attributes have been updated. */
	bool mo_attrs_updated;
	struct mio_obj_attrs mo_attrs;

	/** Pointer to driver specific object structure. */
	void *mo_drv_obj;

	/** Pointer to driver specific object lock. */
	void *mo_drv_obj_lock;
};

extern pthread_mutex_t mio_obj_session_seqno_lock;
extern uint64_t mio_obj_session_seqno;

enum mio_pool_type {
	MIO_POOL_TYPE_NVM = 0,
	MIO_POOL_TYPE_SSD,
	MIO_POOL_TYPE_HDD
};

enum {
	MIO_POOL_MAX_NAME_LEN = 32,
	MIO_POOL_MAX_NR_OPT_BLKSIZES = 16,
};

/**
 * Descriptor of a MIO pool.
 */
struct mio_pool {
	/** the name by which this layer is referenced by the user */
	char mp_name[MIO_POOL_MAX_NAME_LEN + 1];
	
	/** Pool id. */
 	struct mio_pool_id mp_id;

	enum mio_pool_type mp_type;

        /* Characteristics of the pool. */
        /** Capacity of the layer (not necessarily constant or current) */
        size_t mp_capacity;

        /*
	 * Note that the 'available space' is not a slot here, because that
         * would imply that MIO needs to update it frequently. Instead, the
         * free space is inquired by passing the pool id  to a separate
         * mio_pool_freespace() function.
         */

        /** Optimised data buffer alignment */
        size_t mp_opt_alignment;

        /**
	 * Preferred block sizes for read and write operations, ordered in
         * decreasing order of performance. These parameters are initialised
         * using driver specific experiential `formula`, and could be
         * updated by monitoring telemetry data.
         */
        size_t mp_nr_opt_blksizes;
        size_t mp_opt_blksizes[MIO_POOL_MAX_NR_OPT_BLKSIZES];
};

/**
 * MIO pool information structure.
 */
struct mio_pools {
	int mps_default_pool_idx;
	char mps_default_pool_name[MIO_POOL_MAX_NAME_LEN + 1];

        int mps_nr_pools;
        struct mio_pool *mps_pools;
};

extern struct mio_pools mio_pools;

/**
 * Return the available space in pool @arg pool_id.
 *
 * Returns a (reasonable approximation to) the free capacity of the given
 * pool in @param *freespace.
 *
 */
int mio_pool_freespace(const struct mio_pool_id *pool_id,
		       size_t *freespace);

/**
 *  Return information about a requested pool or all MIO pools.
 * 
 * This function can only be called after mio_init() returned successfully.
 * User code is not permitted to modify the content of the @arg config
 * result.
 * 
 * @param *pool_id The pool id.
 * @param *pools will not be NULL on a successful return. If no
 * pools are configured, this will be visible inside the configinfo
 * structure itself.
 *
 * @param pools may not be NULL on call.
 *
 * @param[out] pool The requested pool.
 * @param[out] pools The list of all pools.
 * @return 0 for success
 */
int mio_pool_get(const struct mio_pool_id *pool_id, struct mio_pool **pool);
int mio_pool_get_all(struct mio_pools **pools);

/**
 * Return an object's pool id.
 *
 * @arg *pool_id The pool id.
 * @param obj Pointer to the object handle which can be used can be
 */
int mio_obj_pool_id(const struct mio_obj *obj, struct mio_pool_id *pool_id);

bool mio_obj_pool_id_cmp(struct mio_pool_id *pool_id1,
			 struct mio_pool_id *pool_id2);

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
 * `pool_id` and `hints` together decide which pool to store the object.
 * When `pool_id` isn't NULL, the object will be created in the explicitly
 * specified pool. When `pool_id` is set to NULL, the pool is chosen
 * according to the `hints`. MIO currently offers 2 kinds of hints for
 * pool selection. {MIO_HINT_OBJ_WHERE, MIO_POOL_GOLD|SILVER|BRONZE}
 * allows users choose a pool according to performance requirement.
 * {MIO_HINT_OBJ_HOT_INDEX, hotname} gives MIO information on how
 * frequent the object will be accessed and MIO then decides which
 * pool to store the object. See example in examples/mio_hsm.c.
 *
 * @param oid The object identifier.
 * @param pool_id The pool where the object is stored to.
 * @param hints Hints about which pool to store the object.
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
                   const struct mio_pool_id *pool_id, struct mio_hints *hints,
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
 * Send a request to retrieve object size. When an object is
 * newly openned, its object attributes are retrieved including
 * object size (struct mio_obj::mo_attrs::moa_size). Object's
 * size can later be updated by calling mio_obj_size().
 *
 * @param obj The object in question.
 * @param op[out]. The returned operation data structure for
 * state querying.
 * @return 0 for success, anything else for an error.
 */
int mio_obj_size(struct mio_obj *obj, struct mio_op *op);

/**
 * Lock and unlock an object. Only exclusive whole object lock
 * is supported. The usage of an object lock is similar to file
 * lock flock().
 *   - open object.
 *   - lock
 *   - do some IOs
 *   - unlock
 *   - close object.
 *
 * @param obj The object in question.
 * @return 0 for success, anything else for an error.
 */
int mio_obj_lock(struct mio_obj *obj);
int mio_obj_unlock(struct mio_obj *obj);

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
 * - After successful operation retrieved record
 *   values are stored in mio_kv_pair::mkp_val. If some value
 *   retrieval has failed, then corresponding element in 'rcs' array
 *   is set to a suitable error code.
 *
 * For mio_kvs_pair_put() arguments should be as follows:
 * - 'kvps' key-value pairs should set mio_kv_pair::key’s
 *   for records being requested and set mio_kv_pair::val’s
 *   to corresponding values.
 *
 * For mio_kvs_pair_next():
 * - The first element's key of 'kvps' should be set to the starting key
 *   and other keys are set to NULLs. All value parts of `kvps` should
 *   be set to NULLs.
 *  
 *   If the starting key is set to NULL, mio_kvs_pair_next() will return
 *   the required number of key/value pairs starting with the smallest
 *   key.
 *
 *   If 'exclude_start_key' is set to 'true', mio_kvs_pair_next() will
 *   return pairs starting with the key right after the starting key.
 *
 * - After successful operation retrieved record keys and
 *   values are stored in 'kvps'. If some value
 *   retrieval has failed, then corresponding element in 'rcs' array
 *   is set to a suitable error code. If the error code is set to
 *   EOF, meaning that there are no more pairs. An example in
 *   examples/kvs.c::kvs_query_next() shows how to handle returned
 *   error codes.
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

int mio_kvs_pair_next(struct mio_kvs_id *kvs_id,
		      int nr_kvps, struct mio_kv_pair *kvps,
		      bool exclude_start_key, int32_t *rcs, struct mio_op *op);

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

int mio_obj_hint_set(struct mio_obj *obj, int hint_key, uint64_t hint_value);
int mio_obj_hint_get(struct mio_obj *obj, int hint_key, uint64_t *hint_value);

int mio_sys_hint_set(int hint_key, uint64_t hint_value);
int mio_sys_hint_get(int hint_key, uint64_t *hint_value);
/**
 * Helper functions to set or get individual hint.
 */
int mio_hints_init(struct mio_hints *hints);
void mio_hints_fini(struct mio_hints *hints);
int mio_hint_add(struct mio_hints *hints, int hint_key, uint64_t hint_value);
int mio_hint_lookup(struct mio_hints *hints, int hint_key, uint64_t *hint_value);

enum mio_hint_type mio_hint_type(enum mio_hint_scope scope, int key);
char* mio_hint_name(enum mio_hint_scope scope, int key);

struct mio_pool_id mio_obj_hotness_to_pool_id(uint64_t hotness);

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
	enum mio_telemetry_store_type m_telem_store_type;
	char *m_telem_prefix;
	
	enum mio_log_level m_log_level;
	char *m_log_dir;

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
 * Finalises Maestro IO interface.
 */
void mio_fini();

/**
 * Some object stores such as Cortx/motr supports thread-wise functionalities
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
