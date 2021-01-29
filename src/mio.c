/* -*- C -*- */
/*
 * Copyright: (c) 2020 - 2021 Seagate Technology LLC and/or its its Affiliates,
 * All Rights Reserved
 *
 * This software is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */


#include <errno.h>
#include <assert.h>

#include "logger.h"
#include "mio_internal.h"
#include "mio.h"

struct mio_kvs mio_obj_attrs_kvs;
struct mio *mio_instance = NULL;

int mio_instance_check()
{
	if (mio_instance == NULL) {
		/* As MIO instance is not initialised, can't use mio_log(). */
		fprintf(stderr, "MIO instance has not been initilised!\n");
		return -EINVAL;
	}

	if (mio_instance->m_driver == 0) {
		fprintf(stderr, "MIO instance has not set a driver!\n");
		return -EINVAL;
	}

	if (mio_instance->m_driver->md_sys_ops == NULL ||
	    mio_instance->m_driver->md_op_ops == NULL) {
		fprintf(stderr, "MIO driver is not set properly!\n");
		return -EINVAL;
	}

	return 0;
}

/* --------------------------------------------------------------- *
 *                     Operation                                   *
 * ----------------------------------------------------------------*/

void mio_op_init(struct mio_op *op)
{
	if (op == NULL)
		return;
	if (mio_instance_check())
		return;

	/*
 	 * When mio_op_init() is called, no driver specific operation
 	 * has been created and initialised yet.
 	 */
	mio_memset(op, 0, sizeof *op);
	op->mop_op_ops = mio_instance->m_driver->md_op_ops;
}

void mio_op_fini(struct mio_op *op)
{
	assert(op != NULL && op->mop_op_ops != NULL);

	if (op->mop_op_ops->mopo_fini)
		op->mop_op_ops->mopo_fini(op);
}

struct mio_op* mio_op_alloc_init()
{
	struct mio_op *op;

	if (mio_instance_check())
		return NULL;

	op = mio_mem_alloc(sizeof *op);
	if (op != NULL)
		mio_op_init(op);
	return op;
}

void mio_op_fini_free(struct mio_op *op)
{
	mio_op_fini(op);
	mio_mem_free(op);
}

int mio_op_poll(struct mio_pollop *ops, int nr_ops, uint64_t timeout)
{
	int rc;
	int i;
	int nr_done = 0;
	struct mio_op *mop;
	struct mio_pollop *pop;
	bool op_done = false;
	uint64_t start;
	uint64_t end;
	mio_driver_op_postprocess post_proc;

	if (nr_ops != 0 && ops == NULL)
		return -EINVAL;
	rc = mio_instance_check();
	if (rc < 0)
		return rc;

	start = mio_now();

again:
	for (i = 0; i < nr_ops; i++) {
		pop = ops + i;
		mop = pop->mp_op;
		assert(mop != NULL);
		assert(mop->mop_op_ops != NULL);
		op_done = false;
		mop->mop_rc = mop->mop_op_ops->mopo_wait(
				mop, timeout, &pop->mp_retstate);

		post_proc = mop->mop_drv_op_chain.mdoc_head->mdo_post_proc;
		if (pop->mp_retstate == MIO_OP_COMPLETED) {
			if (post_proc != NULL) {
				rc = post_proc(mop);
				/*
			 	 * Check to see if a new action has been launched.
			 	 * If no more action is launched, it is time to
				 * finalise the operation.
				 */
				if (rc == MIO_DRV_OP_NEXT)
					pop->mp_retstate = MIO_OP_ONFLY;
				else {
					assert(rc == MIO_DRV_OP_FINAL);
					op_done = true;
				}
			} else
				op_done = true;
		} else if (pop->mp_retstate == MIO_OP_FAILED)
			op_done = true;

		if (op_done)
			nr_done++;
	}

	end = mio_now();
	if (timeout == MIO_TIME_NEVER && nr_done != nr_ops)
		goto again;
	else if (timeout != MIO_TIME_NEVER && end - start < timeout) {
		timeout -= end - start;
		goto again;
	}

	/* Return the number of operations done (completed or failed.)*/
	return nr_done;
}

