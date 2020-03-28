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
#include "mio.h"
#include "mio_internal.h"
#include "driver_clovis.h"

struct m0_clovis *mio_clovis_instance;
struct m0_clovis_container mio_clovis_container;
struct m0_clovis_config mio_clovis_conf;
struct mio_mero_config *mio_clovis_inst_confs;

struct m0_uint128 mio_clovis_obj_md_kvs_id;
struct m0_fid mio_clovis_obj_md_kvs_fid = M0_FID_TINIT('x', 0, 0x10);

#define MIO_CLOVIS_OP(op) \
	((struct m0_clovis_op *)op->mop_drv_op_chain.mdoc_head->mdo_op)

/**
 * pp is short for Post-Process to avoid confusion of cb (callback).
 */
static int clovis_obj_attrs_query_free_pp(struct mio_op *op);
static int clovis_obj_attrs_get_pp(struct mio_op *op);
static int clovis_obj_attrs_query(int opcode, struct mio_obj *obj,
				  mio_driver_op_postprocess op_pp,
				  struct mio_op *op);

/**
 * Some helper functions.
 */
static void clovis_bufvec_free(struct m0_bufvec *bv)
{
        if (bv == NULL)
                return;

        m0_free(bv->ov_buf);
        m0_free(bv->ov_vec.v_count);
        m0_free(bv);
}

struct m0_bufvec* mio__clovis_bufvec_alloc(int nr)
{
        struct m0_bufvec *bv;

        bv = m0_alloc(sizeof *bv);
        if (bv == NULL)
                return NULL;

        bv->ov_vec.v_nr = nr;
        M0_ALLOC_ARR(bv->ov_vec.v_count, nr);
        if (bv->ov_vec.v_count == NULL)
                goto error;

        M0_ALLOC_ARR(bv->ov_buf, nr);
        if (bv->ov_buf == NULL)
                goto error;

        return bv;

error:
        m0_bufvec_free(bv);
        return NULL;
}

static int clovis_create_obj_attrs_kvs()
{
        int rc;
	struct m0_clovis_op *cops[1] = {NULL};
	struct m0_clovis_idx *idx;

	idx = mio_mem_alloc(sizeof *idx);
	if (idx == NULL)
		return -ENOMEM;

	mio_clovis_obj_md_kvs_id.u_hi = mio_clovis_obj_md_kvs_fid.f_container;
	mio_clovis_obj_md_kvs_id.u_lo = mio_clovis_obj_md_kvs_fid.f_key;
	m0_clovis_idx_init(idx, &mio_clovis_container.co_realm,
			   &mio_clovis_obj_md_kvs_id);

	/* Check if object's attrs key-value store exists. */
        m0_clovis_idx_op(idx, M0_CLOVIS_IC_LOOKUP,
			 NULL, NULL, NULL, 0, &cops[0]);
        m0_clovis_op_launch(cops, 1);
	rc = m0_clovis_op_wait(cops[0],
			       M0_BITS(M0_CLOVIS_OS_FAILED,
				       M0_CLOVIS_OS_STABLE),
			       M0_TIME_NEVER);
	rc = rc? : m0_clovis_rc(cops[0]);
	m0_clovis_op_fini(cops[0]);
	m0_clovis_op_free(cops[0]);

	/*
 	 * Check returned value (rc): 	
 	 *   - 0: LOOKUP successes and the index has already existed.
 	 *   - -ENOENT: the index doesn't exist yet.
 	 *   - other error code: LOOKUP op failed for some reason.
 	 */
	if (rc != -ENOENT)
		goto exit;

	/* Create the attrs kvs. */
	cops[0] = NULL;
        rc = m0_clovis_entity_create(NULL, &idx->in_entity, &cops[0]);
	if (rc < 0)
		goto exit;
        m0_clovis_op_launch(cops, 1);
	rc = m0_clovis_op_wait(cops[0],
			       M0_BITS(M0_CLOVIS_OS_FAILED,
				       M0_CLOVIS_OS_STABLE),
			       M0_TIME_NEVER);
	rc = rc? : m0_clovis_rc(cops[0]);
	m0_clovis_op_fini(cops[0]);
	m0_clovis_op_free(cops[0]);

exit:
	if (rc == 0)
		mio_obj_attrs_kvs.mk_drv_kvs = idx;
	else {
		m0_clovis_idx_fini(idx);
		mio_mem_free(idx);
	}
	return rc;
}

/**
 * Initialise and finalise clovis instance.
 */
int mio_clovis_init(struct mio *mio_inst) 
{
	int rc;
	struct mio_mero_config *drv;
	struct m0_idx_dix_config dix_conf;

	drv = (struct mio_mero_config *)mio_inst->m_driver_confs;
	mio_clovis_inst_confs = drv;

	/* Set clovis configuration parameters. */
	mio_clovis_conf.cc_is_oostore            = drv->mc_is_oostore;
	mio_clovis_conf.cc_is_read_verify        = drv->mc_is_read_verify;
	mio_clovis_conf.cc_local_addr            = drv->mc_clovis_local_addr;
	mio_clovis_conf.cc_ha_addr               = drv->mc_ha_addr;
	mio_clovis_conf.cc_profile               = drv->mc_profile;
	mio_clovis_conf.cc_process_fid           = drv->mc_process_fid;
	mio_clovis_conf.cc_tm_recv_queue_min_len = drv->mc_tm_recv_queue_min_len;
	mio_clovis_conf.cc_max_rpc_msg_size      = drv->mc_max_rpc_msg_size;

	mio_clovis_conf.cc_layout_id =
		m0_clovis_obj_unit_size_to_layout_id(drv->mc_unit_size);

	mio_clovis_conf.cc_idx_service_id   = M0_CLOVIS_IDX_DIX;
	dix_conf.kc_create_meta = false;
	mio_clovis_conf.cc_idx_service_conf = &dix_conf;

	/* Initial clovis instance. */
	rc = m0_clovis_init(&mio_clovis_instance, &mio_clovis_conf, true);
	if (rc != 0)
		return rc;

	/* Initial a container. */
	m0_clovis_container_init(&mio_clovis_container, NULL,
				 &M0_CLOVIS_UBER_REALM,
				 mio_clovis_instance);
	rc = mio_clovis_container.co_realm.re_entity.en_sm.sm_rc;
	if (rc != 0) {
		mio_log(MIO_ERROR, "Failed to open Clovis's uber realm!\n");
		goto error;
	}

	/* Create object attrs kvs if it doesn't exist. */
 	rc = clovis_create_obj_attrs_kvs();
	if (rc != 0) {
		mio_log(MIO_ERROR, "Failed to create attrs key-value set!\n");
		goto error;
	}
	return 0;

error:	
	m0_clovis_fini(mio_clovis_instance, true);
	return rc;
}

