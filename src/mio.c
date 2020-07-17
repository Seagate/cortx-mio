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
struct mio *mio_instance;

/* --------------------------------------------------------------- *
 *                     Operation                                   *
 * ----------------------------------------------------------------*/

void mio_op_init(struct mio_op *op)
{
	assert(op != NULL);

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

	assert(ops != NULL);
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
	assert(op != NULL);
	op->mop_app_cbs.moc_cb_complete = cb_complete;
	op->mop_app_cbs.moc_cb_failed   = cb_failed;
	op->mop_app_cbs.moc_cb_data     = cb_data;
}

/* --------------------------------------------------------------- *
 *                     Object Access                               *
 * ----------------------------------------------------------------*/
void mio_obj_op_init(struct mio_op *op, struct mio_obj *obj,
			enum mio_obj_opcode opcode)
{
	assert(op != NULL);
	op->mop_opcode = opcode;
	op->mop_who.obj = obj;
	op->mop_op_ops = mio_instance->m_driver->md_op_ops;
}

static void obj_init(struct mio_obj *obj, const struct mio_obj_id *oid)
{
	assert(oid != NULL && obj != NULL);
	mio_mem_copy(obj->mo_id.moi_bytes,
		     (void *)oid->moi_bytes, MIO_OBJ_ID_LEN);
	obj->mo_drv_obj_ops = mio_instance->m_driver->md_obj_ops;
	/**
 	 * TODO: map to an attributes key-value set by a policy
 	 * such as object ID.
 	 */
	obj->mo_md_kvs = &mio_obj_attrs_kvs; 
	mio_hint_map_init(&obj->mo_hints.mh_map, MIO_OBJ_HINT_NUM); 
}

int mio_obj_open(const struct mio_obj_id *oid,
		 struct mio_obj *obj, struct mio_op *op)
{
	assert(oid != NULL && obj != NULL && op != NULL);
	obj_init(obj, oid);
	mio_obj_op_init(op, obj, MIO_OBJ_OPEN);
	return obj->mo_drv_obj_ops->moo_open(obj, op);
}

void mio_obj_close(struct mio_obj *obj)
{
	assert(obj != NULL);
	mio_hint_map_fini(&obj->mo_hints.mh_map); 
	if (obj->mo_drv_obj_ops->moo_close != NULL)
		obj->mo_drv_obj_ops->moo_close(obj);
}

int mio_obj_create(const struct mio_obj_id *oid,
                   const struct mio_pool *pool_id,
                   struct mio_obj *obj, struct mio_op *op)
{
	assert(oid != NULL && obj != NULL && op != NULL);
	obj_init(obj, oid);
	mio_obj_op_init(op, obj, MIO_OBJ_CREATE);
	return obj->mo_drv_obj_ops->moo_create(pool_id, obj, op);
}

int mio_obj_delete(const struct mio_obj_id *oid, struct mio_op *op)
{
	assert(op != NULL);
	mio_obj_op_init(op, NULL, MIO_OBJ_DELETE);
	return mio_instance->m_driver->md_obj_ops->moo_delete(oid, op);
}

int mio_obj_writev(struct mio_obj *obj,
                   const struct mio_iovec *iov,
                   int iovcnt, struct mio_op *op)
{
	assert(obj != NULL && op != NULL);
	mio_obj_op_init(op, obj, MIO_OBJ_WRITE);
	return obj->mo_drv_obj_ops->moo_writev(obj, iov, iovcnt, op);
}

int mio_obj_readv(struct mio_obj *obj,
                  const struct mio_iovec *iov,
                  int iovcnt, struct mio_op *op)
{
	assert(obj != NULL && op != NULL);
	mio_obj_op_init(op, obj, MIO_OBJ_READ);
	return obj->mo_drv_obj_ops->moo_readv(obj, iov, iovcnt, op);
}

int mio_obj_sync(struct mio_obj *obj, struct mio_op *op)
{
	assert(obj != NULL && op != NULL);
	mio_obj_op_init(op, obj, MIO_OBJ_SYNC);
	return obj->mo_drv_obj_ops->moo_sync(obj, op);
}

