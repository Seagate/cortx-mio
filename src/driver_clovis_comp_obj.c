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

#include "lib/vec.h"
#include "clovis/clovis.h"
#include "clovis/clovis_idx.h"

#include "logger.h"
#include "mio.h"
#include "mio_internal.h"
#include "driver_clovis.h"

/*
 * Creating a composite object includes 2 steps:
 * (1) Create a `normal` object.
 * (2) Set composite layout for this newly created object.
 *
 * mio_composite_obj_create() is called after an object is created
 * by mio_obj_create(). The decision to not pack the above 2 steps
 * in mio_composite_obj_create() is to handle failures properly.
 * A failure could happen in creating a `normal` object or setting
 * composite layout, which requires different clear-up handling. 
 *
 * Note: MIO only supports creating a composite object from a newly
 * created one (not yet written any data).
 */

static int
mio_clovis_comp_obj_create(struct mio_obj *obj, struct mio_op *op)
{
	int rc;
	struct m0_clovis_obj *cobj;
	struct m0_clovis_op *cops[1] = {NULL};
        struct m0_clovis_layout *layout;

        layout = m0_clovis_layout_alloc(M0_CLOVIS_LT_COMPOSITE);
	if (layout == NULL)
		return -ENOMEM;

	cobj = (struct m0_clovis_obj *)obj->mo_drv_obj;
        /*
	 * A bug in clovis doesn't set the object's layout pointer.
	 */
	if (cobj->ob_layout == NULL)
		cobj->ob_layout = layout;
	m0_clovis_layout_op(cobj, M0_CLOVIS_EO_LAYOUT_SET,
			    layout, &cops[0]);

	rc = mio_driver_op_add(op, NULL, NULL, cops[0]);
	if (rc < 0)
		goto error;
	m0_clovis_op_launch(cops, ARRAY_SIZE(cops));
	return 0;

error:
	m0_clovis_op_fini(cops[0]);
	m0_clovis_op_free(cops[0]);
	m0_clovis_layout_free(layout);
	return rc;
}

struct clovis_comp_obj_add_layer_pp_args {
	int alpa_nr_layers_to_add;
	struct m0_clovis_obj *alpa_layer_objs;
	struct m0_clovis_layout *alpa_layout;
};

static int clovis_comp_obj_add_layers_pp(struct mio_op *op)
{
	int i;
	struct clovis_comp_obj_add_layer_pp_args *pp_args;

	pp_args = (struct clovis_comp_obj_add_layer_pp_args *)
		  op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;
	for (i = 0; i < pp_args->alpa_nr_layers_to_add; i++)
		m0_clovis_obj_fini(pp_args->alpa_layer_objs + i);
	mio_mem_free(pp_args->alpa_layer_objs);
	mio_mem_free(pp_args);

	return MIO_DRV_OP_FINAL;
}

static int
mio_clovis_comp_obj_add_layers(struct mio_obj *obj, int nr_layers,
			       struct mio_comp_obj_layer *layers,
			       struct mio_op *op)
{
	int rc = 0;
	int i;
	int j;
	struct m0_uint128 id128;
	struct m0_clovis_obj *layer_objs;
	struct m0_clovis_obj *cobj;
	struct m0_clovis_op *cops[1] = {NULL};
        struct m0_clovis_layout *layout = NULL;
	struct clovis_comp_obj_add_layer_pp_args *pp_args;