void mio_op_callbacks_set(struct mio_op *op,
			  mio_callback cb_complete,
			  mio_callback cb_failed,
			  void *cb_data)
{
	if (op == NULL)
		return;
	op->mop_app_cbs.moc_cb_complete = cb_complete;
	op->mop_app_cbs.moc_cb_failed   = cb_failed;
	op->mop_app_cbs.moc_cb_data     = cb_data;
}

/* --------------------------------------------------------------- *
 *                     Object Access                               *
 * ----------------------------------------------------------------*/
int mio_obj_op_init(struct mio_op *op, struct mio_obj *obj,
		    enum mio_obj_opcode opcode)
{
	int rc = 0;

	if (op == NULL)
		return -EINVAL;
	rc = mio_instance_check();
	if (rc < 0)
		return rc;

	op->mop_opcode = opcode;
	op->mop_who.obj = obj;
	op->mop_op_ops = mio_instance->m_driver->md_op_ops;
	return 0;
}

static int obj_init(struct mio_obj *obj, const struct mio_obj_id *oid)
{
	int rc;

	if (oid == NULL || obj == NULL)
		return -EINVAL;
	rc = mio_instance_check();
	if (rc < 0)
		return rc;

	mio_mem_copy(obj->mo_id.moi_bytes,
		     (void *)oid->moi_bytes, MIO_OBJ_ID_LEN);
	obj->mo_drv_obj_ops = mio_instance->m_driver->md_obj_ops;
	/**
 	 * TODO: map to an attributes key-value set by a policy
 	 * such as object ID.
 	 */
	obj->mo_md_kvs = &mio_obj_attrs_kvs;
	mio_hint_map_init(&obj->mo_hints.mh_map, MIO_OBJ_HINT_NUM);
	return 0;
}

int mio_obj_open(const struct mio_obj_id *oid,
		 struct mio_obj *obj, struct mio_op *op)
{
	int rc;

	rc = obj_init(obj, oid)? :
	     mio_obj_op_init(op, obj, MIO_OBJ_OPEN)? :
	     obj->mo_drv_obj_ops->moo_open(obj, op);
	return rc;
}

void mio_obj_close(struct mio_obj *obj)
{
	if (obj == NULL)
		return;
	mio_hint_map_fini(&obj->mo_hints.mh_map);
	if (obj->mo_drv_obj_ops->moo_close != NULL)
		obj->mo_drv_obj_ops->moo_close(obj);
}

/*
 * Select pool by performance hint (GOLD, SILVER or BRONZE).
 * GOLD: choose the best performance pool.
 * SILVER: choose the second best performance pool.
 * BRONZE: choose the third best performance pool.
 */
static int obj_pool_select_by_perf_hint(struct mio_hints *hints, int *which)
{
	int rc = 0;
	uint64_t hint_value;
	int gold_pool_idx = 0;
	int silver_pool_idx = 1;
	int bronze_pool_idx = 2;
	int pool_idx;

	if (silver_pool_idx >= mio_pools.mps_nr_pools)
		silver_pool_idx = gold_pool_idx;
	if (bronze_pool_idx >= mio_pools.mps_nr_pools)
		bronze_pool_idx = silver_pool_idx;

	mio_hint_lookup(hints, MIO_HINT_OBJ_WHERE, &hint_value);
	if (hint_value == MIO_POOL_GOLD) {
		mio_log(MIO_DEBUG, "[obj_pool_select] GOLD\n");
		pool_idx = gold_pool_idx;
	} else if (hint_value == MIO_POOL_SILVER) {
		mio_log(MIO_DEBUG, "[obj_pool_select] SILVER\n");
		pool_idx = silver_pool_idx;
	} else if (hint_value == MIO_POOL_BRONZE) {
		mio_log(MIO_DEBUG, "[obj_pool_select] BRONZE\n");
		pool_idx = bronze_pool_idx;
	} else
		rc = -EINVAL;

	if (rc == 0)
		*which = pool_idx;

	return rc;
}

