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
#include "utils.h"
#include "mio.h"
#include "mio_internal.h"
#include "mio_telemetry.h"
#include "driver_motr.h"

/**
 * pp is short for Post-Process to avoid confusion of cb (callback).
 */
static int motr_obj_attrs_query_free_pp(struct mio_op *op);
static int motr_obj_attrs_get_pp(struct mio_op *op);
static int motr_obj_attrs_query(int opcode, struct mio_obj *obj,
				  mio_driver_op_postprocess op_pp,
				  struct mio_op *op);

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

static int motr_obj_open_pp(struct mio_op *op)
{
	int rc;
	struct mio_obj *obj = op->mop_who.obj;
	struct m0_op *cop;

	cop = MIO_MOTR_OP(op);
	rc = m0_rc(cop);
	if (rc < 0)
		return rc;

	/* Launch a new op to get object attributes. */
	rc = motr_obj_attrs_query(M0_IC_GET, obj,
			          motr_obj_attrs_get_pp, op);
	if (rc < 0)
		return rc;
	else
		return MIO_DRV_OP_NEXT;
}

static int mio_motr_obj_open(struct mio_obj *obj, struct mio_op *op)
{
	int rc;
	struct m0_uint128 id128;
	struct m0_obj *cobj;
	struct m0_op *cops[1] = {NULL};

	cobj = mio_mem_alloc(sizeof *cobj);
	if (cobj == NULL)
		return -ENOMEM;

	mio__obj_id_to_uint128(&obj->mo_id, &id128);
	m0_obj_init(cobj, &mio_motr_container.co_realm, &id128,
			   mio_drv_motr_conf->mc_default_layout_id);
	rc = m0_entity_open(&cobj->ob_entity, &cops[0]);
	if (rc != 0)
		goto error;

	obj->mo_drv_obj = (void *)cobj;
	rc = mio_driver_op_add(op, motr_obj_open_pp, NULL, NULL,
			       cops[0], NULL);
	if (rc < 0)
		goto error;
	m0_op_launch(cops, 1);
	return 0;

error:
	if (cops[0] != NULL) {
		m0_op_fini(cops[0]);
		m0_op_free(cops[0]);
	}
	m0_obj_fini(cobj);
	mio_mem_free(cobj);
	return rc;
}

static int mio_motr_obj_close(struct mio_obj *obj)
{
	int rc = 0;
	struct mio_op mop;
	struct m0_op *cop;

	if (!obj->mo_attrs_updated)
		goto obj_fini;
#if 0
	mio_log(MIO_DEBUG,
		"[mio_motr_obj_close] rcount = %lu, wcount = %lu\n",
		obj->mo_attrs.moa_stats.mos_rcount,
		obj->mo_attrs.moa_stats.mos_wcount);
#endif

	/* Update object attributes to metada key-value store. */
	mio_op_init(&mop);
	mio_obj_op_init(&mop, obj, MIO_OBJ_ATTRS_SET);
	rc = motr_obj_attrs_query(M0_IC_PUT, obj,
				  motr_obj_attrs_query_free_pp, &mop);
	if (rc < 0)
		return rc;

	cop = MIO_MOTR_OP((&mop));
	rc = m0_op_wait(cop,
			M0_BITS(M0_OS_FAILED,
				M0_OS_STABLE),
			M0_TIME_NEVER);
	rc = rc? : m0_rc(cop);
	m0_op_fini(cop);
	m0_op_free(cop);

obj_fini:
	/* Finalise motr's object. */
	m0_obj_fini((struct m0_obj *)obj->mo_drv_obj);
	return rc;
}

void
mio__motr_pool_id_to_fid(const struct mio_pool_id *pool_id, struct m0_fid *fid)
{
	fid->f_container = pool_id->mpi_hi;
	fid->f_key = pool_id->mpi_lo;
}

void
mio__motr_fid_to_pool_id(const struct m0_fid *fid, struct mio_pool_id *pool_id)
{
	pool_id->mpi_hi = fid->f_container;
	pool_id->mpi_lo = fid->f_key;
}

static int mio_motr_obj_create(const struct mio_pool_id *pool_id,
			       struct mio_obj *obj, struct mio_op *op)
{
	int rc = 0;
	struct m0_fid pfid;
	struct m0_fid *ptr_pfid = NULL;
	struct m0_uint128 id128;
	struct m0_obj *cobj;
	struct m0_op *cops[1] = {NULL};

	if (pool_id != NULL) {
		mio__motr_pool_id_to_fid(pool_id, &pfid);
		ptr_pfid = &pfid;
	}

	cobj = mio_mem_alloc(sizeof *cobj);
	if (cobj == NULL)
		return -ENOMEM;

	mio__obj_id_to_uint128(&obj->mo_id, &id128);
	m0_obj_init(cobj, &mio_motr_container.co_realm, &id128,
			   mio_drv_motr_conf->mc_default_layout_id);
	rc = m0_entity_create(ptr_pfid, &cobj->ob_entity, &cops[0]);
	if (rc < 0)
		goto error;

	obj->mo_drv_obj = (void *)cobj;
	rc = mio_driver_op_add(op, NULL, NULL, NULL, cops[0], NULL);
	if (rc < 0)
		goto error;
	m0_op_launch(cops, 1);
	return 0;

error:
	if (cops[0] != NULL) {
		m0_op_fini(cops[0]);
		m0_op_free(cops[0]);
	}
	m0_obj_fini(cobj);
	mio_mem_free(cobj);
	return rc;
}

static int motr_obj_delete_pp(struct mio_op *op)
{
	int rc = 0;
	struct mio_obj *mobj = op->mop_who.obj;
	struct m0_obj *cobj = (struct m0_obj *)mobj->mo_drv_obj;

	/* Launch a new op to delete this object's attributes. */
	rc = motr_obj_attrs_query(M0_IC_DEL, mobj,
				    motr_obj_attrs_query_free_pp, op);

	/* The opened object is not needed any more. */
	mio_mem_free(cobj);
	mio_mem_free(mobj);

	if (rc < 0)
		return rc;
	else
		return MIO_DRV_OP_NEXT;
}

static int
motr_obj_delete_open_pp(struct mio_op *op)
{
	int rc = 0;
	struct m0_op *cops[1] = {NULL};
	struct m0_obj *cobj;
	struct mio_obj *mobj = op->mop_who.obj;

	cobj = (struct m0_obj *)mobj->mo_drv_obj;
	rc = m0_entity_delete(&cobj->ob_entity, &cops[0]);
	if (rc != 0) {
		mio_log(MIO_ERROR, "Creating DELETE op failed!\n");
		goto error;
	}

	rc = mio_driver_op_add(op, motr_obj_delete_pp, NULL, NULL,
			       cops[0], NULL);
	if (rc < 0)
		goto error;
	m0_op_launch(cops, ARRAY_SIZE(cops));
	return MIO_DRV_OP_NEXT;

error:
	if (cops[0] != NULL) {
		m0_op_fini(cops[0]);
		m0_op_free(cops[0]);
	}
	m0_obj_fini(cobj);
	mio_mem_free(cobj);
	mio_mem_free(mobj);
	return rc;
}