	pp_args = mio_mem_alloc(sizeof(*pp_args));
	layer_objs = mio_mem_alloc(nr_layers * sizeof(*layer_objs));
	if (pp_args == NULL || layer_objs == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	cobj = (struct m0_clovis_obj *)obj->mo_drv_obj;
	/* The layout may have been changed. */
	if (cobj->ob_layout != NULL)
		m0_clovis_layout_free(cobj->ob_layout);
	layout = m0_clovis_layout_alloc(M0_CLOVIS_LT_COMPOSITE);
	if (layout == NULL) {
		rc = -ENOMEM;
		goto error;
	}
	cobj->ob_layout = layout;

        for (i = 0; i < nr_layers; i++) {
		mio__obj_id_to_uint128(&layers[i].mcol_oid, &id128);
		m0_clovis_obj_init(layer_objs + i, 
				   &mio_clovis_container.co_realm, &id128,
				   mio_clovis_inst_confs->mc_default_layout_id);
                rc = m0_clovis_composite_layer_add(
			layout, layer_objs + i, layers[i].mcol_priority);
                if (rc < 0)
                        break;
        }
        if (rc != 0) {
                for (j = 0; j < i; j++)
                        m0_clovis_composite_layer_del(
				layout, layer_objs[j].ob_entity.en_id);
		goto error;
        }
	
        m0_clovis_layout_op(cobj, M0_CLOVIS_EO_LAYOUT_SET,
			    layout, &cops[0]);

	pp_args->alpa_nr_layers_to_add = nr_layers;
	pp_args->alpa_layer_objs = layer_objs;
	pp_args->alpa_layout = layout;
	rc = mio_driver_op_add(op, clovis_comp_obj_add_layers_pp,
			       pp_args, cops[0]);
	if (rc < 0)
		goto error;

	m0_clovis_op_launch(cops, ARRAY_SIZE(cops));
	return 0;

error:
	if (layout)
		m0_clovis_layout_free(layout);
	mio_mem_free(layer_objs);
	mio_mem_free(pp_args);
	return rc;
}

struct clovis_comp_obj_get_layers_args {
	/* Returned object layout. */
	struct m0_clovis_layout *gla_clayout;
	/*
	 * Customized data for layer query. For layer listing, it is
	 * set to the pointer of MIO layout data structure from
	 * applications. But for layer deleting, it is set to
	 * the pointer of an array of layers to be deleted.
	 */
	void *gla_data;
};

static int
clovis_comp_obj_get_layers(struct mio_obj *obj,
			   void *query_data, mio_driver_op_postprocess pp,
			   struct mio_op *op)
{
	int rc;
	struct clovis_comp_obj_get_layers_args *args;
	struct m0_clovis_layout *clayout = NULL;
	struct m0_clovis_obj *cobj;
	struct m0_clovis_op *cops[1] = {NULL};
	
	args = mio_mem_alloc(sizeof *args);
	if (args == NULL)
		return -ENOMEM;

	cobj = (struct m0_clovis_obj *)obj->mo_drv_obj;
	/* The layout may have been changed. */
	if (cobj->ob_layout != NULL)
		m0_clovis_layout_free(cobj->ob_layout);
	clayout = m0_clovis_layout_alloc(M0_CLOVIS_LT_COMPOSITE);
	if (clayout == NULL) {
		rc = -ENOMEM;
		goto error;
	}
	cobj->ob_layout = clayout;

        m0_clovis_layout_op(cobj, M0_CLOVIS_EO_LAYOUT_GET, clayout, &cops[0]);
      