static int
obj_pool_select_by_hotness(struct mio_hints *hints, int *which)
{
	int rc;
	uint64_t hotness;

	rc = mio_hint_lookup(hints, MIO_HINT_OBJ_HOT_INDEX, &hotness);
	if (rc < 0)
		return rc;

	*which = mio_obj_hotness_to_pool_idx(hotness);
	return 0;
}

static int obj_pool_select(struct mio_hints *hints, int *which)
{
	if (mio_pools.mps_nr_pools <= 0)
		return -EINVAL;

	if (mio_hint_is_set(hints, MIO_HINT_OBJ_WHERE))
		return obj_pool_select_by_perf_hint(hints, which);
	else if (mio_hint_is_set(hints, MIO_HINT_OBJ_HOT_INDEX))
		return obj_pool_select_by_hotness(hints, which);
	else
		return -EINVAL;
}

int mio_obj_create(const struct mio_obj_id *oid,
                   const struct mio_pool_id *pool_id, struct mio_hints *hints,
                   struct mio_obj *obj, struct mio_op *op)
{
	int rc;
	int selected_pool_idx = 0;
	const struct mio_pool_id *selected_pool_id = NULL;

	if (pool_id == NULL) {
		if (hints == NULL)
			selected_pool_id = NULL;
		else {
			rc = obj_pool_select(hints, &selected_pool_idx);
			if (rc == 0)
				selected_pool_id =
					&mio_pools.mps_pools[selected_pool_idx].mp_id;
			else
				selected_pool_id = NULL;
		}

		/* Check if MIO sets default pool in configuration. */
		if (selected_pool_id == NULL &&
		    mio_conf_default_pool_has_set()) {
			selected_pool_idx = mio_pools.mps_default_pool_idx;
			selected_pool_id =
				&mio_pools.mps_pools[selected_pool_idx].mp_id;
		}

	} else
		selected_pool_id = pool_id;

	rc = obj_init(obj, oid)? :
	     mio_obj_op_init(op, obj, MIO_OBJ_CREATE)? :
	     obj->mo_drv_obj_ops->moo_create(selected_pool_id, obj, op);
	return rc;
}

int mio_obj_delete(const struct mio_obj_id *oid, struct mio_op *op)
{
	int rc;

	rc = mio_obj_op_init(op, NULL, MIO_OBJ_DELETE)? :
	     mio_instance->m_driver->md_obj_ops->moo_delete(oid, op);
	return rc;
}

static void
obj_stats_update(struct mio_obj *obj, bool is_write,
		 const struct mio_iovec *iov, int iovcnt)
{
	int i;
	uint64_t now; /* in nano-secconds. */
	struct mio_obj_stats *stats;

	stats = &obj->mo_attrs.moa_stats;
	now = mio_now();
	for (i = 0; i < iovcnt; i++) {
		if (is_write) {
			stats->mos_wcount++;
			stats->mos_wbytes += iov[i].miov_len;
			stats->mos_wtime = now;
		} else {
			stats->mos_rcount++;
			stats->mos_rbytes += iov[i].miov_len;
			stats->mos_rtime = now;
		}
	}
	obj->mo_attrs_updated = true;
	return;
}

int mio_obj_writev(struct mio_obj *obj,
                   const struct mio_iovec *iov,
                   int iovcnt, struct mio_op *op)
{
	int rc;

	if (obj == NULL || op == NULL)
		return -EINVAL;
	obj_stats_update(obj, true, iov, iovcnt);

	rc = mio_obj_op_init(op, obj, MIO_OBJ_WRITE)? :
	     obj->mo_drv_obj_ops->moo_writev(obj, iov, iovcnt, op);
	return rc;
}