/**
 * Deleting an object takes the following steps:
 * (1) Open the object to fetch required Motr-wise object attributes.
 * (2) Create and launch DELETE op to remove the object data.
 * (3) Create and launch an KVS DEL op to remove this object MIO attributes.
 */
static int
mio_motr_obj_delete(const struct mio_obj_id *oid, struct mio_op *op)
{
	int rc = 0;
	struct m0_uint128 id128;
	struct m0_obj *cobj;
	struct m0_op *cops[1] = {NULL};
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
	m0_obj_init(cobj, &mio_motr_container.co_realm, &id128,
			   mio_drv_motr_conf->mc_default_layout_id);
	rc = m0_entity_open(&cobj->ob_entity, &cops[0]);
	if (rc != 0) {
		mio_log(MIO_ERROR,
			"Creating OPEN op for object deletion failed!\n");
		goto error;
	}

	rc = mio_driver_op_add(op, motr_obj_delete_open_pp,
			       NULL, NULL, cops[0], NULL);
	if (rc < 0)
		goto error;
	m0_op_launch(cops, ARRAY_SIZE(cops));
	return 0;

error:
	if (cops[0] != NULL) {
		m0_op_fini(cops[0]);
		m0_op_free(cops[0]);
	}
	m0_obj_fini(cobj);
	mio_mem_free(cobj);
	mio_mem_free(mobj);
	return rc;
}

/**
 * Object read/write arguments.
 */
struct motr_obj_rw_args {
	struct mio_obj *rwa_obj;

	bool rwa_is_write;
	uint64_t rwa_max_eow;

	/* Original read/write IO vectors. */
	int rwa_orig_iovcnt;
	const struct mio_iovec *rwa_orig_iovs;

	struct mio_iovec *rwa_sorted_iovs;

	/* Aligned IO vector which may split a byte range into a few. */
	int rwa_aligned_progress;
	int rwa_aligned_iovcnt;
	struct mio_iovec *rwa_aligned_iovs;

	 /* rbw - read before write. */
	int rwa_rbw_progress;
	int rwa_rbw_iovcnt;
	struct mio_iovec *rwa_rbw_iovs;

	/*
	 * The memory areas to copy data to/from. The mio_iovec struct
	 * is used to locate the area, but `offset` is the one inside the
	 * memory area not object offset.
	 *
	 * dc - data copy.
	 */
	int rwa_dc_iovcnt;
	struct mio_iovec *rwa_dc_src_iovs;
	struct mio_iovec *rwa_dc_dst_iovs;

	/* Extra memory areas in order to align IO vectors. */
	int rwa_nr_extra_pages;
	char **rwa_extra_pages;
};

/* For motr write/read op. */
struct motr_obj_rw_op_args {
	struct m0_indexvec *rwoa_motr_rw_ext;
	struct m0_bufvec *rwoa_motr_rw_data;
	struct m0_bufvec *rwoa_motr_rw_attr;
};

static int motr_obj_io_pagesize(struct mio_obj *obj)
{
	struct m0_obj *cobj;

	cobj = (struct m0_obj *)obj->mo_drv_obj;
	return 1<<cobj->ob_attr.oa_bshift;
}

static int
motr_obj_max_size_per_op(struct mio_obj *obj, uint64_t *max_size_per_op)
{
        struct m0_obj *cobj;
        struct m0_pool_version *pver;
        struct m0_pdclust_attr *pa;
        struct mio_motr_config *motr_config;

        motr_config = (struct mio_motr_config *)mio_instance->m_driver_confs;
        cobj = (struct m0_obj *)obj->mo_drv_obj;
        if (motr_config == NULL || cobj == NULL)
                return -EINVAL;

        pver = m0_conf_fid_is_valid(&cobj->ob_attr.oa_pver) == false? NULL:
               m0_pool_version_find(&mio_motr_instance->m0c_pools_common,
                                    &cobj->ob_attr.oa_pver);
        if (pver == NULL)
                return -EINVAL;
        pa = &pver->pv_attr;
        *max_size_per_op =
                motr_config->mc_max_iosize_per_dev * pa->pa_N *
                pa->pa_P / (pa->pa_N + pa->pa_K);
        return 0;
}

static int 
motr_obj_rw_args_estimate_iovcnts(struct mio_obj *obj, int iovcnt,
				    const struct mio_iovec *iovs,
				    int *aligned_iovcnt, int *dc_iovcnt)
{
	int i;
	int rc = 0;
	int dc_cnt = 0;
	int aligned_cnt = 0;
	uint64_t max_size_per_op;

	rc = motr_obj_max_size_per_op(obj, &max_size_per_op);
	if (rc < 0)
		return rc;

	for (i = 0; i < iovcnt; i++) {
		dc_cnt += 2;
		aligned_cnt +=
		    (iovs[i].miov_len + max_size_per_op - 1) / max_size_per_op;
		aligned_cnt += 2;
	}
	*aligned_iovcnt = aligned_cnt;
	*dc_iovcnt = dc_cnt;
	return 0;
}

static void motr_obj_rw_args_free(struct motr_obj_rw_args *args)
{
	int i;

	mio_mem_free(args->rwa_rbw_iovs);
	mio_mem_free(args->rwa_dc_src_iovs);
	mio_mem_free(args->rwa_dc_dst_iovs);
	mio_mem_free(args->rwa_aligned_iovs);
	mio_mem_free(args->rwa_sorted_iovs);

	for (i = 0; i < args->rwa_nr_extra_pages; i++)
		mio_mem_free(args->rwa_extra_pages[i]);

	mio_mem_free(args);
}

static struct motr_obj_rw_args*
motr_obj_rw_args_alloc(struct mio_obj *obj, int iovcnt,
			 const struct mio_iovec *iovs)
{
	int rc;
	int dc_iovcnt = 0;
	int aligned_iovcnt = 0;
	struct motr_obj_rw_args *args;

	args = mio_mem_alloc(sizeof *args);
	if (args == NULL)
		return NULL;

	args->rwa_obj = obj;
	args->rwa_orig_iovcnt = iovcnt;
	args->rwa_orig_iovs = iovs;

	args->rwa_sorted_iovs =
		mio_mem_alloc(iovcnt * sizeof(struct mio_iovec));
	if (args->rwa_sorted_iovs == NULL)
		goto error;

	rc = motr_obj_rw_args_estimate_iovcnts(
		obj, iovcnt, iovs, &aligned_iovcnt, &dc_iovcnt);
	if (rc < 0)
		goto error;

	args->rwa_aligned_iovs =
		mio_mem_alloc(aligned_iovcnt * sizeof(struct mio_iovec));
	if (args->rwa_aligned_iovs == NULL)
		goto error;

	args->rwa_rbw_iovs =
		mio_mem_alloc(dc_iovcnt * sizeof(struct mio_iovec));
	if (args->rwa_rbw_iovs == NULL)
		goto error;

	args->rwa_dc_src_iovs =
		mio_mem_alloc(dc_iovcnt * sizeof(struct mio_iovec));
	if (args->rwa_dc_src_iovs == NULL)
		goto error;
	args->rwa_dc_dst_iovs =
		mio_mem_alloc(dc_iovcnt * sizeof(struct mio_iovec));
	if (args->rwa_dc_dst_iovs == NULL)
		goto error;

	args->rwa_extra_pages =
		mio_mem_alloc(dc_iovcnt * sizeof(char *));
	if (args->rwa_extra_pages == NULL)
		goto error;


	return args;

error:
	motr_obj_rw_args_free(args);
	return NULL;
}

