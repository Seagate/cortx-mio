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
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <uuid/uuid.h>

#include "logger.h"
#include "mio.h"
#include "mio_internal.h"
#include "driver_motr.h"

struct m0_client *mio_motr_instance;
struct m0_container mio_motr_container;
struct m0_config mio_motr_inst_conf;
struct mio_motr_config *mio_drv_motr_conf;

struct m0_uint128 mio_motr_obj_md_kvs_id;
struct m0_fid mio_motr_obj_md_kvs_fid = M0_FID_TINIT('x', 0, 0x10);

/**
 * Some helper functions.
 */
void mio__motr_bufvec_free(struct m0_bufvec *bv)
{
        if (bv == NULL)
                return;

        m0_free(bv->ov_buf);
        m0_free(bv->ov_vec.v_count);
        m0_free(bv);
}

struct m0_bufvec* mio__motr_bufvec_alloc(int nr)
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

static int motr_create_obj_attrs_kvs()
{
        int rc;
	struct m0_op *cops[1] = {NULL};
	struct m0_idx *idx;

	idx = mio_mem_alloc(sizeof *idx);
	if (idx == NULL)
		return -ENOMEM;

	mio_motr_obj_md_kvs_id.u_hi = mio_motr_obj_md_kvs_fid.f_container;
	mio_motr_obj_md_kvs_id.u_lo = mio_motr_obj_md_kvs_fid.f_key;
	m0_idx_init(idx, &mio_motr_container.co_realm,
			   &mio_motr_obj_md_kvs_id);

	/* Check if object's attrs key-value store exists. */
        m0_idx_op(idx, M0_IC_LOOKUP,
			 NULL, NULL, NULL, 0, &cops[0]);
        m0_op_launch(cops, 1);
	rc = m0_op_wait(cops[0],
			       M0_BITS(M0_OS_FAILED,
				       M0_OS_STABLE),
			       M0_TIME_NEVER);
	rc = rc? : m0_rc(cops[0]);
	m0_op_fini(cops[0]);
	m0_op_free(cops[0]);

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
        rc = m0_entity_create(NULL, &idx->in_entity, &cops[0]);
	if (rc < 0)
		goto exit;
        m0_op_launch(cops, 1);
	rc = m0_op_wait(cops[0],
			       M0_BITS(M0_OS_FAILED,
				       M0_OS_STABLE),
			       M0_TIME_NEVER);
	rc = rc? : m0_rc(cops[0]);
	m0_op_fini(cops[0]);
	m0_op_free(cops[0]);

exit:
	if (rc == 0)
		mio_obj_attrs_kvs.mk_drv_kvs = idx;
	else {
		m0_idx_fini(idx);
		mio_mem_free(idx);
	}
	return rc;
}

/**
 * Initialise and finalise motr instance.
 */
int mio_motr_init(struct mio *mio_inst)
{
	int rc;
	struct mio_motr_config *drv;
	struct m0_idx_dix_config dix_conf;

	drv = (struct mio_motr_config *)mio_inst->m_driver_confs;
	drv->mc_is_addb_on =
		(mio_inst->m_telem_store_type == MIO_TM_ST_ADDB)? true : false;
	mio_drv_motr_conf = drv;

	/* Set motr configuration parameters. */
	mio_motr_inst_conf.mc_is_addb_init          = drv->mc_is_addb_on; 
	mio_motr_inst_conf.mc_is_oostore            = drv->mc_is_oostore;
	mio_motr_inst_conf.mc_is_read_verify        = drv->mc_is_read_verify;
	mio_motr_inst_conf.mc_local_addr            = drv->mc_motr_local_addr;
	mio_motr_inst_conf.mc_ha_addr               = drv->mc_ha_addr;
	mio_motr_inst_conf.mc_profile               = drv->mc_profile;
	mio_motr_inst_conf.mc_process_fid           = drv->mc_process_fid;
	mio_motr_inst_conf.mc_tm_recv_queue_min_len = drv->mc_tm_recv_queue_min_len;
	mio_motr_inst_conf.mc_max_rpc_msg_size      = drv->mc_max_rpc_msg_size;

	mio_motr_inst_conf.mc_layout_id =
		m0_obj_unit_size_to_layout_id(drv->mc_unit_size);

	mio_motr_inst_conf.mc_idx_service_id   = M0_IDX_DIX;
	dix_conf.kc_create_meta = false;
	mio_motr_inst_conf.mc_idx_service_conf = &dix_conf;

	/* Initial motr instance. */
	rc = m0_client_init(&mio_motr_instance, &mio_motr_inst_conf, true);
	if (rc != 0)
		return rc;

	/* Initial a container. */
	m0_container_init(&mio_motr_container, NULL,
				 &M0_UBER_REALM,
				 mio_motr_instance);
	rc = mio_motr_container.co_realm.re_entity.en_sm.sm_rc;
	if (rc != 0) {
		mio_log(MIO_ERROR, "Failed to open Motr's uber realm!\n");
		goto error;
	}

	/* Create object attrs kvs if it doesn't exist. */
 	rc = motr_create_obj_attrs_kvs();
	if (rc != 0) {
		mio_log(MIO_ERROR, "Failed to create attrs key-value set!\n");
		goto error;
	}
	return 0;

error:
	m0_client_fini(mio_motr_instance, true);
	return rc;
}