static void mio_clovis_fini()
{
	m0_clovis_idx_fini(
		(struct m0_clovis_idx *)mio_obj_attrs_kvs.mk_drv_kvs);
	m0_clovis_fini(mio_clovis_instance, true);
	mio_clovis_instance = NULL;
}

static int mio_clovis_thread_init(struct mio_thread *thread)
{
	struct m0_thread *mthread;

	mthread = mio_mem_alloc(sizeof(*mthread));
	if (mthread == NULL)
		return -ENOMEM;
	memset(mthread, 0, sizeof(struct m0_thread));
	m0_thread_adopt(mthread, mio_clovis_instance->m0c_mero);
	thread->mt_drv_thread = mthread;
	return 0;
}

static void mio_clovis_thread_fini(struct mio_thread *thread)
{
	m0_thread_shun();
	mio_mem_free(thread->mt_drv_thread);
}

static struct mio_driver_sys_ops mio_clovis_sys_ops = {
        .mdo_init = mio_clovis_init,
        .mdo_fini = mio_clovis_fini,
        .mdo_thread_init = mio_clovis_thread_init,
        .mdo_thread_fini = mio_clovis_thread_fini
};

static void mio_clovis_op_fini(struct mio_op *mop)
{
	struct m0_clovis_op *cop; 
	struct mio_driver_op *dop;

	dop = mop->mop_drv_op_chain.mdoc_head;
	while(dop != NULL) {
		mop->mop_drv_op_chain.mdoc_head = dop->mdo_next;
		dop->mdo_next = NULL;

		cop = (struct m0_clovis_op *)dop->mdo_op;
		m0_clovis_op_fini(cop);
		m0_clovis_op_free(cop);
		mio_mem_free(dop);

		dop = mop->mop_drv_op_chain.mdoc_head;
	}
}

static int
mio_clovis_op_wait(struct mio_op *mop, uint64_t timeout, int *retstate)
{
	int rc;
	struct m0_clovis_op *cop; 

	cop = MIO_CLOVIS_OP(mop);
	rc = m0_clovis_op_wait(cop, M0_BITS(M0_CLOVIS_OS_STABLE,
					    M0_CLOVIS_OS_FAILED),
			       timeout);
	/*
	 * Check returned value (rc) from m0_clovis_op_wait:
	 *   - errors (rc < 0), treat timeout error differently. 
	 *   - rc == 0, the operation is completed, then check the
	 *     operation's rc value to see if the op is successful or failed.
 	 */
	if (rc == 0) {
		if (m0_clovis_rc(cop) < 0) {
			*retstate = MIO_OP_FAILED;
			rc = m0_clovis_rc(cop);
		} else
			*retstate = MIO_OP_COMPLETED;
	} else if (rc == -ETIMEDOUT)
		*retstate = MIO_OP_ONFLY;
	else
		*retstate = MIO_OP_FAILED;
	return rc;
}

/**
 * The callback functions defined for MIO operation have different
 * arguments with the ones of drivers', such as Clovis. A jumper
 * function here is used to call the callback functions set by MIO
 * applications.
 *
 * The callback functions shown below give examples on how to relay
 * control to callbacks set by apps.
 *
 * This looks a bit ugly, is there any better solution?
 */
static void clovis_op_cb_complete(struct m0_clovis_op *cop)
{
	int rc;
	struct mio_op *mop;

	mop = (struct mio_op *)cop->op_datum;
	if (!mop->mop_drv_op_chain.mdoc_head->mdo_post_proc)
		goto app_cb;

	rc = mop->mop_drv_op_chain.mdoc_head->mdo_post_proc(mop);
	if (rc == MIO_DRV_OP_NEXT)
		return;

app_cb:
	mio_driver_op_invoke_real_cb(mop, 0);
}

static void clovis_op_cb_failed(struct m0_clovis_op *cop)
{
	struct mio_op *mop;

	mop = (struct mio_op *)cop->op_datum;
	mio_driver_op_invoke_real_cb(mop, m0_clovis_rc(cop));
}

static struct m0_clovis_op_ops clovis_op_cbs;
static int
mio_clovis_op_set_cbs(struct mio_op *mop)

{
	struct m0_clovis_op *cop;

	assert(mop != NULL);

	clovis_op_cbs.oop_executed = NULL;
	clovis_op_cbs.oop_stable = clovis_op_cb_complete;
	clovis_op_cbs.oop_failed = clovis_op_cb_failed;
	cop = MIO_CLOVIS_OP(mop);
	cop->op_datum = (void *)mop;
	m0_clovis_op_setup(cop, &clovis_op_cbs, 0);

	return 0;
}

static struct mio_op_ops mio_clovis_op_ops = {
	.mopo_fini    = mio_clovis_op_fini,
	.mopo_wait    = mio_clovis_op_wait,
	.mopo_set_cbs = mio_clovis_op_set_cbs
};

void mio__uint128_to_obj_id(struct m0_uint128 *uint128,
			    struct mio_obj_id *oid)
{
	uint64_t hi = uint128->u_hi; 
	uint64_t lo = uint128->u_lo;
 