static int
motr_obj_iovec_sort(struct motr_obj_rw_args *args)
{
	int i;
	int j;
	int rc = 0;
	int min_idx;
	uint64_t min_off;
	uint64_t off;
	uint64_t len;
	char *base;
	int orig_iovcnt;
	const struct mio_iovec *orig_iovs;
	struct mio_iovec *sorted_iovs;
	uint64_t max_eow; /* eow: End Of Write */

	orig_iovcnt = args->rwa_orig_iovcnt;
	orig_iovs = args->rwa_orig_iovs;
	sorted_iovs = args->rwa_sorted_iovs;
	mio_mem_copy((char *)sorted_iovs, (char *)orig_iovs,
		     orig_iovcnt * sizeof(struct mio_iovec));

	/*
	 * Sort IO vector by offset to check if there is any
	 * overlapped byte ranges.
	 */
	for (i = 0; i < orig_iovcnt; i++) {
		min_idx = i;
		min_off = sorted_iovs[i].miov_off;
		for (j = i + 1; j < orig_iovcnt; j++) {
			if (min_off > sorted_iovs[j].miov_off) {
				min_idx = j;
				min_off = sorted_iovs[j].miov_off;
			}
		}
		if (min_idx == i)
			continue;

		off = sorted_iovs[i].miov_off;
		len = sorted_iovs[i].miov_len;
		base = sorted_iovs[i].miov_base;
		sorted_iovs[i].miov_off = sorted_iovs[min_idx].miov_off;
		sorted_iovs[i].miov_len = sorted_iovs[min_idx].miov_len;
		sorted_iovs[i].miov_base = sorted_iovs[min_idx].miov_base;
		sorted_iovs[min_idx].miov_off = off;
		sorted_iovs[min_idx].miov_len = len;
		sorted_iovs[min_idx].miov_base = base;
	}

	/* Check if IO vectors are overlapping and End of Write. */
	max_eow = sorted_iovs[0].miov_off + sorted_iovs[0].miov_len;
	for (i = 1; i < orig_iovcnt; i++) {
		off = sorted_iovs[i - 1].miov_off;
		len = sorted_iovs[i - 1].miov_len;
		/* base = sorted_iovs[i - 1].miov_base; */
		if (sorted_iovs[i].miov_off < off + len) {
			rc = -EINVAL;
			break;
		}

		if (args->rwa_is_write &&
		    sorted_iovs[i].miov_off + sorted_iovs[i].miov_len > max_eow)
			max_eow = sorted_iovs[i].miov_off +
				  sorted_iovs[i].miov_len;
	}
	if (args->rwa_is_write && rc == 0)
		args->rwa_max_eow = max_eow;

	return rc;
}

static void motr_obj_iovec_set(struct mio_iovec *iov,
				 uint64_t off, size_t len, char *base)
{
	iov->miov_off = off;
	iov->miov_len = len;
	iov->miov_base = base;
}

static int
motr_obj_iovec_get_page(struct mio_obj *obj, struct mio_iovec *iov,
			  uint64_t off, size_t length)
{
	int pagesize;
	char *base;

	pagesize = motr_obj_io_pagesize(obj);
	base = mio_mem_alloc(pagesize);
	if (base == NULL)
		return -ENOMEM;

	motr_obj_iovec_set(iov, off, length, base);
	return 0;
}

static int
motr_obj_iovec_partial_page(struct motr_obj_rw_args *args,
			      uint64_t off, uint64_t len, char *base)
{
	int rc = 0;
	int pagesize;
	uint64_t aligned_off;
	uint64_t aligned_len;
	struct mio_iovec *aligned_iov;
	struct mio_iovec *prev_iov;
	struct mio_iovec *dc_src_iov;
	struct mio_iovec *dc_dst_iov;

	if (len == 0) {
		mio_log(MIO_WARN, "Length of the IO page is 0!\n");
		return 0;
	}

	pagesize = motr_obj_io_pagesize(args->rwa_obj);
	aligned_off = (off / pagesize) * pagesize;
	aligned_len = pagesize;

	aligned_iov = args->rwa_aligned_iovs + args->rwa_aligned_iovcnt;
	if (args->rwa_aligned_iovcnt != 0)
		prev_iov = args->rwa_aligned_iovs +
			   (args->rwa_aligned_iovcnt - 1);
	else
		prev_iov = NULL;
    	if (prev_iov != NULL && prev_iov->miov_off == aligned_off)
		aligned_iov = prev_iov;
	else {
		rc = motr_obj_iovec_get_page(
			args->rwa_obj, aligned_iov, aligned_off, aligned_len);
		if (rc < 0)
			return rc;
		args->rwa_aligned_iovcnt++;

		args->rwa_extra_pages[args->rwa_nr_extra_pages] =
			aligned_iov->miov_base;
		args->rwa_nr_extra_pages++;
	}

	dc_src_iov = args->rwa_dc_src_iovs + args->rwa_dc_iovcnt;
	dc_dst_iov = args->rwa_dc_dst_iovs + args->rwa_dc_iovcnt;
	if (args->rwa_is_write) {
		struct mio_iovec *rbw_iov;

		/*
		 * No reads beyond the object size are issued.
		 * Don't set the same page twice.
		 */
		if (aligned_off < args->rwa_obj->mo_attrs.moa_size &&
		    prev_iov != aligned_iov) {
			rbw_iov = args->rwa_rbw_iovs + args->rwa_rbw_iovcnt;
			motr_obj_iovec_set(rbw_iov, aligned_off, aligned_len,
					     aligned_iov->miov_base);
			args->rwa_rbw_iovcnt++;
		}

		motr_obj_iovec_set(dc_src_iov, 0, len, base);
		motr_obj_iovec_set(dc_dst_iov, off % pagesize,
				     len, aligned_iov->miov_base);
	} else { /* Read. */
		motr_obj_iovec_set(dc_src_iov, off % pagesize,
				     len, aligned_iov->miov_base);
		motr_obj_iovec_set(dc_dst_iov, 0, len, base);
	}
	args->rwa_dc_iovcnt++;

	return rc;
}