int mio_obj_readv(struct mio_obj *obj,
                  const struct mio_iovec *iov,
                  int iovcnt, struct mio_op *op)
{
	int rc;

	if (obj == NULL || op == NULL)
		return -EINVAL;
	obj_stats_update(obj, false, iov, iovcnt);

	rc = mio_obj_op_init(op, obj, MIO_OBJ_READ)? :
	     obj->mo_drv_obj_ops->moo_readv(obj, iov, iovcnt, op);
	return rc;
}

int mio_obj_sync(struct mio_obj *obj, struct mio_op *op)
{

	int rc;

	if (obj == NULL || op == NULL)
		return -EINVAL;

	rc = mio_obj_op_init(op, obj, MIO_OBJ_SYNC)? :
	     obj->mo_drv_obj_ops->moo_sync(obj, op);
	return rc;
}

int mio_obj_size(struct mio_obj *obj, struct mio_op *op)
{
	int rc;

	if (obj == NULL || op == NULL)
		return -EINVAL;

	rc = mio_obj_op_init(op, obj, MIO_OBJ_ATTRS_GET)? :
	     obj->mo_drv_obj_ops->moo_size(obj, op);
	return rc;
}

int mio_obj_lock(struct mio_obj *obj)
{
	int rc;

	rc = mio_instance_check();
	if (rc < 0)
		return rc;
	if (obj == NULL)
		return -EINVAL;
	if (obj->mo_drv_obj_ops->moo_lock == NULL ||
	    obj->mo_drv_obj_ops->moo_unlock == NULL)
		return -EOPNOTSUPP;

	return obj->mo_drv_obj_ops->moo_lock(obj);
}

int mio_obj_unlock(struct mio_obj *obj)
{
	int rc;

	rc = mio_instance_check();
	if (rc < 0)
		return rc;
	if (obj == NULL)
		return -EINVAL;
	if (obj->mo_drv_obj_ops->moo_lock == NULL ||
	    obj->mo_drv_obj_ops->moo_unlock == NULL)
		return -EOPNOTSUPP;

	return obj->mo_drv_obj_ops->moo_unlock(obj);
}

/* --------------------------------------------------------------- *
 *                     Key-value Store                             *
 * ----------------------------------------------------------------*/
#define drv_kvs_ops (mio_instance->m_driver->md_kvs_ops)
static int kvs_driver_check()
{
	int rc;

	rc = mio_instance_check();
	if (rc < 0)
		return rc;

	if (drv_kvs_ops == NULL ||
	    drv_kvs_ops->mko_get == NULL ||
	    drv_kvs_ops->mko_put == NULL ||
	    drv_kvs_ops->mko_del == NULL ||
	    drv_kvs_ops->mko_create_set == NULL ||
	    drv_kvs_ops->mko_del_set == NULL)
		return -EOPNOTSUPP;

	return 0;
}

static int kvs_op_init(struct mio_op *op, struct mio_kvs_id *kid,
		       enum mio_kvs_opcode opcode)
{
	if (op == NULL)
		return -EINVAL;

	mio_memset(op, 0, sizeof op);
	op->mop_opcode = opcode;
	op->mop_who.kvs_id = kid;
	op->mop_op_ops = mio_instance->m_driver->md_op_ops;
	return 0;
}

int mio_kvs_pair_get(struct mio_kvs_id *kid,
                     int nr_kvps, struct mio_kv_pair *kvps,
                     int32_t *rcs, struct mio_op *op)
{
	int rc;

	rc = kvs_driver_check();
	if (rc < 0)
		return rc;
	if (kid == NULL || op == NULL)
		return -EINVAL;

	rc = kvs_op_init(op, kid, MIO_KVS_GET)? :
	     drv_kvs_ops->mko_get(kid, nr_kvps, kvps, rcs, op);
	return rc;
}

int mio_kvs_pair_put(struct mio_kvs_id *kid,
                     int nr_kvps, struct mio_kv_pair *kvps,
                     int32_t *rcs, struct mio_op *op)
{
	int rc;

	rc = kvs_driver_check();
	if (rc < 0)
		return rc;
	if (kid == NULL || op == NULL)
		return -EINVAL;

	rc = kvs_op_init(op, kid, MIO_KVS_PUT)? :
	     drv_kvs_ops->mko_put(kid, nr_kvps, kvps, rcs, op);
	return rc;
}