	hi = mio_byteorder_cpu_to_be64(hi);
	lo = mio_byteorder_cpu_to_be64(lo);

	mio_mem_copy(&oid->moi_bytes[0], &hi, sizeof(hi));
	mio_mem_copy(&oid->moi_bytes[8], &lo, sizeof(lo));
}

void mio__obj_id_to_uint128(const struct mio_obj_id *oid,
			    struct m0_uint128 *uint128)
{
	uint64_t *hi = (uint64_t *)&oid->moi_bytes[0];
	uint64_t *lo = (uint64_t *)&oid->moi_bytes[8];
 
	uint128->u_hi = mio_byteorder_be64_to_cpu(*hi);
	uint128->u_lo = mio_byteorder_be64_to_cpu(*lo);
}

static int clovis_obj_open_pp(struct mio_op *op)
{
	int rc;
	struct mio_obj *obj = op->mop_who.obj;
	struct m0_clovis_op *cop;

	cop = MIO_CLOVIS_OP(op);
	rc = m0_clovis_rc(cop);
	if (rc < 0)
		return rc;

	/* Launch a new op to get object attributes. */
	rc = clovis_obj_attrs_query(M0_CLOVIS_IC_GET, obj,
			            clovis_obj_attrs_get_pp, op);
	if (rc < 0)
		return rc;
	else
		return MIO_DRV_OP_NEXT;
}

static int mio_clovis_obj_open(struct mio_obj *obj, struct mio_op *op)
{
	int rc;
	struct m0_uint128 id128;
	struct m0_clovis_obj *cobj;
	struct m0_clovis_op *cops[1] = {NULL};

	cobj = mio_mem_alloc(sizeof *cobj);
	if (cobj == NULL)
		return -ENOMEM;

	mio__obj_id_to_uint128(&obj->mo_id, &id128);
	m0_clovis_obj_init(cobj, &mio_clovis_container.co_realm, &id128,
			   mio_clovis_inst_confs->mc_default_layout_id);
	rc = m0_clovis_entity_open(&cobj->ob_entity, &cops[0]);
	if (rc != 0)
		goto error;
 
	obj->mo_drv_obj = (void *)cobj;
	rc = mio_driver_op_add(op, clovis_obj_open_pp, NULL, cops[0]);
	if (rc < 0)
		goto error;
	m0_clovis_op_launch(cops, 1);
	return 0;

error:
	if (cops[0] != NULL) {
		m0_clovis_op_fini(cops[0]);
		m0_clovis_op_free(cops[0]);
	}
	m0_clovis_obj_fini(cobj);
	mio_mem_free(cobj);
	return rc;
}

static int mio_clovis_obj_close(struct mio_obj *obj)
{
	m0_clovis_obj_fini((struct m0_clovis_obj *)obj->mo_drv_obj);
	return 0;
}

static void pool_id_to_fid(const struct mio_pool *pool_id, struct m0_fid *fid)
{
	fid->f_container = pool_id->mp_hi;
	fid->f_key = pool_id->mp_lo;
}

static int mio_clovis_obj_create(const struct mio_pool *pool_id,
				 struct mio_obj *obj, struct mio_op *op)
{
	int rc = 0;
	struct m0_fid pfid;
	struct m0_fid *ptr_pfid = NULL;
	struct m0_uint128 id128;
	struct m0_clovis_obj *cobj;
	struct m0_clovis_op *cops[1] = {NULL};

	if (pool_id != NULL) {
		pool_id_to_fid(pool_id, &pfid);
		ptr_pfid = &pfid;
	}

	cobj = mio_mem_alloc(sizeof *cobj);
	if (cobj == NULL)
		return -ENOMEM;

	mio__obj_id_to_uint128(&obj->mo_id, &id128);
	m0_clovis_obj_init(cobj, &mio_clovis_container.co_realm, &id128,
			   mio_clovis_inst_confs->mc_default_layout_id);
	rc = m0_clovis_entity_create(ptr_pfid, &cobj->ob_entity, &cops[0]);
	if (rc < 0)
		goto error;

	obj->mo_drv_obj = (void *)cobj;
	rc = mio_driver_op_add(op, NULL, NULL, cops[0]);
	if (rc < 0)
		goto error;
	m0_clovis_op_launch(cops, 1);
	return 0;

error:
	if (cops[0] != NULL) {
		m0_clovis_op_fini(cops[0]);
		m0_clovis_op_free(cops[0]);
	}
	m0_clovis_obj_fini(cobj);
	mio_mem_free(cobj);
	return rc;
}

static int clovis_obj_delete_pp(struct mio_op *op)
{
	int rc = 0;
	struct mio_obj *mobj = op->mop_who.obj;
	struct m0_clovis_obj *cobj = (struct m0_clovis_obj *)mobj->mo_drv_obj;

	/* Launch a new op to delete this object's attributes. */
	rc = clovis_obj_attrs_query(M0_CLOVIS_IC_DEL, mobj,
				    clovis_obj_attrs_query_free_pp, op);

	/* The opened object is not needed any more. */
	mio_mem_free(cobj);
	mio_mem_free(mobj);

	if (rc < 0)
		return rc;
	else
		return MIO_DRV_OP_NEXT;
}

static int
clovis_obj_delete_open_pp(struct mio_op *op)
{
	int rc = 0;
	struct m0_clovis_op *cops[1] = {NULL};
	struct m0_clovis_obj *cobj;
	struct mio_obj *mobj = op->mop_who.obj;

	cobj = (struct m0_clovis_obj *)mobj->mo_drv_obj;
	rc = m0_clovis_entity_delete(&cobj->ob_entity, &cops[0]);
	if (rc != 0) {
		mio_log(MIO_ERROR, "Creating DELETE op failed!\n");
		goto error;
	}

	rc = mio_driver_op_add(op, clovis_obj_delete_pp, NULL, cops[0]);
	if (rc < 0)
		goto error;
	m0_clovis_op_launch(cops, ARRAY_SIZE(cops));
	return MIO_DRV_OP_NEXT;

error:
	if (cops[0] != NULL) {
		m0_clovis_op_fini(cops[0]);
		m0_clovis_op_free(cops[0]);
	}
	m0_clovis_obj_fini(cobj);
	mio_mem_free(cobj);
	mio_mem_free(mobj);
	return rc;
}