static int
motr_obj_iovec_clone(struct motr_obj_rw_args *args, uint64_t off,
		       uint64_t len, char *base)
{
	int rc;
	uint64_t off1;
	uint64_t len_left;
	char *base1;
	int pagesize;
	uint64_t max_size_per_op;

	rc = motr_obj_max_size_per_op(args->rwa_obj, &max_size_per_op);
	if (rc < 0)
		return rc;

	pagesize = motr_obj_io_pagesize(args->rwa_obj);
	if (off % pagesize != 0 || len % pagesize != 0)
		return -EINVAL;

	off1 = off;
	base1 = base;
	len_left = len;
	while (off1 < off + len) {
		uint64_t len1;
		struct mio_iovec *aligned_iov;

		len1 = len_left > max_size_per_op? max_size_per_op : len_left;
		aligned_iov = args->rwa_aligned_iovs + args->rwa_aligned_iovcnt;
		motr_obj_iovec_set(aligned_iov, off1, len1, base1);

		off1 += len1;
		base1 += len1;
		len_left -= len1;
		args->rwa_aligned_iovcnt++;
	}

	return 0;
}

static int
motr_obj_iovec_split_3(struct motr_obj_rw_args *args, int iov_idx)
{
	int rc = 0;
	int pagesize;
	uint64_t off;
	uint64_t off1;
	uint64_t off2;
	uint64_t off3;
	uint64_t len;
	uint64_t len1 = 0;
	uint64_t len2;
	uint64_t len3;
	char *base;
	char *base1;
	char *base2;
	char *base3;
	uint64_t first_page;
	uint64_t last_page;
	const struct mio_iovec *sorted_iov;

	pagesize = motr_obj_io_pagesize(args->rwa_obj);
	sorted_iov = args->rwa_sorted_iovs + iov_idx;
	off = sorted_iov->miov_off;
	base = sorted_iov->miov_base;
	len = sorted_iov->miov_len;

	first_page = off / pagesize;
	last_page = (off + len - 1) / pagesize;
	if (last_page - first_page < 2)
		return -EINVAL;

	/* The first part. */
	off1 = off;
	len1 = pagesize - (off % pagesize);
	base1 = base;
	rc = motr_obj_iovec_partial_page(args, off1, len1, base1);
	if (rc < 0)
		return rc;

	/*
	 * The second part includes all pages except the last one, but
	 * if the IO vector's end is aligned, count the last page in
	 * as well.
	 */
	base2 = base1 + len1;
	off2 = off1 + len1;
	if ((off + len) % pagesize != 0)
		len2 = (last_page - first_page - 1) * pagesize;
	else
		len2 = (last_page - first_page) * pagesize;
	motr_obj_iovec_clone(args, off2, len2, base2);

	/* The third  part. */
	off3 = off2 + len2;
	len3 = len - len1 - len2;
	base3 = base2 + len2;
	if (len3 != 0)
		rc = motr_obj_iovec_partial_page(args, off3, len3, base3);
	return rc;
}

/**
 * In only the following cases, the IO vector is divided into 2 parts.
 * (1) The offset is aligned to page size, but the length isn't multiple of
 *     page size. The first part covers m * page size, m >= 1. The second
 *     part covers the last page.
 * (2) The offset isn't aligned to page size and the area only covers 2 pages.
 *     (if the area covers more than 2 pages, it will be divided into 3 parts.
 *     See motr_obj_iovec_split_3().)
 */
static int
motr_obj_iovec_split_2(struct motr_obj_rw_args *args, int iov_idx)
{
	int rc = 0;
	int pagesize;
	uint64_t off;
	uint64_t off1;
	uint64_t off2;
	uint64_t len;
	uint64_t len1;
	uint64_t len2;
	char *base;
	char *base1;
	char *base2;
	const struct mio_iovec *sorted_iov;

	pagesize = motr_obj_io_pagesize(args->rwa_obj);
	sorted_iov = args->rwa_sorted_iovs + iov_idx;
	off = sorted_iov->miov_off;
	base = sorted_iov->miov_base;
	len = sorted_iov->miov_len;

	/* The first part. */
	if (off % pagesize == 0) {
		/* The orignal memory area is re-used. */
		off1 = off;
		len1 = len - (off + len) % pagesize;
		base1 = sorted_iov->miov_base;
		motr_obj_iovec_clone(args, off1, len1, base1);
	} else {
		off1 = off;
		len1 = pagesize - off % pagesize;
		base1 = base;
		rc = motr_obj_iovec_partial_page(args, off1, len1, base1);
		if (rc < 0)
			return rc;
	}

	/* The second part (<= pagesize). */
	off2 = off1 + len1;
	len2 = len - len1;
	base2 = base + len1;
	if (len2 == pagesize)
		motr_obj_iovec_clone(args, off2, len2, base2);
	else
		rc = motr_obj_iovec_partial_page(args, off2, len2, base2);
	return rc;
}

static int
motr_obj_iovec_adjust_with_nonaligned_off(struct motr_obj_rw_args *args,
					    int iov_idx)
{
	int rc;
	int pagesize;
	uint64_t off;
	uint64_t len;
	char *base;
	const struct mio_iovec *sorted_iov;
	uint64_t first_page;
	uint64_t last_page;

	pagesize = motr_obj_io_pagesize(args->rwa_obj);
	sorted_iov = args->rwa_sorted_iovs + iov_idx;
	off = sorted_iov->miov_off;
	base = sorted_iov->miov_base;
	len = sorted_iov->miov_len;

	first_page = off / pagesize;
	last_page = (off + len - 1) / pagesize;

	/* First block and the last one happen to be the same. */
	if (first_page == last_page) {
		rc = motr_obj_iovec_partial_page(args, off, len, base);
		return rc;
	}

	/*
	 * The last page is next to the first one, merge them into
	 * one byte range.
	 */
	if (last_page == first_page + 1) {
		rc = motr_obj_iovec_split_2(args, iov_idx);
		return rc;
	}

	/*
 	 * 3 parts.
 	 */
	rc = motr_obj_iovec_split_3(args, iov_idx);
	return rc;
}

static int
motr_obj_iovec_adjust_with_aligned_off(struct motr_obj_rw_args *args,
					 int iov_idx)
{
	int rc;
	int pagesize;
	uint64_t off;
	uint64_t len;
	char *base;
	const struct mio_iovec *sorted_iov;

	pagesize = motr_obj_io_pagesize(args->rwa_obj);
	sorted_iov = args->rwa_sorted_iovs + iov_idx;
	off = sorted_iov->miov_off;
	base = sorted_iov->miov_base;
	len = sorted_iov->miov_len;

	if (len > pagesize) {
		rc = motr_obj_iovec_split_2(args, iov_idx);
		return rc;
	}

	/* only one block. */
	rc = motr_obj_iovec_partial_page(args, off, len, base);
	return rc;
}

/*
 * Align byte ranges in the IO vector with page size (4KB for default).
 * It also merges and split byte ranges in order to make all byte ranges
 * aligned to page size and at the same time do as least data
 * copy as possible.
 *
 * The byte ranges in IO vector have been sorted by offsets.
 */