int mio_obj_size(struct mio_obj *obj, struct mio_op *op)
{
	assert(obj != NULL && op != NULL);
	mio_obj_op_init(op, obj, MIO_OBJ_ATTRS_GET);
	return obj->mo_drv_obj_ops->moo_size(obj, op);
}

/* --------------------------------------------------------------- *
 *                     Key-value Store                             *
 * ----------------------------------------------------------------*/
static void kvs_op_init(struct mio_op *op, struct mio_kvs_id *kid,
			enum mio_kvs_opcode opcode)
{
	assert(op != NULL);
	mio_memset(op, 0, sizeof op);
	op->mop_opcode = opcode;
	op->mop_who.kvs_id = kid;
	op->mop_op_ops = mio_instance->m_driver->md_op_ops;
}

#define drv_kvs_ops (mio_instance->m_driver->md_kvs_ops)
int mio_kvs_pair_get(struct mio_kvs_id *kid,
                     int nr_kvps, struct mio_kv_pair *kvps,
                     int32_t *rcs, struct mio_op *op)
{
	assert(kid != NULL && op != NULL);
	if (drv_kvs_ops->mko_get == NULL)
		return -EOPNOTSUPP;

	kvs_op_init(op, kid, MIO_KVS_GET);
	return drv_kvs_ops->mko_get(kid, nr_kvps, kvps, rcs, op);
}

int mio_kvs_pair_put(struct mio_kvs_id *kid,
                     int nr_kvps, struct mio_kv_pair *kvps,
                     int32_t *rcs, struct mio_op *op)
{
	assert(kid != NULL && op != NULL);
	if (drv_kvs_ops->mko_put == NULL)
		return -EOPNOTSUPP;

	kvs_op_init(op, kid, MIO_KVS_PUT);
	return drv_kvs_ops->mko_put(kid, nr_kvps, kvps, rcs, op);
}

int mio_kvs_pair_del(struct mio_kvs_id *kid,
                     int nr_kvps, struct mio_kv_pair *kvps,
                     int32_t *rcs, struct mio_op *op) 
{
	assert(kid != NULL && op != NULL);
	if (drv_kvs_ops->mko_del == NULL)
		return -EOPNOTSUPP;

	kvs_op_init(op, kid, MIO_KVS_DEL);
	return drv_kvs_ops->mko_del(kid, nr_kvps, kvps, rcs, op);
}

int mio_kvs_create_set(struct mio_kvs_id *kid, struct mio_op *op)
{
	assert(kid != NULL && op != NULL);
	if (drv_kvs_ops->mko_create_set == NULL)
		return -EOPNOTSUPP;

	kvs_op_init(op, kid, MIO_KVS_CREATE_SET);
	return drv_kvs_ops->mko_create_set(kid, op);
}

int mio_kvs_del_set(struct mio_kvs_id *kid, struct mio_op *op)
{
	assert(kid != NULL && op != NULL);
	if (drv_kvs_ops->mko_del_set == NULL)
		return -EOPNOTSUPP;

	kvs_op_init(op, kid, MIO_KVS_DELETE_SET);
	return drv_kvs_ops->mko_del_set(kid, op);
}

/* --------------------------------------------------------------- *
 *                     Composite Layout                            *
 * ----------------------------------------------------------------*/
#define drv_comp_obj_ops (mio_instance->m_driver->md_comp_obj_ops)

int mio_composite_obj_create(const struct mio_obj_id *oid,
			     struct mio_obj *obj, struct mio_op *op)
{
	assert(obj != NULL && op != NULL);
	obj_init(obj, oid);
	mio_obj_op_init(op, obj, MIO_COMP_OBJ_CREATE);
	return drv_comp_obj_ops->mcoo_create(obj, op);
}

int mio_composite_obj_del(const struct mio_obj_id *oid, struct mio_op *op)
{
	assert(op != NULL);
	mio_obj_op_init(op, NULL, MIO_COMP_OBJ_DELETE);
	return drv_comp_obj_ops->mcoo_del(oid, op);
}