/**
 * Deleting an object takes the following steps:
 * (1) Open the object to fetch required Mero-wise object attributes.
 * (2) Create and launch DELETE op to remove the object data.
 * (3) Create and launch an KVS DEL op to remove this object MIO attributes.
 */
static int
mio_clovis_obj_delete(const struct mio_obj_id *oid, struct mio_op *op)
{
	int rc = 0;
	struct m0_uint128 id128;
	struct m0_clovis_obj *cobj;
	struct m0_clovis_op *cops[1] = {NULL};
	struct mio_obj *mobj;

	/* Create a mio obj handler for callback. */
	mobj = mio_mem_alloc(sizeof *mobj);
	if (mobj == NULL)
		return -ENOMEM;
	mobj->mo_id = *oid;
	op->mop_who.obj = mobj;

	cobj = mio_mem_alloc(sizeof *cobj);
	if (cobj == NULL) {
		mio_mem_free(mobj);
		return -ENOMEM;
	}
	mobj->mo_drv_obj = (void *)cobj;
	mobj->mo_md_kvs = &mio_obj_attrs_kvs;

	mio__obj_id_to_uint128(&mobj->mo_id, &id128);
	m0_clovis_obj_init(cobj, &mio_clovis_container.co_realm, &id128,
			   mio_clovis_inst_confs->mc_default_layout_id);
	rc = m0_clovis_entity_open(&cobj->ob_entity, &cops[0]);
	if (rc != 0) {
		mio_log(MIO_ERROR,
			"Creating OPEN op for object deletion failed!\n");
		goto error;
	}

	rc = mio_driver_op_add(op, clovis_obj_delete_open_pp, NULL, cops[0]);
	if (rc < 0)
		goto error;
	m0_clovis_op_launch(cops, ARRAY_SIZE(cops));
	return 0;

error:
	if (cops[0] != NULL) {
		m0_clovis_op_fini(cops[0]);
		m0_clovis_op_free(cops[0]);
	}
	m0_clovis_obj_fini(cobj);
	mio_mem_free(cobj);
	mio_mem_free(mobj);
	return rc;
}

static bool is_rw_2big(const struct mio_iovec *iov, int iovcnt)
{
	int i;
	int nr_units = 0;
	uint64_t unit_size;

	unit_size = m0_clovis_obj_layout_id_to_unit_size(
				mio_clovis_inst_confs->mc_default_layout_id);
	for (i = 0; i < iovcnt; i++)
       		nr_units += (iov[i].miov_len + unit_size - 1) / unit_size;
	return nr_units > MIO_CLOVIS_MAX_RW_NR_UNITS_PER_OP? true : false;
}

static int clovis_obj_write_pp(struct mio_op *op)
{
	int rc;
	bool update_size;
	uint64_t *max_eow;
	struct mio_obj *obj = op->mop_who.obj;

	max_eow = (uint64_t *)
		  op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;
	update_size = (*max_eow <= obj->mo_attrs.moa_size)? false : true;
	mio_mem_free(max_eow);
	if (!update_size)
		return MIO_DRV_OP_FINAL;

	/* Launch a new op to update object size. */
	obj->mo_attrs.moa_size = *max_eow;
	rc = clovis_obj_attrs_query(M0_CLOVIS_IC_PUT, obj,
				    clovis_obj_attrs_query_free_pp, op);
	if (rc < 0)
		return rc;
	else
		return MIO_DRV_OP_NEXT;
}

static int clovis_obj_rw(struct mio_obj *obj,
			 const struct mio_iovec *iov, int iovcnt,
			 enum m0_clovis_obj_opcode opcode,
			 struct mio_op *op)
{
	int i;
	int rc = 0;
	uint64_t *max_eow = NULL; /* eow: End Of Write */
	struct m0_clovis_obj *cobj;
	struct m0_clovis_op  *cops[1] = {NULL};
	struct m0_indexvec ext;
	struct m0_bufvec data;
	struct m0_bufvec attr;	

	assert(opcode == M0_CLOVIS_OC_READ || opcode == M0_CLOVIS_OC_WRITE);

	if (is_rw_2big(iov, iovcnt))
		return -E2BIG;
	
	if (iovcnt < 1)
		return -EINVAL;

	/* Allocate memory for bufvec and indexvec. */
	rc = m0_bufvec_empty_alloc(&data, iovcnt) ? :
	     m0_bufvec_alloc(&attr, iovcnt, 1) ? :
	     m0_indexvec_alloc(&ext, iovcnt);
	if (rc != 0)
		goto error;

	max_eow = mio_mem_alloc(sizeof *max_eow);
	if (max_eow == NULL)
		goto error;

	/*
	 * Populate bufvec and indexvec. Avoid copying data
	 * into bufvec.
	 */
	for (i = 0; i < iovcnt; i++) {
		data.ov_vec.v_count[i] = iov[i].miov_len;
		data.ov_buf[i] = iov[i].miov_base;

		ext.iv_index[i] = iov[i].miov_off;
		ext.iv_vec.v_count[i] = iov[i].miov_len;
		if (iov[i].miov_off + iov[i].miov_len > *max_eow)
			*max_eow = iov[i].miov_off + iov[i].miov_len;

		/* we don't want any attributes */
		attr.ov_vec.v_count[i] = 0;
	}