static int motr_obj_iovec_adjust(struct motr_obj_rw_args *args)
{
	int i;
	int rc = 0;
	uint64_t off;
	uint64_t len;
	char *base;
	int pagesize;
	int sorted_iovcnt;
	const struct mio_iovec *sorted_iov;

	pagesize = motr_obj_io_pagesize(args->rwa_obj);

	/*
 	 * A byte range is represented with offset and length.
 	 * (1) offset % PAGESIZE == 0 && length % PAGESIZE == 0
 	 *     this byte range is aligned with page size. Keep this byte
 	 *     range without change.
 	 * (2) offset % PAGESIZE ==0 && length % PAGESIZE != 0
 	 *     this byte range is splitted into 2 parts, the 2nd part
 	 *     covers the last block.
 	 * (3) offset % PAGESIZE !=0 && length % PAGESIZE != 0
 	 *     this byte range is splitted into 3 parts. The first and
 	 *     last parts may merge with blocks from other byte ranges.
 	 */
	sorted_iovcnt = args->rwa_orig_iovcnt;
	for (i = 0; i < sorted_iovcnt; i++) {
		sorted_iov = args->rwa_sorted_iovs + i;
		off = sorted_iov->miov_off;
		len = sorted_iov->miov_len;
		base = sorted_iov->miov_base;

		/* The 1st case .*/
		if (off % pagesize == 0 && len % pagesize == 0) {
			motr_obj_iovec_clone(args, off, len, base);
			continue;
		}

		/* The 2nd case .*/
		if (off % pagesize == 0 && len % pagesize != 0) {
			rc = motr_obj_iovec_adjust_with_aligned_off(args, i);
			if (rc < 0)
				goto error;
			continue;
		}

		/* The 3rd case .*/
		rc = motr_obj_iovec_adjust_with_nonaligned_off(args, i);
		if (rc < 0)
			goto error;
	}

error:
	return rc;
}

static void motr_obj_data_copy(struct motr_obj_rw_args *args)
{
	int i;

	for (i = 0; i < args->rwa_dc_iovcnt; i++) {
		struct mio_iovec *src;
		struct mio_iovec *dst;

		src = args->rwa_dc_src_iovs + i;
		dst = args->rwa_dc_dst_iovs + i;
		mio_mem_copy(dst->miov_base + dst->miov_off,
			     src->miov_base + src->miov_off,
			     dst->miov_len);
	}
}

static void
motr_obj_rw_buf_index_vecs_free(struct m0_indexvec *ext,
				  struct m0_bufvec *data,
				  struct m0_bufvec *attr)
{
	m0_indexvec_free(ext);
	mio_mem_free(ext);

	/*
	 * As the memory address set in `data` bufvec come from application,
	 * don't free here, it's the application's job to do it.
	 */
	mio_mem_free(data->ov_buf);
	mio_mem_free(data->ov_vec.v_count);
	mio_mem_free(data);

	m0_bufvec_free(attr);
	mio_mem_free(attr);
}

static int
motr_obj_rw_buf_index_vecs_alloc(struct m0_indexvec **ext,
				   struct m0_bufvec **data,
				   struct m0_bufvec **attr, int iovcnt)
{
	int rc = 0;

	*ext = mio_mem_alloc(sizeof(struct m0_indexvec));
	*data = mio_mem_alloc(sizeof(struct m0_bufvec));
	*attr = mio_mem_alloc(sizeof(struct m0_bufvec));
	if (*ext == NULL || *data == NULL || *attr == NULL)
		goto error;
	/* Allocate memory for bufvec and indexvec. */
	rc = m0_bufvec_empty_alloc(*data, iovcnt) ? :
	     m0_bufvec_alloc(*attr, iovcnt, 1) ? :
	     m0_indexvec_alloc(*ext, iovcnt);

	if (rc < 0)
		goto error;
	else
		return 0;

error:
	motr_obj_rw_buf_index_vecs_free(*ext, *data, *attr);
	return rc;
}

static int
motr_obj_io_op_fini(struct mio_driver_op *dop)
{
	struct motr_obj_rw_op_args *args;

	args = (struct motr_obj_rw_op_args *)dop->mdo_op_args;
	motr_obj_rw_buf_index_vecs_free(args->rwoa_motr_rw_ext,
					  args->rwoa_motr_rw_data,
					  args->rwoa_motr_rw_attr);
	mio_mem_free(args);
	return 0;
}

static int motr_obj_rw_one_op(struct mio_obj *obj,
				const struct mio_iovec *iov, int iovcnt,
				enum m0_obj_opcode opcode,
				mio_driver_op_postprocess op_pp,
				struct motr_obj_rw_args *op_pp_args,
				struct mio_op *op)
{
	int i;
	int rc = 0;
	struct m0_obj *cobj;
	struct m0_op  *cops[1] = {NULL};
	struct motr_obj_rw_op_args *op_args;
	struct m0_indexvec *ext;
	struct m0_bufvec *data;
	struct m0_bufvec *attr;

	assert(opcode == M0_OC_READ || opcode == M0_OC_WRITE);

	if (iovcnt < 1)
		return -EINVAL;

	/* Allocate memory for bufvec and indexvec. */
	op_args = mio_mem_alloc(sizeof(struct motr_obj_rw_op_args));
	if (op_args == NULL)
		return -ENOMEM;
	rc = motr_obj_rw_buf_index_vecs_alloc(&op_args->rwoa_motr_rw_ext,
						&op_args->rwoa_motr_rw_data,
						&op_args->rwoa_motr_rw_attr,
						iovcnt);
	if (rc < 0) {
		mio_mem_free(op_args);
		return rc;
	}
	ext = op_args->rwoa_motr_rw_ext;
	data = op_args->rwoa_motr_rw_data;
	attr = op_args->rwoa_motr_rw_attr;

	/*
	 * Populate bufvec and indexvec. Avoid copying data
	 * into bufvec.
	 */
	for (i = 0; i < iovcnt; i++) {
		data->ov_vec.v_count[i] = iov[i].miov_len;
		data->ov_buf[i] = iov[i].miov_base;

		ext->iv_index[i] = iov[i].miov_off;
		ext->iv_vec.v_count[i] = iov[i].miov_len;

		/* we don't want any attributes */
		attr->ov_vec.v_count[i] = 0;
	}

	/* Create and launch an RW op. */
	cobj = (struct m0_obj *)obj->mo_drv_obj;
	m0_obj_op(cobj, opcode, ext, data, attr, 0, 0, &cops[0]);

	/* Set callback and then launch IO op. */
	rc = mio_driver_op_add(op, op_pp, op_pp_args,
			       motr_obj_io_op_fini, cops[0], op_args);
	if (rc < 0)
		goto error;

	mio_telemetry_array_advertise_noprefix(
		"mio-op-to-motr-io", MIO_TM_TYPE_ARRAY_UINT64,
		3, obj->mo_sess_seqno, op->mop_seqno, cops[0]->op_sm.sm_id);
	m0_op_launch(cops, ARRAY_SIZE(cops));
	return 0;

error:
	motr_obj_rw_buf_index_vecs_free(ext, data, attr);
	return rc;
}