static void mio_motr_fini()
{
	m0_idx_fini(
		(struct m0_idx *)mio_obj_attrs_kvs.mk_drv_kvs);
	m0_client_fini(mio_motr_instance, true);
	mio_motr_instance = NULL;
}

static int mio_motr_thread_init(struct mio_thread *thread)
{
	struct m0_thread *mthread;

	mthread = mio_mem_alloc(sizeof(*mthread));
	if (mthread == NULL)
		return -ENOMEM;
	memset(mthread, 0, sizeof(struct m0_thread));
	m0_thread_adopt(mthread, mio_motr_instance->m0c_motr);
	thread->mt_drv_thread = mthread;
	return 0;
}

static void mio_motr_thread_fini(struct mio_thread *thread)
{
	m0_addb2_force_all();
	m0_thread_shun();
	mio_mem_free(thread->mt_drv_thread);
}

static int mio_motr_user_perm(struct mio *mio_inst)
{
	int i;
	int rc = 0;
	int nr_grps = 0;
	gid_t *gids;
	struct group *motr_grp;
	struct passwd *mio_user_pw;
	struct mio_motr_config *drv;

	errno = 0;
	mio_user_pw = getpwuid(getuid());
	if (mio_user_pw == NULL) {
		fprintf(stderr, "Failed to get user ID: errno = -%d, %s\n",
			errno, strerror(errno));
		return -errno;
	}

	/* If it is `root`. */
	if (mio_user_pw->pw_uid == 0)
		return 0;

	/* Check if the user is in the Motr group. */
	drv = (struct mio_motr_config *)mio_inst->m_driver_confs;
	if (drv->mc_motr_group == NULL) {
		fprintf(stderr,
			"No Motr group is specified and user is not root!\n");
		return -EPERM;
	}
	motr_grp = getgrnam(drv->mc_motr_group);
	if (motr_grp == NULL) {
		fprintf(stderr, "Motr group [%s] doesn't exist!\n",
			drv->mc_motr_group);
		return -EPERM;
	}

	getgrouplist(mio_user_pw->pw_name, mio_user_pw->pw_gid,
		     NULL, &nr_grps);
	if (nr_grps == 0)
		return -EPERM;
	gids = mio_mem_alloc(nr_grps * sizeof(int));
	if (gids == NULL) {
		rc = -ENOMEM;
		goto exit;
	}

	rc = getgrouplist(mio_user_pw->pw_name, mio_user_pw->pw_gid,
			  gids, &nr_grps);
	if (rc < 0)
		goto exit;
	for (i = 0; i < nr_grps; i++) {
		if (gids[i] == motr_grp->gr_gid)
			break;
	}
	if (i == nr_grps)
		rc = -EPERM;
	else
		rc = 0;

exit:
	mio_mem_free(gids);
	return rc;
}

static struct mio_driver_sys_ops mio_motr_sys_ops = {
        .mdo_init = mio_motr_init,
        .mdo_fini = mio_motr_fini,
        .mdo_user_perm = mio_motr_user_perm,
        .mdo_thread_init = mio_motr_thread_init,
        .mdo_thread_fini = mio_motr_thread_fini
};

static void mio_motr_op_fini(struct mio_op *mop)
{
	struct m0_op *cop;
	struct mio_driver_op *dop;

	dop = mop->mop_drv_op_chain.mdoc_head;
	while(dop != NULL) {
		mop->mop_drv_op_chain.mdoc_head = dop->mdo_next;
		dop->mdo_next = NULL;

		cop = (struct m0_op *)dop->mdo_op;
		m0_op_fini(cop);
		m0_op_free(cop);
		if (dop->mdo_op_fini)
			dop->mdo_op_fini(dop);
		mio_mem_free(dop);

		dop = mop->mop_drv_op_chain.mdoc_head;
	}
}