	/* Create and launch an RW op. */
	cobj = (struct m0_clovis_obj *)obj->mo_drv_obj;
	m0_clovis_obj_op(cobj, opcode, &ext, &data, &attr, 0, &cops[0]);

	/* Set callback and then launch IO op. */
	if (opcode == M0_CLOVIS_OC_WRITE)
		rc = mio_driver_op_add(
			op, clovis_obj_write_pp, max_eow, cops[0]);
	else
		rc = mio_driver_op_add(op, NULL, NULL, cops[0]);
	if (rc < 0)
		goto error;

	m0_clovis_op_launch(cops, ARRAY_SIZE(cops));
	return 0;

error:
	m0_indexvec_free(&ext);
	m0_bufvec_free(&data);
	m0_bufvec_free(&attr);
	mio_mem_free(max_eow);
	return rc;
}

static int mio_clovis_obj_readv(struct mio_obj *obj,
				 const struct mio_iovec *iov,
				 int iovcnt, struct mio_op *op)
{
	return clovis_obj_rw(obj, iov, iovcnt, M0_CLOVIS_OC_READ, op);
}

static int mio_clovis_obj_writev(struct mio_obj *obj,
				 const struct mio_iovec *iov,
				 int iovcnt, struct mio_op *op)
{
	return clovis_obj_rw(obj, iov, iovcnt, M0_CLOVIS_OC_WRITE, op);
}

static int mio_clovis_obj_sync(struct mio_obj *obj, struct mio_op *op)
{
	int rc;
	struct m0_clovis_obj *cobj;
	struct m0_clovis_op  *sync_op = {NULL};

	rc = m0_clovis_sync_op_init(&sync_op);
	if (rc < 0)
		return rc;

	cobj = (struct m0_clovis_obj *)obj->mo_drv_obj;
	rc = m0_clovis_sync_entity_add(sync_op, &cobj->ob_entity);
	if (rc < 0)
		goto error;	

	rc = mio_driver_op_add(op, NULL, NULL, sync_op);
	if (rc < 0)
		goto error;
	m0_clovis_op_launch(&sync_op, 1);
	return 0;

error:
	m0_clovis_op_fini(sync_op);
	m0_clovis_op_free(sync_op);
	return rc;
}

/**
 * Note: currently Mero Clovis doesn't store size as object attribute
 * and doesn't have any API to query object size. MIO will use an index
 * (attribute index) to manage object attributes such as object size.
 * This also implies that any object WRITE has to update the size.
 * Depending on workload and performance of Mero index, MIO may support
 * multiple indics.
 * 
 * Format of object attributes that are stored in attribute index are
 * defined as below:
 *
 * struct clovis_obj_attrs_onwire {
 *	struct mio_obj_attrs coa_attrs;
 *      int coa_nr_hints;
 *      int coa_hint_keys[];   // array of hint's keys
 *      uint64_t coa_hint_values[]; // array of hint's values
 * };
 *
 * Note: MIO assumes application takes care of concurrent accesses to
 * an object and its attributes.
 */

struct clovis_obj_attrs_pp_args {
	int32_t *aca_rc;
	struct m0_bufvec *aca_key;
	struct m0_bufvec *aca_val;
	/* Where the returned attributes are copied to. */
	struct mio_obj *aca_to;
};

static int clovis_obj_attr_nonhint_size(struct mio_obj *obj)
{
	int size;

	size = sizeof obj->mo_attrs.moa_size;
	size += sizeof obj->mo_attrs.moa_wtime;
	return size;
}

static int
clovis_obj_attrs_mem2wire(struct mio_obj *obj,
			  uint64_t *attr_size, void **attr_buf)
{
	int i;
	int nonhint_size;
	uint64_t size;
	void *buf;
	char *ptr;
	struct mio_hint_map *map = &obj->mo_attrs.moa_phints.mh_map;

	nonhint_size  = clovis_obj_attr_nonhint_size(obj);
	size = nonhint_size;
	size += sizeof(int);
	size += map->mhm_nr_set * (sizeof(int) + sizeof(uint64_t));

	buf = mio_mem_alloc(size);
	if (buf == NULL)
		return -ENOMEM;

	ptr = buf;
	mio_mem_copy(ptr, &obj->mo_attrs, nonhint_size);
	ptr += nonhint_size;

	mio_mem_copy(ptr, &map->mhm_nr_set, sizeof(int));
	ptr += sizeof(int);

	for (i = 0; i < map->mhm_nr_set; i++) {
		mio_mem_copy(ptr, map->mhm_keys + i, sizeof(int));
		ptr += sizeof(int);
	}

	for (i = 0; i < map->mhm_nr_set; i++) {
		mio_mem_copy(ptr, map->mhm_values + i, sizeof(uint64_t));
		ptr += sizeof(uint64_t);
	}

	*attr_size = size;
	*attr_buf = buf;
	return 0;
}

static int
clovis_obj_attrs_wire2mem(struct mio_obj *obj, int attr_size, void *attr_buf)
{
	int i;
	int nonhint_size;
	int size;
	int nr_hints;
	char *ptr;
	/* Note that hint map has been initialised with default number. */
	struct mio_hint_map *map = &obj->mo_hints.mh_map;

	nonhint_size = clovis_obj_attr_nonhint_size(obj);
	ptr = attr_buf;
	mio_mem_copy(&obj->mo_attrs, ptr, nonhint_size);
	ptr += nonhint_size;

	mio_mem_copy(&nr_hints, ptr, sizeof(int));
	ptr += sizeof(int);
	if (nr_hints < 0 || nr_hints > MIO_OBJ_HINT_NUM)
		return -EIO;
	else if (nr_hints == 0)
		return 0;
	map->mhm_nr_set = nr_hints;

	size  = nonhint_size;
	size += sizeof(int);
	size += nr_hints * (sizeof(int) + sizeof(uint64_t));
	if (attr_size != size)
		return -EIO;

	for (i = 0; i < nr_hints; i++) {
		mio_mem_copy(map->mhm_keys + i, ptr, sizeof(int));
		ptr += sizeof(int);
	}

	for (i = 0; i < nr_hints; i++) {
		mio_mem_copy(map->mhm_values + i, ptr, sizeof(uint64_t));
		ptr += sizeof(uint64_t);
	}

	return 0;
}