static int
motr_obj_rw_aligned(struct mio_obj *obj, struct mio_iovec *iovs,
		      int iovcnt, int *iov_cursor,
		      enum m0_obj_opcode opcode,
		      mio_driver_op_postprocess op_pp,
		      struct motr_obj_rw_args *op_pp_args, struct mio_op *op)
{
	int i;
	int rc;
	int iovcnt_to_rw = 0;
	uint64_t io_size = 0;
	uint64_t max_size_per_op;
	struct mio_iovec *st_iov;

	rc = motr_obj_max_size_per_op(obj, &max_size_per_op);
	if (rc < 0)
		return rc;

	st_iov = iovs + (*iov_cursor);
	for (i = *iov_cursor; i < iovcnt; i++) {
		if (io_size + iovs[i].miov_len  > max_size_per_op)
			break;
		io_size += iovs[i].miov_len;
		iovcnt_to_rw++;
	}
	if (i == *iov_cursor) {
		mio_log(MIO_ERROR, "The IO vector is too big!\n");
		return -E2BIG;
	}
	*iov_cursor = *iov_cursor + iovcnt_to_rw;

	return motr_obj_rw_one_op(obj, st_iov, iovcnt_to_rw, opcode,
				    op_pp, op_pp_args, op);
}

static int motr_obj_write_pp(struct mio_op *op)
{
	int rc;
	struct motr_obj_rw_args *args;
	struct mio_obj *obj = op->mop_who.obj;

	args = (struct motr_obj_rw_args *)
		  op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;

	/* Check if all IO vectors done. */
	if (args->rwa_aligned_progress != args->rwa_aligned_iovcnt) {
		rc = motr_obj_rw_aligned(obj, args->rwa_aligned_iovs,
					   args->rwa_aligned_iovcnt,
					   &args->rwa_aligned_progress,
					   M0_OC_WRITE,
					   motr_obj_write_pp, args, op);
	} else {
		motr_obj_rw_args_free(args);

		/* Launch a new op to update object size. */
		if (args->rwa_max_eow <= obj->mo_attrs.moa_size)
			return MIO_DRV_OP_FINAL;
		obj->mo_attrs.moa_size = args->rwa_max_eow;
		rc = motr_obj_attrs_query(M0_IC_PUT, obj,
					    motr_obj_attrs_query_free_pp, op);
	}
	if (rc < 0)
		return rc;
	else
		return MIO_DRV_OP_NEXT;
}

static int motr_obj_read_before_write_pp(struct mio_op *op)
{
	int rc;
	struct motr_obj_rw_args *args;
	struct mio_obj *obj = op->mop_who.obj;

	args = (struct motr_obj_rw_args *)
		  op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;

	if (args->rwa_rbw_progress == args->rwa_rbw_iovcnt) {
		motr_obj_data_copy(args);
		rc = motr_obj_rw_aligned(obj, args->rwa_aligned_iovs,
					   args->rwa_aligned_iovcnt,
					   &args->rwa_aligned_progress,
					   M0_OC_WRITE,
				  	   motr_obj_write_pp, args, op);
	} else
		rc = motr_obj_rw_aligned(obj, args->rwa_rbw_iovs,
					   args->rwa_rbw_iovcnt,
					   &args->rwa_rbw_progress,
					   M0_OC_WRITE,
				  	   motr_obj_read_before_write_pp,
					   args, op);

	if (rc < 0)
		return rc;
	else
		return MIO_DRV_OP_NEXT;
}

/*
 * A scan is carried to find the data copy vectors which cover a whole page,
 * no read-before-write for the page is needed. As read-before-write vectors
 * are ordered by offset and are not overlapped, the memory vectors
 * (motr_obj_rw_args::rwa_dc_dst_iovs) with the same `base` sit next to
 * each other.
 */
static void
motr_obj_read_before_write_optimise(struct motr_obj_rw_args *args)
{
	int i;
	int j;
	int k;
	int dst_cursor = 0;
	int pagesize;
	int rbw_iovcnt;
	int dst_iovcnt;
	struct mio_iovec *rbw_iovs;
	struct mio_iovec *dst_iovs;

	pagesize = motr_obj_io_pagesize(args->rwa_obj);
	rbw_iovcnt = args->rwa_rbw_iovcnt;
	rbw_iovs = args->rwa_rbw_iovs;
	dst_iovcnt = args->rwa_dc_iovcnt;
	dst_iovs = args->rwa_dc_dst_iovs;
	for (i = 0; i < rbw_iovcnt; i++) {
		uint64_t st_off;
		uint64_t len;

		for (j = dst_cursor; j < dst_iovcnt; j++) {
			if (rbw_iovs[i].miov_base == dst_iovs[j].miov_base)
				break;
		}

		len = dst_iovs[j].miov_len;
		st_off = dst_iovs[j].miov_off;
		if (st_off != 0)
			break;

		for (k = j + 1; k < dst_iovcnt; k++) {
			if (len == pagesize)
				break;

			if (dst_iovs[j].miov_base !=
			    dst_iovs[k].miov_base)
				break;

			if (dst_iovs[k].miov_off > st_off + len)
				break;
			len += dst_iovs[k].miov_len;
		}
		dst_cursor = j;

		/*
		 * A whole page is found. Set the offset and len to 0, so
		 * this IO vector is not sent to motr.
		 */
		if (len == pagesize) {
			rbw_iovs[i].miov_off = 0;
			rbw_iovs[i].miov_len = 0;
		}

	}
}

static int
motr_obj_read_before_write(struct mio_obj *obj,
			     struct motr_obj_rw_args *args,
			     struct mio_op *op)
{
	int iovcnt_to_read;
	struct mio_iovec *iovs_to_read;


	iovcnt_to_read = args->rwa_rbw_iovcnt;
	iovs_to_read =	args->rwa_rbw_iovs;
	motr_obj_read_before_write_optimise(args);
	return motr_obj_rw_aligned(obj, iovs_to_read, iovcnt_to_read,
				     &args->rwa_rbw_progress,
				     M0_OC_READ,
				     motr_obj_read_before_write_pp, args, op);
}

/**
 * Motr sets a limit on how many data units an op can Read/write
 * due to the implementation at the service size, the value is queried
 * using motr_obj_max_size_per_op(). For an IO which is bigger than the
 * limit, it is divided into multiple parts, each part is less or equal to
 * the limit in size and is done in one op.
 *
 * Note that currently the ops for all parts are launched and served
 * sequentially (not in parallel). When each op is completed, post processing
 * functions (motr_obj_write/read_pp()) are triggered to check if all parts
 * are done, if not, launch a new op.
 */