int mio_composite_obj_add_layers(struct mio_obj *obj, int nr_layers,
				 struct mio_comp_obj_layer *layers,
				 struct mio_op *op)
{
	assert(obj != NULL && op != NULL);
	mio_obj_op_init(op, obj, MIO_COMP_OBJ_ADD_LAYERS);
	return drv_comp_obj_ops->mcoo_add_layers(obj, nr_layers, layers, op);
}

int mio_composite_obj_del_layers(struct mio_obj *obj,
				 int nr_layers_to_del,
				 struct mio_comp_obj_layer *layers_to_del,
				 struct mio_op *op)
{
	assert(obj != NULL && op != NULL);
	mio_obj_op_init(op, obj, MIO_COMP_OBJ_DEL_LAYERS);
	return drv_comp_obj_ops->mcoo_del_layers(
			obj, nr_layers_to_del, layers_to_del, op);
}

int mio_composite_obj_list_layers(struct mio_obj *obj,
                                  struct mio_comp_obj_layout *ret_layout,
                   		  struct mio_op *op)
{
	assert(obj != NULL && op != NULL);
	mio_obj_op_init(op, obj, MIO_COMP_OBJ_LIST_LAYERS);
	return drv_comp_obj_ops->mcoo_list_layers(obj, ret_layout, op);
}

int mio_composite_obj_add_extents(struct mio_obj *obj,
				  struct mio_obj_id *layer_id,
				  int nr_exts, struct mio_obj_ext *exts,
				  struct mio_op *op)
{
	assert(obj != NULL && op != NULL);
	mio_obj_op_init(op, obj, MIO_COMP_OBJ_ADD_EXTENTS);
	return drv_comp_obj_ops->mcoo_add_extents(
				obj, layer_id, nr_exts, exts, op);
}

int mio_composite_obj_del_extents(struct mio_obj *obj,
				  struct mio_obj_id *layer_id,
				  int nr_exts, struct mio_obj_ext *exts,
				  struct mio_op *op)
{
	assert(obj != NULL && op != NULL);
	mio_obj_op_init(op, obj, MIO_COMP_OBJ_DEL_EXTENTS);
	return drv_comp_obj_ops->mcoo_del_extents(
				obj, layer_id, nr_exts, exts, op);
}

int
mio_composite_obj_get_extents(struct mio_obj *obj,
			      struct mio_obj_id *layer_id, off_t offset,
			      int nr_exts, struct mio_obj_ext *exts,
			      int *nr_ret_exts, struct mio_op *op)
{
	assert(obj != NULL && op != NULL);
	mio_obj_op_init(op, obj, MIO_COMP_OBJ_GET_EXTENTS);
	return drv_comp_obj_ops->mcoo_get_extents(
			obj, layer_id, offset, nr_exts, exts, nr_ret_exts, op);
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
		return rc;
	}

	rc = mio_instance->m_driver->md_sys_ops->mdo_user_perm(mio_instance);
	if (rc < 0) {
		fprintf(stderr, "User's permission denied!\n");
		mio_conf_fini();
		return rc;
	}

	rc = mio_log_init(mio_instance->m_log_level, mio_instance->m_log_file);
	if (rc < 0) {
		fprintf(stderr, "Failed to initialise logging sub-system. \n");
		return rc;
	}

	rc = mio_instance->m_driver->md_sys_ops->mdo_init(mio_instance);
	if (rc < 0) {
		mio_log(MIO_ERROR, "Initialising MIO driver failed!\n");
		mio_conf_fini();
	}
	return rc;
}

void mio_fini()
{
	mio_instance->m_driver->md_sys_ops->mdo_fini();
	mio_mem_free(mio_instance);
	mio_conf_fini();
}

int mio_thread_init(struct mio_thread *thread)
{
	return mio_instance->m_driver->md_sys_ops->mdo_thread_init(thread);
}

void mio_thread_fini(struct mio_thread *thread)
{
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
 */