static int clovis_obj_attrs_query(int opcode, struct mio_obj *obj,
				  mio_driver_op_postprocess op_pp,
				  struct mio_op *op)
{
	int rc;
	int32_t *qrc = NULL; /* return value for kvs query. */
        struct m0_uint128 *id128;
        struct m0_bufvec *key = NULL;
        struct m0_bufvec *val = NULL;
	struct m0_clovis_idx *idx;
	struct m0_clovis_op *cops[1] = {NULL};
	struct clovis_obj_attrs_pp_args *args;

	assert(opcode == M0_CLOVIS_IC_GET || opcode == M0_CLOVIS_IC_PUT ||
	       opcode == M0_CLOVIS_IC_DEL);

	id128 = mio_mem_alloc(sizeof *id128);
	qrc = mio_mem_alloc(sizeof(int32_t));
	args = mio_mem_alloc(sizeof *args);
	if (id128 == NULL || qrc == NULL || args == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	/* Allocate bufvec's for keys and values. */
	key = mio__clovis_bufvec_alloc(1);
	if (key == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	if (opcode != M0_CLOVIS_IC_DEL) {
		val = mio__clovis_bufvec_alloc(1);
		if (val == NULL) {
			rc = -ENOMEM;
			goto error;
		}
	}

        /* Fill key and value. TODO: serialise key and value? */
	mio__obj_id_to_uint128(&obj->mo_id, id128);
	key->ov_vec.v_count[0] = sizeof *id128;
	key->ov_buf[0] = id128;

	if (opcode == M0_CLOVIS_IC_PUT) 
		clovis_obj_attrs_mem2wire(obj, &val->ov_vec.v_count[0],
					  &val->ov_buf[0]);

	/* Create index's op. */
	idx = (struct m0_clovis_idx *)obj->mo_md_kvs->mk_drv_kvs;
	rc = m0_clovis_idx_op(idx, opcode, key, val, qrc, 0, &cops[0]);
	if (rc < 0)
		goto error;

	/* Set callback function and arguments. */
	args->aca_val = val;
	args->aca_key = key;
	args->aca_rc = qrc;
	args->aca_to = obj;
	rc = mio_driver_op_add(op, op_pp, args, cops[0]);
	if (rc < 0)
		goto error;

	m0_clovis_op_launch(cops, 1);
	return 0;

error:
	clovis_bufvec_free(key);
	clovis_bufvec_free(val);
	mio_mem_free(qrc);
	mio_mem_free(args);
	return rc;
}

static int clovis_obj_attrs_query_free_pp(struct mio_op *op)
{
	struct clovis_obj_attrs_pp_args *args;

	args = (struct clovis_obj_attrs_pp_args *)
	       op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;
	clovis_bufvec_free(args->aca_key);
	clovis_bufvec_free(args->aca_val);
	mio_mem_free(args->aca_rc);
	mio_mem_free(args);

	return MIO_DRV_OP_FINAL;
}

static int clovis_obj_attrs_get_pp(struct mio_op *op)
{
	struct m0_bufvec *ret_val;
	struct m0_clovis_op *cop = MIO_CLOVIS_OP(op);
	struct mio_obj *obj;
	struct clovis_obj_attrs_pp_args *args;

	assert(cop != NULL);
	args = (struct clovis_obj_attrs_pp_args *)
	       op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;
	obj = args->aca_to;
	ret_val = args->aca_val;
	if (ret_val->ov_vec.v_count[0] != 0)
		clovis_obj_attrs_wire2mem(obj, ret_val->ov_vec.v_count[0],
					  ret_val->ov_buf[0]);

	return clovis_obj_attrs_query_free_pp(op);
}

static int mio_clovis_obj_size(struct mio_obj *obj, struct mio_op *op)
{
	return clovis_obj_attrs_query(M0_CLOVIS_IC_GET, obj,
				      clovis_obj_attrs_get_pp, op);
}

static int mio_clovis_obj_hint_store(struct mio_obj *obj)
{
	int rc;
	struct mio_op mop;
	struct m0_clovis_op *cop;

	mio_memset(&mop, 0, sizeof mop);
	mio_obj_op_init(&mop, obj, MIO_OBJ_ATTRS_SET);
	rc = clovis_obj_attrs_query(M0_CLOVIS_IC_PUT, obj,
				    clovis_obj_attrs_query_free_pp, &mop);
	if (rc < 0)
		return rc;

	cop = MIO_CLOVIS_OP((&mop));
	rc = m0_clovis_op_wait(cop,
			       M0_BITS(M0_CLOVIS_OS_FAILED,
				       M0_CLOVIS_OS_STABLE),
			       M0_TIME_NEVER);
	rc = rc? : m0_clovis_rc(cop);
	m0_clovis_op_fini(cop);
	m0_clovis_op_free(cop);
	return rc;
}

static int mio_clovis_obj_hint_load(struct mio_obj *obj)
{
	/*
	 * Hints are retrieved when an object is opened, no more work
	 * here.
	 */
	return 0;
}

static struct mio_obj_ops mio_clovis_obj_ops = {
        .moo_open         = mio_clovis_obj_open,
        .moo_close        = mio_clovis_obj_close,
        .moo_create       = mio_clovis_obj_create,
        .moo_delete       = mio_clovis_obj_delete,
        .moo_writev       = mio_clovis_obj_writev,
        .moo_readv        = mio_clovis_obj_readv,
        .moo_sync         = mio_clovis_obj_sync,
        .moo_size         = mio_clovis_obj_size,
        .moo_hint_store   = mio_clovis_obj_hint_store,
        .moo_hint_load    = mio_clovis_obj_hint_load,
};

static void kvs_id_to_uint128(const struct mio_kvs_id *kid,
			      struct m0_uint128 *uint128)
{
	struct m0_fid idx_fid;
	uint64_t hi;
	uint64_t lo;
 
	hi = mio_byteorder_be64_to_cpu(*((uint64_t *)(kid->mki_bytes + 0)));
	lo = mio_byteorder_be64_to_cpu(*((uint64_t *)(kid->mki_bytes + 8)));
	/* Make mero KVS happy */
        idx_fid = M0_FID_TINIT('x', hi, lo);

	uint128->u_hi = idx_fid.f_container; 
	uint128->u_lo = idx_fid.f_key;
}

static int
clovis_kvs_idx_alloc_init(struct mio_kvs_id *kid, struct m0_clovis_idx **out)
{	
        struct m0_uint128 id;
	struct m0_clovis_idx *idx;

	idx = mio_mem_alloc(sizeof *idx);
	if (idx == NULL)
		return -ENOMEM;
	
	kvs_id_to_uint128(kid, &id);
	m0_clovis_idx_init(idx, &mio_clovis_container.co_realm, &id);
	*out = idx;
	return 0;
}

static void clovis_kvs_idx_fini_free(struct m0_clovis_idx *idx)
{
	m0_clovis_entity_fini(&idx->in_entity);
	mio_mem_free(idx);
}

static int
clovis_kvs_keys_vals_alloc(int opcode, int nr_kvps,
			   struct m0_bufvec **ret_keys,
			   struct m0_bufvec **ret_vals)
{
	int rc = 0;
        struct m0_bufvec *keys;
        struct m0_bufvec *vals = NULL;

	assert(opcode == M0_CLOVIS_IC_GET || opcode == M0_CLOVIS_IC_PUT ||
	       opcode == M0_CLOVIS_IC_DEL);

	keys = mio__clovis_bufvec_alloc(nr_kvps);
	if (keys == NULL) {
		rc = -ENOMEM;
		goto error;
	}
	
	if (opcode != M0_CLOVIS_IC_DEL) {
		vals = mio__clovis_bufvec_alloc(nr_kvps);
		if (vals == NULL) {
			rc = -ENOMEM;
			goto error;
		}
	}

	*ret_keys = keys;
	*ret_vals = vals;
	return 0;

error:
	clovis_bufvec_free(keys);
	clovis_bufvec_free(vals);
	return rc;
}

static int clovis_kvs_query(struct m0_clovis_idx *idx, int opcode,
			    int nr_kvps, struct mio_kv_pair *kvps, int32_t *rcs,
			    struct m0_bufvec *keys, struct m0_bufvec *vals,
			    mio_driver_op_postprocess query_pp, void *cb_args,
			    struct mio_op *op)
{
	int i;
	int rc = 0;
	bool set_vals;
	struct m0_clovis_op *cops[1] = {NULL};

	assert(opcode == M0_CLOVIS_IC_GET || opcode == M0_CLOVIS_IC_PUT ||
	       opcode == M0_CLOVIS_IC_DEL);

	set_vals = (opcode == M0_CLOVIS_IC_PUT)? true : false;

        /* Fill keys and values. */
	for (i = 0; i < nr_kvps; i++) {
		keys->ov_vec.v_count[i] = kvps[i].mkp_klen;
		keys->ov_buf[i] = kvps[i].mkp_key;

		if (set_vals) {
			assert(vals != NULL);
			vals->ov_vec.v_count[i] = kvps[i].mkp_vlen;
			vals->ov_buf[i] = kvps[i].mkp_val;
		}
	}

	/* Create index op. */
	rc = m0_clovis_idx_op(idx, opcode, keys, vals, rcs, 0, &cops[0]);
	if (rc < 0)
		goto error;

	/* Set query's callback and arguments. */
	rc = mio_driver_op_add(op, query_pp, cb_args, cops[0]);
	if (rc < 0)
		goto error;

	/* Launch query. */
	m0_clovis_op_launch(cops, 1);

	return 0;

error:
	clovis_bufvec_free(keys);
	clovis_bufvec_free(vals);
	return rc;
}

struct clovis_kvs_get_args {
	struct m0_bufvec *kga_rvs; /* Returned values from clovis GET op. */
	struct mio_kv_pair *kga_orig_pairs; /* Original pointer from app. */
	struct m0_clovis_idx *kga_idx; /* The clovis index for the GET op. */
};

static int clovis_kvs_get_pp(struct mio_op *op)
{
	int i;
	int nr_kvps;
	struct clovis_kvs_get_args *args;
	struct mio_kv_pair *kvps;
	struct m0_bufvec *rvs;
	struct m0_clovis_op *cop = MIO_CLOVIS_OP(op);

	assert(cop != NULL);
	if (cop->op_sm.sm_state != M0_CLOVIS_OS_STABLE)
		return -EIO;

	/* Copy returned values. */
	args = (struct clovis_kvs_get_args *)
	       op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;
	kvps = args->kga_orig_pairs;
	rvs  = args->kga_rvs;
	nr_kvps = rvs->ov_vec.v_nr;
	for (i = 0; i < nr_kvps; i++) {
		kvps[i].mkp_val = rvs->ov_buf[i];
		kvps[i].mkp_vlen = rvs->ov_vec.v_count[i];
	}

	clovis_kvs_idx_fini_free(args->kga_idx);
	mio_mem_free(args);
	return MIO_DRV_OP_FINAL;
}

static int mio_clovis_kvs_get(struct mio_kvs_id *kid,
			      int nr_kvps, struct mio_kv_pair *kvps,
			      int32_t *rcs, struct mio_op *op)
{
	int rc;
	struct clovis_kvs_get_args *args;
	struct m0_clovis_idx *idx;
	struct m0_bufvec *keys = NULL;
	struct m0_bufvec *vals = NULL;	

	rc = clovis_kvs_idx_alloc_init(kid, &idx);
	if (rc < 0)
		return rc;

	rc = clovis_kvs_keys_vals_alloc(
			M0_CLOVIS_IC_GET, nr_kvps, &keys, &vals);
	if (rc < 0)
		goto error;

	args = mio_mem_alloc(sizeof *args);
	if (args == NULL) {
		rc = -ENOMEM;
		goto error;
	}

       	/* Save the arguments for post-processing. */
	args->kga_rvs = vals;
	args->kga_orig_pairs = kvps;
	args->kga_idx = idx;

	rc = clovis_kvs_query(idx, M0_CLOVIS_IC_GET,
			      nr_kvps, kvps, rcs, keys, vals,
			      clovis_kvs_get_pp, args, op);
	if (rc < 0) {
		clovis_kvs_idx_fini_free(idx);
		mio_mem_free(args);
	}
	return rc;

error:
	clovis_kvs_idx_fini_free(idx);
	clovis_bufvec_free(keys);
	clovis_bufvec_free(vals);
	mio_mem_free(args);
	return rc;
}

static int clovis_kvs_generic_pp(struct mio_op *op)
{
	struct m0_clovis_idx *idx;

	idx = (struct m0_clovis_idx*)
	      op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;
	clovis_kvs_idx_fini_free(idx);

	return MIO_DRV_OP_FINAL;
}

static int mio_clovis_kvs_put(struct mio_kvs_id *kid,
			      int nr_kvps, struct mio_kv_pair *kvps,
			      int32_t *rcs, struct mio_op *op)
{
	int rc;
	struct m0_clovis_idx *idx;
	struct m0_bufvec *keys = NULL;
	struct m0_bufvec *vals = NULL;	

	rc = clovis_kvs_idx_alloc_init(kid, &idx);
	if (rc < 0)
		return rc;

	rc = clovis_kvs_keys_vals_alloc(
			M0_CLOVIS_IC_PUT, nr_kvps, &keys, &vals);
	if (rc < 0) {
		clovis_kvs_idx_fini_free(idx);
		return rc;
	}

	return clovis_kvs_query(idx, M0_CLOVIS_IC_PUT,
				nr_kvps, kvps, rcs, keys, vals,
				clovis_kvs_generic_pp, idx, op);
}

static int mio_clovis_kvs_del(struct mio_kvs_id *kid,
			      int nr_kvps, struct mio_kv_pair *kvps,
			      int32_t *rcs, struct mio_op *op)
{

	int rc;
	struct m0_clovis_idx *idx;
	struct m0_bufvec *keys = NULL;
	struct m0_bufvec *vals = NULL;	

	rc = clovis_kvs_idx_alloc_init(kid, &idx);
	if (rc < 0)
		return rc;

	rc = clovis_kvs_keys_vals_alloc(
			M0_CLOVIS_IC_DEL, nr_kvps, &keys, &vals);
	if (rc < 0) {
		clovis_kvs_idx_fini_free(idx);
		return rc;
	}

	return clovis_kvs_query(idx, M0_CLOVIS_IC_DEL,
				nr_kvps, kvps, rcs, keys, vals,
				clovis_kvs_generic_pp, idx, op);
}

static int mio_clovis_kvs_create_set(struct mio_kvs_id *kid,
				     struct mio_op *op)
{
        int rc;
        struct m0_clovis_op *cops[1] = {NULL};
        struct m0_clovis_idx *idx;

	rc = clovis_kvs_idx_alloc_init(kid, &idx);
	if (rc < 0)
		return rc;

        rc = m0_clovis_entity_create(NULL, &idx->in_entity, &cops[0]);
	if (rc < 0)
		return rc;

	rc = mio_driver_op_add(op, clovis_kvs_generic_pp, idx, cops[0]);
	if (rc < 0)
		goto error;
        m0_clovis_op_launch(cops, 1);
	return 0;

error:
	clovis_kvs_idx_fini_free(idx);
	m0_clovis_op_fini(cops[0]);
	m0_clovis_op_free(cops[0]);
	return rc;
}

static int mio_clovis_kvs_del_set(struct mio_kvs_id *kid,
				  struct mio_op *op)
{
        int rc;
        struct m0_clovis_op *cops[1] = {NULL};
        struct m0_clovis_idx *idx;

	rc = clovis_kvs_idx_alloc_init(kid, &idx);
	if (rc < 0)
		return rc;

	/*
 	 * Clovis Index has to be opened before deleting it but not before
 	 * querying it. How odd! Who to blame? :).
 	 * The only thing openning an index is to make sure the index is in
 	 * a right state. 
 	 */
	m0_clovis_entity_open(&idx->in_entity, &cops[0]);
        rc = m0_clovis_entity_delete(&idx->in_entity, &cops[0]);
	if (rc < 0)
		goto error;

	rc = mio_driver_op_add(op, clovis_kvs_generic_pp, idx, cops[0]);
	if (rc < 0)
		goto error;

        m0_clovis_op_launch(cops, 1);
	return 0;

error:
	clovis_kvs_idx_fini_free(idx);
	m0_clovis_op_fini(cops[0]);
	m0_clovis_op_free(cops[0]);
	return rc;
}

static struct mio_kvs_ops mio_clovis_kvs_ops = {
        .mko_get        = mio_clovis_kvs_get,
        .mko_put        = mio_clovis_kvs_put,
        .mko_del        = mio_clovis_kvs_del,
        .mko_create_set = mio_clovis_kvs_create_set,
        .mko_del_set    = mio_clovis_kvs_del_set
};

void mio_clovis_driver_register()
{
	mio_driver_register(
		MIO_MERO, &mio_clovis_sys_ops,
		&mio_clovis_op_ops, &mio_clovis_obj_ops,
		&mio_clovis_kvs_ops, &mio_clovis_comp_obj_ops);
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