int mio_kvs_pair_del(struct mio_kvs_id *kid,
                     int nr_kvps, struct mio_kv_pair *kvps,
                     int32_t *rcs, struct mio_op *op)
{
	int rc;

	rc = kvs_driver_check();
	if (rc < 0)
		return rc;
	if (kid == NULL || op == NULL)
		return -EINVAL;

	rc = kvs_op_init(op, kid, MIO_KVS_DEL);
	     drv_kvs_ops->mko_del(kid, nr_kvps, kvps, rcs, op);
	return rc;
}

int mio_kvs_create_set(struct mio_kvs_id *kid, struct mio_op *op)
{
	int rc;

	rc = kvs_driver_check();
	if (rc < 0)
		return rc;
	if (kid == NULL || op == NULL)
		return -EINVAL;

	rc = kvs_op_init(op, kid, MIO_KVS_CREATE_SET)? :
	     drv_kvs_ops->mko_create_set(kid, op);
	return rc;
}

int mio_kvs_del_set(struct mio_kvs_id *kid, struct mio_op *op)
{
	int rc;

	rc = kvs_driver_check();
	if (rc < 0)
		return rc;
	if (kid == NULL || op == NULL)
		return -EINVAL;

	rc = kvs_op_init(op, kid, MIO_KVS_DELETE_SET)? :
	     drv_kvs_ops->mko_del_set(kid, op);
	return rc;
}

/* --------------------------------------------------------------- *
 *                     Composite Layout                            *
 * ----------------------------------------------------------------*/
#define drv_comp_obj_ops (mio_instance->m_driver->md_comp_obj_ops)

int mio_composite_obj_create(const struct mio_obj_id *oid,
			     struct mio_obj *obj, struct mio_op *op)
{
	int rc;

	rc = obj_init(obj, oid)? :
	     mio_obj_op_init(op, obj, MIO_COMP_OBJ_CREATE)? :
	     drv_comp_obj_ops->mcoo_create(obj, op);
	return rc;
}

int mio_composite_obj_del(const struct mio_obj_id *oid, struct mio_op *op)
{
	int rc;

	if (oid == NULL)
		return -EINVAL;
	rc = mio_obj_op_init(op, NULL, MIO_COMP_OBJ_DELETE)? :
	     drv_comp_obj_ops->mcoo_del(oid, op);
	return rc;
}

int mio_composite_obj_add_layers(struct mio_obj *obj, int nr_layers,
				 struct mio_comp_obj_layer *layers,
				 struct mio_op *op)
{
	int rc;

	if (obj == NULL)
		return -EINVAL;
	rc = mio_obj_op_init(op, obj, MIO_COMP_OBJ_ADD_LAYERS)? :
	     drv_comp_obj_ops->mcoo_add_layers(obj, nr_layers, layers, op);
	return rc;
}

int mio_composite_obj_del_layers(struct mio_obj *obj,
				 int nr_layers_to_del,
				 struct mio_comp_obj_layer *layers_to_del,
				 struct mio_op *op)
{
	int rc;

	if (obj == NULL)
		return -EINVAL;
	rc = mio_obj_op_init(op, obj, MIO_COMP_OBJ_DEL_LAYERS)? :
	     drv_comp_obj_ops->mcoo_del_layers(
			obj, nr_layers_to_del, layers_to_del, op);
	return rc;
}

int mio_composite_obj_list_layers(struct mio_obj *obj,
                                  struct mio_comp_obj_layout *ret_layout,
                   		  struct mio_op *op)
{
	int rc;

	if (obj == NULL)
		return -EINVAL;
	rc = mio_obj_op_init(op, obj, MIO_COMP_OBJ_LIST_LAYERS)? :
	     drv_comp_obj_ops->mcoo_list_layers(obj, ret_layout, op);
	return rc;
}