static int mio_motr_obj_writev(struct mio_obj *obj,
				 const struct mio_iovec *iov,
				 int iovcnt, struct mio_op *op)
{
	int rc;
	struct motr_obj_rw_args *args;

	args = motr_obj_rw_args_alloc(obj, iovcnt, iov);
	if (args == NULL)
		return -ENOMEM;
	args->rwa_is_write = true;

	/* 1. Sort IO vectors. */
	rc = motr_obj_iovec_sort(args);
	if (rc < 0)
		goto error;

	/* 2. Align byte ranges of the sorted IO vector. */
	rc = motr_obj_iovec_adjust(args);
	if (rc < 0)
		goto error;

	/*
	 * 3. If offset and end of an IO vector are not aligned with page size,
	 * read back the first or last block (of page size) to avoid data
	 * overwriting. If the offset or end are aligned with page size, no
	 * blocks are required to read before write, IO operation can be
	 * issued immediately to Motr.
	 */
	if (args->rwa_rbw_iovcnt != 0)
		rc = motr_obj_read_before_write(obj, args, op);
	else {
		motr_obj_data_copy(args);
		rc = motr_obj_rw_aligned(obj, args->rwa_aligned_iovs,
					   args->rwa_aligned_iovcnt,
					   &args->rwa_aligned_progress,
				   	   M0_OC_WRITE,
					   motr_obj_write_pp, args, op);
	}
	if (rc < 0)
		goto error;
	else
		return 0;

error:
	motr_obj_rw_args_free(args);
	return rc;
}

static int motr_obj_read_pp(struct mio_op *op)
{
	int rc = 0;
	struct motr_obj_rw_args *args;

	args = (struct motr_obj_rw_args *)
		  op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;

	if (args->rwa_aligned_iovcnt == args->rwa_aligned_progress) {
		/* Copy data into application's memory if needed. */
		motr_obj_data_copy(args);
		motr_obj_rw_args_free(args);
		return MIO_DRV_OP_FINAL;
	} else {
		rc = motr_obj_rw_aligned(args->rwa_obj,
					   args->rwa_aligned_iovs,
					   args->rwa_aligned_iovcnt,
					   &args->rwa_aligned_progress,
					   M0_OC_READ,
					   motr_obj_read_pp, args, op);
	}

	if (rc < 0)
		return rc;
	else
		return MIO_DRV_OP_NEXT;
}

static int mio_motr_obj_readv(struct mio_obj *obj,
				 const struct mio_iovec *iov,
				 int iovcnt, struct mio_op *op)
{
	int rc;
	struct motr_obj_rw_args *args;

	args = motr_obj_rw_args_alloc(obj, iovcnt, iov);
	if (args == NULL)
		return -ENOMEM;
	args->rwa_is_write = false;

	/* 1. Sort IO vectors. */
	motr_obj_iovec_sort(args);

	/* 2. Align byte ranges of the sorted IO vector. */
	rc = motr_obj_iovec_adjust(args);
	if (rc < 0)
		goto error;

	/*
	 * 3. Read data into aligned IO vectors.
	 */
	rc = motr_obj_rw_aligned(obj, args->rwa_aligned_iovs,
				   args->rwa_aligned_iovcnt,
				   &args->rwa_aligned_progress,
				   M0_OC_READ,
				   motr_obj_read_pp, args, op);
	if (rc < 0)
		goto error;
	else
		return 0;

error:
	motr_obj_rw_args_free(args);
	return rc;
}

static int mio_motr_obj_sync(struct mio_obj *obj, struct mio_op *op)
{
	int rc;
	struct m0_obj *cobj;
	struct m0_op  *sync_op = {NULL};

	rc = m0_sync_op_init(&sync_op);
	if (rc < 0)
		return rc;

	cobj = (struct m0_obj *)obj->mo_drv_obj;
	rc = m0_sync_entity_add(sync_op, &cobj->ob_entity);
	if (rc < 0)
		goto error;

	rc = mio_driver_op_add(op, NULL, NULL, NULL, sync_op, NULL);
	if (rc < 0)
		goto error;
	m0_op_launch(&sync_op, 1);
	return 0;

error:
	m0_op_fini(sync_op);
	m0_op_free(sync_op);
	return rc;
}

/**
 * Note: currently Motr Motr doesn't store size as object attribute
 * and doesn't have any API to query object size. MIO will use an index
 * (attribute index) to manage object attributes such as object size.
 * This also implies that any object WRITE has to update the size.
 * Depending on workload and performance of Motr index, MIO may support
 * multiple indics.
 *
 * Format of object attributes that are stored in attribute index are
 * defined as below:
 *
 * struct motr_obj_attrs_onwire {
 *	struct mio_obj_attrs coa_attrs;
 *      int coa_nr_hints;
 *      int coa_hint_keys[];   // array of hint's keys
 *      uint64_t coa_hint_values[]; // array of hint's values
 * };
 *
 * Note: MIO assumes application takes care of concurrent accesses to
 * an object and its attributes.
 */

struct motr_obj_attrs_pp_args {
	int32_t *aca_rc;
	struct m0_bufvec *aca_key;
	struct m0_bufvec *aca_val;
	/* Where the returned attributes are copied to. */
	struct mio_obj *aca_to;
};

static int motr_obj_attr_nonhint_size(struct mio_obj *obj)
{
	int size;

	size = sizeof obj->mo_attrs.moa_size;
	size += sizeof obj->mo_attrs.moa_stats;
	return size;
}

