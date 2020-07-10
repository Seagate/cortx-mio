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
	mio__clovis_bufvec_free(keys);
	mio__clovis_bufvec_free(vals);
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
	rc = mio_driver_op_add(op, query_pp, cb_args, NULL, cops[0], NULL);
	if (rc < 0)
		goto error;

	/* Launch query. */
	m0_clovis_op_launch(cops, 1);

	return 0;

error:
	mio__clovis_bufvec_free(keys);
	mio__clovis_bufvec_free(vals);
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
	mio__clovis_bufvec_free(keys);
	mio__clovis_bufvec_free(vals);
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

	rc = mio_driver_op_add(op, clovis_kvs_generic_pp, idx, NULL,
			       cops[0], NULL);
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

	rc = mio_driver_op_add(op, clovis_kvs_generic_pp, idx, NULL,
			       cops[0], NULL);
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

struct mio_kvs_ops mio_clovis_kvs_ops = {
        .mko_get        = mio_clovis_kvs_get,
        .mko_put        = mio_clovis_kvs_put,
        .mko_del        = mio_clovis_kvs_del,
        .mko_create_set = mio_clovis_kvs_create_set,
        .mko_del_set    = mio_clovis_kvs_del_set
};

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