int mio_composite_obj_add_extents(struct mio_obj *obj,
				  struct mio_obj_id *layer_id,
				  int nr_exts, struct mio_obj_ext *exts,
				  struct mio_op *op)
{
	int rc;

	if (obj == NULL)
		return -EINVAL;
	rc = mio_obj_op_init(op, obj, MIO_COMP_OBJ_ADD_EXTENTS)? :
	     drv_comp_obj_ops->mcoo_add_extents(
			obj, layer_id, nr_exts, exts, op);
	return rc;
}

int mio_composite_obj_del_extents(struct mio_obj *obj,
				  struct mio_obj_id *layer_id,
				  int nr_exts, struct mio_obj_ext *exts,
				  struct mio_op *op)
{
	int rc;

	if (obj == NULL)
		return -EINVAL;
	rc = mio_obj_op_init(op, obj, MIO_COMP_OBJ_DEL_EXTENTS)? :
	     drv_comp_obj_ops->mcoo_del_extents(
			obj, layer_id, nr_exts, exts, op);
	return rc;
}

int
mio_composite_obj_get_extents(struct mio_obj *obj,
			      struct mio_obj_id *layer_id, off_t offset,
			      int nr_exts, struct mio_obj_ext *exts,
			      int *nr_ret_exts, struct mio_op *op)
{
	int rc;

	if (obj == NULL)
		return -EINVAL;
	rc = mio_obj_op_init(op, obj, MIO_COMP_OBJ_GET_EXTENTS)? :
	     drv_comp_obj_ops->mcoo_get_extents(
			obj, layer_id, offset, nr_exts, exts, nr_ret_exts, op);
	return rc;
}

/* --------------------------------------------------------------- *
 *                           MIO pool                              *
 * ----------------------------------------------------------------*/

static struct mio_pool* mio_pool_lookup(const struct mio_pool_id *pool_id)
{
	int i;
	struct mio_pool *pool = NULL;

	for (i = 0; i < mio_pools.mps_nr_pools; i++) {
		pool = mio_pools.mps_pools + i;
		if (pool->mp_id.mpi_hi == pool_id->mpi_hi &&
		    pool->mp_id.mpi_lo == pool_id->mpi_lo)
			break;
	}

	if (i == mio_pools.mps_nr_pools)
		return NULL;
	else
		return pool;
}

int mio_pool_get(const struct mio_pool_id *pool_id, struct mio_pool **pool)
{
	int rc = 0;
	struct mio_pool *out_pool = NULL;
	struct mio_pool *found_pool = NULL;

	if (pool_id == NULL) {
		mio_log(MIO_ERROR, "Pool id is not set!");
		return -EINVAL;
	}
	if (pool == NULL) {
		mio_log(MIO_ERROR, "Memory for returned pool is NULL!\n");
		return -EINVAL;
	}
	*pool = NULL;

	rc = mio_instance_check();
	if (rc < 0)
		return rc;

	found_pool = mio_pool_lookup(pool_id);
	if (found_pool == NULL)
		return -EINVAL;

	out_pool = mio_mem_alloc(sizeof *out_pool);
	if (out_pool == NULL)
		return -ENOMEM;
	
	/* Update pool information from driver. */
	rc = mio_instance->m_driver->md_pool_ops->mpo_get(pool_id, found_pool);
	if (rc != 0) {
		mio_mem_free(out_pool);
		return rc;
	}

	/* Copy to the out_pool. */
	mio_mem_copy(out_pool, found_pool, sizeof(*found_pool));
	*pool = out_pool;
	return 0;

}