	args->gla_data = query_data;
	args->gla_clayout = clayout;
	rc = mio_driver_op_add(op, pp, args, cops[0]);
	if (rc < 0)
		goto error;
	m0_clovis_op_launch(cops, ARRAY_SIZE(cops));
	return 0;

error:
	m0_clovis_op_fini(cops[0]);
	m0_clovis_op_free(cops[0]);
	m0_clovis_layout_free(clayout);
	return rc;
}

struct clovis_comp_obj_del_layers_args {
	int dla_nr_layers_to_del;
	struct mio_comp_obj_layer *dla_layers_to_del;
};

static int
clovis_comp_obj_del_layers_pp(struct mio_op *op)
{
	int rc = 0;
	int i;
	struct mio_obj *obj;
	struct m0_uint128 id128;
	struct m0_clovis_obj *cobj;
	struct m0_clovis_op *cops[1] = {NULL};
        struct m0_clovis_layout *clayout;
	int nr_layers_to_del;
	struct mio_comp_obj_layer *layers_to_del;
	struct clovis_comp_obj_get_layers_args *args;
	struct clovis_comp_obj_del_layers_args *del_layers_args;

	obj = op->mop_who.obj;
	assert(obj != NULL);

	args = (struct clovis_comp_obj_get_layers_args *)
	       op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;
	assert(args != NULL);
	del_layers_args = 
		(struct clovis_comp_obj_del_layers_args*) args->gla_data;
	nr_layers_to_del = del_layers_args->dla_nr_layers_to_del;
	layers_to_del = del_layers_args->dla_layers_to_del;
	clayout = (struct m0_clovis_layout *)args->gla_clayout;

	/* Here to remove the layers required. */
        for (i = 0; i < nr_layers_to_del; i++) {
		mio__obj_id_to_uint128(&layers_to_del[i].mcol_oid, &id128);	
		m0_clovis_composite_layer_del(clayout, id128);
        }

	/* Create and launch LAYOUT op. */
	cobj = (struct m0_clovis_obj *)obj->mo_drv_obj;
        m0_clovis_layout_op(cobj, M0_CLOVIS_EO_LAYOUT_SET, clayout, &cops[0]);

	rc = mio_driver_op_add(op, NULL, NULL, cops[0]);
	if (rc < 0)
		goto exit;
	m0_clovis_op_launch(cops, ARRAY_SIZE(cops));
	rc = MIO_DRV_OP_NEXT;

exit:
	mio_mem_free(del_layers_args);
	mio_mem_free(args);
	return rc;
}

static int
mio_clovis_comp_obj_del_layers(struct mio_obj *obj,
			       int nr_layers_to_del,
			       struct mio_comp_obj_layer *layers_to_del,
			       struct mio_op *op)
{
	struct clovis_comp_obj_del_layers_args *args;

	/*
	 * Deleting layers of a composite object is done in 2 steps:
	 * (1) retrieve the composite object's layout (how many layers).
	 * (2) delete layers from the layout and launch an operation to
	 *     update the layout in Mero service side.
	 */
	
	args = mio_mem_alloc(sizeof *args);
	if (args == NULL)
		return -ENOMEM;
	args->dla_nr_layers_to_del = nr_layers_to_del;
	args->dla_layers_to_del = layers_to_del;