static int
mio_motr_op_wait(struct mio_op *mop, uint64_t timeout, int *retstate)
{
	int rc;
	struct m0_op *cop;

	cop = MIO_MOTR_OP(mop);
	rc = m0_op_wait(cop, M0_BITS(M0_OS_STABLE, M0_OS_FAILED), timeout);
	/*
	 * Check returned value (rc) from m0_op_wait:
	 *   - errors (rc < 0), treat timeout error differently.
	 *   - rc == 0, the operation is completed, then check the
	 *     operation's rc value to see if the op is successful or failed.
 	 */
	if (rc == 0) {
		if (m0_rc(cop) < 0) {
			*retstate = MIO_OP_FAILED;
			rc = m0_rc(cop);
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
 * arguments with the ones of drivers', such as Motr. A jumper
 * function here is used to call the callback functions set by MIO
 * applications.
 *
 * The callback functions shown below give examples on how to relay
 * control to callbacks set by apps.
 *
 * This looks a bit ugly, is there any better solution?
 */
static void motr_op_cb_complete(struct m0_op *cop)
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

static void motr_op_cb_failed(struct m0_op *cop)
{
	struct mio_op *mop;

	mop = (struct mio_op *)cop->op_datum;
	mio_driver_op_invoke_real_cb(mop, m0_rc(cop));
}

static struct m0_op_ops motr_op_cbs;
static int
mio_motr_op_set_cbs(struct mio_op *mop)

{
	struct m0_op *cop;

	assert(mop != NULL);

	motr_op_cbs.oop_executed = NULL;
	motr_op_cbs.oop_stable = motr_op_cb_complete;
	motr_op_cbs.oop_failed = motr_op_cb_failed;
	cop = MIO_MOTR_OP(mop);
	cop->op_datum = (void *)mop;
	m0_op_setup(cop, &motr_op_cbs, 0);

	return 0;
}

static struct mio_op_ops mio_motr_op_ops = {
	.mopo_fini    = mio_motr_op_fini,
	.mopo_wait    = mio_motr_op_wait,
	.mopo_set_cbs = mio_motr_op_set_cbs
};

static int
mio_motr_pool_get(const struct mio_pool_id *pool_id, struct mio_pool *pool)
{
       int rc;
       int i;
       int nr_opt_blksizes;
       unsigned long blksize;
       unsigned long unit_size;
       unsigned long grp_size;
       unsigned long max_blksize;
       unsigned long opt_blksize_incr;
       unsigned lid;
       struct m0_fid pool_fid;
       struct m0_reqh *reqh = &mio_motr_instance->m0c_reqh;
       struct m0_pool_version *pver;
       struct m0_pdclust_attr *pa;

       if (pool_id == NULL || pool == NULL)
               return -EINVAL;

       mio__motr_pool_id_to_fid(pool_id, &pool_fid);
       rc = m0_pool_version_get(reqh->rh_pools, &pool_fid, &pver);
       if (rc != 0) {
               mio_log(MIO_ERROR,
                       "m0_pool_version_get(%"PRIx64":%"PRIx64") failed: %d",
                       pool_fid.f_container, pool_fid.f_key, rc);
               return rc;
       }

       lid = m0_client_layout_id(mio_motr_instance);
       /* The unit size returned is power of 2. */
       unit_size = m0_obj_layout_id_to_unit_size(lid);
       pa = &pver->pv_attr;
       grp_size = unit_size * pa->pa_N;
       /* max 2-times pool-width deep, otherwise we may get -E2BIG */
       max_blksize =
               unit_size * 2 * pa->pa_P * pa->pa_N / (pa->pa_N + 2 * pa->pa_K);
       max_blksize = 
               ((max_blksize + grp_size - 1) / grp_size) * grp_size;

       mio_log(MIO_DEBUG, "unit_size=%lu (N,K,P)=(%u,%u,%u) max_blksize=%lu\n",
           unit_size, pa->pa_N, pa->pa_K, pa->pa_P, max_blksize);

       /*
        * Calculate optimised buffer block size for READ/WRITE. It is rounded
        * up to be multiple of group size.
        */
       nr_opt_blksizes = max_blksize / grp_size > MIO_POOL_MAX_NR_OPT_BLKSIZES?
                         MIO_POOL_MAX_NR_OPT_BLKSIZES :
                         max_blksize / grp_size;
       opt_blksize_incr = max_blksize / nr_opt_blksizes;
       opt_blksize_incr = 
               ((opt_blksize_incr + grp_size - 1) / grp_size) * grp_size;

       for (i = 0, blksize = opt_blksize_incr;
            blksize < max_blksize; blksize += opt_blksize_incr)
               pool->mp_opt_blksizes[i++] = blksize;
       pool->mp_nr_opt_blksizes = i;
       pool->mp_opt_alignment = 4096;

       return 0;
}

static struct mio_pool_ops mio_motr_pool_ops = {
       .mpo_get    = mio_motr_pool_get
};

void mio_motr_driver_register()
{
	mio_driver_register(
		MIO_MOTR, &mio_motr_sys_ops, &mio_motr_pool_ops,
		&mio_motr_op_ops, &mio_motr_obj_ops,
		&mio_motr_kvs_ops, &mio_motr_comp_obj_ops);
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