static int
motr_obj_attrs_mem2wire(struct mio_obj *obj,
			  uint64_t *attr_size, void **attr_buf)
{
	int i;
	int nonhint_size;
	uint64_t size;
	void *buf;
	char *ptr;
	struct mio_hint_map *map = &obj->mo_attrs.moa_phints.mh_map;

	nonhint_size  = motr_obj_attr_nonhint_size(obj);
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
motr_obj_attrs_wire2mem(struct mio_obj *obj, int attr_size, void *attr_buf)
{
	int i;
	int nonhint_size;
	int size;
	int nr_hints;
	char *ptr;
	/* Note that hint map has been initialised with default number. */
	struct mio_hint_map *map = &obj->mo_hints.mh_map;

	nonhint_size = motr_obj_attr_nonhint_size(obj);
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

static int motr_obj_attrs_query(int opcode, struct mio_obj *obj,
				  mio_driver_op_postprocess op_pp,
				  struct mio_op *op)
{
	int rc;
	int32_t *qrc = NULL; /* return value for kvs query. */
	uint32_t flags = 0;
	struct m0_uint128 *id128;
        struct m0_bufvec *key = NULL;
        struct m0_bufvec *val = NULL;
	struct m0_idx *idx;
	struct m0_op *cops[1] = {NULL};
	struct motr_obj_attrs_pp_args *args;

	assert(opcode == M0_IC_GET || opcode == M0_IC_PUT ||
	       opcode == M0_IC_DEL);

	id128 = mio_mem_alloc(sizeof *id128);
	qrc = mio_mem_alloc(sizeof(int32_t));
	args = mio_mem_alloc(sizeof *args);
	if (id128 == NULL || qrc == NULL || args == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	/* Allocate bufvec's for keys and values. */
	key = mio__motr_bufvec_alloc(1);
	if (key == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	if (opcode != M0_IC_DEL) {
		val = mio__motr_bufvec_alloc(1);
		if (val == NULL) {
			rc = -ENOMEM;
			goto error;
		}
	}

        /* Fill key and value. TODO: serialise key and value? */
	mio__obj_id_to_uint128(&obj->mo_id, id128);
	key->ov_vec.v_count[0] = sizeof *id128;
	key->ov_buf[0] = id128;

	if (opcode == M0_IC_PUT) {
		flags = M0_OIF_OVERWRITE;
		motr_obj_attrs_mem2wire(obj, &val->ov_vec.v_count[0],
					  &val->ov_buf[0]);
	}

	/* Create index's op. */
	idx = (struct m0_idx *)obj->mo_md_kvs->mk_drv_kvs;
	rc = m0_idx_op(idx, opcode, key, val, qrc, flags, &cops[0]);
	if (rc < 0)
		goto error;

	/* Set callback function and arguments. */
	args->aca_val = val;
	args->aca_key = key;
	args->aca_rc = qrc;
	args->aca_to = obj;
	rc = mio_driver_op_add(op, op_pp, args, NULL, cops[0], NULL);
	if (rc < 0)
		goto error;

	mio_telemetry_array_advertise_noprefix(
		"mio-op-to-motr-kv", MIO_TM_TYPE_ARRAY_UINT64,
		3, obj->mo_sess_seqno, op->mop_seqno, cops[0]->op_sm.sm_id);
	m0_op_launch(cops, 1);
	return 0;

error:
	mio__motr_bufvec_free(key);
	mio__motr_bufvec_free(val);
	mio_mem_free(qrc);
	mio_mem_free(args);
	return rc;
}

static int motr_obj_attrs_query_free_pp(struct mio_op *op)
{
	struct motr_obj_attrs_pp_args *args;

	args = (struct motr_obj_attrs_pp_args *)
	       op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;
	mio__motr_bufvec_free(args->aca_key);
	mio__motr_bufvec_free(args->aca_val);
	mio_mem_free(args->aca_rc);
	mio_mem_free(args);

	return MIO_DRV_OP_FINAL;
}

static int motr_obj_attrs_get_pp(struct mio_op *op)
{
	struct m0_bufvec *ret_val;
	struct m0_op *cop = MIO_MOTR_OP(op);
	struct mio_obj *obj;
	struct motr_obj_attrs_pp_args *args;

	assert(cop != NULL);
	args = (struct motr_obj_attrs_pp_args *)
	       op->mop_drv_op_chain.mdoc_head->mdo_post_proc_data;
	obj = args->aca_to;
	ret_val = args->aca_val;
	if (ret_val->ov_vec.v_count[0] != 0)
		motr_obj_attrs_wire2mem(obj, ret_val->ov_vec.v_count[0],
					  ret_val->ov_buf[0]);

	return motr_obj_attrs_query_free_pp(op);
}

static int mio_motr_obj_size(struct mio_obj *obj, struct mio_op *op)
{
	return motr_obj_attrs_query(M0_IC_GET, obj,
				      motr_obj_attrs_get_pp, op);
}

static int
mio_motr_obj_pool_id(const struct mio_obj *obj, struct mio_pool_id *pool_id)
{
        struct m0_obj *cobj = (struct m0_obj *)obj->mo_drv_obj;
	struct m0_pool_version *pver;

        if (cobj == NULL)
                return -EINVAL;

	pver = m0_conf_fid_is_valid(&cobj->ob_attr.oa_pver) == false? NULL:
	       m0_pool_version_find(&mio_motr_instance->m0c_pools_common,
				    &cobj->ob_attr.oa_pver);
	if (pver == NULL)
		return -EINVAL;
	mio__motr_fid_to_pool_id(&pver->pv_pool->po_id, pool_id);
	return 0;
}

static int mio_motr_obj_lock(struct mio_obj *obj)
{
        int rc = 0;
        struct m0_obj *cobj = (struct m0_obj *)obj->mo_drv_obj;
        struct m0_rm_lock_req  *rm_lock;

        if (cobj == NULL)
                return -EINVAL;

        rc = m0_obj_lock_init(cobj);
        if (rc != 0)
                return rc;

        rm_lock = mio_mem_alloc(sizeof(*rm_lock));
        if (rm_lock == NULL) {
                m0_obj_lock_fini(cobj);
                return -ENOMEM;
        }

        rc = m0_obj_write_lock_get_sync(cobj, rm_lock);
        if (rc < 0) {
                m0_obj_lock_fini(cobj);
                mio_mem_free(rm_lock);
                return rc;
        }

        obj->mo_drv_obj_lock = (void *)rm_lock;
        return 0;
}

static int mio_motr_obj_unlock(struct mio_obj *obj)
{
        struct m0_obj *cobj;
        struct m0_rm_lock_req  *rm_lock;

        cobj = (struct m0_obj *)obj->mo_drv_obj;
        rm_lock = (struct m0_rm_lock_req *)obj->mo_drv_obj_lock;
        if (cobj == NULL || rm_lock == NULL)
                return -EINVAL;

        m0_obj_lock_put(rm_lock);
        m0_obj_lock_fini(cobj);
        return 0;
}

static int mio_motr_obj_hint_store(struct mio_obj *obj)
{
	int rc;
	struct mio_op mop;
	struct m0_op *cop;

	mio_op_init(&mop);
	mio_obj_op_init(&mop, obj, MIO_OBJ_ATTRS_SET);
	rc = motr_obj_attrs_query(M0_IC_PUT, obj,
				    motr_obj_attrs_query_free_pp, &mop);
	if (rc < 0)
		return rc;

	cop = MIO_MOTR_OP((&mop));
	rc = m0_op_wait(cop, M0_BITS(M0_OS_FAILED, M0_OS_STABLE),
			M0_TIME_NEVER);
	rc = rc? : m0_rc(cop);
	m0_op_fini(cop);
	m0_op_free(cop);
	return rc;
}

static int mio_motr_obj_hint_load(struct mio_obj *obj)
{
	/*
	 * Hints are retrieved when an object is opened, no more work
	 * here.
	 */
	return 0;
}

struct mio_obj_ops mio_motr_obj_ops = {
        .moo_open         = mio_motr_obj_open,
        .moo_close        = mio_motr_obj_close,
        .moo_create       = mio_motr_obj_create,
        .moo_delete       = mio_motr_obj_delete,
        .moo_writev       = mio_motr_obj_writev,
        .moo_readv        = mio_motr_obj_readv,
        .moo_sync         = mio_motr_obj_sync,
        .moo_size         = mio_motr_obj_size,
        .moo_pool_id      = mio_motr_obj_pool_id,
        .moo_lock         = mio_motr_obj_lock,
        .moo_unlock       = mio_motr_obj_unlock,
        .moo_hint_store   = mio_motr_obj_hint_store,
        .moo_hint_load    = mio_motr_obj_hint_load,
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