	return clovis_comp_obj_get_layers(obj, args,
			clovis_comp_obj_del_layers_pp, op);
}

/**
 * A new Clovis API to parse the Clovis composite layout data structure is
 * needed as a privately defined list is used in Clovis.
 * Place holder: m0_clovis_composite_layer_get().
 *
 * Temporary solution: MIO hacks it by iterating the layer list by explictly
 * manipulating the list as m0_tl_* APIs are exposed in 'libmero'.
 */
#ifdef __MIO_CLOVIS_COMP_OBJ_LAYER_GET_SUPP__
static int clovis_comp_obj_list_layers_pp(struct mio_op *op)
{
	int i;
	int rc = 0;
	uint64_t nr_layers = 0;
	struct m0_uint128 *layer_ids;
	struct m0_clovis_op *cop;
	struct m0_clovis_layout *clayout;
	struct mio_comp_obj_layout *mlayout;
	struct clovis_comp_obj_get_layers_args *args;

	cop = (struct m0_clovis_op *)
	      op->mop_drv_op_chain.mdoc_head->mdo_op;
	assert(cop != NULL);
	if (cop->op_sm.sm_state != M0_CLOVIS_OS_STABLE)
		return -EIO;

	args = (struct clovis_comp_obj_get_layers_args *)
	       op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;
	mlayout = (struct mio_comp_obj_layout *)args->gla_data;
	clayout = (struct m0_clovis_layout *)args->gla_clayout;

	/* Retrieve layers. */
	rc = m0_clovis_composite_layer_get(clayout, &nr_layers, &layer_ids);
	if (rc < 0)
		goto exit;

	mlayout->mlo_nr_layers = nr_layers;
	mlayout->mlo_layers =
		mio_mem_alloc(nr_layers * sizeof(struct mio_comp_obj_layer));
	if (mlayout->mlo_layers == NULL) {
		rc = -ENOMEM;
		goto exit;
	}

	for (i = 0; i < nr_layers; i++)
		mio__uint128_to_obj_id(
			layers_ids + i, &((mlayout->mlo_layers[i]).mcol_oid));
	assert(rc == 0);

exit:
	mio_mem_free(layer_ids);
	return rc;
}

#else

/*
 * Uggly hacking :).
 */
static int clovis_comp_obj_list_layers_pp(struct mio_op *op)
{
	int i;
	int rc = 0;
	uint64_t nr_layers = 0;
	struct m0_clovis_op *cop;
	struct m0_clovis_layout *clayout;
	struct mio_comp_obj_layout *mlayout;
	struct m0_clovis_composite_layout *comp_layout;
	struct m0_clovis_composite_layer *comp_layer;
	struct m0_list_link *lnk;
	struct m0_tlink *tlnk;
	struct clovis_comp_obj_get_layers_args *args;

	cop = (struct m0_clovis_op *)
	      op->mop_drv_op_chain.mdoc_head->mdo_op;
	assert(cop != NULL);
	if (cop->op_sm.sm_state != M0_CLOVIS_OS_STABLE)
		return -EIO;

	args = (struct clovis_comp_obj_get_layers_args *)
	       op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;
	mlayout = (struct mio_comp_obj_layout *)args->gla_data;
	clayout = (struct m0_clovis_layout *)args->gla_clayout;
	comp_layout = container_of(clayout, struct m0_clovis_composite_layout,
				   ccl_layout);

	nr_layers = comp_layout->ccl_nr_layers;
	mlayout->mlo_nr_layers = nr_layers;
	mlayout->mlo_layers =
		mio_mem_alloc(nr_layers * sizeof(struct mio_comp_obj_layer));
	if (mlayout->mlo_layers == NULL) {
		rc = -ENOMEM;
		goto exit;
	}

	lnk = comp_layout->ccl_layers.t_head.l_head;
	for (i = 0; i < nr_layers; i++) {
		if (lnk == NULL)
			break;

		tlnk = container_of(lnk, struct m0_tlink, t_link);
		comp_layer = container_of(tlnk,
					  struct m0_clovis_composite_layer,
					  ccr_tlink);
		mio__uint128_to_obj_id(&comp_layer->ccr_subobj,
				       &(mlayout->mlo_layers[i]).mcol_oid);

		lnk = lnk->ll_next;
	}
	assert(rc == 0);
	return MIO_DRV_OP_FINAL;

exit:
	/*
	 * No need to call m0_clovis_layout_free(clayout) as
	 * m0_clovis_obj_fini() will release layout data structure
	 * which is triggerred by mio_obj_close().
	 */
	return rc;
}
#endif

static int
mio_clovis_comp_obj_list_layers(struct mio_obj *obj,
				struct mio_comp_obj_layout *ret_layout,
				struct mio_op *op)
{
	return clovis_comp_obj_get_layers(obj, ret_layout,
			clovis_comp_obj_list_layers_pp, op);
}

struct clovis_comp_obj_exts_pp_args {
        struct m0_clovis_idx *epa_idx;
	struct m0_bufvec *epa_keys;
	struct m0_bufvec *epa_vals; /* Returned values from Clovis GET op. */
	int *epa_rcs;
	int *epa_nr_ret_exts;
	struct mio_obj_ext *epa_ret_exts;
};

static int
clovis_comp_obj_extent_query(struct mio_obj_id *layer_id,
			     int nr_exts, struct mio_obj_ext *exts,
			     int *nr_ret_exts, int opcode, struct mio_op *op,
			     mio_driver_op_postprocess pp,
			     struct clovis_comp_obj_exts_pp_args *pp_args)
{
	int i;
        int rc = 0;
	bool set_vals;
        struct m0_bufvec *keys = NULL;
        struct m0_bufvec *vals = NULL;
        int *rcs = NULL;
	struct m0_uint128 layer_id128;
        struct m0_clovis_op *cops[1] = {NULL};
        struct m0_clovis_idx *idx;
        struct m0_clovis_composite_layer_idx_key key;
        struct m0_clovis_composite_layer_idx_val val;