int mio_pool_get_all(struct mio_pools **pools)
{
	int i;
	int rc = 0;
	struct mio_pool *pool;
	struct mio_pools *out_pools;

	if (pools == NULL) {
		mio_log(MIO_ERROR, "Memory for returned pools is NULL!\n");
		return -EINVAL;

	}

	rc = mio_instance_check();
	if (rc < 0)
		return rc;

	out_pools = mio_mem_alloc(sizeof(struct mio_pools));
	if (out_pools == NULL)
		return -ENOMEM;
	out_pools->mps_pools =
		mio_mem_alloc(mio_pools.mps_nr_pools * sizeof(struct mio_pool));
	if (out_pools->mps_pools == NULL) {
		mio_mem_free(out_pools);
		return -ENOMEM;
	}

	for (i = 0; i < mio_pools.mps_nr_pools; i++) {
		pool = mio_pools.mps_pools + i;
		rc = mio_instance->m_driver->md_pool_ops->mpo_get(
			&pool->mp_id, pool);
		if (rc < 0)
			break;

		mio_mem_copy(out_pools->mps_pools + i, pool, sizeof *pool);
	}
	if (rc < 0) {
		mio_mem_free(out_pools->mps_pools);
		mio_mem_free(out_pools);
		*pools = NULL;
	} else {
		out_pools->mps_nr_pools = mio_pools.mps_nr_pools;
		*pools = out_pools;
	}

	return rc;
}

int mio_obj_pool_id(const struct mio_obj *obj, struct mio_pool_id *pool_id)
{
	int rc = -EINVAL;

	rc = (obj->mo_drv_obj_ops->moo_pool_id == NULL)?
	     -EOPNOTSUPP :
	     obj->mo_drv_obj_ops->moo_pool_id(obj, pool_id);
	return rc;
}

bool mio_obj_pool_id_cmp(struct mio_pool_id *pool_id1,
			 struct mio_pool_id *pool_id2)
{
	if (pool_id1->mpi_hi == pool_id2->mpi_hi &&
	    pool_id1->mpi_lo == pool_id2->mpi_lo)
		return true;
	else
		return false;
}

/* --------------------------------------------------------------- *
 *                    MIO initialisation/finalisation              *
 * ----------------------------------------------------------------*/
int mio_init(const char *yaml_conf)
{
	int rc;

	mio_instance = mio_mem_alloc(sizeof *mio_instance);
	if (mio_instance == NULL)
		return -ENOMEM;

	mio_drivers_register();

	rc = mio_conf_init(yaml_conf);
	if (rc < 0) {
		fprintf(stderr, "Failed in parsing configuration file\n");
		mio_mem_free(mio_instance);
		mio_instance = NULL;
		return rc;
	}

	rc = mio_instance->m_driver->md_sys_ops->mdo_user_perm(mio_instance);
	if (rc < 0) {
		fprintf(stderr, "User's permission denied!\n");
		goto error;
	}

	rc = mio_log_init(mio_instance->m_log_level, mio_instance->m_log_file);
	if (rc < 0) {
		fprintf(stderr, "Failed to initialise logging sub-system. \n");
		goto error;
	}

	rc = mio_instance->m_driver->md_sys_ops->mdo_init(mio_instance);
	if (rc < 0) {
		mio_log(MIO_ERROR, "Initialising MIO driver failed!\n");
		goto error;
	}

	mio_hints_init(&mio_sys_hints);

	return rc;

error:
	mio_mem_free(mio_instance);
	mio_instance = NULL;
	mio_conf_fini();
	return rc;
}

void mio_fini()
{
	if (mio_instance == NULL)
		return;

	mio_instance->m_driver->md_sys_ops->mdo_fini();
	mio_mem_free(mio_instance);
	mio_conf_fini();
}

int mio_thread_init(struct mio_thread *thread)
{
	int rc = 0;

	rc = mio_instance_check();
	if (rc < 0)
		return 0;
	if (mio_instance->m_driver->md_sys_ops->mdo_thread_init == NULL)
		return -EOPNOTSUPP;

	return mio_instance->m_driver->md_sys_ops->mdo_thread_init(thread);
}

void mio_thread_fini(struct mio_thread *thread)
{
	if (mio_instance_check())
		return;
	if (mio_instance->m_driver->md_sys_ops->mdo_thread_fini == NULL)
		return;

	mio_instance->m_driver->md_sys_ops->mdo_thread_fini(thread);
}
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
 *
 */