	set_vals = (opcode == M0_CLOVIS_IC_PUT)? true : false;

	idx = mio_mem_alloc(sizeof *idx);
        keys = mio__clovis_bufvec_alloc(nr_exts);
        M0_ALLOC_ARR(rcs, nr_exts);
	if (idx == NULL || keys == NULL || rcs == NULL) {
                rc = -ENOMEM;
                goto err_exit;
        }
	if (opcode != M0_CLOVIS_IC_DEL) {
		vals = mio__clovis_bufvec_alloc(nr_exts);
		if (vals == NULL) {
			rc = -ENOMEM;
			goto err_exit;
		}
	}

	mio__obj_id_to_uint128(layer_id, &layer_id128);
        for (i = 0; i < nr_exts; i++) {
                /* For NEXT op, only set the first key. */
		if (opcode == M0_CLOVIS_IC_NEXT && i >= 1)
			break;

                key.cek_layer_id = layer_id128;
                key.cek_off = exts[i].moe_off;
                rc = m0_clovis_composite_layer_idx_key_to_buf(
                        &key, &keys->ov_buf[i], &keys->ov_vec.v_count[i]);
		if (rc < 0)
			break;
 
		if (set_vals) {
                	val.cev_len = exts[i].moe_size;
			rc = m0_clovis_composite_layer_idx_val_to_buf(
				&val, &vals->ov_buf[i],
				&vals->ov_vec.v_count[i]);
		}
                if (rc < 0)
                        break;
        }
	if (rc != 0)
		goto err_exit;

        mio_memset(idx, 0, sizeof *idx);
        m0_clovis_composite_layer_idx(layer_id128, true, idx);
        m0_clovis_idx_op(idx, opcode, keys, vals, rcs, 0, &cops[0]);

	assert(pp_args != NULL);
	pp_args->epa_idx = idx;
	pp_args->epa_keys = keys;
	pp_args->epa_vals = vals;
	pp_args->epa_rcs = rcs;
	pp_args->epa_ret_exts = exts;
	pp_args->epa_nr_ret_exts = nr_ret_exts;
	rc = mio_driver_op_add(op, pp, pp_args, cops[0]);
	if (rc < 0)
		goto err_exit;

        m0_clovis_op_launch(cops, 1);
	return 0;

err_exit:
	if (cops[0] != NULL) {
		m0_clovis_op_fini(cops[0]);
		m0_clovis_op_free(cops[0]);
	}
        m0_bufvec_free(keys);
        m0_bufvec_free(vals);
        m0_free0(&rcs);
        mio_mem_free(idx);
        return rc;
}

static int clovis_comp_obj_exts_release_pp(struct mio_op *op)
{
	struct clovis_comp_obj_exts_pp_args *pp_args;

	pp_args = (struct clovis_comp_obj_exts_pp_args *)
		  op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;
	mio_mem_free(pp_args->epa_idx);
        m0_bufvec_free(pp_args->epa_keys);
        m0_bufvec_free(pp_args->epa_vals);
        m0_free0(&pp_args->epa_rcs);
	mio_mem_free(pp_args);

	return MIO_DRV_OP_FINAL;
}

static int
mio_clovis_comp_obj_add_extents(struct mio_obj *obj,
				struct mio_obj_id *layer_id,
				int nr_exts, struct mio_obj_ext *exts,
				struct mio_op *op)
{
	int rc;
	struct clovis_comp_obj_exts_pp_args *pp_args;
	
	pp_args = mio_mem_alloc(sizeof *pp_args);
	if (pp_args == NULL)
		return -ENOMEM;

	rc = clovis_comp_obj_extent_query(
		layer_id, nr_exts, exts, NULL, M0_CLOVIS_IC_PUT, op,
		clovis_comp_obj_exts_release_pp, pp_args);
	if (rc < 0)
		mio_mem_free(pp_args);
	return rc;
}

static int
mio_clovis_comp_obj_del_extents(struct mio_obj *obj,
				struct mio_obj_id *layer_id,
				int nr_exts, struct mio_obj_ext *exts,
				struct mio_op *op)
{
	int rc;
	struct clovis_comp_obj_exts_pp_args *pp_args;
	
	pp_args = mio_mem_alloc(sizeof *pp_args);
	if (pp_args == NULL)
		return -ENOMEM;

	rc = clovis_comp_obj_extent_query(
		layer_id, nr_exts, exts, NULL, M0_CLOVIS_IC_DEL, op,
		clovis_comp_obj_exts_release_pp, pp_args);
	if (rc < 0)
		mio_mem_free(pp_args);
	return rc;
}

static int clovis_comp_obj_exts_get_pp(struct mio_op *op)
{
	int i;
	int nr_exts;
        struct m0_bufvec *keys;
        struct m0_bufvec *vals;
	struct m0_clovis_op *cop;
	struct m0_clovis_composite_layer_idx_key ekey;
	struct m0_clovis_composite_layer_idx_val eval;
	struct clovis_comp_obj_exts_pp_args *pp_args;
	struct mio_obj_ext *ret_exts;

	cop = (struct m0_clovis_op *)
	      op->mop_drv_op_chain.mdoc_head->mdo_op;
	assert(cop != NULL);
	if (cop->op_sm.sm_state != M0_CLOVIS_OS_STABLE)
		return -EIO;

	/* Copy returned values. */
	pp_args = (struct clovis_comp_obj_exts_pp_args *)
		  op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;
	ret_exts = pp_args->epa_ret_exts;
	assert(ret_exts != NULL);
	keys  = pp_args->epa_keys;
	vals  = pp_args->epa_vals;
	nr_exts = keys->ov_vec.v_nr;
	for (i = 0; i < nr_exts; i++) {
		m0_clovis_composite_layer_idx_key_from_buf(
			&ekey, keys->ov_buf[i]);
		ret_exts[i].moe_off = ekey.cek_off;
		if (vals->ov_buf[i] == NULL)
			continue;
		m0_clovis_composite_layer_idx_val_from_buf(
			&eval, vals->ov_buf[i]);
		ret_exts[i].moe_size = eval.cev_len;
	}
	assert(pp_args->epa_nr_ret_exts != NULL);
	*pp_args->epa_nr_ret_exts = nr_exts;

	/* Free all allocated resources for the extent query. */
        return clovis_comp_obj_exts_release_pp(op);
}

static int
mio_clovis_comp_obj_get_extents(struct mio_obj *obj,
				struct mio_obj_id *layer_id,
				off_t offset, int nr_exts,
				struct mio_obj_ext *exts, int *nr_ret_exts,
				struct mio_op *op)
{
	int rc;
	struct clovis_comp_obj_exts_pp_args *pp_args;

	pp_args = mio_mem_alloc(sizeof *pp_args);
	if (pp_args == NULL)
		return -ENOMEM;

	mio_memset(exts, 0, nr_exts * sizeof(*exts));
	exts[0].moe_off = offset;
	rc = clovis_comp_obj_extent_query(
			layer_id, nr_exts, exts, nr_ret_exts,
			M0_CLOVIS_IC_NEXT, op,
			clovis_comp_obj_exts_get_pp, pp_args);
	if (rc < 0) {
		mio_mem_free(pp_args);
		return rc;
	}

	return 0;
}

struct mio_comp_obj_ops mio_clovis_comp_obj_ops = {
        .mcoo_create      = mio_clovis_comp_obj_create,
        .mcoo_add_layers  = mio_clovis_comp_obj_add_layers,
        .mcoo_del_layers  = mio_clovis_comp_obj_del_layers,
        .mcoo_list_layers = mio_clovis_comp_obj_list_layers,
        .mcoo_add_extents = mio_clovis_comp_obj_add_extents,
        .mcoo_del_extents = mio_clovis_comp_obj_del_extents,
        .mcoo_get_extents = mio_clovis_comp_obj_get_extents
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
